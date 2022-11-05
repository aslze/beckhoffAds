// Copyright(c) 2019-2022 aslze

// https://infosys.beckhoff.com/

//#define NOTIF_THREAD // notifications start a thread each

// could have a thread with a queue of notifications to call in sequence, and semaphore to trigger

#include "BeckhoffAds.h"
#include <asl/StreamBuffer.h>
#include <asl/Thread.h>
#include <asl/Date.h>

typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;

using namespace asl;

inline ULong pack(unsigned cmd, unsigned invokeId)
{
	return ULong(cmd) << 32 | invokeId;
}

struct NotifThread : public asl::Thread
{
	const ByteArray                             data;
	asl::Function<void, const asl::ByteArray&>* func;
	NotifThread(const ByteArray& b, asl::Function<void, const asl::ByteArray&>& f) : data(b), func(&f) { start(); }
	void run()
	{
		(*func)(data);
		delete this;
	}
};

enum AdsCommand
{
	ADSCOM_INVALID,
	ADSCOM_READDEVICEINFO,
	ADSCOM_READ,
	ADSCOM_WRITE,
	ADSCOM_READSTATE,
	ADSCOM_WRITECTRL,
	ADSCOM_ADDDEVICENOTIF,
	ADSCOM_DELDEVICENOTIF,
	ADSCOM_DEVICENOTIF,
	ADSCOM_READWRITE
};

enum AdsIndexGroup
{
	ADSIGRP_SYMTAB = 0xF000,
	ADSIGRP_SYMNAME = 0xF001,
	ADSIGRP_SYMVAL = 0xF002,
	ADSIGRP_HNDBYNAME = 0xF003,
	ADSIGRP_VALBYNAME = 0xF004,
	ADSIGRP_VALBYHND = 0xF005,
	ADSIGRP_RELEASEHND = 0xF006,
	ADSIGRP_INFOBYNAME = 0xF007,
	ADSIGRP_VERSION = 0xF008,
	ADSIGRP_INFOBYNAMEEX = 0xF009,
	ADSIGRP_DOWNLOAD = 0xF00A,
	ADSIGRP_UPLOAD = 0xF00B,
	ADSIGRP_UPLOADINFO = 0xF00C
};

BeckhoffAds::NetId::NetId(const char* s)
{
	data.resize(6);
	Array<String> parts = String(s).split('.');
	for (int i = 0; i < 6; i++)
		data[i] = (byte)(int)parts[i];
}

BeckhoffAds::NetId::NetId(const String& s)
{
	data.resize(6);
	Array<String> parts = s.split('.');
	for (int i = 0; i < 6; i++)
		data[i] = (byte)(int)parts[i];
}

struct BeckhoffThread : public asl::Thread
{
	BeckhoffAds* _ads;

	void run();
};

void BeckhoffThread::run()
{
	_ads->receiveLoop();
}

BeckhoffAds::BeckhoffAds()
{
	_connected = false;
	_error = false;
	_source = String("192.168.0.2.1.1");
	_sourcePort = (uint16_t)34000;
	_invokeId = 0;
	_thread = 0;
}

BeckhoffAds::~BeckhoffAds()
{
	foreach (unsigned h, _handles)
		releaseHandle(h);

	foreach (unsigned h, _notifications)
		removeNotification(h);

	_socket.close();
}

bool BeckhoffAds::connect(const String& host)
{
	_host = host;
	_error = false;
	Lock _(_mutex);
	_socket.close();
	_socket = Socket();
	_socket.setEndian(ENDIAN_BIG);
	_connected = _socket.connect(_host, 48898);
	if (!_connected)
	{
		printf("Error connecting\n");
	}
	else
	{
		_thread = new BeckhoffThread;
		_thread->_ads = this;
		_thread->start();
	}
	return _connected;
}

void BeckhoffAds::disconnect()
{
	_connected = false;
	_error = false;
	_socket.close();
}

