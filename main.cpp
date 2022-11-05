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

	unsigned notif =
	    ads.onChange<short>("GVL.count", [](short value) { printf("Notified of count change to %i\n", value); }, 0.2);

	for (int i = 0; i < 10; i++)
	{
		float speed = ads.readValue<float>("GVL.speed");
		short count = ads.readValue<short>("GVL.count");

		printf("v = %f c = %i\n", speed, count);

		sleep(0.5);
	}

	ads.removeNotification(notif);

	ads.writeValue<short>("GVL.count", 0);
}
