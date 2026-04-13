include $(TOPDIR)/rules.mk

PKG_NAME:=ai-manager
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0
PKG_MAINTAINER:=Anand Kumar
PKG_BUILD_PARALLEL:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/ai-manager
  SECTION:=net
  CATEGORY:=Network
  TITLE:=OpenWrt AI stack central coordinator
  DEPENDS:=+libubox +libubus +libblobmsg-json +libjson-c +libuci
endef

define Package/ai-manager/description
  Central coordinator daemon for the OpenWrt AI stack. Manages the
  lifecycle of all AI modules (traffic-classifier, smart-qos, wifi-sense,
  net-ids), handles model version management, health monitoring, and
  provides a unified ubus API for external management and uCentral
  integration. Aggregates telemetry from all modules.
endef

define Package/ai-manager/conffiles
/etc/config/ai-manager
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(PKG_BUILD_DIR)/ai-manager \
		$(PKG_BUILD_DIR)/ai_manager.c \
		$(PKG_BUILD_DIR)/model_manager.c \
		$(PKG_BUILD_DIR)/health_monitor.c \
		-lubox -lubus -lblobmsg_json -ljson-c -luci
endef

define Package/ai-manager/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ai-manager $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/ai-manager.init $(1)/etc/init.d/ai-manager

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) ./files/ai-manager.conf $(1)/etc/config/ai-manager

	$(INSTALL_DIR) $(1)/etc/ai-manager/models
endef

$(eval $(call BuildPackage,ai-manager))
