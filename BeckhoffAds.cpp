// Copyright(c) 2019-2022 aslze
// Licensed under the MIT License (http://opensource.org/licenses/MIT)

// https://infosys.beckhoff.com/

#define NOTIF_THREAD // notifications start a thread each

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
	ADSIGRP_SYM_UPLOAD = 0xF00B,
	ADSIGRP_SYM_UPLOADINFO = 0xF00C,
	ADSIGRP_DEVICE_DATA = 0xF100,
	ADSIOFFS_DEVDATA_ADSSTATE = 0,
	ADSIOFFS_DEVDATA_DEVSTATE = 2,
};

asl::Map<int, asl::String> adsErrors = String("6:Port not found,"
                                              "7:Target not found,"
                                              "18:Port disabled,"
                                              "1793:Service not supported,"
                                              "1797:Bad size,"
                                              "1827:Access denied,"
                                              "1810:Invalid state,"
                                              "1803:Invalid parameters,"
                                              "1808:Symbol not found,"
                                              "1809:Invalid handle,"
                                              "1812:Invalid notification handle,"
                                              "1829:License expired,"
                                              "1843:Invalid function")
                                           .split(",", ":");

void BeckhoffAds::NetId::set(const asl::String& s)
{
	if (!s.ok())
		return;

	Array<String> parts = s.split('.');
	if (parts.length() != 6)
	{
		printf("ADS: Bad NetID %s\n", *s);
		return;
	}
	for (int i = 0; i < 6; i++)
		data[i] = (byte)(int)parts[i];
}

String BeckhoffAds::NetId::toString() const
{
	return data.join('.');
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
	_sourcePort = (uint16_t)34000;
	_targetPort = 851;
	_invokeId = 0;
	_thread = 0;
	_lastError = 0;
	_adsError = 0;
}

BeckhoffAds::~BeckhoffAds()
{
	disconnect();
	sleep(0.1);
}

bool BeckhoffAds::connect(const String& host, int adsPort)
{
	_host = host | "127.0.0.1";
	_lastError = _adsError = 0;
	Lock _(_mutex);
	_socket = Socket();
	_connected = _socket.connect(_host, 48898);

	if (adsPort >= 0)
		_targetPort = adsPort;

	if (!_connected)
	{
		printf("ADS: Error connecting\n");
	}
	else
	{
		if (!_source)
			_source.set(_socket.localAddress().toString().split(':')[0] + ".1.1");
		if (!_target)
			_target.set(_socket.remoteAddress().toString().split(':')[0] + ".1.1");
		if (_host == "127.0.0.1")
		{
			_socket << (ByteArray(), 0, 16, 2, 0, 0, 0, 0, 0);
			ByteArray res = _socket.read(14);
			if (res.length() == 14)
			{
				memcpy(_source.data.ptr(), res.ptr() + 6, 6);
				_sourcePort = (unsigned short)res[12] | ((unsigned short)res[13] << 8);
			}
			else
				printf("oops\n");
		}

		_thread = new BeckhoffThread;
		_thread->_ads = this;
		_thread->start();
	}
	return _connected;
}

void BeckhoffAds::disconnect()
{
	if (!_connected)
		return;
	foreach (unsigned h, _notifications)
		removeNotification(h);

	foreach (unsigned h, _handles)
		releaseHandle(h);

	_handles.clear();
	_notifications.clear();
	sleep(0.2);
	_connected = false;
	if (_thread)
		_thread->join();
	delete _thread;
	_socket.close();
}

