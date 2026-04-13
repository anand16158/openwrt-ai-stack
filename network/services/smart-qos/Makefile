include $(TOPDIR)/rules.mk

PKG_NAME:=smart-qos
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=Anand Kumar
PKG_BUILD_PARALLEL:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/smart-qos
  SECTION:=net
  CATEGORY:=Network
  TITLE:=AI-driven QoS enforcement daemon
  DEPENDS:=+libubox +libubus +libblobmsg-json +libjson-c +traffic-classifier
endef

define Package/smart-qos/description
  AI-driven QoS enforcement daemon for the OpenWrt AI stack.
  Subscribes to traffic-classifier.classify ubus events and programs
  Linux tc HTB classes with nftables connmark steering for per-flow
  priority enforcement based on ML classification results.
endef

define Package/smart-qos/conffiles
/etc/config/smart-qos
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(PKG_BUILD_DIR)/smart-qos \
		$(PKG_BUILD_DIR)/smart_qos.c \
		-lubox -lubus -lblobmsg_json -ljson-c
endef

define Package/smart-qos/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/smart-qos $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/smart-qos.init $(1)/etc/init.d/smart-qos

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) ./files/smart-qos.conf $(1)/etc/config/smart-qos
endef

$(eval $(call BuildPackage,smart-qos))
