include $(TOPDIR)/rules.mk

PKG_NAME:=traffic-classifier
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=Anand Kumar
PKG_BUILD_PARALLEL:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/traffic-classifier
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Edge AI network traffic classifier
  DEPENDS:=+libubox +libubus +libblobmsg-json +libjson-c +libpcap \
	+TRAFFIC_CLASSIFIER_TFLITE:tensorflow-lite
endef

define Package/traffic-classifier/config
  choice
    prompt "ML model backend"
    default TRAFFIC_CLASSIFIER_XGBOOST
    help
      Select which ML inference backend to use for traffic classification.

    config TRAFFIC_CLASSIFIER_XGBOOST
      bool "XGBoost (Treelite compiled C)"
      help
        Use a treelite-exported XGBoost model compiled directly into the
        binary as C code. No runtime dependencies.

    config TRAFFIC_CLASSIFIER_TFLITE
      bool "TensorFlow Lite"
      select PACKAGE_tensorflow-lite
      help
        Use TensorFlow Lite for inference. Loads .tflite models at runtime
        from /etc/traffic-classifier/model.tflite. Requires tensorflow-lite
        library.

    config TRAFFIC_CLASSIFIER_STUB
      bool "None (heuristic only)"
      help
        No ML model. Classification uses domain-based and port/protocol
        heuristics only.
  endchoice
endef

define Package/traffic-classifier/description
  Real-time network traffic classification daemon for OpenWRT.
  Captures flows on the bridge interface, extracts statistical features
  (packet sizes, inter-arrival times, byte ratios), and classifies
  traffic into application categories (video, gaming, social, browsing).
  Exposes per-client per-SSID results via ubus.
endef

define Package/traffic-classifier/conffiles
/etc/config/traffic-classifier
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

TC_MODEL_SRC = $(PKG_BUILD_DIR)/tc_model_stub.c
TC_MODEL_LIBS =

ifdef CONFIG_TRAFFIC_CLASSIFIER_XGBOOST
  TC_MODEL_SRC = $(if $(wildcard $(PKG_BUILD_DIR)/tc_model_xgb.c),$(PKG_BUILD_DIR)/tc_model_xgb.c,$(PKG_BUILD_DIR)/tc_model_stub.c)
endif
ifdef CONFIG_TRAFFIC_CLASSIFIER_TFLITE
  TC_MODEL_SRC = $(PKG_BUILD_DIR)/tc_model_tflite.c
  TC_MODEL_LIBS = -ltensorflow-lite -lstdc++
endif

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(PKG_BUILD_DIR)/traffic-classifier \
		$(PKG_BUILD_DIR)/main.c \
		$(PKG_BUILD_DIR)/capture.c \
		$(PKG_BUILD_DIR)/flow_table.c \
		$(PKG_BUILD_DIR)/features.c \
		$(PKG_BUILD_DIR)/classifier.c \
		$(PKG_BUILD_DIR)/dns_cache.c \
		$(TC_MODEL_SRC) \
		$(PKG_BUILD_DIR)/qos_manager.c \
		$(PKG_BUILD_DIR)/device_fingerprint.c \
		$(PKG_BUILD_DIR)/usage_profile.c \
		$(PKG_BUILD_DIR)/data_collect.c \
		$(PKG_BUILD_DIR)/telemetry.c \
		$(PKG_BUILD_DIR)/ubus_api.c \
		$(PKG_BUILD_DIR)/sta_tracker.c \
		-lubox -lubus -lblobmsg_json -ljson-c -lpcap -lm $(TC_MODEL_LIBS)
endef

define Package/traffic-classifier/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/traffic-classifier $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/traffic-classifier.init $(1)/etc/init.d/traffic-classifier

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) ./files/traffic-classifier.conf $(1)/etc/config/traffic-classifier

	$(INSTALL_DIR) $(1)/etc/traffic-classifier
	$(INSTALL_DATA) ./files/model_stub.json $(1)/etc/traffic-classifier/model.json
endef

$(eval $(call BuildPackage,traffic-classifier))
