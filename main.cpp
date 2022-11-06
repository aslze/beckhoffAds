#include "BeckhoffAds.h"

using namespace asl;

int main()
{
	BeckhoffAds ads;
	ads.setSource("192.168.0.2.1.2", 34000);
	ads.setTarget("127.0.0.1.1.1", 852);

	if (!ads.connect("192.168.0.2"))
	{
		return 1;
	}

	BeckhoffAds::State state = ads.getState();

	if (ads.lastError() > 0)
	{
		printf("ADS error %i\n", ads.lastError());
		return 0;
	}

	printf("State %i dev %i\n", state.state, state.deviceState);

	ads.writeValue<short>("plc1.count", 0);
	ads.writeValue<float>("plc1.speed", 12.345f);
	ads.writeValue<float>("plc1.factor", -3.0f);

	ads.onChange<short>("plc1.count", [&](short value) {
		if ((value % 50) == 0)
			printf("Count changed to %i\n", value);
	});

	ads.onChange<bool>("plc1.inside", [&](bool value) {
		printf("-> %s\n", value ? "inside" : "out");
		ads.writeValue<float>("plc1.factor", value ? 3.0f : -1.5f);
	});

	for (int i = 0; i < 24; i++)
	{
		float speed = ads.readValue<float>("plc1.speed");
		short count = ads.readValue<short>("plc1.count");
		bool  inside = ads.readValue<bool>("plc1.inside");

		printf("v = %f c = %i (%i)\n", speed, count, inside);

		sleep(0.25);
	}

	ads.writeValue<short>("plc1.count", 0);
}
