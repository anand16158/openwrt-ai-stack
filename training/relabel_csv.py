#!/usr/bin/env python3
"""
Relabel training CSV based on user-provided timestamp ranges.
Flows within a time range get the correct label from the user's activity log.
Flows outside any range keep their original daemon-assigned label.
"""

import csv
import sys

HEADER = [
    "flow_duration_sec", "total_fwd_packets", "total_bwd_packets",
    "total_fwd_bytes", "total_bwd_bytes", "fwd_bwd_bytes_ratio",
    "avg_packet_size", "std_packet_size", "avg_iat_usec",
    "std_iat_usec", "min_packet_size", "max_packet_size",
    "packets_per_second", "bytes_per_second", "fwd_pkt_ratio",
    "avg_fwd_pkt_size", "avg_bwd_pkt_size", "tcp_flags_or",
    "syn_count", "protocol", "label_id", "label", "domain",
    "app_name", "src_port", "dst_port", "timestamp",
]

CLASS_NAMES = [
    "unknown", "video", "gaming", "social",
    "browsing", "download", "voip", "other",
]

LABEL_MAP = {name: i for i, name in enumerate(CLASS_NAMES)}

TIME_RANGES = [
    (1776087136, 1776087280, "video"),
    (1776087280, 1776087391, "social"),
    (1776087391, 1776087507, "browsing"),
    (1776087507, 1776087649, "voip"),
    (1776087649, 1776087720, "download"),
    (1776087720, 1776088000, "gaming"),
]


def get_label_for_timestamp(ts):
    for start, end, cls in TIME_RANGES:
        if start <= ts <= end:
            return LABEL_MAP[cls], cls
    return None, None


def main():
    input_path = sys.argv[1] if len(sys.argv) > 1 else "data/tc-training-data.csv"
    output_path = sys.argv[2] if len(sys.argv) > 2 else "data/labeled_real_data.csv"

    relabeled = 0
    kept = 0
    skipped = 0
    total = 0
    stats = {cls: 0 for cls in CLASS_NAMES}

    with open(input_path, "r") as fin, open(output_path, "w", newline="") as fout:
        writer = csv.writer(fout)
        writer.writerow(HEADER)

        for line_no, line in enumerate(fin):
            line = line.strip()
            if not line or line.startswith("flow_duration_sec"):
                continue

            parts = line.split(",")
            if len(parts) < 27:
                skipped += 1
                continue

            try:
                float(parts[0])
            except ValueError:
                skipped += 1
                continue

            total += 1

            try:
                ts = int(float(parts[26]))
            except (ValueError, IndexError):
                skipped += 1
                continue

            new_label, new_class = get_label_for_timestamp(ts)
            if new_label is not None:
                parts[20] = str(new_label)
                parts[21] = new_class
                relabeled += 1
            else:
                kept += 1

            class_name = parts[21]
            if class_name in stats:
                stats[class_name] += 1

            writer.writerow(parts)

    print(f"\nRelabeling complete:")
    print(f"  Total flows:   {total}")
    print(f"  Relabeled:     {relabeled}")
    print(f"  Kept original: {kept}")
    print(f"  Skipped:       {skipped}")
    print(f"\nClass distribution:")
    for cls in CLASS_NAMES:
        print(f"  {cls:>10s}: {stats[cls]:6d}")
    print(f"\nOutput: {output_path}")


if __name__ == "__main__":
    main()
