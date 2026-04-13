#ifndef CFR_READER_H
#define CFR_READER_H

#include <stdint.h>
#include <stdbool.h>

/*
 * ath11k CFR capture reader.
 *
 * Reads Channel Frequency Response data from the ath11k debugfs
 * interface at /sys/kernel/debug/ath11k/<phy>/cfr_capture.
 * Falls back to netlink-based CFR relay if available.
 *
 * CFR data format: complex I/Q samples per subcarrier per capture.
 */

#define CFR_MAX_SUBCARRIERS 256
#define CFR_MAX_CHAINS      4
#define CFR_CAPTURE_BUF_SZ  (CFR_MAX_SUBCARRIERS * CFR_MAX_CHAINS * 4)

struct cfr_sample {
	int16_t i;
	int16_t q;
};

struct cfr_capture {
	uint64_t timestamp_us;
	uint8_t  peer_mac[6];
	uint8_t  num_chains;
	uint16_t num_subcarriers;
	uint32_t bandwidth_mhz;
	uint32_t center_freq_mhz;
	struct cfr_sample data[CFR_MAX_SUBCARRIERS * CFR_MAX_CHAINS];
	int      valid;
};

struct cfr_reader;

struct cfr_reader *cfr_reader_init(const char *phy_name);
void cfr_reader_destroy(struct cfr_reader *r);

int cfr_reader_get_fd(struct cfr_reader *r);

/*
 * Read one CFR capture. Returns 0 on success, -1 on no data / error.
 * Non-blocking when called after select/poll on the fd.
 */
int cfr_reader_next(struct cfr_reader *r, struct cfr_capture *cap);

/*
 * Enable CFR capture on the ath11k interface.
 * Requires writing to debugfs to start periodic captures.
 */
int cfr_reader_enable(struct cfr_reader *r);
int cfr_reader_disable(struct cfr_reader *r);

#endif
