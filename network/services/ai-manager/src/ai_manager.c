/*
 * AI Manager — OpenWrt AI stack central coordinator
 *
 * Coordinates all AI modules:
 *   - traffic-classifier (edge AI traffic classification)
 *   - smart-qos (AI-driven QoS enforcement)
 *   - wifi-sense (WiFi motion/presence detection)
 *   - net-ids (network intrusion detection)
 *
 * Provides:
 *   - Unified ubus API for status, model management, telemetry
 *   - Health monitoring of all AI daemons
 *   - Model version management (deploy, validate, rollback)
 *   - Aggregated telemetry for uCentral integration
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

#include "model_manager.h"
#include "health_monitor.h"

#define HEALTH_CHECK_MS  30000
#define TELEMETRY_MS     60000

struct ai_mgr_ctx {
	struct ubus_context *ubus;
	struct ubus_object obj;

	struct model_manager *mm;
	struct health_monitor *hm;

	struct uloop_timeout health_timer;
	struct uloop_timeout telemetry_timer;

	uint64_t start_time;
};

static struct ai_mgr_ctx g_ctx;

/* ---- ubus methods ---- */

static int handle_status(struct ubus_context *ctx, struct ubus_object *obj,
			 struct ubus_request_data *req, const char *method,
			 struct blob_attr *msg)
{
	struct ai_mgr_ctx *mgr = container_of(obj, struct ai_mgr_ctx, obj);
	struct blob_buf b = {};
	uint64_t now = (uint64_t)time(NULL);

	blob_buf_init(&b, 0);
	blobmsg_add_string(&b, "version", "0.1.0");
	blobmsg_add_string(&b, "status", "running");
	blobmsg_add_u64(&b, "uptime_sec", now - mgr->start_time);

	struct module_health modules[MAX_MODULES];
	int n = health_monitor_get_status(mgr->hm, modules, MAX_MODULES);

	void *arr = blobmsg_open_array(&b, "modules");
	for (int i = 0; i < n; i++) {
		static const char *state_names[] = {
			"unknown", "running", "stopped", "error"
		};
		void *entry = blobmsg_open_table(&b, NULL);
		blobmsg_add_string(&b, "name", modules[i].name);
		blobmsg_add_string(&b, "state",
			state_names[modules[i].state]);
		blobmsg_add_u64(&b, "last_seen", modules[i].last_seen);
		blobmsg_add_u32(&b, "restarts", modules[i].restart_count);
		blobmsg_close_table(&b, entry);
	}
	blobmsg_close_array(&b, arr);

	ubus_send_reply(ctx, req, b.head);
	blob_buf_free(&b);
	return UBUS_STATUS_OK;
}

static int handle_get_models(struct ubus_context *ctx,
			     struct ubus_object *obj,
			     struct ubus_request_data *req,
			     const char *method,
			     struct blob_attr *msg)
{
	struct ai_mgr_ctx *mgr = container_of(obj, struct ai_mgr_ctx, obj);
	struct blob_buf b = {};
	struct model_info models[MAX_MODELS];

	int n = model_manager_get_models(mgr->mm, models, MAX_MODELS);

	blob_buf_init(&b, 0);
	void *arr = blobmsg_open_array(&b, "models");
	for (int i = 0; i < n; i++) {
		void *entry = blobmsg_open_table(&b, NULL);
		blobmsg_add_string(&b, "name", models[i].name);
		blobmsg_add_string(&b, "version", models[i].version);
		blobmsg_add_string(&b, "path", models[i].path);
		blobmsg_add_u8(&b, "active", models[i].active);
		blobmsg_add_u64(&b, "deployed_at", models[i].deployed_at);
		blobmsg_close_table(&b, entry);
	}
	blobmsg_close_array(&b, arr);

	ubus_send_reply(ctx, req, b.head);
	blob_buf_free(&b);
	return UBUS_STATUS_OK;
}

