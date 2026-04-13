include $(TOPDIR)/rules.mk

PKG_NAME:=tensorflow-lite
PKG_VERSION:=2.18.1
PKG_RELEASE:=1

PKG_SOURCE:=tensorflow-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/tensorflow/tensorflow/archive/v$(PKG_VERSION)
PKG_HASH:=skip

PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=Anand Kumar

PKG_BUILD_DIR:=$(BUILD_DIR)/tensorflow-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1

CMAKE_BINARY_SUBDIR:=tflite_build

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/tensorflow-lite
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=TensorFlow Lite inference runtime
  URL:=https://www.tensorflow.org/lite
  DEPENDS:=+libstdcpp +libpthread +librt
endef

define Package/tensorflow-lite/description
  Cross-compiled TensorFlow Lite shared library for on-device ML inference.
  Includes the XNNPack CPU delegate for SIMD-optimized (NEON) execution
  on ARM64. Serves as the shared inference runtime for all OpenWrt AI modules.
endef

TFLITE_FLAGS:=-Wno-psabi -fdata-sections -ffunction-sections

CMAKE_SOURCE_SUBDIR:=tensorflow/lite

CMAKE_OPTIONS += \
	-DCMAKE_SYSTEM_NAME=Linux \
	-DCMAKE_SYSTEM_PROCESSOR=aarch64 \
	-DCMAKE_C_FLAGS="$(TFLITE_FLAGS)" \
	-DCMAKE_CXX_FLAGS="$(TFLITE_FLAGS)" \
	-DTFLITE_ENABLE_XNNPACK=ON \
	-DTFLITE_ENABLE_EXTERNAL_DELEGATE=ON \
	-DTFLITE_ENABLE_INSTALL=ON \
	-DBUILD_SHARED_LIBS=ON \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
	-DTFLITE_ENABLE_GPU=OFF \
	-DTFLITE_ENABLE_NNAPI=OFF \
	-DTFLITE_ENABLE_MMAP=ON

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include/tensorflow/lite
	$(CP) $(PKG_BUILD_DIR)/tensorflow/lite/*.h $(1)/usr/include/tensorflow/lite/

	$(INSTALL_DIR) $(1)/usr/include/tensorflow/lite/c
	$(CP) $(PKG_BUILD_DIR)/tensorflow/lite/c/*.h $(1)/usr/include/tensorflow/lite/c/

	$(INSTALL_DIR) $(1)/usr/include/tensorflow/lite/core
	$(CP) $(PKG_BUILD_DIR)/tensorflow/lite/core/*.h $(1)/usr/include/tensorflow/lite/core/

	$(INSTALL_DIR) $(1)/usr/include/tensorflow/lite/kernels
	$(CP) $(PKG_BUILD_DIR)/tensorflow/lite/kernels/register.h $(1)/usr/include/tensorflow/lite/kernels/

	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(CMAKE_BINARY_DIR)/libtensorflow-lite.so $(1)/usr/lib/
endef

define Package/tensorflow-lite/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(CMAKE_BINARY_DIR)/libtensorflow-lite.so $(1)/usr/lib/
endef

$(eval $(call BuildPackage,tensorflow-lite))
