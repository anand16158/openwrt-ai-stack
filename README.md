# openwrt-ai-stack

An on-device AI/ML stack for OpenWrt routers. Provides real-time traffic classification, intelligent QoS, WiFi-based motion sensing, network intrusion detection, and centralized model management -- all running locally on the router with no cloud dependency.

## Architecture

```
                   ┌──────────────────────────────────┐
                   │         ai-manager                │
                   │  health · models · telemetry      │
                   └──┬──────────┬──────────┬─────────┘
                      │          │          │
         ┌────────────▼──┐  ┌───▼────┐  ┌──▼─────────┐
         │traffic-        │  │wifi-   │  │net-ids     │
         │classifier      │  │sense   │  │(intrusion  │
         │(DPI + ML)      │  │(CFR)   │  │ detection) │
         └──────┬─────────┘  └───┬────┘  └──────┬─────┘
                │                │               │
  ubus event:   │                │               │
  "traffic-     │                │               │
   classifier   │                │               │
   .classify"   │                │               │
                │                │               │
         ┌──────▼─────────┐     │               │
         │smart-qos       │     │               │
         │(tc HTB +       │     │               │
         │ nftables)      │     │               │
         └────────────────┘     │               │
                                │               │
         ┌──────────────────────┘               │
         │  ubus event: "wifi-sense.detect"     │
         │  ubus event: "net-ids.alert" ────────┘
         │
         └──> luci-app-traffic-classifier (web dashboard)

         ┌────────────────────────────────────────┐
         │  tensorflow-lite (shared .so library)   │
         │  Used by: traffic-classifier, wifi-     │
         │  sense, net-ids                         │
         └────────────────────────────────────────┘
```

All daemons communicate via **ubus** (OpenWrt IPC), are managed by **procd** (init/restart), and configured via **UCI**.

## Packages

| Package | Description |
|---------|-------------|
| **tensorflow-lite** | TensorFlow Lite 2.18.1 shared library with XNNPack NEON acceleration for ARM64 |
| **traffic-classifier** | Real-time DPI + ML traffic classification engine (libpcap capture, flow tracking, DNS context, device fingerprinting, anomaly detection) |
| **smart-qos** | Subscribes to classification events; programs Linux `tc` HTB queues and `nft` connmark rules for per-flow QoS |
| **wifi-sense** | Reads ath11k CFR data from debugfs, extracts channel features, runs TFLite model for motion/presence detection |
| **net-ids** | Subscribes to flow events, extracts connection-level features, runs TFLite anomaly model, emits intrusion alerts |
| **ai-manager** | Central coordinator: health monitoring, model deployment/rollback, aggregated telemetry, unified ubus API |
| **luci-app-traffic-classifier** | LuCI web UI with dashboards for flows, clients, anomalies, and settings |

## Prerequisites

- OpenWrt 23.05 or later
- Target: ARM64 (aarch64) -- tested on Qualcomm IPQ50xx
- Build system dependencies: CMake 3.16+, Python 3

## Installation

### Option 1: Use as an OpenWrt feed (recommended)

Clone this repo alongside your OpenWrt source tree:

```bash
git clone https://github.com/anand16158/openwrt-ai-stack.git```

Add to your OpenWrt `feeds.conf`:

```
src-link ai /path/to/openwrt-ai-stack
```

Or directly from GitHub:

```
src-git ai https://github.com/anand16158/openwrt-ai-stack.git
```

Then update and install:

```bash
./scripts/feeds update ai
./scripts/feeds install -a -p ai
```

### Option 2: Copy into your OpenWrt build tree

```bash
cp -r /path/to/openwrt-ai-stack/{libs,network,luci-app-traffic-classifier} \
  /path/to/openwrt/package/
```

### Enable packages

Add to your `.config` or profile:

```
CONFIG_PACKAGE_tensorflow-lite=y
CONFIG_PACKAGE_traffic-classifier=y
CONFIG_PACKAGE_luci-app-traffic-classifier=y
CONFIG_PACKAGE_smart-qos=y
CONFIG_PACKAGE_wifi-sense=y
CONFIG_PACKAGE_net-ids=y
CONFIG_PACKAGE_ai-manager=y
```

Then build:

```bash
make -j$(nproc)
```

## Configuration (UCI)

Each daemon reads its config from `/etc/config/<package-name>`. Examples:

```bash
# Set traffic-classifier capture interface
uci set traffic-classifier.global.interface='br-lan'
uci commit traffic-classifier

# Set smart-qos bandwidth limit
uci set smart-qos.global.bandwidth='100000'
uci commit smart-qos

# Set wifi-sense zone name
uci set wifi-sense.global.zone='living_room'
uci commit wifi-sense
```

## ubus API

```bash
# Check overall AI stack health
ubus call ai-manager status
ubus call ai-manager health

# List deployed models
ubus call ai-manager get_models

# Deploy a new model
ubus call ai-manager deploy_model \
  '{"staging_path":"/tmp/model.tflite","name":"traffic","version":"2.0","target_path":"/etc/traffic-classifier/model.tflite"}'

# Get active flows
ubus call traffic-classifier get_flows

# Get per-client traffic stats
ubus call traffic-classifier get_clients

# Listen for real-time events
ubus listen traffic-classifier.classify
ubus listen wifi-sense.detect
ubus listen net-ids.alert
```

## ML Model Backends

The **traffic-classifier** supports three selectable backends (build-time config):

| Backend | Config option | Description |
|---------|--------------|-------------|
| XGBoost (default) | `CONFIG_TRAFFIC_CLASSIFIER_XGBOOST` | Pre-compiled decision tree in C (~1.3MB, zero runtime deps) |
| TFLite | `CONFIG_TRAFFIC_CLASSIFIER_TFLITE` | Neural network loaded from `.tflite` file at runtime |
| Stub | `CONFIG_TRAFFIC_CLASSIFIER_STUB` | Returns "unknown" for all flows (testing/development) |

**wifi-sense** and **net-ids** use TFLite exclusively and expect `.tflite` model files at their configured paths.

## Directory Structure

```
openwrt-ai-stack/
  libs/
    tensorflow-lite/            # Shared ML inference library
      Makefile
  network/
    services/
      traffic-classifier/       # Core classification engine
        Makefile
        src/                    # C sources (capture, classifier, flow_table, ...)
        files/                  # UCI config, init script, stub model
      smart-qos/                # QoS enforcement daemon
      wifi-sense/               # WiFi motion/presence detection
      net-ids/                  # Network intrusion detection
      ai-manager/               # Central AI coordinator
  luci-app-traffic-classifier/  # Web UI dashboard
    Makefile
    htdocs/                     # JavaScript views
    root/                       # LuCI menu and ACL definitions
  LICENSE
  README.md
```

## License

Apache License 2.0 -- see [LICENSE](LICENSE).
