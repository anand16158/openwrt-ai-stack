#ifndef CFR_FEATURES_H
#define CFR_FEATURES_H

#include "cfr_reader.h"

/*
 * Feature extraction from CFR captures for Wi-Fi sensing.
 *
 * Computes amplitude and phase from raw I/Q samples, then derives
 * statistical features suitable for a TFLite motion detection model:
 *   - Mean/variance of amplitude across subcarriers
 *   - Mean/variance of phase across subcarriers
 *   - Temporal delta (amplitude change vs previous capture)
 *   - Bandwidth and frequency info
 *
 * Output is a fixed-size float vector fed directly into the model.
 */

#define SENSE_N_FEATURES 32

struct cfr_feature_ctx;

struct cfr_feature_ctx *cfr_features_init(void);
void cfr_features_destroy(struct cfr_feature_ctx *ctx);

/*
 * Extract features from a CFR capture into a float vector.
 * Maintains state from the previous capture for temporal deltas.
 * Returns 0 on success. First capture returns features with zeroed deltas.
 */
int cfr_features_extract(struct cfr_feature_ctx *ctx,
			 const struct cfr_capture *cap,
			 float features[SENSE_N_FEATURES]);

#endif