bool BeckhoffAds::checkConnection()
{
	if (_connected && _socket.disconnected())
	{
		_connected = false;
	}
	return _connected;
}

void BeckhoffAds::setSource(const BeckhoffAds::NetId& net, int port)
{
	_source = net;
	_sourcePort = (uint16_t)port;
}

void BeckhoffAds::setTarget(const BeckhoffAds::NetId& net, int port)
{
	_target = net;
	_targetPort = (uint16_t)port;
}

bool BeckhoffAds::send(int command, const asl::ByteArray& data)
{
	Lock _(_mutex);
	if (!checkConnection())
		return false;

	StreamBuffer buffer;
	buffer.setEndian(ENDIAN_LITTLE);

	buffer << (uint16_t)0 << (uint32_t)(data.length() + 32); // AMS/TCP Header
	buffer << _target.data << (uint16_t)_targetPort << _source.data << (uint16_t)_sourcePort;
	buffer << (uint16_t)command << (uint16_t)(0x0004) << (uint32_t)data.length();
	buffer << (uint32_t)0 << (uint32_t)_invokeId++;
	buffer << data;

	int n = _socket.write(buffer.ptr(), buffer.length());
	if (n != buffer.length())
	{
		printf("send failed %i\n", n);
		return false;
	}

	_lastRequestId = pack(command, _invokeId - 1);

	return true;
}

void BeckhoffAds::receiveLoop()
{
	while (1)
	{
		if (_socket.waitInput(5))
		{
			if (_socket.disconnected())
				break;
			ByteArray data = readPacket();
		}
	}
	_sem.post(); // wake possible response waiter
}

void BeckhoffAds::processNotification(const asl::ByteArray& data)
{
	StreamBufferReader buffer(data);
	uint32_t           length = 0, stamps = 0;
	buffer >> (uint32_t)length >> (uint32_t)stamps;
	// printf("notification: %i stamps\n", stamps);
	for (unsigned i = 0; i < stamps; i++)
	{
		ULong    time = 0;
		uint32_t samples = 0;
		buffer >> time >> samples;
		Date t = time * 100e-9 - 11644473600.0;
		// printf(" notification: %i samples\n", samples);
		for (unsigned j = 0; j < samples; j++)
		{
			uint32_t handle = 0, size = 0;
			buffer >> handle >> size;
			if (_callbacks.has(handle))
#ifndef NOTIF_THREAD
				_callbacks[handle](buffer.read(size)); // send more info, like timestamp??
#else
				new NotifThread(buffer.read(size), _callbacks[handle]);
#endif

			// buffer.skip(size); // this is the actual data
			// printf("  notification at %s.%i for handle %u size %u\n", *t.toUTCString(), int(1000*fract(t.time())),
			// handle, size);
		}
	}
}

asl::ByteArray BeckhoffAds::readPacket()
{
	if (!checkConnection())
		return asl::ByteArray();
	uint16_t  reserved = 0;
	uint32_t  totalLen = 0;
	ByteArray head = _socket.read(6);
	if (head.length() < 6)
		return ByteArray();
	StreamBufferReader reader(head);
	reader >> reserved >> totalLen;
	if (_socket.error() || reserved != 0 || totalLen > 5000)
	{
		printf("bad ADS response (len=%i reserved=%i, read=%i)\n", totalLen, reserved, head.length());
		return asl::ByteArray();
	}
	ByteArray          packet = _socket.read(totalLen);
	StreamBufferReader buffer(packet);
	buffer.skip(16);
	uint16_t commandId, flags;
	uint32_t len = 0, error = 0, invokeId;
	buffer >> commandId >> flags >> len >> error >> invokeId;
	if (error != 0)
	{
		printf("bad ADS message error: %i len %i cmd %x \n", error, len, commandId);
		return asl::ByteArray(8);
	}

	ByteArray data = buffer.read(len);

	if ((flags & 1) == 0 && commandId != ADSCOM_DEVICENOTIF)
	{
		printf("received request, not response (cmd: %u)\n", commandId);
		return asl::ByteArray();
	}

	switch (commandId)
	{
	case ADSCOM_DEVICENOTIF:
		processNotification(data);
		break;
	case ADSCOM_READSTATE:
	case ADSCOM_READWRITE:
	case ADSCOM_READ:
	case ADSCOM_WRITE:
	case ADSCOM_ADDDEVICENOTIF:
	case ADSCOM_DELDEVICENOTIF:
		_response = data.clone();
		_responses[pack(commandId, invokeId)] = _response;
		_sem.post();
		break;
	default:;
	}
	return data;
}

