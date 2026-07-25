#
# Copyright (C) 2022 FlamingoOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Dolby path
DOLBY_PATH := vendor/oneplus/dolby

# Soong Namespace
PRODUCT_SOONG_NAMESPACES += \
    $(DOLBY_PATH) \
    hardware/oplus

# HIDL
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += $(DOLBY_PATH)/configs/vintf/dolby_framework_matrix.xml
DEVICE_MANIFEST_FILE += \
    $(DOLBY_PATH)/configs/vintf/vendor.dolby.hardware.dms@2.0-service.xml \
    $(DOLBY_PATH)/configs/vintf/vendor.dolby.media.c2@1.0-service.xml

# Configs
PRODUCT_COPY_FILES += \
    $(DOLBY_PATH)/configs/dax/dax-default.xml:$(TARGET_COPY_OUT_VENDOR)/etc/dolby/dax-default.xml \
    $(DOLBY_PATH)/configs/media/media_codecs_dolby_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_dolby_audio.xml

# Dolby
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.dolby.dax.version=DAX3_3.7.0.8_r1 \
    ro.audio.spatializer_enabled=true \
    ro.vendor.audio.dolby.dax.support=true \
    ro.vendor.audio.dolby.surround.enable=true \
    ro.audio.spatializer_transaural_enabled_default=false \
    vendor.audio.dolby.ds2.enabled=false \
    vendor.audio.dolby.ds2.hardbypass=false

# DolbyManager
PRODUCT_PACKAGES += \
    LunarisDolby

# Overlays
DEVICE_PACKAGE_OVERLAYS += \
    $(DOLBY_PATH)/overlay

# Proprietary-files
PRODUCT_COPY_FILES += \
    $(DOLBY_PATH)/proprietary/vendor/etc/init/vendor.dolby.hardware.dms@2.0-service.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/vendor.dolby.hardware.dms@2.0-service.rc \
    $(DOLBY_PATH)/proprietary/vendor/etc/init/vendor.dolby.media.c2@1.0-service.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/vendor.dolby.media.c2@1.0-service.rc

PRODUCT_PACKAGES += \
    vendor.dolby.hardware.dms@2.0-impl \
    vendor.dolby.hardware.dms@2.0 \
    vendor.dolby.hardware.dms@2.0-service \
    vendor.dolby.media.c2@1.0-service \
    libcodec2_soft_ac4dec \
    libcodec2_soft_ddpdec \
    libcodec2_soft_dolby \
    libcodec2_store_dolby \
    libdapparamstorage \
    libdeccfg \
    libdlbdsservice \
    libdlbpreg \
    libspatializerparamstorage \
    libdlbvol \
    libswdap \
    libswgamedap \
    libswspatializer \
    libswvqe

# AIDL adapter for libswdap.so. Only the AIDL effect factory needs it, but it is
# inert on HIDL targets so it is built unconditionally.
PRODUCT_PACKAGES += \
    libswdapaidl

# DAP reaches its parameter store over HIDL (vendor.dolby.hardware.dms@2.0), so
# hwservicemanager has to be on the device. base_system.mk only installs the
# /system/bin compat symlink; without the real binary from base_system_ext.mk the
# symlink dangles, dms-hal-2-0 exits 1 in a loop, and every DAP set_param returns
# -ENODEV.
PRODUCT_PACKAGES += \
    hwservicemanager

# Shipping the binary is not enough. hwservicemanager looks itself up in VINTF on
# startup and self-disables (setting hwservicemanager.disabled=true) when
# android.hidl.manager resolves to Transport::EMPTY. The default fragment declares
# it with max-level="8", which this device's target-level 202404 filters out as
# obsolete. This flag swaps in hwservicemanager_no_max.xml, which drops the
# max-level so the interface stays visible. Must be paired with hwservicemanager in
# PRODUCT_PACKAGES above - soong_config.mk hard-errors otherwise.
PRODUCT_HIDL_ENABLED := true

DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += \
     $(DOLBY_PATH)/vintf/device_framework_matrix.xml
