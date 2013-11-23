LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := rdnlib
LOCAL_SRC_FILES := rdnlib.cpp
LOCAL_LDLIBS    := -lm -llog -ljnigraphics
LOCAL_CFLAGS    := -ffast-math -O3 -funroll-loops -Wall
# FIXME
LOCAL_C_INCLUDES := /home/dstahlke/apps/eigen

include $(BUILD_SHARED_LIBRARY)