enum {
	DEPLOY_STAGING_PATH,
	DEPLOY_NAME,
	DEPLOY_VERSION,
	DEPLOY_TARGET_PATH,
	__DEPLOY_MAX
};

static const struct blobmsg_policy deploy_policy[__DEPLOY_MAX] = {
	[DEPLOY_STAGING_PATH] = { "staging_path", BLOBMSG_TYPE_STRING },
	[DEPLOY_NAME]         = { "name",         BLOBMSG_TYPE_STRING },
	[DEPLOY_VERSION]      = { "version",      BLOBMSG_TYPE_STRING },
	[DEPLOY_TARGET_PATH]  = { "target_path",  BLOBMSG_TYPE_STRING },
};

static int handle_deploy_model(struct ubus_context *ctx,
			       struct ubus_object *obj,
			       struct ubus_request_data *req,
			       const char *method,
			       struct blob_attr *msg)
{
	struct ai_mgr_ctx *mgr = container_of(obj, struct ai_mgr_ctx, obj);
	struct blob_attr *tb[__DEPLOY_MAX];
	struct blob_buf b = {};

	blobmsg_parse(deploy_policy, __DEPLOY_MAX, tb,
		      blob_data(msg), blob_len(msg));

	if (!tb[DEPLOY_STAGING_PATH] || !tb[DEPLOY_NAME] ||
	    !tb[DEPLOY_VERSION] || !tb[DEPLOY_TARGET_PATH]) {
		blob_buf_init(&b, 0);
		blobmsg_add_string(&b, "error", "missing required fields");
		ubus_send_reply(ctx, req, b.head);
		blob_buf_free(&b);
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	int ret = model_manager_deploy(mgr->mm,
		blobmsg_get_string(tb[DEPLOY_STAGING_PATH]),
		blobmsg_get_string(tb[DEPLOY_NAME]),
		blobmsg_get_string(tb[DEPLOY_VERSION]),
		blobmsg_get_string(tb[DEPLOY_TARGET_PATH]));

	blob_buf_init(&b, 0);
	blobmsg_add_u8(&b, "success", ret == 0);
	if (ret != 0)
		blobmsg_add_string(&b, "error", "deployment failed");
	ubus_send_reply(ctx, req, b.head);
	blob_buf_free(&b);

	return UBUS_STATUS_OK;
}

static int handle_health(struct ubus_context *ctx, struct ubus_object *obj,
			 struct ubus_request_data *req, const char *method,
			 struct blob_attr *msg)
{
	struct ai_mgr_ctx *mgr = container_of(obj, struct ai_mgr_ctx, obj);
	struct blob_buf b = {};

	health_monitor_check_all(mgr->hm);

	struct module_health modules[MAX_MODULES];
	int n = health_monitor_get_status(mgr->hm, modules, MAX_MODULES);

	blob_buf_init(&b, 0);
	int running = 0, total = n;
	for (int i = 0; i < n; i++) {
		if (modules[i].state == MOD_RUNNING)
			running++;
	}

	blobmsg_add_u32(&b, "modules_running", running);
	blobmsg_add_u32(&b, "modules_total", total);
	blobmsg_add_u8(&b, "healthy", running == total);

	void *arr = blobmsg_open_array(&b, "details");
	for (int i = 0; i < n; i++) {
		static const char *state_names[] = {
			"unknown", "running", "stopped", "error"
		};
		void *entry = blobmsg_open_table(&b, NULL);
		blobmsg_add_string(&b, "name", modules[i].name);
		blobmsg_add_string(&b, "state",
			state_names[modules[i].state]);
		blobmsg_add_u32(&b, "restarts", modules[i].restart_count);
		blobmsg_close_table(&b, entry);
	}
	blobmsg_close_array(&b, arr);

