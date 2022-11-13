# beckhoffAds

**A C++ Beckhoff AMS/ADS protocol client**

This is a basic implementation of a client of the AMS/ADS protocol over TCP/IP. Class `BeckhoffAds` allows connecting to a TwinCAT device and reading and writing variables. 

Requires the [ASL](https://github.com/aslze/asl) library. With a recent cmake it will be downloaded if needed.

Some basic usage:

```cpp
BeckhoffAds plc;

plc.connect("192.168.0.2", 852);
```

You can also set source and target NetID and ports manually but it will try to guess if not. You will also need to set up twincat *routes* to remote client devices.

You can then read and write variables:

```cpp
float speed = plc.readValue<float>("GVL.speed");
plc.writeValue<int>("GVL.count", 0);
```

You have to take into account what C++ type correspond to each ADS type (e.g. INT -> int16_t, DINT -> int32_t, REAL -> float, BOOL -> char, etc.).


You can also register notifications, so that a callback will be called when a variable changes:

```cpp
plc.onChange<int>("GVL.count", [&](int value)
{
	printf("Notified of 'count' change to %i\n", value);
});
```

On older compilers without lambdas you can use a function pointer or a functor as the notification handler instead.

Array variables can be read/written by individual elements (with an index `[]` in the name):

```cpp
plc.writeValue<int>("GVL.values[1]", 35);
```

or as a complete array.

```cpp
plc.writeArray<int>("GVL.values", { 35, -5, 27 });
```

A specific function is used to read string values: `plc.readString("GVL.name");`.


Communication errors can be detected with:

```cpp
plc.hasFatalError();
```
