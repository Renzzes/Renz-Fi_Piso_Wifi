include $(TOPDIR)/rules.mk

PKG_NAME:=renzfi
PKG_VERSION:=0.5.0
PKG_RELEASE:=1
PKG_LICENSE:=Proprietary

include $(INCLUDE_DIR)/package.mk

define Package/renzfi
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Captive Portals
  TITLE:=Renz-Fi Piso WiFi Core (Router Edition)
  URL:=https://github.com/Renzzes/Renz-Fi_Piso_Wifi
  DEPENDS:=+uhttpd +uhttpd-mod-lua +lua +libuci-lua +rpcd +jshn
  PKGARCH:=all
endef

define Package/renzfi/description
  Renz-Fi Router Edition: Lua Core plus Admin PWA hosted on OpenWrt.
  Coin, voucher, session, and sales run on-device. Internet grant uses
  openNDS/nodogsplash when present, otherwise nftables. Admin is an
  optional client of /api. This package does not include ESP32 firmware
  or MikroTik RouterOS integration.
endef

define Package/renzfi/conffiles
/etc/config/renzfi
/etc/renzfi/
endef

define Build/Prepare
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/renzfi/install
	$(INSTALL_DIR) $(1)/opt/renzfi/lua
	$(CP) ./files/opt/renzfi/lua/* $(1)/opt/renzfi/lua/
	$(INSTALL_DIR) $(1)/www/renzfi
	$(CP) ./files/www/renzfi/. $(1)/www/renzfi/ 2>/dev/null || true
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/renzfi $(1)/etc/init.d/renzfi
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/renzfi $(1)/etc/config/renzfi
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/etc/uci-defaults/99-renzfi $(1)/etc/uci-defaults/99-renzfi
	$(INSTALL_DIR) $(1)/opt/renzfi/data
	$(INSTALL_DIR) $(1)/etc/renzfi
endef

$(eval $(call BuildPackage,renzfi))
