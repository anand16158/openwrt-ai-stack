#ifndef UBUS_API_H
#define UBUS_API_H

#include <libubus.h>
#include "flow_table.h"
#include "classifier.h"
#include "sta_tracker.h"
#include "device_fingerprint.h"
#include "usage_profile.h"
#include "data_collect.h"

struct tc_ubus_ctx {
	struct ubus_context *ubus;
	struct ubus_object obj;
	struct flow_table *ft;
	struct classifier_ctx *classifier;
	struct sta_tracker *sta;
	struct device_fp_ctx *devfp;
	struct usage_profile_ctx *profiler;
	struct data_collect_ctx *collector;
};

int tc_ubus_init(struct tc_ubus_ctx *ctx);
void tc_ubus_cleanup(struct tc_ubus_ctx *ctx);

/*
 * Broadcast a classification result as a ubus event.
 * smart-qos and other modules subscribe to
 * "traffic-classifier.classify" events for real-time flow data.
 */
void tc_ubus_notify_classification(struct tc_ubus_ctx *ctx,
				   const struct flow_entry *entry);

/*
 * Broadcast an anomaly detection event via ubus.
 */
void tc_ubus_notify_anomaly(struct tc_ubus_ctx *ctx,
			    const struct anomaly_event *ev);

#endif
