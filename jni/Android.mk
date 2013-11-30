LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := rdnlib
LOCAL_SRC_FILES := rdnlib.cpp
LOCAL_LDLIBS    := -lm -llog -ljnigraphics
LOCAL_CFLAGS    := -O3 -funroll-loops -Wall #-mfpu=vfpv3
LOCAL_C_INCLUDES := eigen-android

#LOCAL_C_INCLUDES += /home/dstahlke/Desktop/android-ndk-profiler
#LOCAL_CFLAGS += -pg
#LOCAL_STATIC_LIBRARIES += android-ndk-profiler

include $(BUILD_SHARED_LIBRARY)

#$(call import-module,android-ndk-profiler)
