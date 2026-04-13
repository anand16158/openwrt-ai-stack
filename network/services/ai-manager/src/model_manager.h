#ifndef MODEL_MANAGER_H
#define MODEL_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Model version management.
 *
 * Tracks installed .tflite models, validates checksums, and supports
 * staged deployment (download -> validate -> deploy -> rollback).
 *
 * Model metadata is stored in /etc/ai-manager/models/manifest.json:
 *   [{ "name": "traffic_classify", "version": "1.0.0",
 *      "path": "/etc/traffic-classifier/model.tflite",
 *      "sha256": "abc123...", "deployed_at": 1234567890 }]
 */

#define MODEL_DIR         "/etc/ai-manager/models"
#define MODEL_STAGING_DIR "/tmp/ai-manager/staging"
#define MAX_MODELS        8

struct model_info {
	char name[64];
	char version[32];
	char path[256];
	char sha256[65];
	uint64_t deployed_at;
	bool active;
};

struct model_manager;

struct model_manager *model_manager_init(void);
void model_manager_destroy(struct model_manager *m);

int model_manager_get_models(struct model_manager *m,
			     struct model_info *buf, int max_count);

int model_manager_deploy(struct model_manager *m,
			 const char *staging_path,
			 const char *name,
			 const char *version,
			 const char *target_path);

int model_manager_rollback(struct model_manager *m, const char *name);

#endif
