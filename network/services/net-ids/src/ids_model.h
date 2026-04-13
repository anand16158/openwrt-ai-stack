#ifndef IDS_MODEL_H
#define IDS_MODEL_H

#include "ids_features.h"

/*
 * TFLite anomaly detection model wrapper.
 *
 * Input:  float[IDS_N_FEATURES]
 * Output: float[1] — anomaly score (0.0 = normal, 1.0 = malicious)
 */

struct ids_model;

struct ids_model *ids_model_init(const char *tflite_path);
void ids_model_destroy(struct ids_model *m);

/*
 * Run anomaly detection. Returns anomaly score [0.0, 1.0].
 * Returns -1.0 on error.
 */
float ids_model_score(struct ids_model *m,
		      const float features[IDS_N_FEATURES]);

int ids_model_available(struct ids_model *m);

#endif
