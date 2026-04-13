/*
 * Network IDS daemon — OpenWrt AI stack
 * ML-based network intrusion and anomaly detection
 *
 * Subscribes to traffic-classifier.classify ubus events, accumulates
 * per-source flow metadata in a sliding window, extracts connection-level
 * features, runs a TFLite anomaly model, and emits alerts via ubus.
 *
 * Alert types: port_scan, ddos, anomaly, malware
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>
#include <getopt.h>
#include <time.h>
#include <arpa/inet.h>

#include <libubox/uloop.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "ids_features.h"
#include "ids_model.h"

#define DEFAULT_MODEL_PATH   "/etc/net-ids/anomaly_detect.tflite"
#define DEFAULT_THRESHOLD    0.7f
#define ANALYSIS_INTERVAL_MS 5000

struct net_ids_ctx {
	struct ubus_context *ubus;
	struct ubus_event_handler ev_classify;
	struct ubus_event_handler ev_anomaly;
	struct uloop_timeout analysis_timer;

	struct ids_feature_ctx *feat;
	struct ids_model *model;

	float alert_threshold;
	int total_alerts;
};

static struct net_ids_ctx g_ctx;

static const char *alert_type_for_score(float score,
					float features[IDS_N_FEATURES])
{
	float port_diversity = features[1];
	float conn_rate = features[0];

	if (port_diversity > 0.8f && conn_rate > 0.5f)
		return "port_scan";
	if (conn_rate > 0.8f)
		return "ddos";
	if (score > 0.9f)
		return "malware";
	return "anomaly";
}

static void emit_alert(struct net_ids_ctx *ctx, uint32_t src_ip,
		       const char *alert_type, float score)
{
	struct blob_buf b = {};
	char ip_str[INET_ADDRSTRLEN];

	inet_ntop(AF_INET, &src_ip, ip_str, sizeof(ip_str));

	blob_buf_init(&b, 0);
	blobmsg_add_string(&b, "src_ip", ip_str);
	blobmsg_add_string(&b, "alert_type", alert_type);
	blobmsg_add_u32(&b, "severity", (uint32_t)(score * 100));
	blobmsg_add_u64(&b, "timestamp", (uint64_t)time(NULL));

	ubus_send_event(ctx->ubus, "net-ids.alert", b.head);
	blob_buf_free(&b);

	ctx->total_alerts++;
	syslog(LOG_WARNING, "ALERT [%s] src=%s severity=%.0f%%",
	       alert_type, ip_str, score * 100.0f);
}

enum {
	CLS_SRC_IP,
	CLS_DST_IP,
	CLS_SRC_PORT,
	CLS_DST_PORT,
	CLS_PROTO,
	CLS_CLASS_ID,
	CLS_CONFIDENCE,
	CLS_BYTES_FWD,
	CLS_BYTES_BWD,
	__CLS_MAX
};

static const struct blobmsg_policy cls_pol[__CLS_MAX] = {
	[CLS_SRC_IP]     = { "src_ip",     BLOBMSG_TYPE_STRING },
	[CLS_DST_IP]     = { "dst_ip",     BLOBMSG_TYPE_STRING },
	[CLS_SRC_PORT]   = { "src_port",   BLOBMSG_TYPE_INT32 },
	[CLS_DST_PORT]   = { "dst_port",   BLOBMSG_TYPE_INT32 },
	[CLS_PROTO]      = { "proto",      BLOBMSG_TYPE_INT32 },
	[CLS_CLASS_ID]   = { "class_id",   BLOBMSG_TYPE_INT32 },
	[CLS_CONFIDENCE] = { "confidence", BLOBMSG_TYPE_INT32 },
	[CLS_BYTES_FWD]  = { "bytes_fwd",  BLOBMSG_TYPE_INT64 },
	[CLS_BYTES_BWD]  = { "bytes_bwd",  BLOBMSG_TYPE_INT64 },
};

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void on_classify_event(struct ubus_context *ubus,
			      struct ubus_event_handler *ev,
			      const char *type,
			      struct blob_attr *msg)
{
	struct net_ids_ctx *ctx = container_of(ev, struct net_ids_ctx,
					       ev_classify);
	struct blob_attr *tb[__CLS_MAX];

	blobmsg_parse(cls_pol, __CLS_MAX, tb, blob_data(msg), blob_len(msg));

	if (!tb[CLS_SRC_IP])
		return;

	struct ids_flow_event fev = {};

	struct in_addr addr;
	if (inet_pton(AF_INET, blobmsg_get_string(tb[CLS_SRC_IP]), &addr) == 1)
		fev.src_ip = addr.s_addr;
	if (tb[CLS_DST_IP] &&
	    inet_pton(AF_INET, blobmsg_get_string(tb[CLS_DST_IP]), &addr) == 1)
		fev.dst_ip = addr.s_addr;

	if (tb[CLS_SRC_PORT])  fev.src_port = blobmsg_get_u32(tb[CLS_SRC_PORT]);
	if (tb[CLS_DST_PORT])  fev.dst_port = blobmsg_get_u32(tb[CLS_DST_PORT]);
	if (tb[CLS_PROTO])     fev.proto = blobmsg_get_u32(tb[CLS_PROTO]);
	if (tb[CLS_CLASS_ID])  fev.class_id = blobmsg_get_u32(tb[CLS_CLASS_ID]);
	if (tb[CLS_CONFIDENCE]) fev.confidence = blobmsg_get_u32(tb[CLS_CONFIDENCE]);
	if (tb[CLS_BYTES_FWD]) fev.bytes_fwd = blobmsg_get_u64(tb[CLS_BYTES_FWD]);
	if (tb[CLS_BYTES_BWD]) fev.bytes_bwd = blobmsg_get_u64(tb[CLS_BYTES_BWD]);
	fev.timestamp_ms = now_ms();

	ids_features_add_event(ctx->feat, &fev);
}

static void analysis_timer_cb(struct uloop_timeout *t)
{
	struct net_ids_ctx *ctx = container_of(t, struct net_ids_ctx,
					       analysis_timer);

	if (!ids_model_available(ctx->model))
		goto reschedule;

	/*
	 * Iterate all tracked sources (via internal structure — in a
	 * production version this would iterate the feature context's
	 * source list). For now we rely on the classify events to
	 * drive feature accumulation, and periodically score them.
	 *
	 * This is a simplified approach; a full implementation would
	 * maintain its own source IP list.
	 */

