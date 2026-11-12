TARGET := iphone:clang:latest:14.0
ARCHS := arm64
INSTALL_TARGET_PROCESSES = FreeFire

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = FFCheat
FFCheat_FILES = Tweak.xm
FFCheat_CFLAGS = -fobjc-arc -std=c++17 -Wall -Wno-unused-variable -Wno-unused-function
FFCheat_FRAMEWORKS = Foundation UIKit QuartzCore
FFCheat_LIBRARIES = substrate

include $(THEOS_MAKE_PATH)/tweak.mk
