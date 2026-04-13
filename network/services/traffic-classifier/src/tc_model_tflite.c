#include "tc_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <tensorflow/lite/c/c_api.h>

static TfLiteModel *g_model;
static TfLiteInterpreterOptions *g_options;
static TfLiteInterpreter *g_interpreter;
static int g_ready;

__attribute__((constructor))
static void tflite_model_init(void)
{
	const char *path = getenv("TC_TFLITE_MODEL");
	if (!path)
		path = "/etc/traffic-classifier/model.tflite";

	g_model = TfLiteModelCreateFromFile(path);
	if (!g_model) {
		syslog(LOG_WARNING, "tflite: failed to load model %s", path);
		return;
	}

	g_options = TfLiteInterpreterOptionsCreate();
	TfLiteInterpreterOptionsSetNumThreads(g_options, 2);

	g_interpreter = TfLiteInterpreterCreate(g_model, g_options);
	if (!g_interpreter) {
		syslog(LOG_ERR, "tflite: failed to create interpreter");
		TfLiteModelDelete(g_model);
		g_model = NULL;
		return;
	}

	if (TfLiteInterpreterAllocateTensors(g_interpreter) != kTfLiteOk) {
		syslog(LOG_ERR, "tflite: failed to allocate tensors");
		TfLiteInterpreterDelete(g_interpreter);
		TfLiteModelDelete(g_model);
		g_interpreter = NULL;
		g_model = NULL;
		return;
	}

	const TfLiteTensor *input = TfLiteInterpreterGetInputTensor(g_interpreter, 0);
	int input_dims = TfLiteTensorNumDims(input);
	int input_size = 1;
	for (int i = 0; i < input_dims; i++)
		input_size *= TfLiteTensorDim(input, i);

	if (input_size != TC_MODEL_N_FEATURES) {
		syslog(LOG_WARNING, "tflite: model expects %d features, we provide %d",
		       input_size, TC_MODEL_N_FEATURES);
	}

	g_ready = 1;
	syslog(LOG_INFO, "tflite: model loaded from %s (%d features, ready)",
	       path, input_size);
}

__attribute__((destructor))
static void tflite_model_cleanup(void)
{
	if (g_interpreter)
		TfLiteInterpreterDelete(g_interpreter);
	if (g_options)
		TfLiteInterpreterOptionsDelete(g_options);
	if (g_model)
		TfLiteModelDelete(g_model);
	g_interpreter = NULL;
	g_options = NULL;
	g_model = NULL;
	g_ready = 0;
}

int tc_model_predict(const float *features, float *output)
{
	if (!g_ready) {
		memset(output, 0, sizeof(float) * TC_MODEL_N_CLASSES);
		return 0;
	}

	TfLiteTensor *input = TfLiteInterpreterGetInputTensor(g_interpreter, 0);
	memcpy(TfLiteTensorData(input), features,
	       sizeof(float) * TC_MODEL_N_FEATURES);

	if (TfLiteInterpreterInvoke(g_interpreter) != kTfLiteOk) {
		memset(output, 0, sizeof(float) * TC_MODEL_N_CLASSES);
		return 0;
	}

	const TfLiteTensor *out_tensor =
		TfLiteInterpreterGetOutputTensor(g_interpreter, 0);
	const float *scores = (const float *)TfLiteTensorData(out_tensor);

	int num_classes = TfLiteTensorDim(out_tensor,
			   TfLiteTensorNumDims(out_tensor) - 1);
	if (num_classes > TC_MODEL_N_CLASSES)
		num_classes = TC_MODEL_N_CLASSES;

	memcpy(output, scores, sizeof(float) * num_classes);
	if (num_classes < TC_MODEL_N_CLASSES)
		memset(output + num_classes, 0,
		       sizeof(float) * (TC_MODEL_N_CLASSES - num_classes));

	int best = 0;
	for (int i = 1; i < num_classes; i++) {
		if (output[i] > output[best])
			best = i;
	}

	return best;
}

int tc_model_available(void)
{
	return g_ready;
}
