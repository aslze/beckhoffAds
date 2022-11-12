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
		int  state;
		int  deviceState;
		bool invalid;
	};

	struct DevInfo
	{
		int         minor, major, build;
		asl::String name;
	};

	struct SymInfo
	{
		asl::String name;
		asl::String type;
		int         typecode;
		unsigned    flags;
	};

	struct Handle
	{
		unsigned h;
		bool     ok;
		Handle() : h(0), ok(false) {}
		Handle(unsigned h) : h(h), ok(true) {}
		bool operator!() const { return !ok; }
	};

	struct NetId
	{
		asl::ByteArray data;

		NetId() : data(6, 0) {}
		NetId(const char* s) : data(6, 0) { set(s); }
		NetId(const asl::String& s) : data(6, 0) { set(s); }
		void        set(const asl::String& s);
		bool        operator!() const { return data == asl::ByteArray(6, 0); }
		asl::String toString() const;
	};

	enum NotificationMode
	{
		NOTIF_CYCLE = 3,
		NOTIF_CHANGE = 4
	};

	BeckhoffAds();
	~BeckhoffAds();

	/**
	Connects to an ADS device at the given host via TCP/IP and given ADS port (default 851); it target NetID not set it
	will use the host's IP + ".1.1".
	*/
	bool connect(const asl::String& host, int adsPort = -1);

	/**
	Disconnects from the remote host
	*/
	void disconnect();

	bool connected() const { return _connected; }

	/**
	Sets the NetID and port of the source for communications (set before connect)
	*/
	void setSource(const NetId& net, int port);

	/**
	Sets the NetID and port of the target for communications (set before connect)
	*/
	void setTarget(const NetId& net, int port);

	/**
	Gets the state of the ADS server and device
	*/
	State getState();

	/**
	Gets device information including version and device name
	*/
	DevInfo getInfo();

	/**
	Gets the list of symbols (variable names) in the device, with their types
	*/
	asl::Array<BeckhoffAds::SymInfo> getSymbols();

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
	Handle addNotification(unsigned group, unsigned offset, int length, NotificationMode mode, double maxt, double cycle,
	                       asl::Function<void, const asl::ByteArray&> f);

	/**
	Enables notifications for a variable given its handle and returns a notification handle, times are in seconds
	*/
	Handle addNotification(Handle handle, int length, NotificationMode mode, double maxt, double cycle,
	                       asl::Function<void, const asl::ByteArray&> f);

	/**
	Enables notifications for a variable given its name and returns a notification handle, times are in seconds
	*/
	Handle addNotification(const asl::String& name, int length, NotificationMode mode, double maxt, double cycle,
	                       asl::Function<void, const asl::ByteArray&> f);

	/**
	Disables notifications for previously returned notification handle
	*/
	bool removeNotification(Handle handle);

	/**
	Gets the handle associated to a variable name
	*/
	Handle getHandle(const asl::String& name);

	/**
	Releases a handle
	*/
	void releaseHandle(Handle handle);

	/**
	Reads a variable given a handle as data
	*/
	asl::ByteArray readValue(Handle handle, int n, bool exact = false);

	/**
	Reads a named variable as data
	*/
	asl::ByteArray readValue(const asl::String& name, int n, bool exact = false);

	/**
	Writes a named variable as data
	*/
	bool writeValue(const asl::String& name, const asl::ByteArray& data);

	/**
	Reads a variable given a handle as a specific type
	*/
	template<class T>
	T readValue(Handle handle)
	{
		asl::ByteArray response = readValue(handle, sizeof(T), true);
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
	Handle addNotification(const asl::String& name, NotificationMode mode, double maxt, double cycle,
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
	 * \param maxt max delay (?)
	 */
	template<class T>
	Handle onChange(const asl::String& name, const asl::Function<void, T>& f, double interval = 0.01, double maxt = 0.01)
	{
		return addNotification(name, NOTIF_CHANGE, maxt, interval, f);
	}

	/**
	 * Returns the code of the last error
	 */
	int lastError() const { return _adsError; }

	/**
	 * Return true if there were errors
	 */
	bool hasError() const;

	/**
	 * Return true if there were fatal errors like communication failures
	 */
	bool hasFatalError() const;

protected:
	asl::ByteArray getResponse();
	void           processNotification(const asl::ByteArray& data);
	bool           checkConnection();
	void           receiveLoop();
	bool           send(int command, const asl::ByteArray& data);
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
	int                                                            _adsError;
	asl::Array<unsigned>                                           _handles;
	asl::Array<unsigned>                                           _notifications;
	asl::Map<asl::ULong, asl::ByteArray>                           _responses;
	asl::ByteArray                                                 _response;
	BeckhoffThread*                                                _thread;
	asl::ULong                                                     _lastRequestId;
	asl::Map<unsigned, asl::Function<void, const asl::ByteArray&>> _callbacks;
};

#endif
