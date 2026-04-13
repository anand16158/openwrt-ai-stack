#ifndef SENSE_MODEL_H
#define SENSE_MODEL_H

#include "cfr_features.h"

/*
 * TFLite model wrapper for Wi-Fi sensing inference.
 *
 * Model input:  float[SENSE_N_FEATURES] — CFR-derived features
 * Model output: float[3] — probabilities for [empty, presence, motion]
 */

enum sense_state {
	SENSE_EMPTY    = 0,
	SENSE_PRESENCE = 1,
	SENSE_MOTION   = 2,
	SENSE_STATE_COUNT
};

struct sense_result {
	enum sense_state state;
	float confidence;
	float probs[SENSE_STATE_COUNT];
};

struct sense_model;

struct sense_model *sense_model_init(const char *tflite_path);
void sense_model_destroy(struct sense_model *m);

int sense_model_predict(struct sense_model *m,
			const float features[SENSE_N_FEATURES],
			struct sense_result *result);

int sense_model_available(struct sense_model *m);

#endif
