// Copyright(c) 2019-2022 aslze

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
		asl::ByteArray data;

		NetId() : data(6, 0) {}
		NetId(const char* s) : data(6, 0) { set(s); }
		NetId(const asl::String& s) : data(6, 0) { set(s); }
		void set(const asl::String& s);
		bool operator!() const { return data == asl::ByteArray(6, 0); }
	};

	enum NotificationMode
	{
		NOTIF_CYCLE = 3,
		NOTIF_CHANGE = 4
	};

	BeckhoffAds();
	~BeckhoffAds();

	/**
	Connects to an ADS device at the given host via TCP/IP and given ADS port (default 851)
	*/
	bool connect(const asl::String& host, int adsPort = -1);

	/**
	Disconnects from the remote host
	*/
	void disconnect();

	/**
	Sets the NetID and port of the source for communications
	*/
	void setSource(const NetId& net, int port);

	/**
	Sets the NetID and port of the target for communications
	*/
	void setTarget(const NetId& net, int port);

	/**
	Writes data to a given index group and offset
	*/
	bool write(unsigned group, unsigned offset, const asl::ByteArray& data);

	/**
	Reads data from a given index group and offset, and given byte length
	*/
	asl::ByteArray read(unsigned group, unsigned offset, int length);

	/**
	Reads and writes data at a given index group and offset
	*/
	asl::ByteArray readWrite(unsigned group, unsigned offset, int length, const asl::ByteArray& data);

	/**
	Enables notifications for an index group and offset and returns a handle, times are in seconds
	*/
	unsigned addNotification(unsigned group, unsigned offset, int length, NotificationMode mode, double maxt,
	                         double cycle, asl::Function<void, const asl::ByteArray&> f);

	/**
	Enables notifications for a variable given its handle and returns a notification handle, times are in seconds
	*/
	unsigned addNotificationH(unsigned handle, int length, NotificationMode mode, double maxt, double cycle,
	                          asl::Function<void, const asl::ByteArray&> f);

	/**
	Enables notifications for a variable given its name and returns a notification handle, times are in seconds
	*/
	unsigned addNotification(const asl::String& name, int length, NotificationMode mode, double maxt, double cycle,
	                         asl::Function<void, const asl::ByteArray&> f);

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
	asl::ByteArray readValue(const asl::String& name, int n, bool exact = false);

	/**
	Writes a named variable as data
	*/
	bool writeValue(const asl::String& name, const asl::ByteArray& data);

	/**
	Reads a variable given a handle as data
	*/
	asl::ByteArray readValueH(unsigned handle, int n, bool exact = false);

	/**
	Reads a variable given a handle as a specific type
	*/
	template<class T>
	T readValueH(unsigned handle)
	{
		asl::ByteArray response = readValueH(handle, sizeof(T), true);
		if (!response)
			return T();
		return asl::StreamBufferReader(response).read<T>();
	}

	/**
	Reads a named variable as a specific type
	*/
	template<class T>
	T readValue(const asl::String& name)
	{
		asl::ByteArray response = readValue(name, sizeof(T), true);
		if (!response)
			return T();
		return asl::StreamBufferReader(response).read<T>();
	}

	/**
	Writes a named variable as a specific type
	*/
	template<class T>
	bool writeValue(const asl::String& name, const T& value)
	{
		return writeValue(name, *(asl::StreamBuffer() << value));
	}

	template<class T>
	unsigned addNotification(const asl::String& name, NotificationMode mode, double maxt, double cycle,
	                         const asl::Function<void, T>& f)
	{
		struct NotFunctor
		{
			asl::Function<void, T> _f;
			void                   operator()(const asl::ByteArray& data) { _f(asl::StreamBufferReader(data).read<T>()); }
			NotFunctor(const asl::Function<void, T>& f) : _f(f) {}
		} ftor(f);
		return addNotification(name, sizeof(T), mode, maxt, cycle, ftor);
	}

	/**
	 * Sets a function to be called when a variable by name changes value.
	 * \param name variable name
	 * \param f functor to be called with the new value
	 * \param interval variable checked internally every t seconds
	 * \param maxt events accumulate up to maxt seconds and are sent together (apparently)
	 */
	template<class T>
	unsigned onChange(const asl::String& name, const asl::Function<void, T>& f, double interval = 0.01,
	                  double maxt = 0.01)
	{
		return addNotification(name, NOTIF_CHANGE, maxt, interval, f);
	}

	/**
	 * Returns the code of the last error
	 */
	int lastError() const { return _lastError; }

	/**
	 * Return true if there were errors
	 */
	bool hasError() const { return _lastError != 0; }

protected:
	asl::ByteArray getResponse();
	void processNotification(const asl::ByteArray& data);
	bool checkConnection();
	void receiveLoop();
	
	/**
	Sends an ADS  packet with the given command ID and data
	*/
	bool send(int command, const asl::ByteArray& data);

	/**
	Reads an incoming ADS packet
	*/
	asl::ByteArray readPacket();

protected:
	asl::Socket                                                    _socket;
	asl::String                                                    _host;
	asl::Mutex                                                     _mutex;
	asl::Mutex                                                     _cmdMutex;
	asl::Semaphore                                                 _sem;
	bool                                                           _connected;
	NetId                                                          _source;
	NetId                                                          _target;
	unsigned                                                       _invokeId;
	int                                                            _sourcePort;
	int                                                            _targetPort;
	int                                                            _lastError;
	asl::Array<unsigned>                                           _handles;
	asl::Array<unsigned>                                           _notifications;
	asl::Dic<unsigned>                                             _namedHandles;
	asl::Map<asl::ULong, asl::ByteArray>                           _responses;
	asl::ByteArray                                                 _response;
	BeckhoffThread*                                                _thread;
	asl::ULong                                                     _lastRequestId;
	asl::Map<unsigned, asl::Function<void, const asl::ByteArray&>> _callbacks;
};

#endif
