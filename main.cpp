#include "BeckhoffAds.h"

using namespace asl;

int main()
{
	BeckhoffAds ads;
	ads.setSource("192.168.0.2.1.2", 34000);
	ads.setTarget("192.168.0.2.1.1", 851);

	if (!ads.connect("127.0.0.1"))
	{
		return 1;
	}

	BeckhoffAds::State state = ads.getState();

	printf("State %i dev %i\n", state.state, state.deviceState);

	unsigned notif = ads.addNotification<short>("GVL.count", BeckhoffAds::NOTIF_CHANGE, 0.1, 0.1, [](short value)
	{
		printf("Notified of count change to %i\n", value);
	});

	for (int i = 0; i < 10; i++)
	{
		float speed = ads.readValue<float>("GVL.speed");
		short count = ads.readValue<short>("GVL.count");

		printf("v = %f c = %i\n", speed, count);

		sleep(0.25);
	}

	ads.writeValue<short>("GVL.count", 0);
}
