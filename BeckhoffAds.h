// Copyright(c) 2019-2021 aslze

#ifndef ADSLIB_H
#define ADSLIB_H

#include <asl/Socket.h>
#include <asl/Mutex.h>
#include <asl/StreamBuffer.h>
#include <asl/Map.h>
#include <asl/util.h>

struct BeckhoffThread;

/**
An interface to Beckhoff PLC or TwinCAT software using the AMS/ADS protocol over TCP/IP.
*/
class BeckhoffAds
{
	friend BeckhoffThread;

public:
	struct State
	{
		int state;
		int deviceState;
	};

	struct NetId
	{
		NetId(): data(6) { }
		NetId(const char* s);
		NetId(const asl::String& s);
		asl::Array<byte> data;
	};

	enum NotificationMode
	{
		NOTIF_CYCLE = 3,
		NOTIF_CHANGE = 4
	};

	BeckhoffAds();
	~BeckhoffAds();

	/**
	Connects to an ADS device at the given host via TCP/IP
	*/
	bool connect(const asl::String& host);

	/**
	Disconnects from the remote host
	*/
	void disconnect();

	bool checkConnection();
	
	/**
	Sets the NetID and port of the source for communications
	*/
	void setSource(const NetId& net, int port);
	/**
	Sets the NetID and port of the target for communications
	*/
	void setTarget(const NetId& net, int port);
	
	//bool writePacket(const asl::Array<byte>& data);

	/**
	Sends an ADS  packet with the given command ID and data
	*/
	bool send(int command, const asl::Array<byte>& data);

	/**
	Reads an incoming ADS packet
	*/
	asl::Array<byte> readPacket();

	asl::Array<byte> getResponse();

	/**
	Writes data to a given index group and offset
	*/
	bool write(unsigned group, unsigned offset, const asl::Array<byte>& data);

	/**
	Reads data from a given index group and offset, and given byte length
	*/
	asl::Array<byte> read(unsigned group, unsigned offset, int length);

	/**
	Reads and writes data at a given index group and offset
	*/
	asl::Array<byte> readWrite(unsigned group, unsigned offset, int length, const asl::Array<byte>& data);

	/**
	Enables notifications for an index group and offset and returns a handle, times are in seconds
	*/
	unsigned addNotification(unsigned group, unsigned offset, int length, NotificationMode mode, double maxt, double cycle,
		asl::Function<void, const asl::Array<byte>&> f);

	/**
	Enables notifications for a variable given its handle and returns a notification handle, times are in seconds
	*/
	unsigned addNotification(unsigned handle, int length, NotificationMode mode, double maxt, double cycle,
		asl::Function<void, const asl::Array<byte>&> f);

	/**
	Enables notifications for a variable given its name and returns a notification handle, times are in seconds
	*/
	unsigned addNotification(const asl::String& name, int length, NotificationMode mode, double maxt, double cycle,
		asl::Function<void, const asl::Array<byte>&> f);

	/**
	Disables notifications for previously returned notification handle
	*/
	unsigned removeNotification(unsigned handle);

	/**
	Gets the handle associated to a variable name
	*/
	unsigned getHandle(const asl::String& name);

	/**
	Releases a handle
	*/
	void releaseHandle(unsigned handle);

	/**
	Gets the state of the ADS server and device
	*/
	State getState();

	/**
	Reads a named variable as data
	*/
	asl::Array<byte> readValue(const asl::String& name, int n);

	/**
	Writes a named variable as data
	*/
	bool writeValue(const asl::String& name, const asl::Array<byte>& data);

	/**
	Reads a variable given a handle as data
	*/
	asl::Array<byte> readValue(unsigned handle, int n);

	/**
	Reads a variable given a handle as a specific type
	*/
	template<class T>
	T readValue(unsigned handle)
	{
		asl::Array<byte> response = readValue(handle, sizeof(T));
		asl::StreamBufferReader buffer(response);
		T value = buffer.read<T>();
		return value;
	}

	/**
	Reads a named variable as a specific type
	*/
	template<class T>
	T readValue(const char* name)
	{
		asl::Array<byte> response = readValue(String(name), sizeof(T));
		asl::StreamBufferReader buffer(response);
		T value = buffer.read<T>();
		return value;
	}

	/**
	Writes a named variable as a specific type
	*/
	template<class T>
	bool writeValue(const char* name, const T& value)
	{
		asl::StreamBuffer buffer;
		buffer << value;
		return writeValue(String(name), *buffer);
	}
	
	template<class T>
	unsigned addNotification(const asl::String& name, NotificationMode mode, double maxt, double cycle, asl::Function<void, T> f)
	{
		struct NotFunctor {
			asl::Function<void, T> _f;
			void operator()(const asl::Array<byte>& data) { _f(asl::StreamBufferReader(data).read<T>()); }
			NotFunctor(const asl::Function<void, T>& f) : _f(f) {}
		} ftor(f);
		return addNotification(name, sizeof(T), mode, maxt, cycle, ftor);
	}


protected:

	void processNotification(const asl::Array<byte>& data);

	void receiveLoop();

protected:
	asl::Socket _socket;
	asl::String _host;
	asl::Mutex _mutex;
	asl::Semaphore _sem;
	bool _connected;
	bool _error;
	NetId _source;
	NetId _target;
	unsigned _invokeId;
	int _sourcePort;
	int _targetPort;
	asl::Array<unsigned> _handles;
	asl::Array<unsigned> _notifications;
	asl::Dic<unsigned> _namedHandles;
	asl::Map<asl::ULong, asl::Array<byte> > _responses;
	asl::Array<byte> _response;
	BeckhoffThread* _thread;
	asl::ULong _lastRequestId;
	asl::Map<unsigned, asl::Function<void, const asl::Array<byte>&>> _callbacks;
};


#endif