	ubus_send_reply(ctx, req, b.head);
	blob_buf_free(&b);
	return UBUS_STATUS_OK;
}

static const struct ubus_method mgr_methods[] = {
	UBUS_METHOD_NOARG("status", handle_status),
	UBUS_METHOD_NOARG("get_models", handle_get_models),
	UBUS_METHOD("deploy_model", handle_deploy_model, deploy_policy),
	UBUS_METHOD_NOARG("health", handle_health),
};

static struct ubus_object_type mgr_obj_type =
	UBUS_OBJECT_TYPE("ai-manager", mgr_methods);

/* ---- timers ---- */

static void health_timer_cb(struct uloop_timeout *t)
{
	struct ai_mgr_ctx *ctx = container_of(t, struct ai_mgr_ctx,
					       health_timer);
	health_monitor_check_all(ctx->hm);
	uloop_timeout_set(t, HEALTH_CHECK_MS);
}

static void telemetry_timer_cb(struct uloop_timeout *t)
{
	struct ai_mgr_ctx *ctx = container_of(t, struct ai_mgr_ctx,
					       telemetry_timer);
	struct blob_buf b = {};

	health_monitor_check_all(ctx->hm);

	struct module_health modules[MAX_MODULES];
	int n = health_monitor_get_status(ctx->hm, modules, MAX_MODULES);

	blob_buf_init(&b, 0);
	blobmsg_add_u64(&b, "timestamp", (uint64_t)time(NULL));
	blobmsg_add_u64(&b, "uptime",
			(uint64_t)time(NULL) - ctx->start_time);

	int running = 0;
	for (int i = 0; i < n; i++) {
		if (modules[i].state == MOD_RUNNING)
			running++;
	}
	blobmsg_add_u32(&b, "modules_running", running);
	blobmsg_add_u32(&b, "modules_total", n);

	ubus_send_event(ctx->ubus, "ai-manager.telemetry", b.head);
	blob_buf_free(&b);

	uloop_timeout_set(t, TELEMETRY_MS);
}

/* ---- main ---- */

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"  -d    Debug mode (foreground, verbose)\n",
		prog);
}

int main(int argc, char **argv)
{
	struct ai_mgr_ctx *ctx = &g_ctx;
	bool debug = false;
	int opt;

	while ((opt = getopt(argc, argv, "dh")) != -1) {
		switch (opt) {
		case 'd': debug = true; break;
		default:
			usage(argv[0]);
			return (opt == 'h') ? 0 : 1;
		}
	}

	openlog("ai-manager", LOG_PID | (debug ? LOG_PERROR : 0), LOG_DAEMON);
	syslog(LOG_INFO, "starting");

	ctx->start_time = (uint64_t)time(NULL);

	uloop_init();

	ctx->ubus = ubus_connect(NULL);
	if (!ctx->ubus) {
		syslog(LOG_ERR, "ubus connect failed");
		return 1;
	}
	ubus_add_uloop(ctx->ubus);

	ctx->mm = model_manager_init();
	ctx->hm = health_monitor_init(ctx->ubus);

	ctx->obj.name = "ai-manager";
	ctx->obj.type = &mgr_obj_type;
	ctx->obj.methods = mgr_methods;
	ctx->obj.n_methods = ARRAY_SIZE(mgr_methods);

	if (ubus_add_object(ctx->ubus, &ctx->obj) != 0) {
		syslog(LOG_ERR, "failed to register ubus object");
		return 1;
	}

	ctx->health_timer.cb = health_timer_cb;
	uloop_timeout_set(&ctx->health_timer, HEALTH_CHECK_MS);

	ctx->telemetry_timer.cb = telemetry_timer_cb;
	uloop_timeout_set(&ctx->telemetry_timer, TELEMETRY_MS);

	syslog(LOG_INFO, "ready, entering main loop");
	uloop_run();

	ubus_remove_object(ctx->ubus, &ctx->obj);
	health_monitor_destroy(ctx->hm);
	model_manager_destroy(ctx->mm);
	ubus_free(ctx->ubus);
	uloop_done();

	syslog(LOG_INFO, "shutdown");
	closelog();
	return 0;
}
