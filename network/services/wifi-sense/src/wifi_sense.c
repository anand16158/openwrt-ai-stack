/*
 * WiFi Sensing daemon — OpenWrt AI stack
 * CFR-based motion and presence detection
 *
 * Reads CFR (Channel Frequency Response) data from ath11k debugfs,
 * extracts features, runs TFLite model for motion/presence detection,
 * and publishes results via ubus events.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>
#include <getopt.h>
#include <time.h>

#include <libubox/uloop.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "cfr_reader.h"
#include "cfr_features.h"
#include "sense_model.h"

#define DEFAULT_MODEL_PATH   "/etc/wifi-sense/wifi_sensing.tflite"
#define DEFAULT_POLL_MS      200
#define DEFAULT_EVENT_MIN_MS 2000
#define DEFAULT_PHY          ""

struct wifi_sense_ctx {
	struct ubus_context *ubus;
	struct cfr_reader *cfr;
	struct cfr_feature_ctx *feat;
	struct sense_model *model;

	struct uloop_fd cfr_fd;
	struct uloop_timeout poll_timer;

	enum sense_state last_state;
	uint64_t last_event_ms;
	int poll_interval_ms;
	int event_min_interval_ms;
	char zone[64];
};

static struct wifi_sense_ctx g_ctx;

static const char *state_names[] = {
	[SENSE_EMPTY]    = "empty",
	[SENSE_PRESENCE] = "presence",
	[SENSE_MOTION]   = "motion",
};

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void publish_event(struct wifi_sense_ctx *ctx,
			  struct sense_result *r)
{
	struct blob_buf b = {};

	blob_buf_init(&b, 0);
	blobmsg_add_string(&b, "state", state_names[r->state]);
	blobmsg_add_u32(&b, "confidence", (uint32_t)(r->confidence * 100));
	blobmsg_add_string(&b, "zone", ctx->zone);

	void *arr = blobmsg_open_array(&b, "probs");
	for (int i = 0; i < SENSE_STATE_COUNT; i++)
		blobmsg_add_u32(&b, NULL, (uint32_t)(r->probs[i] * 100));
	blobmsg_close_array(&b, arr);

	ubus_send_event(ctx->ubus, "wifi-sense.detect", b.head);
	blob_buf_free(&b);
}

static void process_cfr(struct wifi_sense_ctx *ctx)
{
	struct cfr_capture cap;

	while (cfr_reader_next(ctx->cfr, &cap) == 0) {
		float features[SENSE_N_FEATURES];

		if (cfr_features_extract(ctx->feat, &cap, features) < 0)
			continue;

		if (!sense_model_available(ctx->model))
			continue;

		struct sense_result result;
		if (sense_model_predict(ctx->model, features, &result) < 0)
			continue;

		if (result.state == ctx->last_state &&
		    result.state == SENSE_EMPTY)
			continue;

		uint64_t now = now_ms();
		if (now - ctx->last_event_ms < (uint64_t)ctx->event_min_interval_ms)
			continue;

		if (result.state != ctx->last_state || result.state != SENSE_EMPTY) {
			publish_event(ctx, &result);
			ctx->last_state = result.state;
			ctx->last_event_ms = now;

			syslog(LOG_INFO, "wifi-sense: %s (conf=%.0f%%)",
			       state_names[result.state],
			       result.confidence * 100.0f);
		}
	}
}

static void cfr_fd_cb(struct uloop_fd *fd, unsigned int events)
{
	struct wifi_sense_ctx *ctx = container_of(fd, struct wifi_sense_ctx,
						  cfr_fd);
	(void)events;
	process_cfr(ctx);
}

static void poll_timer_cb(struct uloop_timeout *t)
{
	struct wifi_sense_ctx *ctx = container_of(t, struct wifi_sense_ctx,
						  poll_timer);
	process_cfr(ctx);
	uloop_timeout_set(t, ctx->poll_interval_ms);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"  -m <path>    TFLite model path (default: %s)\n"
		"  -p <phy>     ath11k PHY name (auto-detect if empty)\n"
		"  -i <ms>      Poll interval in ms (default: %d)\n"
		"  -z <zone>    Zone name for events (default: 'default')\n"
		"  -d           Debug mode\n",
		prog, DEFAULT_MODEL_PATH, DEFAULT_POLL_MS);
}

int main(int argc, char **argv)
{
	struct wifi_sense_ctx *ctx = &g_ctx;
	const char *model_path = DEFAULT_MODEL_PATH;
	const char *phy_name = DEFAULT_PHY;
	bool debug = false;
	int opt;

	ctx->poll_interval_ms = DEFAULT_POLL_MS;
	ctx->event_min_interval_ms = DEFAULT_EVENT_MIN_MS;
	strncpy(ctx->zone, "default", sizeof(ctx->zone) - 1);

	while ((opt = getopt(argc, argv, "m:p:i:z:dh")) != -1) {
		switch (opt) {
		case 'm': model_path = optarg; break;
		case 'p': phy_name = optarg; break;
		case 'i': ctx->poll_interval_ms = atoi(optarg); break;
		case 'z':
			strncpy(ctx->zone, optarg, sizeof(ctx->zone) - 1);
			break;
		case 'd': debug = true; break;
		default:
			usage(argv[0]);
			return (opt == 'h') ? 0 : 1;
		}
	}

	openlog("wifi-sense", LOG_PID | (debug ? LOG_PERROR : 0), LOG_DAEMON);
	syslog(LOG_INFO, "starting (model=%s, poll=%dms)", model_path,
	       ctx->poll_interval_ms);

	uloop_init();

	ctx->ubus = ubus_connect(NULL);
	if (!ctx->ubus) {
		syslog(LOG_ERR, "ubus connect failed");
		return 1;
	}
	ubus_add_uloop(ctx->ubus);

	ctx->model = sense_model_init(model_path);
	if (!ctx->model)
		syslog(LOG_WARNING, "no model loaded, running in capture-only mode");

	ctx->feat = cfr_features_init();
	ctx->cfr = cfr_reader_init(phy_name[0] ? phy_name : NULL);

	if (ctx->cfr) {
		cfr_reader_enable(ctx->cfr);

		int fd = cfr_reader_get_fd(ctx->cfr);
		if (fd >= 0) {
			ctx->cfr_fd.fd = fd;
			ctx->cfr_fd.cb = cfr_fd_cb;
			uloop_fd_add(&ctx->cfr_fd, ULOOP_READ);
			syslog(LOG_INFO, "using fd-based CFR reading");
		} else {
			syslog(LOG_INFO, "using poll-based CFR reading");
		}
	} else {
		syslog(LOG_WARNING, "no CFR source, polling only");
	}

	ctx->poll_timer.cb = poll_timer_cb;
	uloop_timeout_set(&ctx->poll_timer, ctx->poll_interval_ms);

	syslog(LOG_INFO, "entering main loop");
	uloop_run();

	if (ctx->cfr) {
		cfr_reader_disable(ctx->cfr);
		cfr_reader_destroy(ctx->cfr);
	}
	cfr_features_destroy(ctx->feat);
	sense_model_destroy(ctx->model);
	ubus_free(ctx->ubus);
	uloop_done();

	syslog(LOG_INFO, "shutdown");
	closelog();
	return 0;
}
