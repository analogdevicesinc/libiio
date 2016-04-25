
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libiio
LOCAL_SRC_FILES := channel.c device.c context.c buffer.c utilities.c
LOCAL_CFLAGS += -Wall -Wextra

# LOG_LEVEL 0=off, 1=error, 2=warning, 3=info, 4=debug
LOCAL_CFLAGS += -DLOG_LEVEL=4 -DLOG_TAG=\"libiio\"
LOCAL_SHARED_LIBRARIES += liblog

LOCAL_CFLAGS += -fvisibility=hidden
LOCAL_CFLAGS += -DHAVE_IPV6=1
LOCAL_CFLAGS += -D_GNU_SOURCE=1
LOCAL_CFLAGS += -D_POSIX_C_SOURCE=200809L -DLIBIIO_EXPORTS=1
LOCAL_CFLAGS += -DLIBIIO_VERSION_MAJOR=0 -DLIBIIO_VERSION_MINOR=6
LOCAL_CFLAGS += -DLIBIIO_VERSION_GIT=\"0.6\"

LOCAL_CFLAGS += -DLOCAL_BACKEND=1
LOCAL_SRC_FILES += local.c

#LIBIIO_USE_NETWORK = 1
ifdef LIBIIO_USE_NETWORK
    LOCAL_CFLAGS += -DNETWORK_BACKEND=1
    LOCAL_SRC_FILES += network.c

    # network requires XML
    LOCAL_CFLAGS += -DXML_BACKEND=1
    LOCAL_C_INCLUDES += external/libxml2/include
    LOCAL_SHARED_LIBRARIES += libxml2 libicuuc
    LOCAL_SRC_FILES += xml.c

    # Network requires threads
    LOCAL_CFLAGS += -pthread
    LOCAL_SRC_FILES += lock.c

    # Network requires iiod-client
    LOCAL_SRC_FILES += iiod-client.c
endif

LOCAL_EXPORT_C_INCLUDES = iio.h

include $(BUILD_SHARED_LIBRARY)

################################################################################
## iio_genxml ##################################################################
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := iio_genxml
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CXXFLAGS += -Weffc++
LOCAL_SRC_FILES := tests/iio_genxml.c
LOCAL_SHARED_LIBRARIES := libiio

include $(BUILD_EXECUTABLE)

################################################################################
## iio_info ####################################################################
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := iio_info
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CXXFLAGS += -Weffc++
LOCAL_SRC_FILES := tests/iio_info.c
LOCAL_SHARED_LIBRARIES := libiio

include $(BUILD_EXECUTABLE)

################################################################################
## iio_readdev #################################################################
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := iio_readdev
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CXXFLAGS += -Weffc++
LOCAL_SRC_FILES := tests/iio_readdev.c
LOCAL_SHARED_LIBRARIES := libiio

include $(BUILD_EXECUTABLE)

################################################################################
## iio_reg #####################################################################
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := iio_reg
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CXXFLAGS += -Weffc++
LOCAL_SRC_FILES := tests/iio_reg.c
LOCAL_SHARED_LIBRARIES := libiio

include $(BUILD_EXECUTABLE)

