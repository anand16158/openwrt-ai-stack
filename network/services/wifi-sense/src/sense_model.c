#include "sense_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <tensorflow/lite/c/c_api.h>

struct sense_model {
	TfLiteModel *model;
	TfLiteInterpreterOptions *options;
	TfLiteInterpreter *interpreter;
	int ready;
};

struct sense_model *sense_model_init(const char *tflite_path)
{
	struct sense_model *m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;

	m->model = TfLiteModelCreateFromFile(tflite_path);
	if (!m->model) {
		syslog(LOG_WARNING, "wifi-sense: failed to load model %s",
		       tflite_path);
		free(m);
		return NULL;
	}

	m->options = TfLiteInterpreterOptionsCreate();
	TfLiteInterpreterOptionsSetNumThreads(m->options, 1);

	m->interpreter = TfLiteInterpreterCreate(m->model, m->options);
	if (!m->interpreter) {
		syslog(LOG_ERR, "wifi-sense: interpreter creation failed");
		TfLiteModelDelete(m->model);
		free(m);
		return NULL;
	}

	if (TfLiteInterpreterAllocateTensors(m->interpreter) != kTfLiteOk) {
		syslog(LOG_ERR, "wifi-sense: tensor allocation failed");
		TfLiteInterpreterDelete(m->interpreter);
		TfLiteModelDelete(m->model);
		free(m);
		return NULL;
	}

	m->ready = 1;
	syslog(LOG_INFO, "wifi-sense: model loaded from %s", tflite_path);
	return m;
}

void sense_model_destroy(struct sense_model *m)
{
	if (!m)
		return;
	if (m->interpreter)
		TfLiteInterpreterDelete(m->interpreter);
	if (m->options)
		TfLiteInterpreterOptionsDelete(m->options);
	if (m->model)
		TfLiteModelDelete(m->model);
	free(m);
}

int sense_model_predict(struct sense_model *m,
			const float features[SENSE_N_FEATURES],
			struct sense_result *result)
{
	memset(result, 0, sizeof(*result));

	if (!m || !m->ready)
		return -1;

	TfLiteTensor *input = TfLiteInterpreterGetInputTensor(m->interpreter, 0);
	if (!input)
		return -1;

	memcpy(TfLiteTensorData(input), features,
	       sizeof(float) * SENSE_N_FEATURES);

	if (TfLiteInterpreterInvoke(m->interpreter) != kTfLiteOk)
		return -1;

	const TfLiteTensor *output =
		TfLiteInterpreterGetOutputTensor(m->interpreter, 0);
	const float *scores = (const float *)TfLiteTensorData(output);

	int num_out = TfLiteTensorDim(output,
			TfLiteTensorNumDims(output) - 1);
	if (num_out > SENSE_STATE_COUNT)
		num_out = SENSE_STATE_COUNT;

	int best = 0;
	for (int i = 0; i < num_out; i++) {
		result->probs[i] = scores[i];
		if (scores[i] > scores[best])
			best = i;
	}

	result->state = (enum sense_state)best;
	result->confidence = result->probs[best];

	return 0;
}

int sense_model_available(struct sense_model *m)
{
	return (m && m->ready) ? 1 : 0;
}
