#include "BeckhoffAds.h"

using namespace asl;

void onInsideChanged(bool b)
{
	printf("inside changed to %s\n", b ? "true" : "false");
}

int main()
{
	BeckhoffAds plc;

	if (!plc.connect("127.0.0.1", 852))
	{
		return 1;
	}

	BeckhoffAds::State state = plc.getState();

	if (state.invalid || plc.hasFatalError())
	{
		printf("ADS error %i\n", plc.lastError());
		return 0;
	}

	BeckhoffAds::DevInfo info = plc.getInfo();

	printf("Version: %i.%i.%i\nDevice name: '%s'\n", info.major, info.minor, info.build, *info.name);

	Array<BeckhoffAds::SymInfo> symbols = plc.getSymbols();

	foreach (BeckhoffAds::SymInfo& sym, symbols)
	{
		printf("%s : %s\n", *sym.name, *sym.type);
	}

	printf("State %i dev %i\n", state.state, state.deviceState);

	String name = plc.readValue("plc1.name", 80);

	printf("name = '%s'\n", *name);

	plc.writeValue<int>("plc1.count", 0);
	plc.writeValue<float>("plc1.speed", 12.345f);
	plc.writeValue<float>("plc1.factor", -3.0f);

	plc.writeValue<bool>("plc1.flag", true);

#ifdef ASL_HAVE_LAMBDA

	plc.onChange<int>("plc1.count", [&](int value) {
		if (value > 1000)
			plc.writeValue<int>("plc1.count", 0);
	});

	plc.onChange<bool>("plc1.inside", [&](bool value) {
		printf("-> %s\n", value ? "inside" : "out");
		plc.writeValue<float>("plc1.factor", value ? 3.0f : -1.5f);
	});

#else
	plc.onChange<bool>("plc1.inside", &onInsideChanged);
#endif

	double t1 = now();

	while (1)
	{
		if (now() - t1 > 1000)
			break;

		float speed = plc.readValue<float>("plc1.speed");
		int   count = plc.readValue<int>("plc1.count");
		bool  inside = plc.readValue<bool>("plc1.inside");
		bool  flag = plc.readValue<bool>("plc1.flag");

		printf("v = %f c = %i (%i, %i)\n", speed, count, inside, flag);

		sleep(0.2);

		if (plc.hasFatalError())
		{
			printf("ADS Error after %.2f s\n", now() - t1);
			break;
		}
	}

	plc.writeValue<short>("plc1.count", 0);
}
