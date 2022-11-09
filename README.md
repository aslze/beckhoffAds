# beckhoffAds

**A C++ Beckhoff AMS/ADS protocol client**

This is a basic implementation of a client of the AMS/ADS protocol over TCP/IP. Class `BeckhoffAds` allows connecting to a TwinCAT device and reading and writing variables. 

Requires the [ASL](https://github.com/aslze/asl) library. With a recent cmake it will be downloaded if needed.

Some basic usage:

```cpp
BeckhoffAds ads;

ads.connect("192.168.0.2", 852);
```

You can also set source and target NetID and ports manually but it will try to guess if not. You will also need to set up twincat *routes* to remote client devices (`C:/TwinCAT/3.1/Target/StaticRoutes.xml`).

You can then read and write variables:

```cpp
float speed = ads.readValue<float>("GVL.speed");
ads.writeValue<int>("GVL.count", 0);
```

You have to take into account what C++ type correspond to each ADS type (e.g. INT -> int16_t, DINT -> int32_t, REAL -> float, BOOL -> char, etc.).


You can also register notifications, so that a callback will be called when a variable changes:

```cpp
ads.onChange<int>("GVL.count", [&](int value)
{
	printf("Notified of 'count' change to %i\n", value);
});
```

On older compilers without lambdas you can use a function pointer or a functor as the notification handler instead.

Communication errors can be detected with:

```cpp
ads.hasFatalError();
```
