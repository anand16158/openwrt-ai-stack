#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include <libubus.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * Health monitoring for AI stack daemons.
 *
 * Periodically polls each module via ubus to check if it is alive and
 * responsive. Tracks uptime, restart counts, and last-seen timestamps.
 */

#define MAX_MODULES 8

enum module_state {
	MOD_UNKNOWN = 0,
	MOD_RUNNING,
	MOD_STOPPED,
	MOD_ERROR,
};

struct module_health {
	char name[32];
	char ubus_object[64];
	enum module_state state;
	uint64_t last_seen;
	uint64_t uptime_sec;
	int restart_count;
	bool enabled;
};

struct health_monitor;

struct health_monitor *health_monitor_init(struct ubus_context *ubus);
void health_monitor_destroy(struct health_monitor *hm);

void health_monitor_check_all(struct health_monitor *hm);

int health_monitor_get_status(struct health_monitor *hm,
			      struct module_health *buf, int max_count);

#endif