bool BeckhoffAds::checkConnection()
{
	if (_connected && _socket.disconnected())
	{
		printf("ADS: Peer disconnected (invokeid: %u)\n", _invokeId);
		_lastError = -5;
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

	_adsError = 0;

	StreamBuffer buffer(ENDIAN_LITTLE);

	buffer << uint16_t(0) << uint32_t(data.length() + 32); // AMS/TCP Header
	buffer << _target.data << uint16_t(_targetPort) << _source.data << uint16_t(_sourcePort);
	buffer << uint16_t(command) << uint16_t(0x0004) << uint32_t(data.length());
	buffer << uint32_t(0) << uint32_t(_invokeId);
	buffer << data;

	int n = _socket.write(buffer.ptr(), buffer.length());
	if (n != buffer.length())
	{
		printf("ADS: send failed %i\n", n);
		_lastError = -6;
		return false;
	}

	_lastRequestId = pack(command, _invokeId);

	_invokeId = (_invokeId + 1) % (1 << 30);

	return true;
}

void BeckhoffAds::receiveLoop()
{
	while (_connected)
	{
		if (_socket.waitInput(1))
		{
			if (_socket.disconnected())
				break;
			ByteArray data = readPacket();
		}
	}
	_newdata.post();
}

void BeckhoffAds::processNotification(const ByteArray& data)
{
	StreamBufferReader buffer(data);
	uint32_t           length = 0, stamps = 0;
	buffer >> length >> stamps;

	for (unsigned i = 0; i < stamps; i++)
	{
		if (buffer.length() < 12)
			return;
		ULong    time = 0;
		uint32_t samples = 0;
		buffer >> time >> samples;
		Date t = time * 100e-9 - 11644473600.0;

		for (unsigned j = 0; j < samples; j++)
		{
			if (buffer.length() < 8)
				return;
			uint32_t handle = 0, size = 0;
			buffer >> handle >> size;
			if ((unsigned)buffer.length() < size)
			{
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
		_lastError = -4;
		_newdata.post();
		return data.resize(0);
	}
	data = _socket.read(totalLen);
	StreamBufferReader buffer(data);

	uint16_t portT, portS, commandId, flags;
	buffer.skip(6); // target net
	buffer >> portT;
	buffer.skip(6); // source net
	buffer >> portS;

	if (portT != _sourcePort || portS != _targetPort) // not my conversation
		return data.resize(0);

	uint32_t len = 0, error = 0, invokeId;
	buffer >> commandId >> flags >> len >> error >> invokeId;
	if (buffer.length() != len)
		printf("ADS: len=%u, remaining=%i\n", len, buffer.length());
	if (error != 0)
	{
		_adsError = error;
		printf("ADS: error: (%u) %s\n", error, *adsErrors[error]);
		_newdata.post();
		return data.resize(0); // 8?
	}

	data = buffer.read(len);

	if ((flags & 1) == 0 && commandId != ADSCOM_DEVICENOTIF)
	{
		printf("ADS: received request, not response (cmd: %u)\n", commandId);
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
	case ADSCOM_READDEVICEINFO:
	case ADSCOM_WRITECTRL:
		_response = data.clone();
		_responses[pack(commandId, invokeId)] = _response;
		_newdata.post();
		break;
	default:;
	}
	return data;
}

bool BeckhoffAds::hasError() const
{
	return _adsError != 0;
}

bool BeckhoffAds::hasFatalError() const
{
	return _lastError != 0;
}

ByteArray BeckhoffAds::getResponse()
{
	for (int i = 0; i < 5; i++)
	{
		_newdata.wait();
		Lock _(_mutex);
		if (_adsError != 0)
			return ByteArray();
		if (!_responses.has(_lastRequestId))
		{
			printf("ADS: Timeout waiting response\n");
			_lastError = -3;
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
		printf("ADS: write error (%u) %s\n", error, *adsErrors[error]);
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
	if (error != 0 || len > 60000)
	{
		printf("ADS: read error (%u) %s\n", error, *adsErrors[error]);
		_adsError = error;
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
		printf("ADS: readWrite error (%u) %s\n", error, *adsErrors[error]);
		_adsError = error;
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
	return uint32_t(t / 100e-9); // for 100ns
}

BeckhoffAds::Handle BeckhoffAds::addNotification(unsigned group, unsigned offset, int length, NotificationMode mode,
                                                 double maxt, double cycle, Function<void, const ByteArray&> f)
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
		return Handle();
	}
	StreamBufferReader reader(response);
	uint32_t           error, handle;
	reader >> error >> handle;
	if (error != 0)
	{
		printf("ADS: addNotification error: (%u) %s\n", error, *adsErrors[error]);
		_adsError = error;
		return Handle();
	}

	_callbacks[handle] = f;
	_notifications << handle;
	return handle;
}

BeckhoffAds::Handle BeckhoffAds::addNotification(BeckhoffAds::Handle handle, int length, NotificationMode mode,
                                                 double maxt, double cycle, Function<void, const ByteArray&> f)
{
	return addNotification(ADSIGRP_VALBYHND, handle.h, length, mode, maxt, cycle, f);
}

BeckhoffAds::Handle BeckhoffAds::addNotification(const asl::String& name, int length, NotificationMode mode, double maxt,
                                                 double cycle, Function<void, const ByteArray&> f)
{
	BeckhoffAds::Handle handle = getHandle(name);
	if (!handle)
		return handle;
	_handles << handle.h;
	return addNotification(handle, length, mode, maxt, cycle, f);
}

bool BeckhoffAds::removeNotification(BeckhoffAds::Handle handle)
{
	StreamBuffer buffer(ENDIAN_LITTLE);
	buffer << (uint32_t)handle.h;

	Lock _(_cmdMutex);

	if (!send(ADSCOM_DELDEVICENOTIF, buffer))
		return false;

	ByteArray response = getResponse();
	if (!response)
		return false;

	StreamBufferReader reader(response);
	uint32_t           error;
	reader >> error;
	if (error != 0)
	{
		printf("ADS: removeNotification error (%u) %s\n", error, *adsErrors[error]);
		return false;
	}

	_callbacks.remove(handle.h);

	return true;
}

BeckhoffAds::Handle BeckhoffAds::getHandle(const asl::String& name)
{
	ByteArray response = readWrite(ADSIGRP_HNDBYNAME, 0, 4, ByteArray((byte*)*name, name.length() + 1));
	if (response.length() != 4)
	{
		printf("ADS: cannot get handle of %s\n", *name);
		return Handle();
	}
	return StreamBufferReader(response).read<unsigned>();
}

void BeckhoffAds::releaseHandle(BeckhoffAds::Handle handle)
{
	StreamBuffer buffer(ENDIAN_LITTLE);
	buffer << (uint32_t)handle.h;
	if (!write(ADSIGRP_RELEASEHND, 0, buffer))
	{
		printf("ADS: cannot release handle %u\n", handle.h);
	}
}

BeckhoffAds::State BeckhoffAds::getState()
{
	Lock _(_cmdMutex);

	BeckhoffAds::State state = { 0, 0, true };
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
		printf("ADS: getState error %u\n", error);
		return state;
	}

	state.state = stat;
	state.deviceState = devstate;
	state.invalid = false;
	return state;
}

BeckhoffAds::DevInfo BeckhoffAds::getInfo()
{
	DevInfo info = { 0, 0, 0 };
	Lock    _(_cmdMutex);

	if (!send(ADSCOM_READDEVICEINFO, ByteArray()))
		return info;
	ByteArray response = getResponse();
	if (!response || response.length() != 24)
	{
		printf("ADS: Cannot read device info\n");
		return info;
	}
	StreamBufferReader reader(response);
	uint32_t           error;
	byte               major, minor;
	uint16_t           build;
	reader >> error >> major >> minor >> build;
	ByteArray name = reader.read(16);
	if (error != 0)
	{
		printf("ADS: getInfo error %u\n", error);
		return info;
	}

	info.minor = minor;
	info.major = major;
	info.build = build;
	info.name = name;
	return info;
}

Array<BeckhoffAds::SymInfo> BeckhoffAds::getSymbols()
{
	Array<BeckhoffAds::SymInfo> info;

	ByteArray data = read(ADSIGRP_SYM_UPLOADINFO, 0, 8);

	if (data.length() < 8)
		return info;

	StreamBufferReader reader(data);
	uint32_t           syms, size;
	reader >> syms >> size;

	data = read(ADSIGRP_SYM_UPLOAD, 0, size);

	if (data.length() < int(size))
		return info;

	reader = StreamBufferReader(data);
	uint32_t len, group, offset, dtype, flags;
	uint16_t namelen, typelen, comlen;

	for (int i = 0; i < int(syms); i++)
	{
		BeckhoffAds::SymInfo sym;
		const byte*          p = reader.ptr();
		reader >> len >> group >> offset >> size >> dtype >> flags >> namelen >> typelen >> comlen;
		sym.name = reader.read(namelen);
		reader.skip(1);
		sym.type = reader.read(typelen);
		reader.skip(1);
		String com = reader.read(comlen);
		reader.skip(len - int(reader.ptr() - p));
		sym.typecode = dtype;
		sym.flags = flags;

		info << sym;
	}

	return info;
}

bool BeckhoffAds::writeControl(BeckhoffAds::State state, const asl::ByteArray& data)
{
	StreamBuffer buffer(ENDIAN_LITTLE);
	buffer << (uint16_t)state.state << (uint16_t)state.deviceState << (uint32_t)data.length() << data;

	Lock _(_cmdMutex);

	if (!send(ADSCOM_WRITECTRL, buffer))
		return false;

	ByteArray response = getResponse();

	if (!response)
		return false;

	StreamBufferReader reader(response);
	uint32_t           error;
	reader >> error;
	if (error != 0)
	{
		printf("ADS: control error (%u) %s\n", error, *adsErrors[error]);
		return false;
	}
	return true;
}

ByteArray BeckhoffAds::readValue(const asl::String& name, int n, bool exact)
{
	ByteArray response = readWrite(ADSIGRP_VALBYNAME, 0, n, ByteArray((byte*)*name, name.length() + 1));
	if (response.length() > n || !response || exact && response.length() != n)
	{
		printf("ADS: error reading value of %s (%i: %s)\n", *name, _adsError, *adsErrors[_adsError]);
		response.clear();
	}
	return response;
}

ByteArray BeckhoffAds::readValue(BeckhoffAds::Handle handle, int n, bool exact)
{
	ByteArray response = read(ADSIGRP_VALBYHND, handle.h, n);
	if (response.length() > n || exact && response.length() != n)
	{
		printf("ADS: cannot read value by handle\n");
		response.clear();
	}
	return response;
}

bool BeckhoffAds::writeValue(const BeckhoffAds::Handle& handle, const asl::ByteArray& data)
{
	return write(ADSIGRP_VALBYHND, handle.h, data);
}
