/*
 * Smart QoS daemon — OpenWrt AI stack
 * AI-driven per-flow QoS enforcement
 *
 * Subscribes to "traffic-classifier.classify" ubus events and programs
 * Linux tc (HTB) + nftables connmark for per-flow QoS priority.
 *
 * HTB class hierarchy:
 *   root 1:   HTB qdisc
 *   1:1       Root class (total bandwidth)
 *   1:10      Realtime  — VoIP, Gaming  (prio 1, fq_codel low latency)
 *   1:20      Streaming — Video         (prio 2, high bandwidth ceil)
 *   1:30      Normal    — Web, Social   (prio 3, default)
 *   1:40      Bulk      — Downloads     (prio 4, best effort)
 *   1:50      Background — Unknown/IoT  (prio 5, lowest)
 *
 * Flow steering: nft connmark -> tc fw filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>

#include <libubox/uloop.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubus.h>

#define DEFAULT_WAN_IFACE    "eth0"
#define DEFAULT_LAN_IFACE    "br-lan"
#define DEFAULT_BANDWIDTH    100000
#define MIN_CONFIDENCE       40

struct qos_class {
	int class_id;
	int priority;
	int rate_pct;
	int ceil_pct;
	const char *name;
};

static const struct qos_class qos_classes[] = {
	{ 10, 1, 30, 100, "realtime"   },
	{ 20, 2, 40, 100, "streaming"  },
	{ 30, 3, 20, 100, "normal"     },
	{ 40, 4,  8, 100, "bulk"       },
	{ 50, 5,  2,  80, "background" },
};

struct smart_qos_ctx {
	struct ubus_context *ubus;
	struct ubus_event_handler ev_handler;
	char wan_iface[32];
	char lan_iface[32];
	int bandwidth_kbit;
	int min_confidence;
	bool tc_setup_done;
};

static struct smart_qos_ctx g_ctx;

static int class_for_traffic(const char *cls_name)
{
	if (!cls_name)
		return 50;

	if (strcmp(cls_name, "voip") == 0 || strcmp(cls_name, "gaming") == 0)
		return 10;
	if (strcmp(cls_name, "video") == 0)
		return 20;
	if (strcmp(cls_name, "browsing") == 0 || strcmp(cls_name, "social") == 0)
		return 30;
	if (strcmp(cls_name, "download") == 0)
		return 40;

	return 50;
}

static void run_cmd(const char *fmt, ...)
{
	char cmd[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);

	int rc = system(cmd);
	if (rc != 0)
		syslog(LOG_DEBUG, "cmd failed (%d): %s", rc, cmd);
}

static void setup_tc(struct smart_qos_ctx *ctx)
{
	run_cmd("tc qdisc del dev %s root 2>/dev/null", ctx->wan_iface);

	run_cmd("tc qdisc add dev %s root handle 1: htb default 50",
		ctx->wan_iface);
	run_cmd("tc class add dev %s parent 1: classid 1:1 htb rate %dkbit",
		ctx->wan_iface, ctx->bandwidth_kbit);

	for (int i = 0; i < 5; i++) {
		int rate = ctx->bandwidth_kbit * qos_classes[i].rate_pct / 100;
		int ceil = ctx->bandwidth_kbit * qos_classes[i].ceil_pct / 100;

		run_cmd("tc class add dev %s parent 1:1 classid 1:%d htb "
			"rate %dkbit ceil %dkbit prio %d",
			ctx->wan_iface, qos_classes[i].class_id,
			rate, ceil, qos_classes[i].priority);

		if (qos_classes[i].priority <= 2) {
			run_cmd("tc qdisc add dev %s parent 1:%d handle %d: "
				"fq_codel target 5ms interval 50ms",
				ctx->wan_iface, qos_classes[i].class_id,
				qos_classes[i].class_id);
		}

		run_cmd("tc filter add dev %s parent 1: protocol ip "
			"handle 0x%x fw classid 1:%d",
			ctx->wan_iface, qos_classes[i].class_id,
			qos_classes[i].class_id);
	}

	run_cmd("nft add table inet smart_qos 2>/dev/null");
	run_cmd("nft add chain inet smart_qos mark_flows "
		"{ type filter hook forward priority mangle\\; }");

	ctx->tc_setup_done = true;
	syslog(LOG_INFO, "tc HTB setup done on %s (%dkbit)",
	       ctx->wan_iface, ctx->bandwidth_kbit);
}

static void cleanup_tc(struct smart_qos_ctx *ctx)
{
	run_cmd("tc qdisc del dev %s root 2>/dev/null", ctx->wan_iface);
	run_cmd("nft delete table inet smart_qos 2>/dev/null");
	ctx->tc_setup_done = false;
	syslog(LOG_INFO, "tc cleanup on %s", ctx->wan_iface);
}

enum {
	CLS_SRC_IP,
	CLS_DST_IP,
	CLS_SRC_PORT,
	CLS_DST_PORT,
	CLS_PROTO,
	CLS_CLASS,
	CLS_CLASS_ID,
	CLS_CONFIDENCE,
	CLS_SRC_MAC,
	__CLS_MAX
};

static const struct blobmsg_policy cls_policy[__CLS_MAX] = {
	[CLS_SRC_IP]     = { "src_ip",     BLOBMSG_TYPE_STRING },
	[CLS_DST_IP]     = { "dst_ip",     BLOBMSG_TYPE_STRING },
	[CLS_SRC_PORT]   = { "src_port",   BLOBMSG_TYPE_INT32 },
	[CLS_DST_PORT]   = { "dst_port",   BLOBMSG_TYPE_INT32 },
	[CLS_PROTO]      = { "proto",      BLOBMSG_TYPE_INT32 },
	[CLS_CLASS]      = { "class",      BLOBMSG_TYPE_STRING },
	[CLS_CLASS_ID]   = { "class_id",   BLOBMSG_TYPE_INT32 },
	[CLS_CONFIDENCE] = { "confidence", BLOBMSG_TYPE_INT32 },
	[CLS_SRC_MAC]    = { "src_mac",    BLOBMSG_TYPE_STRING },
};

static void apply_flow_mark(struct smart_qos_ctx *ctx,
			    const char *src_ip, const char *dst_ip,
			    int src_port, int dst_port, int proto,
			    int mark)
{
	const char *proto_str = (proto == 6) ? "tcp" : "udp";

	run_cmd("nft add rule inet smart_qos mark_flows "
		"ip saddr %s ip daddr %s %s sport %d %s dport %d "
		"meta mark set 0x%x",
		src_ip, dst_ip, proto_str, src_port,
		proto_str, dst_port, mark);
}

static void on_classify_event(struct ubus_context *ubus,
			      struct ubus_event_handler *ev,
			      const char *type,
			      struct blob_attr *msg)
{
	struct smart_qos_ctx *ctx = container_of(ev, struct smart_qos_ctx,
						 ev_handler);
	struct blob_attr *tb[__CLS_MAX];

	blobmsg_parse(cls_policy, __CLS_MAX, tb, blob_data(msg), blob_len(msg));

	if (!tb[CLS_CLASS] || !tb[CLS_CONFIDENCE])
		return;

	int confidence = blobmsg_get_u32(tb[CLS_CONFIDENCE]);
	if (confidence < ctx->min_confidence)
		return;

	const char *cls_name = blobmsg_get_string(tb[CLS_CLASS]);
	int mark = class_for_traffic(cls_name);

	if (!ctx->tc_setup_done)
		return;

	if (tb[CLS_SRC_IP] && tb[CLS_DST_IP] &&
	    tb[CLS_SRC_PORT] && tb[CLS_DST_PORT] && tb[CLS_PROTO]) {
		apply_flow_mark(ctx,
			blobmsg_get_string(tb[CLS_SRC_IP]),
			blobmsg_get_string(tb[CLS_DST_IP]),
			blobmsg_get_u32(tb[CLS_SRC_PORT]),
			blobmsg_get_u32(tb[CLS_DST_PORT]),
			blobmsg_get_u32(tb[CLS_PROTO]),
			mark);

		syslog(LOG_DEBUG, "flow %s:%d->%s:%d -> class %s (mark=0x%x, conf=%d)",
		       blobmsg_get_string(tb[CLS_SRC_IP]),
		       blobmsg_get_u32(tb[CLS_SRC_PORT]),
		       blobmsg_get_string(tb[CLS_DST_IP]),
		       blobmsg_get_u32(tb[CLS_DST_PORT]),
		       cls_name, mark, confidence);
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"  -w <iface>       WAN interface (default: %s)\n"
		"  -l <iface>       LAN interface (default: %s)\n"
		"  -b <kbit>        Total bandwidth in kbit (default: %d)\n"
		"  -c <percent>     Min confidence threshold (default: %d)\n"
		"  -d               Debug mode (foreground, verbose)\n",
		prog, DEFAULT_WAN_IFACE, DEFAULT_LAN_IFACE,
		DEFAULT_BANDWIDTH, MIN_CONFIDENCE);
}

int main(int argc, char **argv)
{
	struct smart_qos_ctx *ctx = &g_ctx;
	bool debug = false;
	int opt;

	strncpy(ctx->wan_iface, DEFAULT_WAN_IFACE, sizeof(ctx->wan_iface) - 1);
	strncpy(ctx->lan_iface, DEFAULT_LAN_IFACE, sizeof(ctx->lan_iface) - 1);
	ctx->bandwidth_kbit = DEFAULT_BANDWIDTH;
	ctx->min_confidence = MIN_CONFIDENCE;

	while ((opt = getopt(argc, argv, "w:l:b:c:dh")) != -1) {
		switch (opt) {
		case 'w':
			strncpy(ctx->wan_iface, optarg,
				sizeof(ctx->wan_iface) - 1);
			break;
		case 'l':
			strncpy(ctx->lan_iface, optarg,
				sizeof(ctx->lan_iface) - 1);
			break;
		case 'b':
			ctx->bandwidth_kbit = atoi(optarg);
			break;
		case 'c':
			ctx->min_confidence = atoi(optarg);
			break;
		case 'd':
			debug = true;
			break;
		default:
			usage(argv[0]);
			return (opt == 'h') ? 0 : 1;
		}
	}

	openlog("smart-qos", LOG_PID | (debug ? LOG_PERROR : 0), LOG_DAEMON);
	syslog(LOG_INFO, "starting (wan=%s, bw=%dkbit, min_conf=%d%%)",
	       ctx->wan_iface, ctx->bandwidth_kbit, ctx->min_confidence);

	uloop_init();

	ctx->ubus = ubus_connect(NULL);
	if (!ctx->ubus) {
		syslog(LOG_ERR, "failed to connect to ubus");
		return 1;
	}
	ubus_add_uloop(ctx->ubus);

	setup_tc(ctx);

	ctx->ev_handler.cb = on_classify_event;
	ubus_register_event_handler(ctx->ubus, &ctx->ev_handler,
				    "traffic-classifier.classify");
	syslog(LOG_INFO, "subscribed to traffic-classifier.classify events");

	syslog(LOG_INFO, "entering main loop");
	uloop_run();

	cleanup_tc(ctx);
	ubus_free(ctx->ubus);
	uloop_done();

	syslog(LOG_INFO, "shutdown complete");
	closelog();
	return 0;
}
