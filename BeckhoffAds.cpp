// Copyright(c) 2019-2022 aslze

// https://infosys.beckhoff.com/

#define NOTIF_THREAD // notifications start a thread each

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
	const ByteArray                        data;
	asl::Function<void, const ByteArray&>* func;
	NotifThread(const ByteArray& b, asl::Function<void, const ByteArray&>& f) : data(b), func(&f) { start(); }
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

asl::Map<int, asl::String> adsErrors = String("1827:Access denied,"
                                              "1810:Invalid state,"
                                              "1803:Invalid parameters,"
                                              "1808:Symbol not found,"
                                              "1812:Invalid notification handle,"
                                              "1829:License expired,"
                                              "1843:Invalid function")
                                           .split(",", ":");

void BeckhoffAds::NetId::set(const asl::String& s)
{
	Array<String> parts = s.split('.');
	if (parts.length() != 6)
	{
		printf("ADS: Bad NetID %s\n", *s);
		return;
	}
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
	_lastError = 0;
}

BeckhoffAds::~BeckhoffAds()
{
	foreach (unsigned h, _notifications)
		removeNotification(h);

	foreach (unsigned h, _handles)
		releaseHandle(h);

	sleep(0.1);
}

bool BeckhoffAds::connect(const String& host)
{
	_host = host;
	_error = false;
	Lock _(_mutex);
	_socket = Socket();
	_connected = _socket.connect(_host, 48898);
	if (!_connected)
	{
		printf("ADS: Error connecting\n");
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

bool BeckhoffAds::send(int command, const ByteArray& data)
{
	Lock _(_mutex);
	if (!checkConnection())
		return false;

	StreamBuffer buffer(ENDIAN_LITTLE);

	buffer << uint16_t(0) << uint32_t(data.length() + 32); // AMS/TCP Header
	buffer << _target.data << uint16_t(_targetPort) << _source.data << uint16_t(_sourcePort);
	buffer << uint16_t(command) << uint16_t(0x0004) << uint32_t(data.length());
	buffer << uint32_t(0) << uint32_t(_invokeId++);
	buffer << data;

	int n = _socket.write(buffer.ptr(), buffer.length());
	if (n != buffer.length())
	{
		printf("ADS: send failed %i\n", n);
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

void BeckhoffAds::processNotification(const ByteArray& data)
{
	StreamBufferReader buffer(data);
	uint32_t           length = 0, stamps = 0;
	buffer >> length >> stamps;
	// printf("notification: %i stamps\n", stamps);
	for (unsigned i = 0; i < stamps; i++)
	{
		if (buffer.length() < 12)
			return;
		ULong    time = 0;
		uint32_t samples = 0;
		buffer >> time >> samples;
		Date t = time * 100e-9 - 11644473600.0;
		// printf(" notification: %i samples\n", samples);
		for (unsigned j = 0; j < samples; j++)
		{
			if (buffer.length() < 8)
				return;
			uint32_t handle = 0, size = 0;
			buffer >> handle >> size;
			if ((unsigned)buffer.length() < size)
			{
				// printf("ADS: notification data %u bytes, remaining=%i\n", size, buffer.length());
				return;
			}
			if (_callbacks.has(handle))
#ifndef NOTIF_THREAD
				_callbacks[handle](buffer.read(size)); // send more info, like timestamp??
#else
				new NotifThread(buffer.read(size), _callbacks[handle]);
#endif
			else
			{
				// printf("Not my notification\n");
				buffer.skip(size);
			}
			// buffer.skip(size); // this is the actual data
			// printf("  notification at %s.%i for handle %u size %u\n", *t.toUTCString(), int(1000*fract(t.time())),
			// handle, size);
		}
	}
}

ByteArray BeckhoffAds::readPacket()
{
	ByteArray data;
	if (!checkConnection())
		return data;
	uint16_t reserved = 0;
	uint32_t totalLen = 0;
	data = _socket.read(6);
	if (data.length() < 6)
		return data.resize(0);
	StreamBufferReader reader(data);
	reader >> reserved >> totalLen;
	if (_socket.error() || reserved != 0 || totalLen > 5000)
	{
		printf("ADS: bad comm (len=%i reserved=%i, read=%i)\n", totalLen, reserved, data.length());
		_lastError = -1;
		_sem.post();
		return data.resize(0);
	}
	data = _socket.read(totalLen);
	StreamBufferReader buffer(data);
	buffer.skip(16); // target net+port, source net+port
	uint16_t commandId, flags;
	uint32_t len = 0, error = 0, invokeId;
	buffer >> commandId >> flags >> len >> error >> invokeId;
	if (buffer.length() != len)
		printf("ADS: len=%u, remaining=%i\n", len, buffer.length());
	if (error != 0)
	{
		_lastError = error;
		printf("ADS: bad message: (%u) %s\n", error, *adsErrors[error]);
		//_responses[pack(commandId, invokeId)] = ByteArray();
		_sem.post();
		return data.resize(0); // 8?
	}

	data = buffer.read(len);

	if ((flags & 1) == 0 && commandId != ADSCOM_DEVICENOTIF)
	{
		printf("ADS: received request, not response (cmd: %u)\n", commandId);
		_lastError = -2;
		//_sem.post();
		return data.resize(0);
	}

	Lock _(_mutex);

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

ByteArray BeckhoffAds::getResponse()
{
	for (int i = 0; i < 3; i++)
	{
		_sem.wait();
		Lock _(_mutex);
		if (!_responses.has(_lastRequestId))
		{
			printf("ADS: Timeout waiting response\n");
			return ByteArray();
		}

		ByteArray res = _responses[_lastRequestId];

		_responses.remove(_lastRequestId);
		return res;
	}

	return ByteArray();
}

bool BeckhoffAds::write(unsigned group, unsigned offset, const ByteArray& data)
{
	StreamBuffer buffer(ENDIAN_LITTLE);
	buffer << (uint32_t)group << (uint32_t)offset << (uint32_t)data.length();
	buffer << data;

	Lock _(_cmdMutex);

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
		printf("ADS: write: bad response (%u) %s\n", error, *adsErrors[error]);
		return false;
	}
	return true;
}

ByteArray BeckhoffAds::read(unsigned group, unsigned offset, int length)
{
	StreamBuffer buffer(ENDIAN_LITTLE);
	buffer << (uint32_t)group << (uint32_t)offset << (uint32_t)length;

	Lock _(_cmdMutex);

	if (!send(ADSCOM_READ, buffer))
		return ByteArray();
	ByteArray response = getResponse();
	if (!response)
	{
		return ByteArray();
	}
	StreamBufferReader reader(response);
	uint32_t           error, len;
	reader >> error >> len;
	if (error != 0 || len > 1000)
	{
		printf("ADS: read: bad response (%u) %s\n", error, *adsErrors[error]);
		return ByteArray();
	}
	return reader.read(len);
}

ByteArray BeckhoffAds::readWrite(unsigned group, unsigned offset, int length, const ByteArray& data)
{
	StreamBuffer buffer(ENDIAN_LITTLE);
	buffer << (uint32_t)group << (uint32_t)offset << (uint32_t)length << (uint32_t)data.length();
	buffer << data;

	Lock _(_cmdMutex);

	if (!send(ADSCOM_READWRITE, buffer))
		return ByteArray();

	ByteArray response = getResponse();

	if (!response)
		return ByteArray();

	StreamBufferReader reader(response);
	uint32_t           error, len;
	reader >> error >> len;
	if (error != 0 || len > 1000)
	{
		printf("ADS: readWrite: bad response (%u) %s\n", error, *adsErrors[error]);
		return ByteArray();
	}
	return reader.read(len);
}

// ADS notification times are in 100 ns units
// https://infosys.beckhoff.com/english.php?content=../content/1033/tcadscommon/12440296075.html&id=
// here it says unit is 1 ms ?!

// convert seconds to internal ADS time

inline uint32_t toBTime(double t)
{
	return uint32_t(t / 100e-9); // for 100ns)
}

unsigned BeckhoffAds::addNotification(unsigned group, unsigned offset, int length, NotificationMode mode, double maxt,
                                      double cycle, Function<void, const ByteArray&> f)
{
	StreamBuffer buffer(ENDIAN_LITTLE);
	buffer << (uint32_t)group << (uint32_t)offset << (uint32_t)length;
	buffer << (uint32_t)mode << toBTime(maxt) << toBTime(cycle);
	buffer << (uint32_t)0 << (uint32_t)0 << (uint32_t)0 << (uint32_t)0;

	Lock _(_cmdMutex);

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
		printf("ADS: addNotification: bad response (%u) %s\n", error, *adsErrors[error]);
		return 0;
	}

	_callbacks[handle] = f;
	_notifications << handle;
	return handle;
}

unsigned BeckhoffAds::addNotificationH(unsigned handle, int length, NotificationMode mode, double maxt, double cycle,
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
	return addNotificationH(handle, length, mode, maxt, cycle, f);
}

unsigned BeckhoffAds::removeNotification(unsigned handle)
{
	StreamBuffer buffer(ENDIAN_LITTLE);
	buffer << (uint32_t)handle;

	Lock _(_cmdMutex);

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
		_error = true;
		printf("ADS: bad response %u\n", error);
		return 0;
	}

	_callbacks.remove(handle);

	return handle;
}

unsigned BeckhoffAds::getHandle(const asl::String& name)
{
	ByteArray response = readWrite(ADSIGRP_HNDBYNAME, 0, 4, ByteArray((byte*)*name, name.length() + 1));
	if (response.length() != 4)
	{
		printf("ADS: cannot get handle of %s\n", *name);
		return 0;
	}
	unsigned h = StreamBufferReader(response).read<unsigned>();
	//_handles << h;
	return h;
}

void BeckhoffAds::releaseHandle(unsigned handle)
{
	ByteArray data((byte*)&handle, sizeof(handle));
	// memcpy(data.ptr(), &handle, sizeof(handle));
	bool ok = write(ADSIGRP_RELEASEHND, 0, data);
	if (!ok)
	{
		printf("ADS: cannot release handle %u\n", handle);
	}
}

BeckhoffAds::State BeckhoffAds::getState()
{
	Lock _(_cmdMutex);

	BeckhoffAds::State state = { 0, 0 };
	if (!send(ADSCOM_READSTATE, ByteArray()))
		return state;
	ByteArray response = getResponse();
	if (!response)
	{
		printf("ADS: Cannot read state\n");
		return state;
	}
	StreamBufferReader reader(response);
	uint32_t           error;
	uint16_t           stat, devstate;
	reader >> error >> stat >> devstate;
	if (error != 0)
	{
		printf("ADS: bad response %u\n", error);
		return state;
	}

	state.state = stat;
	state.deviceState = devstate;
	return state;
}

ByteArray BeckhoffAds::readValue(const asl::String& name, int n)
{
	ByteArray response = readWrite(ADSIGRP_VALBYNAME, 0, n, ByteArray((byte*)*name, name.length() + 1));
	if (response.length() != n)
	{
		printf("ADS: error reading value of %s\n", *name);
		response.clear();
	}
	return response;
}

bool BeckhoffAds::writeValue(const asl::String& name, const ByteArray& data)
{
	unsigned handle = getHandle(name);
	return write(ADSIGRP_VALBYHND, handle, data);
}

ByteArray BeckhoffAds::readValue(unsigned handle, int n)
{
	ByteArray response = read(ADSIGRP_VALBYHND, handle, n);
	if (response.length() != n)
	{
		printf("ADS: cannot read value\n");
		response.clear();
	}
	return response;
}
