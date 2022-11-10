#include "BeckhoffAds.h"

using namespace asl;

void onInsideChanged(bool b)
{
	printf("inside changed to %s\n", b ? "true" : "false");
}

int main()
{
	BeckhoffAds ads;

	if (!ads.connect("127.0.0.1", 852))
	{
		return 1;
	}

	BeckhoffAds::State state = ads.getState();

	if (state.invalid || ads.hasFatalError())
	{
		printf("ADS error %i\n", ads.lastError());
		return 0;
	}

	BeckhoffAds::DevInfo info = ads.getInfo();

	printf("Version: %i.%i.%i\nDevice name: '%s'\n", info.major, info.minor, info.build, *info.name);

	Array<BeckhoffAds::SymInfo> symbols = ads.getSymbols();

	foreach(BeckhoffAds::SymInfo& sym, symbols)
	{
		printf("%s : %s (%i) [%x]\n", *sym.name, *sym.typeName, sym.type, sym.flags);
	}

	printf("State %i dev %i\n", state.state, state.deviceState);

	String name = ads.readValue("plc1.name", 80);

	printf("name = '%s'\n", *name);

	ads.writeValue<int>("plc1.count", 0);
	ads.writeValue<float>("plc1.speed", 12.345f);
	ads.writeValue<float>("plc1.factor", -3.0f);

	ads.writeValue<bool>("plc1.flag", true);

#ifdef ASL_HAVE_LAMBDA

	ads.onChange<int>("plc1.count", [&](int value) {
		if (value > 1000)
			ads.writeValue<int>("plc1.count", 0);
	});

	ads.onChange<bool>("plc1.inside", [&](bool value) {
		printf("-> %s\n", value ? "inside" : "out");
		ads.writeValue<float>("plc1.factor", value ? 3.0f : -1.5f);
	});

#else
	ads.onChange<bool>("plc1.inside", &onInsideChanged);
#endif

	double t1 = now();

	while (1)
	{
		if (now() - t1 > 1000)
			break;
		float speed = ads.readValue<float>("plc1.speed");
		int count = ads.readValue<int>("plc1.count");
		bool  inside = ads.readValue<bool>("plc1.inside");
		bool  flag = ads.readValue<bool>("plc1.flag");

		printf("v = %f c = %i (%i, %i)\n", speed, count, inside, flag);

		sleep(0.2);

		if (ads.hasFatalError())
		{
			printf("ADS Error after %.2f s\n", now() - t1);
			break;
		}
	}

	ads.writeValue<short>("plc1.count", 0);
}