asl::ByteArray BeckhoffAds::getResponse()
{
	_sem.wait();
	if (!_responses.has(_lastRequestId))
	{
		printf("Timeout waiting response\n");
		return asl::ByteArray();
	}

	asl::ByteArray res = _responses[_lastRequestId];

	_responses.remove(_lastRequestId);

	return res;
}

bool BeckhoffAds::write(unsigned group, unsigned offset, const asl::ByteArray& data)
{
	StreamBuffer buffer;
	buffer.setEndian(ENDIAN_LITTLE);
	buffer << (uint32_t)group << (uint32_t)offset << (uint32_t)data.length();
	buffer << data;

	if (!send(ADSCOM_WRITE, buffer))
		return false;

	ByteArray response = getResponse();

	if (!response)
	{
		return false;
	}

	StreamBufferReader reader(response);
	uint32_t           error;
	reader >> error;
	if (error != 0)
	{
		printf("bad ADS response %x\n", error);
		return false;
	}
	return true;
}

asl::ByteArray BeckhoffAds::read(unsigned group, unsigned offset, int length)
{
	StreamBuffer buffer;
	buffer.setEndian(ENDIAN_LITTLE);
	buffer << (uint32_t)group << (uint32_t)offset << (uint32_t)length;

	if (!send(ADSCOM_READ, buffer))
		return asl::ByteArray();
	ByteArray response = getResponse();
	if (!response)
	{
		return asl::ByteArray();
	}
	StreamBufferReader reader(response);
	uint32_t           error, len;
	reader >> error >> len;
	if (error != 0 || len > 1000)
	{
		printf("bad ADS response %x\n", error);
		return asl::ByteArray();
	}
	ByteArray data = reader.read(len);
	return data;
}

asl::ByteArray BeckhoffAds::readWrite(unsigned group, unsigned offset, int length, const asl::ByteArray& data)
{
	StreamBuffer buffer;
	buffer.setEndian(ENDIAN_LITTLE);
	buffer << (uint32_t)group << (uint32_t)offset << (uint32_t)length << (uint32_t)data.length();
	buffer << data;

	if (!send(ADSCOM_READWRITE, buffer))
		return asl::ByteArray();

	ByteArray response = getResponse();

	if (!response)
		return asl::ByteArray();

	StreamBufferReader reader(response);
	uint32_t           error, len;
	reader >> error >> len;
	if (error != 0 || len > 1000)
	{
		printf("bad ADS response %x\n", error);
		return asl::ByteArray();
	}
	ByteArray resdata = reader.read(len);
	return resdata;
}

// ADS notification times are in 100 ns units
// https://infosys.beckhoff.com/english.php?content=../content/1033/tcadscommon/12440296075.html&id=
// here it says unit is 1 ms ?!

// convert seconds to internal ADS time (milliseconds?)

inline unsigned toBTime(double t)
{
	return unsigned(t / 0.001); // (100e-9 for 100ns)
}

