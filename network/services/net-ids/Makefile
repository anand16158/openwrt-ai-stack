include $(TOPDIR)/rules.mk

PKG_NAME:=net-ids
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=Anand Kumar
PKG_BUILD_PARALLEL:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/net-ids
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Network intrusion detection with ML
  DEPENDS:=+libubox +libubus +libblobmsg-json +libjson-c +tensorflow-lite +libstdcpp +traffic-classifier
endef

define Package/net-ids/description
  Network intrusion detection daemon for the OpenWrt AI stack.
  Subscribes to traffic-classifier events and flow data via
  ubus, extracts connection-level features (connection rates, port
  distributions, packet size entropy), runs a TFLite anomaly detection
  model, and alerts on suspicious patterns.
endef

define Package/net-ids/conffiles
/etc/config/net-ids
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(PKG_BUILD_DIR)/net-ids \
		$(PKG_BUILD_DIR)/net_ids.c \
		$(PKG_BUILD_DIR)/ids_features.c \
		$(PKG_BUILD_DIR)/ids_model.c \
		-lubox -lubus -lblobmsg_json -ljson-c \
		-ltensorflow-lite -lstdc++ -lm
endef

define Package/net-ids/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/net-ids $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/net-ids.init $(1)/etc/init.d/net-ids

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) ./files/net-ids.conf $(1)/etc/config/net-ids

	$(INSTALL_DIR) $(1)/etc/net-ids
endef

$(eval $(call BuildPackage,net-ids))
