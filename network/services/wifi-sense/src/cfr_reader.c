/*
 * ath11k CFR reader — reads Channel Frequency Response data from debugfs.
 *
 * The ath11k driver exposes CFR captures at:
 *   /sys/kernel/debug/ath11k/<phy>/cfr_capture
 * as a binary relay file. Each capture contains a header followed by
 * complex I/Q samples for all subcarriers and antenna chains.
 */

#include "cfr_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <sys/stat.h>
#include <time.h>
#include <glob.h>

#define ATH11K_DEBUGFS_BASE "/sys/kernel/debug/ath11k"
#define CFR_CAPTURE_FILE    "cfr_capture"
#define CFR_ENABLE_FILE     "cfr_enable"

/*
 * ath11k CFR capture header (may vary by firmware version).
 * This is a reasonable default for QCA IPQ50xx.
 */
struct __attribute__((packed)) cfr_capture_hdr {
	uint32_t magic;
	uint32_t hdr_len;
	uint16_t capture_type;
	uint16_t capture_bw;
	uint32_t num_subcarriers;
	uint8_t  num_chains;
	uint8_t  peer_mac[6];
	uint8_t  padding;
	uint64_t timestamp;
	uint32_t center_freq;
};

#define CFR_MAGIC 0xCF520001

struct cfr_reader {
	int fd;
	char path[256];
	char enable_path[256];
	char phy_name[64];
};

static int find_phy_path(const char *phy_name, char *buf, int buflen)
{
	if (phy_name && phy_name[0]) {
		snprintf(buf, buflen, "%s/%s", ATH11K_DEBUGFS_BASE, phy_name);
		struct stat st;
		if (stat(buf, &st) == 0)
			return 0;
	}

	glob_t gl;
	char pattern[256];
	snprintf(pattern, sizeof(pattern), "%s/*", ATH11K_DEBUGFS_BASE);

	if (glob(pattern, GLOB_ONLYDIR, NULL, &gl) != 0 || gl.gl_pathc == 0) {
		globfree(&gl);
		return -1;
	}

	snprintf(buf, buflen, "%s", gl.gl_pathv[0]);
	globfree(&gl);
	return 0;
}

struct cfr_reader *cfr_reader_init(const char *phy_name)
{
	struct cfr_reader *r = calloc(1, sizeof(*r));
	if (!r)
		return NULL;

	r->fd = -1;

	if (phy_name)
		strncpy(r->phy_name, phy_name, sizeof(r->phy_name) - 1);

	char phy_path[256];
	if (find_phy_path(phy_name, phy_path, sizeof(phy_path)) < 0) {
		syslog(LOG_WARNING, "cfr: no ath11k debugfs found");
		free(r);
		return NULL;
	}

	snprintf(r->path, sizeof(r->path), "%s/%s", phy_path, CFR_CAPTURE_FILE);
	snprintf(r->enable_path, sizeof(r->enable_path), "%s/%s",
		 phy_path, CFR_ENABLE_FILE);

	syslog(LOG_INFO, "cfr: using %s", r->path);
	return r;
}

void cfr_reader_destroy(struct cfr_reader *r)
{
	if (!r)
		return;
	if (r->fd >= 0)
		close(r->fd);
	free(r);
}

int cfr_reader_enable(struct cfr_reader *r)
{
	FILE *fp = fopen(r->enable_path, "w");
	if (!fp) {
		syslog(LOG_WARNING, "cfr: cannot write %s: %s",
		       r->enable_path, strerror(errno));
		return -1;
	}
	fprintf(fp, "1\n");
	fclose(fp);
	syslog(LOG_INFO, "cfr: capture enabled via %s", r->enable_path);
	return 0;
}

int cfr_reader_disable(struct cfr_reader *r)
{
	FILE *fp = fopen(r->enable_path, "w");
	if (!fp)
		return -1;
	fprintf(fp, "0\n");
	fclose(fp);
	return 0;
}

int cfr_reader_get_fd(struct cfr_reader *r)
{
	if (r->fd < 0) {
		r->fd = open(r->path, O_RDONLY | O_NONBLOCK);
		if (r->fd < 0) {
			syslog(LOG_DEBUG, "cfr: open %s failed: %s",
			       r->path, strerror(errno));
			return -1;
		}
	}
	return r->fd;
}

int cfr_reader_next(struct cfr_reader *r, struct cfr_capture *cap)
{
	if (r->fd < 0)
		return -1;

	memset(cap, 0, sizeof(*cap));

	uint8_t buf[sizeof(struct cfr_capture_hdr) + CFR_CAPTURE_BUF_SZ];
	ssize_t n = read(r->fd, buf, sizeof(buf));
	if (n <= 0)
		return -1;

	if ((size_t)n < sizeof(struct cfr_capture_hdr))
		return -1;

	struct cfr_capture_hdr *hdr = (struct cfr_capture_hdr *)buf;

	if (hdr->magic != CFR_MAGIC) {
		syslog(LOG_DEBUG, "cfr: bad magic 0x%08x", hdr->magic);
		return -1;
	}

	cap->timestamp_us = hdr->timestamp;
	memcpy(cap->peer_mac, hdr->peer_mac, 6);
	cap->num_chains = hdr->num_chains;
	cap->num_subcarriers = hdr->num_subcarriers;
	cap->bandwidth_mhz = hdr->capture_bw;
	cap->center_freq_mhz = hdr->center_freq;

	if (cap->num_subcarriers > CFR_MAX_SUBCARRIERS)
		cap->num_subcarriers = CFR_MAX_SUBCARRIERS;
	if (cap->num_chains > CFR_MAX_CHAINS)
		cap->num_chains = CFR_MAX_CHAINS;

	size_t data_bytes = cap->num_subcarriers * cap->num_chains *
			    sizeof(struct cfr_sample);
	size_t avail = n - hdr->hdr_len;

	if (avail < data_bytes)
		data_bytes = avail;

	memcpy(cap->data, buf + hdr->hdr_len, data_bytes);
	cap->valid = 1;

	return 0;
}