unsigned BeckhoffAds::addNotification(unsigned group, unsigned offset, int length, NotificationMode mode, double maxt,
                                      double cycle, Function<void, const ByteArray&> f)
{
	StreamBuffer buffer;
	buffer.setEndian(ENDIAN_LITTLE);
	buffer << (uint32_t)group << (uint32_t)offset << (uint32_t)length;
	buffer << (uint32_t)mode << (uint32_t)toBTime(maxt) << (uint32_t)toBTime(cycle);
	buffer << (uint32_t)0 << (uint32_t)0 << (uint32_t)0 << (uint32_t)0;

	if (!send(ADSCOM_ADDDEVICENOTIF, buffer))
		return 0;

	ByteArray response = getResponse();
	if (!response)
	{
		return 0;
	}
	StreamBufferReader reader(response);
	uint32_t           error, handle;
	reader >> error >> handle;
	if (error != 0)
	{
		printf("bad ADS response %x\n", error);
		return 0;
	}

	_callbacks[handle] = f;
	return handle;
}

unsigned BeckhoffAds::addNotification(unsigned handle, int length, NotificationMode mode, double maxt, double cycle,
                                      Function<void, const ByteArray&> f)
{
	return addNotification(ADSIGRP_VALBYHND, handle, length, mode, maxt, cycle, f);
}

unsigned BeckhoffAds::addNotification(const asl::String& name, int length, NotificationMode mode, double maxt,
                                      double cycle, Function<void, const ByteArray&> f)
{
	unsigned handle = getHandle(name);
	if (!handle)
		return 0;
	_handles << handle;
	return addNotification(handle, length, mode, maxt, cycle, f);
}

unsigned BeckhoffAds::removeNotification(unsigned handle)
{
	StreamBuffer buffer;
	buffer.setEndian(ENDIAN_LITTLE);
	buffer << (uint32_t)handle;

	if (!send(ADSCOM_DELDEVICENOTIF, buffer))
		return 0;

	ByteArray response = getResponse();
	if (!response)
	{
		return 0;
	}
	StreamBufferReader reader(response);
	uint32_t           error;
	reader >> error;
	if (error != 0)
	{
		printf("bad ADS response %x\n", error);
		return 0;
	}
	return handle;
}

unsigned BeckhoffAds::getHandle(const asl::String& name)
{
	ByteArray response = readWrite(ADSIGRP_HNDBYNAME, 0, 4, ByteArray((byte*)*name, name.length() + 1));
	if (response.length() != 4)
	{
		printf("cannot get handle of %s\n", *name);
		return 0;
	}
	StreamBufferReader reader(response);
	return reader.read<unsigned>();
}

void BeckhoffAds::releaseHandle(unsigned handle)
{
	ByteArray data(4);
	memcpy(data.ptr(), &handle, sizeof(handle));
	bool ok = write(ADSIGRP_RELEASEHND, 0, data);
	if (!ok)
	{
		printf("cannot release handle %u\n", handle);
		return;
	}
}

BeckhoffAds::State BeckhoffAds::getState()
{
	BeckhoffAds::State state = { 0, 0 };
	if (!send(ADSCOM_READSTATE, asl::ByteArray()))
		return state;
	ByteArray response = getResponse();
	if (!response)
	{
		printf("Cannot read state\n");
		return state;
	}
	StreamBufferReader reader(response);
	uint32_t           error;
	uint16_t           stat, devstate;
	reader >> error >> stat >> devstate;
	if (error != 0)
	{
		printf("bad ADS response %x\n", error);
		return state;
	}

	state.state = stat;
	state.deviceState = devstate;
	return state;
}

asl::ByteArray BeckhoffAds::readValue(const asl::String& name, int n)
{
	ByteArray response = readWrite(ADSIGRP_VALBYNAME, 0, n, ByteArray((byte*)*name, name.length() + 1));
	if (response.length() != n)
	{
		printf("error reading value of %s\n", *name);
		return asl::ByteArray();
	}
	return response;
}

bool BeckhoffAds::writeValue(const asl::String& name, const asl::ByteArray& data)
{
	unsigned handle = getHandle(name);
	return write(ADSIGRP_VALBYHND, handle, data);
}

asl::ByteArray BeckhoffAds::readValue(unsigned handle, int n)
{
	ByteArray response = read(ADSIGRP_VALBYHND, handle, n);
	if (response.length() != n)
	{
		printf("cannot read value\n");
		return asl::ByteArray();
	}
	return response;
}
