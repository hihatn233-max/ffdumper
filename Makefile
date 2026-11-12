TARGET := iphone:clang:latest:14.0
ARCHS := arm64
INSTALL_TARGET_PROCESSES = FreeFire

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = FFOffsetDumper

FFOffsetDumper_FILES = main.mm FFOffsetDumper.cpp
FFOffsetDumper_CFLAGS = -fobjc-arc -std=c++17 -Wall -Wextra -Wno-unused-parameter -I$(THEOS)/include
FFOffsetDumper_FRAMEWORKS = Foundation
FFOffsetDumper_LIBRARIES = substrate

include $(THEOS_MAKE_PATH)/tweak.mk
