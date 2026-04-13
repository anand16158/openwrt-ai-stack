#include "health_monitor.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

struct health_monitor {
	struct ubus_context *ubus;
	struct module_health modules[MAX_MODULES];
	int count;
};

static const struct {
	const char *name;
	const char *ubus_object;
} known_modules[] = {
	{ "traffic-classifier", "traffic-classifier" },
	{ "smart-qos",          "smart-qos"          },
	{ "wifi-sense",         "wifi-sense"          },
	{ "net-ids",            "net-ids"             },
	{ NULL, NULL }
};

struct health_monitor *health_monitor_init(struct ubus_context *ubus)
{
	struct health_monitor *hm = calloc(1, sizeof(*hm));
	if (!hm)
		return NULL;

	hm->ubus = ubus;

	for (int i = 0; known_modules[i].name; i++) {
		if (hm->count >= MAX_MODULES)
			break;

		struct module_health *mh = &hm->modules[hm->count++];
		strncpy(mh->name, known_modules[i].name,
			sizeof(mh->name) - 1);
		strncpy(mh->ubus_object, known_modules[i].ubus_object,
			sizeof(mh->ubus_object) - 1);
		mh->state = MOD_UNKNOWN;
		mh->enabled = true;
	}

	syslog(LOG_INFO, "health_monitor: tracking %d modules", hm->count);
	return hm;
}

void health_monitor_destroy(struct health_monitor *hm)
{
	free(hm);
}

void health_monitor_check_all(struct health_monitor *hm)
{
	uint64_t now = (uint64_t)time(NULL);

	for (int i = 0; i < hm->count; i++) {
		struct module_health *mh = &hm->modules[i];
		uint32_t id;

		int ret = ubus_lookup_id(hm->ubus, mh->ubus_object, &id);
		if (ret == 0) {
			if (mh->state != MOD_RUNNING) {
				syslog(LOG_INFO, "health: %s is now running",
				       mh->name);
				if (mh->state == MOD_ERROR ||
				    mh->state == MOD_STOPPED)
					mh->restart_count++;
			}
			mh->state = MOD_RUNNING;
			mh->last_seen = now;

			if (mh->uptime_sec == 0)
				mh->uptime_sec = 1;
			else
				mh->uptime_sec = now - (mh->last_seen - mh->uptime_sec);
		} else {
			if (mh->state == MOD_RUNNING) {
				syslog(LOG_WARNING, "health: %s went down",
				       mh->name);
			}
			mh->state = MOD_STOPPED;
		}
	}
}

int health_monitor_get_status(struct health_monitor *hm,
			      struct module_health *buf, int max_count)
{
	int n = hm->count < max_count ? hm->count : max_count;
	memcpy(buf, hm->modules, n * sizeof(struct module_health));
	return n;
}
