include $(TOPDIR)/rules.mk

PKG_NAME:=wifi-sense
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=Anand Kumar
PKG_BUILD_PARALLEL:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/wifi-sense
  SECTION:=net
  CATEGORY:=Network
  TITLE:=WiFi sensing motion/presence detection
  DEPENDS:=+libubox +libubus +libblobmsg-json +tensorflow-lite +libstdcpp
endef

define Package/wifi-sense/description
  WiFi sensing daemon for the OpenWrt AI stack.
  Reads Channel Frequency Response (CFR) data from ath11k debugfs,
  extracts amplitude/phase features, and runs a TFLite model for
  motion and presence detection. Publishes events via ubus.
endef

define Package/wifi-sense/conffiles
/etc/config/wifi-sense
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(PKG_BUILD_DIR)/wifi-sense \
		$(PKG_BUILD_DIR)/wifi_sense.c \
		$(PKG_BUILD_DIR)/cfr_reader.c \
		$(PKG_BUILD_DIR)/cfr_features.c \
		$(PKG_BUILD_DIR)/sense_model.c \
		-lubox -lubus -lblobmsg_json -ltensorflow-lite -lstdc++ -lm
endef

define Package/wifi-sense/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wifi-sense $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/wifi-sense.init $(1)/etc/init.d/wifi-sense

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) ./files/wifi-sense.conf $(1)/etc/config/wifi-sense

	$(INSTALL_DIR) $(1)/etc/wifi-sense
endef

$(eval $(call BuildPackage,wifi-sense))
