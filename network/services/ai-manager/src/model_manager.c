#include "model_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <json-c/json.h>

#define MANIFEST_PATH MODEL_DIR "/manifest.json"

struct model_manager {
	struct model_info models[MAX_MODELS];
	int count;
};

static void load_manifest(struct model_manager *m)
{
	m->count = 0;

	struct json_object *root = json_object_from_file(MANIFEST_PATH);
	if (!root)
		return;

	int len = json_object_array_length(root);
	for (int i = 0; i < len && i < MAX_MODELS; i++) {
		struct json_object *item = json_object_array_get_idx(root, i);
		struct model_info *mi = &m->models[m->count];
		struct json_object *val;

		if (json_object_object_get_ex(item, "name", &val))
			strncpy(mi->name, json_object_get_string(val),
				sizeof(mi->name) - 1);
		if (json_object_object_get_ex(item, "version", &val))
			strncpy(mi->version, json_object_get_string(val),
				sizeof(mi->version) - 1);
		if (json_object_object_get_ex(item, "path", &val))
			strncpy(mi->path, json_object_get_string(val),
				sizeof(mi->path) - 1);
		if (json_object_object_get_ex(item, "sha256", &val))
			strncpy(mi->sha256, json_object_get_string(val),
				sizeof(mi->sha256) - 1);
		if (json_object_object_get_ex(item, "deployed_at", &val))
			mi->deployed_at = json_object_get_int64(val);

		struct stat st;
		mi->active = (stat(mi->path, &st) == 0);
		m->count++;
	}

	json_object_put(root);
}

static int save_manifest(struct model_manager *m)
{
	struct json_object *root = json_object_new_array();

	for (int i = 0; i < m->count; i++) {
		struct model_info *mi = &m->models[i];
		struct json_object *item = json_object_new_object();

		json_object_object_add(item, "name",
			json_object_new_string(mi->name));
		json_object_object_add(item, "version",
			json_object_new_string(mi->version));
		json_object_object_add(item, "path",
			json_object_new_string(mi->path));
		json_object_object_add(item, "sha256",
			json_object_new_string(mi->sha256));
		json_object_object_add(item, "deployed_at",
			json_object_new_int64(mi->deployed_at));

		json_object_array_add(root, item);
	}

	int ret = json_object_to_file_ext(MANIFEST_PATH, root,
					  JSON_C_TO_STRING_PRETTY);
	json_object_put(root);

	if (ret < 0) {
		syslog(LOG_ERR, "model_manager: failed to write manifest");
		return -1;
	}
	return 0;
}

struct model_manager *model_manager_init(void)
{
	struct model_manager *m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;

	load_manifest(m);
	syslog(LOG_INFO, "model_manager: loaded %d models from manifest",
	       m->count);
	return m;
}

void model_manager_destroy(struct model_manager *m)
{
	free(m);
}

int model_manager_get_models(struct model_manager *m,
			     struct model_info *buf, int max_count)
{
	int n = m->count < max_count ? m->count : max_count;
	memcpy(buf, m->models, n * sizeof(struct model_info));
	return n;
}

int model_manager_deploy(struct model_manager *m,
			 const char *staging_path,
			 const char *name,
			 const char *version,
			 const char *target_path)
{
	struct stat st;
	if (stat(staging_path, &st) != 0) {
		syslog(LOG_ERR, "model_manager: staging file not found: %s",
		       staging_path);
		return -1;
	}

	/* Back up existing model if present */
	char backup[300];
	snprintf(backup, sizeof(backup), "%s.bak", target_path);
	if (stat(target_path, &st) == 0)
		rename(target_path, backup);

	if (rename(staging_path, target_path) != 0) {
		syslog(LOG_ERR, "model_manager: deploy failed for %s", name);
		if (stat(backup, &st) == 0)
			rename(backup, target_path);
		return -1;
	}

	/* Update manifest */
	int idx = -1;
	for (int i = 0; i < m->count; i++) {
		if (strcmp(m->models[i].name, name) == 0) {
			idx = i;
			break;
		}
	}

	if (idx < 0) {
		if (m->count >= MAX_MODELS) {
			syslog(LOG_ERR, "model_manager: manifest full");
			return -1;
		}
		idx = m->count++;
	}

	struct model_info *mi = &m->models[idx];
	strncpy(mi->name, name, sizeof(mi->name) - 1);
	strncpy(mi->version, version, sizeof(mi->version) - 1);
	strncpy(mi->path, target_path, sizeof(mi->path) - 1);
	mi->deployed_at = (uint64_t)time(NULL);
	mi->active = true;

	save_manifest(m);

	syslog(LOG_INFO, "model_manager: deployed %s v%s to %s",
	       name, version, target_path);
	return 0;
}

int model_manager_rollback(struct model_manager *m, const char *name)
{
	for (int i = 0; i < m->count; i++) {
		if (strcmp(m->models[i].name, name) != 0)
			continue;

		char backup[300];
		snprintf(backup, sizeof(backup), "%s.bak", m->models[i].path);

		struct stat st;
		if (stat(backup, &st) != 0) {
			syslog(LOG_ERR, "model_manager: no backup for %s", name);
			return -1;
		}

		rename(m->models[i].path, "/tmp/ai-manager-failed.tflite");
		rename(backup, m->models[i].path);

		syslog(LOG_INFO, "model_manager: rolled back %s", name);
		return 0;
	}

	return -1;
}
