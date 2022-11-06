# beckhoffAds

**A C++ Beckhoff AMS/ADS protocol client**

This is a basic implementation of a client of the AMS/ADS protocol over TCP/IP. Class `BeckhoffAds` allows connecting to a TwinCAT device and reading and writing variables. 

Requires the [ASL](https://github.com/aslze/asl) library.

Some basic usage:

First, establish source and target net IDs and ports, and connect to the server.

```cpp
BeckhoffAds ads;
ads.setSource("192.168.0.2.1.2", 34000);
ads.setTarget("192.168.0.2.1.1", 851);
ads.connect("192.168.0.2");
```

You will need to set up twincat *routes* (`C:/TwinCAT/3.1/Target/StaticRoutes.xml`).

You can then read and write variables:

```cpp
float speed = ads.readValue<float>("GVL.speed");
ads.writeValue<short>("GVL.count", 0);
```

You have to take into account what C++ type correspond to each ADS type (e.g. INT -> int16_t, REAL -> float, BOOL -> char, etc.).


You can also register notifications, so that a callback will be called when a variable changes:

```cpp
ads.onChange<short>("GVL.count", [=](short value)
{
	printf("Notified of 'count' change to %i\n", value);
});
```

Callbacks can read and write other variables.
