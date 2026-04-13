#include "ids_model.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <tensorflow/lite/c/c_api.h>

struct ids_model {
	TfLiteModel *model;
	TfLiteInterpreterOptions *options;
	TfLiteInterpreter *interpreter;
	int ready;
};

struct ids_model *ids_model_init(const char *tflite_path)
{
	struct ids_model *m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;

	m->model = TfLiteModelCreateFromFile(tflite_path);
	if (!m->model) {
		syslog(LOG_WARNING, "net-ids: failed to load model %s",
		       tflite_path);
		free(m);
		return NULL;
	}

	m->options = TfLiteInterpreterOptionsCreate();
	TfLiteInterpreterOptionsSetNumThreads(m->options, 1);

	m->interpreter = TfLiteInterpreterCreate(m->model, m->options);
	if (!m->interpreter ||
	    TfLiteInterpreterAllocateTensors(m->interpreter) != kTfLiteOk) {
		syslog(LOG_ERR, "net-ids: interpreter setup failed");
		if (m->interpreter)
			TfLiteInterpreterDelete(m->interpreter);
		TfLiteModelDelete(m->model);
		free(m);
		return NULL;
	}

	m->ready = 1;
	syslog(LOG_INFO, "net-ids: model loaded from %s", tflite_path);
	return m;
}

void ids_model_destroy(struct ids_model *m)
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

float ids_model_score(struct ids_model *m,
		      const float features[IDS_N_FEATURES])
{
	if (!m || !m->ready)
		return -1.0f;

	TfLiteTensor *input = TfLiteInterpreterGetInputTensor(m->interpreter, 0);
	if (!input)
		return -1.0f;

	memcpy(TfLiteTensorData(input), features,
	       sizeof(float) * IDS_N_FEATURES);

	if (TfLiteInterpreterInvoke(m->interpreter) != kTfLiteOk)
		return -1.0f;

	const TfLiteTensor *output =
		TfLiteInterpreterGetOutputTensor(m->interpreter, 0);
	const float *score = (const float *)TfLiteTensorData(output);

	return score[0];
}

int ids_model_available(struct ids_model *m)
{
	return (m && m->ready) ? 1 : 0;
}
