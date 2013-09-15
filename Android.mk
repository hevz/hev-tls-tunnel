# Copyright (C) 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := libintl
LOCAL_SRC_FILES := $(GSTREAMER_SDK)/lib/libintl.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libiconv
LOCAL_SRC_FILES := $(GSTREAMER_SDK)/lib/libiconv.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libffi
LOCAL_SRC_FILES := $(GSTREAMER_SDK)/lib/libffi.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libglib-2.0
LOCAL_SRC_FILES := $(GSTREAMER_SDK)/lib/libglib-2.0.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libgobject-2.0
LOCAL_SRC_FILES := $(GSTREAMER_SDK)/lib/libgobject-2.0.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libgmodule-2.0
LOCAL_SRC_FILES := $(GSTREAMER_SDK)/lib/libgmodule-2.0.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := libgio-2.0
LOCAL_SRC_FILES := $(GSTREAMER_SDK)/lib/libgio-2.0.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := hev-tls-tunnel
LOCAL_SRC_FILES := \
	src/hev-client.c \
	src/hev-main.c \
	src/hev-protocol.c \
	src/hev-server.c \
	src/hev-utils.c
LOCAL_C_INCLUDES := $(GSTREAMER_SDK)/include $(GSTREAMER_SDK)/include/glib-2.0 $(GSTREAMER_SDK)/lib/glib-2.0/include
LOCAL_STATIC_LIBRARIES := gio-2.0 gobject-2.0 gmodule-2.0 glib-2.0 iconv intl ffi
include $(BUILD_EXECUTABLE)

