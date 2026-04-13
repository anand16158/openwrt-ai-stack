#include "ids_features.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct source_window {
	uint32_t src_ip;
	struct ids_flow_event events[IDS_WINDOW_SIZE];
	int head;
	int count;
};

struct ids_feature_ctx {
	struct source_window sources[IDS_MAX_SOURCES];
	int n_sources;
};

struct ids_feature_ctx *ids_features_init(void)
{
	return calloc(1, sizeof(struct ids_feature_ctx));
}

void ids_features_destroy(struct ids_feature_ctx *ctx)
{
	free(ctx);
}

static struct source_window *find_or_create_source(
	struct ids_feature_ctx *ctx, uint32_t src_ip)
{
	for (int i = 0; i < ctx->n_sources; i++) {
		if (ctx->sources[i].src_ip == src_ip)
			return &ctx->sources[i];
	}

	if (ctx->n_sources >= IDS_MAX_SOURCES) {
		/* Evict oldest (LRU would be better, but FIFO is simple) */
		memmove(&ctx->sources[0], &ctx->sources[1],
			sizeof(struct source_window) * (IDS_MAX_SOURCES - 1));
		ctx->n_sources--;
	}

	struct source_window *w = &ctx->sources[ctx->n_sources++];
	memset(w, 0, sizeof(*w));
	w->src_ip = src_ip;
	return w;
}

void ids_features_add_event(struct ids_feature_ctx *ctx,
			    const struct ids_flow_event *ev)
{
	struct source_window *w = find_or_create_source(ctx, ev->src_ip);

	w->events[w->head] = *ev;
	w->head = (w->head + 1) % IDS_WINDOW_SIZE;
	if (w->count < IDS_WINDOW_SIZE)
		w->count++;
}

static int count_unique_u16(const uint16_t *vals, int n)
{
	if (n <= 0)
		return 0;

	int unique = 1;
	for (int i = 1; i < n; i++) {
		bool found = false;
		for (int j = 0; j < i; j++) {
			if (vals[i] == vals[j]) {
				found = true;
				break;
			}
		}
		if (!found)
			unique++;
	}
	return unique;
}

static int count_unique_u32(const uint32_t *vals, int n)
{
	if (n <= 0)
		return 0;

	int unique = 1;
	for (int i = 1; i < n; i++) {
		bool found = false;
		for (int j = 0; j < i; j++) {
			if (vals[i] == vals[j]) {
				found = true;
				break;
			}
		}
		if (!found)
			unique++;
	}
	return unique;
}

int ids_features_extract(struct ids_feature_ctx *ctx,
			 uint32_t src_ip,
			 float features[IDS_N_FEATURES])
{
	memset(features, 0, sizeof(float) * IDS_N_FEATURES);

	struct source_window *w = NULL;
	for (int i = 0; i < ctx->n_sources; i++) {
		if (ctx->sources[i].src_ip == src_ip) {
			w = &ctx->sources[i];
			break;
		}
	}

	if (!w || w->count < 3)
		return -1;

	int n = w->count;
	uint16_t dst_ports[IDS_WINDOW_SIZE];
	uint32_t dst_ips[IDS_WINDOW_SIZE];
	int tcp_count = 0, udp_count = 0;
	double bytes_sum = 0, bytes_sq = 0;
	int short_flows = 0;

	for (int i = 0; i < n; i++) {
		int idx = (w->head - n + i + IDS_WINDOW_SIZE) % IDS_WINDOW_SIZE;
		struct ids_flow_event *e = &w->events[idx];

		dst_ports[i] = e->dst_port;
		dst_ips[i] = e->dst_ip;

		if (e->proto == 6) tcp_count++;
		else if (e->proto == 17) udp_count++;

		double total = (double)(e->bytes_fwd + e->bytes_bwd);
		bytes_sum += total;
		bytes_sq += total * total;

		if (total < 1000)
			short_flows++;
	}

	uint64_t time_span = 0;
	if (n >= 2) {
		int first = (w->head - n + IDS_WINDOW_SIZE) % IDS_WINDOW_SIZE;
		int last = (w->head - 1 + IDS_WINDOW_SIZE) % IDS_WINDOW_SIZE;
		if (w->events[last].timestamp_ms > w->events[first].timestamp_ms)
			time_span = w->events[last].timestamp_ms -
				    w->events[first].timestamp_ms;
	}

	double time_sec = (time_span > 0) ? time_span / 1000.0 : 1.0;
	int unique_ports = count_unique_u16(dst_ports, n);
	int unique_ips = count_unique_u32(dst_ips, n);

	double mean_bytes = bytes_sum / n;
	double var_bytes = (bytes_sq / n) - (mean_bytes * mean_bytes);
	if (var_bytes < 0) var_bytes = 0;

	/*
	 * Feature vector:
	 *  0: connection_rate (flows/min)
	 *  1: unique_dst_ports / window_size
	 *  2: unique_dst_ips / window_size
	 *  3: tcp_ratio
	 *  4: udp_ratio
	 *  5: mean_flow_bytes (normalized)
	 *  6: std_flow_bytes (normalized)
	 *  7: short_flow_ratio
	 *  8: port_scan_score (unique_ports / flows)
	 *  9: dst_ip_diversity (unique_ips / flows)
	 * 10: max_port (normalized)
	 * 11: low_port_ratio (ports < 1024)
	 * 12: window_fill_ratio
	 * 13: time_span_sec (normalized)
	 * 14: bytes_per_second (normalized)
	 * 15: flow_size_entropy
	 */

	features[0] = (float)(n / (time_sec / 60.0)) / 1000.0f;
	features[1] = (float)unique_ports / (float)n;
	features[2] = (float)unique_ips / (float)n;
	features[3] = (float)tcp_count / (float)n;
	features[4] = (float)udp_count / (float)n;
	features[5] = (float)(mean_bytes / 100000.0);
	features[6] = (float)(sqrt(var_bytes) / 100000.0);
	features[7] = (float)short_flows / (float)n;
	features[8] = (float)unique_ports / (float)n;
	features[9] = (float)unique_ips / (float)n;

	uint16_t max_port = 0;
	int low_ports = 0;
	for (int i = 0; i < n; i++) {
		if (dst_ports[i] > max_port) max_port = dst_ports[i];
		if (dst_ports[i] < 1024) low_ports++;
	}
	features[10] = (float)max_port / 65535.0f;
	features[11] = (float)low_ports / (float)n;
	features[12] = (float)n / (float)IDS_WINDOW_SIZE;
	features[13] = (float)(time_sec / 300.0);
	features[14] = (float)(bytes_sum / time_sec / 1e6);

	/* Simple flow-size entropy */
	float entropy = 0;
	if (n > 1) {
		int buckets[8] = {};
		for (int i = 0; i < n; i++) {
			int idx_b = (w->head - n + i + IDS_WINDOW_SIZE) % IDS_WINDOW_SIZE;
			double total = (double)(w->events[idx_b].bytes_fwd +
						w->events[idx_b].bytes_bwd);
			int b = 0;
			if (total > 100) b = 1;
			if (total > 1000) b = 2;
			if (total > 10000) b = 3;
			if (total > 50000) b = 4;
			if (total > 100000) b = 5;
			if (total > 500000) b = 6;
			if (total > 1000000) b = 7;
			buckets[b]++;
		}
		for (int b = 0; b < 8; b++) {
			if (buckets[b] > 0) {
				float p = (float)buckets[b] / (float)n;
				entropy -= p * log2f(p);
			}
		}
	}
	features[15] = entropy / 3.0f;

	return 0;
}
