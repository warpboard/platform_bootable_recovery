LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	Retouch.c

LOCAL_MODULE := libminelf

LOCAL_CFLAGS += -Wall

include $(BUILD_STATIC_LIBRARY)
