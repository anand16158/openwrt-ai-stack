# OpenWrt AI Traffic Classifier

Real-time network traffic classification for OpenWrt access points using an
XGBoost machine learning model compiled to pure C. Classifies every network
flow into one of 8 application categories with **89% accuracy** on real
traffic -- no cloud, no external dependencies, runs entirely on-device.

## Quick Start

Add this feed to any OpenWrt build tree:

```bash
echo "src-git aistack https://github.com/anand16158/openwrt-ai-stack.git;openwrt-ai-new" >> feeds.conf
./scripts/feeds update aistack
./scripts/feeds install -a -p aistack
```

Enable in `make menuconfig`:

```
Network  --->  traffic-classifier        (the daemon)
LuCI     --->  Applications  --->  luci-app-traffic-classifier  (web UI)
```

Build:

```bash
make package/traffic-classifier/compile
make package/luci-app-traffic-classifier/compile
# or just: make
```

## What's Inside

### traffic-classifier (C daemon)

| Feature | Description |
|---------|-------------|
| ML Classification | XGBoost model (100 trees, 8 classes) compiled to C -- zero runtime deps |
| DNS-aware | Caches DNS lookups to classify by domain (YouTube, Netflix, etc.) |
| Usage Profiling | Per-client behavioral baselines with anomaly detection |
| Device Fingerprint | Passive OS/device-type detection via DNS + OUI + behavior |
| Data Capture | Records labeled flows to CSV for model retraining |
| ubus API | Full API: `get_stats`, `get_flows`, `status`, `get_anomalies`, etc. |
| QoS | Optional DSCP marking via nftables |
| Telemetry | Periodic JSON export for dashboards |

### luci-app-traffic-classifier (Web UI)

LuCI dashboard with 5 views: Overview, Clients, Flows, Anomalies, Settings.

### training/ (ML Pipeline)

Complete Python pipeline to retrain the model with your own data:

```bash
cd training && pip install -r requirements.txt

# Retrain with data captured from the device:
python build_model.py --input data/tc-training-data.csv --augment

# Train on synthetic data only:
python build_model.py --samples 10000

# The script exports tc_model_xgb.c directly into the package source
```

## Classification Categories

| ID | Class | Examples |
|----|-------|---------|
| 0 | unknown | Unclassified traffic |
| 1 | video | YouTube, Netflix, Twitch, Hotstar |
| 2 | gaming | PUBG, Fortnite, Roblox |
| 3 | social | Facebook, Instagram, WhatsApp, TikTok |
| 4 | browsing | General HTTP/HTTPS web browsing |
| 5 | download | Large file transfers, app updates |
| 6 | voip | Zoom, Teams, Skype, Discord voice |
| 7 | other | DNS, NTP, background services |

## How It Works

```
Packet Capture (libpcap)
    |
    v
Flow Aggregation (5-tuple)
    |
    v
Feature Extraction (20-dim vector)
    |
    v
Classification Pipeline:
    1. DNS domain match  (highest priority)
    2. ML model          (if confidence > 40%)
    3. Heuristic fallback
    |
    v
ubus API --> LuCI Dashboard
         --> QoS/DSCP Marking
         --> Telemetry Export
```

## Feature Vector (20 dimensions)

| # | Feature | What it captures |
|---|---------|-----------------|
| 0 | flow_duration_sec | Session length |
| 1 | total_fwd_packets | Upload packet count |
| 2 | total_bwd_packets | Download packet count |
| 3 | total_fwd_bytes | Upload volume |
| 4 | total_bwd_bytes | Download volume |
| 5 | fwd_bwd_bytes_ratio | Asymmetry (streaming ~ 0.01, gaming ~ 1.0) |
| 6 | avg_packet_size | Video = large, gaming = small |
| 7 | std_packet_size | Steady (VoIP) vs bursty (browsing) |
| 8 | avg_iat_usec | Packet spacing |
| 9 | std_iat_usec | Regularity of timing |
| 10 | min_packet_size | ACKs, keepalives |
| 11 | max_packet_size | MTU-sized data packets |
| 12 | packets_per_second | Flow intensity |
| 13 | bytes_per_second | Throughput |
| 14 | fwd_pkt_ratio | Direction balance |
| 15 | avg_fwd_pkt_size | Upload packet profile |
| 16 | avg_bwd_pkt_size | Download packet profile |
| 17 | tcp_flags_or | Connection behavior |
| 18 | syn_count | Connection setup pattern |
| 19 | protocol | TCP (6) vs UDP (17) |

## Model Accuracy

Trained on 21,972 real device-captured flows + 10,000 synthetic augmentation:

| Class | Precision | Recall | F1 |
|-------|-----------|--------|-----|
| unknown | 0.88 | 0.96 | 0.92 |
| video | 0.99 | 0.80 | 0.89 |
| gaming | 0.93 | 0.95 | 0.94 |
| social | 0.81 | 0.74 | 0.77 |
| browsing | 0.87 | 0.71 | 0.78 |
| download | 1.00 | 0.95 | 0.97 |
| voip | 0.94 | 0.94 | 0.94 |
| other | 0.90 | 0.95 | 0.93 |
| **Overall** | | | **0.89** |

## ubus API

Query the classifier from the command line or scripts:

```bash
# Daemon status and summary
ubus call traffic-classifier status

# Per-client classification results
ubus call traffic-classifier get_clients

# Active flow table with per-flow class + confidence
ubus call traffic-classifier get_flows

# Aggregate statistics (bytes/packets per category)
ubus call traffic-classifier get_stats

# Anomaly alerts (unusual traffic patterns)
ubus call traffic-classifier get_anomalies
```

All methods return JSON. The LuCI dashboard uses these same endpoints.

## On-Device Data Capture

The daemon can record labeled flows for retraining:

```bash
# On the AP:
uci set traffic-classifier.main.data_capture_enabled='1'
uci commit traffic-classifier
/etc/init.d/traffic-classifier restart

# After collecting data:
scp root@<AP_IP>:/tmp/tc-training-data.csv .

# Retrain:
cd training
python build_model.py --input tc-training-data.csv --augment
# Then rebuild the package
```

## Runtime Dependencies

libubox, libubus, libblobmsg-json, libjson-c, libpcap

## License

GPL-2.0