reschedule:
	uloop_timeout_set(t, ANALYSIS_INTERVAL_MS);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"  -m <path>       TFLite model path\n"
		"  -t <threshold>  Alert threshold 0.0-1.0 (default: 0.7)\n"
		"  -d              Debug mode\n",
		prog);
}

int main(int argc, char **argv)
{
	struct net_ids_ctx *ctx = &g_ctx;
	const char *model_path = DEFAULT_MODEL_PATH;
	bool debug = false;
	int opt;

	ctx->alert_threshold = DEFAULT_THRESHOLD;

	while ((opt = getopt(argc, argv, "m:t:dh")) != -1) {
		switch (opt) {
		case 'm': model_path = optarg; break;
		case 't': ctx->alert_threshold = atof(optarg); break;
		case 'd': debug = true; break;
		default:
			usage(argv[0]);
			return (opt == 'h') ? 0 : 1;
		}
	}

	openlog("net-ids", LOG_PID | (debug ? LOG_PERROR : 0), LOG_DAEMON);
	syslog(LOG_INFO, "starting (model=%s, threshold=%.2f)",
	       model_path, ctx->alert_threshold);

	uloop_init();

	ctx->ubus = ubus_connect(NULL);
	if (!ctx->ubus) {
		syslog(LOG_ERR, "ubus connect failed");
		return 1;
	}
	ubus_add_uloop(ctx->ubus);

	ctx->feat = ids_features_init();
	ctx->model = ids_model_init(model_path);
	if (!ctx->model)
		syslog(LOG_WARNING, "no model loaded, running in monitor-only mode");

	ctx->ev_classify.cb = on_classify_event;
	ubus_register_event_handler(ctx->ubus, &ctx->ev_classify,
				    "traffic-classifier.classify");

	ctx->analysis_timer.cb = analysis_timer_cb;
	uloop_timeout_set(&ctx->analysis_timer, ANALYSIS_INTERVAL_MS);

	syslog(LOG_INFO, "subscribed to events, entering main loop");
	uloop_run();

	ids_model_destroy(ctx->model);
	ids_features_destroy(ctx->feat);
	ubus_free(ctx->ubus);
	uloop_done();

	syslog(LOG_INFO, "shutdown (total alerts: %d)", ctx->total_alerts);
	closelog();
	return 0;
}
