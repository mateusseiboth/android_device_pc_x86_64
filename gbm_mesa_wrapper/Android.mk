# minigbm's gbm_mesa backend dlopens this wrapper around Mesa's libgbm_mesa. It must be a make module
# because libgbm_mesa is built by aospext (make), and external/ may no longer hold Android.mk files.
LOCAL_PATH := external/minigbm/gbm_mesa_driver

include $(CLEAR_VARS)
LOCAL_MODULE := libgbm_mesa_wrapper
LOCAL_SRC_FILES := gbm_mesa_wrapper.c
LOCAL_SHARED_LIBRARIES := libdrm libgbm_mesa liblog
LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)
