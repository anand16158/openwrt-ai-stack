#include "cfr_features.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct cfr_feature_ctx {
	float prev_amplitude[CFR_MAX_SUBCARRIERS];
	int   prev_valid;
	int   n_captures;
};

struct cfr_feature_ctx *cfr_features_init(void)
{
	struct cfr_feature_ctx *ctx = calloc(1, sizeof(*ctx));
	return ctx;
}

void cfr_features_destroy(struct cfr_feature_ctx *ctx)
{
	free(ctx);
}

static float compute_amplitude(struct cfr_sample s)
{
	return sqrtf((float)(s.i * s.i) + (float)(s.q * s.q));
}

static float compute_phase(struct cfr_sample s)
{
	return atan2f((float)s.q, (float)s.i);
}

int cfr_features_extract(struct cfr_feature_ctx *ctx,
			 const struct cfr_capture *cap,
			 float features[SENSE_N_FEATURES])
{
	memset(features, 0, sizeof(float) * SENSE_N_FEATURES);

	if (!cap->valid || cap->num_subcarriers == 0)
		return -1;

	int n = cap->num_subcarriers;
	int nc = cap->num_chains > 0 ? cap->num_chains : 1;

	float amp_sum = 0, amp_sum_sq = 0;
	float phase_sum = 0, phase_sum_sq = 0;
	float amp_min = 1e9, amp_max = 0;
	float delta_sum = 0, delta_sum_sq = 0;

	float amplitude[CFR_MAX_SUBCARRIERS];

	for (int sc = 0; sc < n; sc++) {
		float chain_amp_sum = 0;
		float chain_phase_sum = 0;

		for (int ch = 0; ch < nc; ch++) {
			int idx = sc * nc + ch;
			float a = compute_amplitude(cap->data[idx]);
			float p = compute_phase(cap->data[idx]);
			chain_amp_sum += a;
			chain_phase_sum += p;
		}

		float amp = chain_amp_sum / nc;
		float phase = chain_phase_sum / nc;
		amplitude[sc] = amp;

		amp_sum += amp;
		amp_sum_sq += amp * amp;
		if (amp < amp_min) amp_min = amp;
		if (amp > amp_max) amp_max = amp;

		phase_sum += phase;
		phase_sum_sq += phase * phase;

		if (ctx->prev_valid) {
			float d = amp - ctx->prev_amplitude[sc];
			delta_sum += d;
			delta_sum_sq += d * d;
		}
	}

	float amp_mean = amp_sum / n;
	float amp_var = (amp_sum_sq / n) - (amp_mean * amp_mean);
	if (amp_var < 0) amp_var = 0;

	float phase_mean = phase_sum / n;
	float phase_var = (phase_sum_sq / n) - (phase_mean * phase_mean);
	if (phase_var < 0) phase_var = 0;

	float delta_mean = 0, delta_var = 0;
	if (ctx->prev_valid) {
		delta_mean = delta_sum / n;
		delta_var = (delta_sum_sq / n) - (delta_mean * delta_mean);
		if (delta_var < 0) delta_var = 0;
	}

	/*
	 * Feature vector layout:
	 *  0: amplitude mean            16: delta amplitude variance
	 *  1: amplitude variance        17: delta amplitude max
	 *  2: amplitude std             18: temporal consistency
	 *  3: amplitude min             19: phase spread
	 *  4: amplitude max             20-23: per-chain amplitude means
	 *  5: amplitude range           24: bandwidth_mhz / 160
	 *  6: phase mean                25: center_freq / 6000
	 *  7: phase variance            26: num_subcarriers / 256
	 *  8: phase std                 27: num_chains / 4
	 *  9-15: amplitude subband means (8 equal subbands)
	 */

	features[0] = amp_mean / 1000.0f;
	features[1] = amp_var / 1e6f;
	features[2] = sqrtf(amp_var) / 1000.0f;
	features[3] = amp_min / 1000.0f;
	features[4] = amp_max / 1000.0f;
	features[5] = (amp_max - amp_min) / 1000.0f;
	features[6] = phase_mean / M_PI;
	features[7] = phase_var / (M_PI * M_PI);
	features[8] = sqrtf(phase_var) / M_PI;

	int subbands = 7;
	int per_band = n / subbands;
	if (per_band < 1) per_band = 1;
	for (int b = 0; b < subbands && b < 7; b++) {
		float band_sum = 0;
		int start = b * per_band;
		int end = (b + 1) * per_band;
		if (end > n) end = n;
		for (int sc = start; sc < end; sc++)
			band_sum += amplitude[sc];
		features[9 + b] = band_sum / (end - start) / 1000.0f;
	}

	features[16] = delta_var / 1e6f;

	float delta_max = 0;
	if (ctx->prev_valid) {
		for (int sc = 0; sc < n; sc++) {
			float d = fabsf(amplitude[sc] - ctx->prev_amplitude[sc]);
			if (d > delta_max) delta_max = d;
		}
	}
	features[17] = delta_max / 1000.0f;
	features[18] = ctx->prev_valid ? (1.0f - sqrtf(delta_var) / (amp_mean + 1.0f)) : 0.5f;
	features[19] = sqrtf(phase_var);

	for (int ch = 0; ch < nc && ch < 4; ch++) {
		float ch_sum = 0;
		for (int sc = 0; sc < n; sc++)
			ch_sum += compute_amplitude(cap->data[sc * nc + ch]);
		features[20 + ch] = ch_sum / n / 1000.0f;
	}

	features[24] = (float)cap->bandwidth_mhz / 160.0f;
	features[25] = (float)cap->center_freq_mhz / 6000.0f;
	features[26] = (float)n / 256.0f;
	features[27] = (float)nc / 4.0f;

	memcpy(ctx->prev_amplitude, amplitude, n * sizeof(float));
	ctx->prev_valid = 1;
	ctx->n_captures++;

	return 0;
}
