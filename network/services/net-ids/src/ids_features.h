#ifndef IDS_FEATURES_H
#define IDS_FEATURES_H

#include <stdint.h>
#include <stdbool.h>

/*
 * IDS feature extraction from flow metadata received via ubus events.
 *
 * Maintains a sliding window of recent connection events per source MAC
 * and computes aggregate features for anomaly scoring:
 *   - Connection rate (new flows per minute)
 *   - Unique destination port count (port scan indicator)
 *   - Unique destination IP count
 *   - Protocol distribution
 *   - Byte ratio entropy
 *   - Mean/stddev of flow sizes
 *   - Proportion of short-lived flows
 */

#define IDS_N_FEATURES       16
#define IDS_WINDOW_SIZE      256
#define IDS_MAX_SOURCES      64

struct ids_flow_event {
	uint32_t src_ip;
	uint32_t dst_ip;
	uint16_t src_port;
	uint16_t dst_port;
	uint8_t  proto;
	uint8_t  class_id;
	uint32_t confidence;
	uint64_t bytes_fwd;
	uint64_t bytes_bwd;
	uint64_t timestamp_ms;
};

struct ids_feature_ctx;

struct ids_feature_ctx *ids_features_init(void);
void ids_features_destroy(struct ids_feature_ctx *ctx);

void ids_features_add_event(struct ids_feature_ctx *ctx,
			    const struct ids_flow_event *ev);

/*
 * Extract features for a specific source IP.
 * Returns 0 on success, -1 if not enough data.
 */
int ids_features_extract(struct ids_feature_ctx *ctx,
			 uint32_t src_ip,
			 float features[IDS_N_FEATURES]);

#endif
