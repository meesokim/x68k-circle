#
# Makefile - PX68K Baremetal for Raspberry Pi with Circle
#

CIRCLEHOME ?= $(HOME)/circle

# Command-line RASPPI wins; otherwise follow CIRCLEHOME Config.mk; else Pi 1.
ifeq ($(origin RASPPI),command line)
else
  RASPPI := $(shell awk '/^RASPPI/{print $$3; exit}' $(CIRCLEHOME)/Config.mk 2>/dev/null)
  ifeq ($(strip $(RASPPI)),)
    RASPPI := 1
  endif
endif

BUILDDIR := build/rpi$(RASPPI)
CIRCLELIBDIR := $(CURDIR)/build/circle/rpi$(RASPPI)

ifeq ($(RASPPI),1)
KERNEL_NAME := kernel
else ifeq ($(RASPPI),2)
KERNEL_NAME := kernel7
else ifeq ($(RASPPI),3)
KERNEL_NAME := kernel8-32
else ifeq ($(RASPPI),4)
KERNEL_NAME := kernel7l
else
$(error RASPPI must be 1, 2, 3 or 4)
endif

TARGET := $(BUILDDIR)/$(KERNEL_NAME)

USE_VCHIQ_SOUND = 1

CFLAGS += -DRASPPI=$(RASPPI) -DKERNEL_MAX_SIZE=$(KERNEL_MAX_SIZE) \
          -DUSE_VCHIQ_SOUND=$(USE_VCHIQ_SOUND) -DLSB_FIRST -D__LIBRETRO__ \
          -DHAVE_C68K -DC68K_NO_JUMP_TABLE -D__LINUX__ -DARM -marm \
          -DPRId64='"lld"' -DPRIu64='"llu"' -DPRIuPTR='"u"' \
          -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-conversion -Wno-implicit-int \
          -I$(CIRCLEHOME)/addon -I$(CIRCLEHOME)/include -I$(CIRCLEHOME)/addon/vc4 \
          -I$(CURDIR)/Circle -I$(CURDIR)/px68k-libretro \
          -I$(CURDIR)/px68k-libretro/libretro-common/include -I$(CURDIR)/px68k-libretro/libretro \
          -I$(CURDIR)/px68k-libretro/x68k -I$(CURDIR)/px68k-libretro/fmgen -I$(CURDIR)/px68k-libretro/m68000

CPPFLAGS += -DRASPPI=$(RASPPI) -DKERNEL_MAX_SIZE=$(KERNEL_MAX_SIZE) \
            -DUSE_VCHIQ_SOUND=$(USE_VCHIQ_SOUND) -DLSB_FIRST -D__LIBRETRO__ \
            -DHAVE_C68K -DC68K_NO_JUMP_TABLE -D__LINUX__ -DARM -marm \
            -DPRId64='"lld"' -DPRIu64='"llu"' -DPRIuPTR='"u"' \
            -fpermissive -Wno-narrowing -Wno-unused-variable -Wno-unused-function \
            -I$(CIRCLEHOME)/addon -I$(CIRCLEHOME)/include -I$(CIRCLEHOME)/addon/vc4 \
            -I$(CURDIR)/Circle -I$(CURDIR)/px68k-libretro \
            -I$(CURDIR)/px68k-libretro/libretro-common/include -I$(CURDIR)/px68k-libretro/libretro \
            -I$(CURDIR)/px68k-libretro/x68k -I$(CURDIR)/px68k-libretro/fmgen -I$(CURDIR)/px68k-libretro/m68000

KERNEL_MAX_SIZE = 0x2000000
DEFINE += -DKERNEL_MAX_SIZE=$(KERNEL_MAX_SIZE)

LDFLAGS += --allow-multiple-definition

C_SRCS := $(wildcard px68k-libretro/m68000/m68000.c) \
          $(wildcard px68k-libretro/m68000/c68k/*.c) \
          $(wildcard px68k-libretro/x68k/*.c) \
          $(wildcard px68k-libretro/libretro/*.c) \
          px68k-libretro/libretro.c \
          Circle/webx68k_baremetal.c

CPP_SRCS := $(wildcard px68k-libretro/fmgen/*.cpp) \
            $(wildcard Circle/*.cpp)

OBJS := $(addprefix $(BUILDDIR)/,$(C_SRCS:.c=.o) $(CPP_SRCS:.cpp=.o))

LIBS = $(CIRCLELIBDIR)/addon/vc4/sound/libvchiqsound.a \
       $(CIRCLELIBDIR)/addon/vc4/vchiq/libvchiq.a \
       $(CIRCLELIBDIR)/addon/linux/liblinuxemu.a \
       $(CIRCLELIBDIR)/lib/sched/libsched.a \
       $(CIRCLELIBDIR)/lib/sound/libsound.a \
       $(CIRCLELIBDIR)/lib/usb/libusb.a \
       $(CIRCLELIBDIR)/lib/input/libinput.a \
       $(CIRCLELIBDIR)/lib/fs/libfs.a \
       $(CIRCLELIBDIR)/addon/SDCard/libsdcard.a \
       $(CIRCLELIBDIR)/addon/fatfs/libfatfs.a \
       $(CIRCLELIBDIR)/lib/libcircle.a

.DEFAULT_GOAL := all

include $(CIRCLEHOME)/Rules.mk

CIRCLE_LIB_RASPPI := $(shell awk '/^RASPPI/{print $$3; exit}' $(CIRCLELIBDIR)/Config.mk 2>/dev/null)

ifeq ($(filter clean clean-all circle,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(CIRCLELIBDIR)/lib/libcircle.a),)
$(error Circle libs missing in $(CIRCLELIBDIR). Run: make circle RASPPI=$(RASPPI))
endif
endif

LIBC := $(shell $(PREFIX)gcc $(ARCHCPU) -print-file-name=libc.a)
LIBM := $(shell $(PREFIX)gcc $(ARCHCPU) -print-file-name=libm.a)
EXTRALIBS += $(LIBM) $(LIBC)

.SECONDEXPANSION:

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) $(C_STANDARD) -c -o $@ $<

$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) -c -o $@ $<

.PHONY: all
all: $(TARGET).img
	@mkdir -p dist_baremetal
ifeq ($(RASPPI),1)
	@cp -f $(TARGET).img dist_baremetal/app.bin
	@cp -f $(TARGET).img dist_baremetal/kernel.img
	@echo "  INST  dist_baremetal/app.bin (<= $(TARGET).img)"
else ifeq ($(RASPPI),2)
	@cp -f $(TARGET).img dist_baremetal/app7.bin
	@cp -f $(TARGET).img dist_baremetal/kernel7.img
	@echo "  INST  dist_baremetal/app7.bin (<= $(TARGET).img)"
else ifeq ($(RASPPI),3)
	@cp -f $(TARGET).img dist_baremetal/app8-32.bin
	@cp -f $(TARGET).img dist_baremetal/kernel8-32.img
	@echo "  INST  dist_baremetal/app8-32.bin (<= $(TARGET).img)"
else ifeq ($(RASPPI),4)
	@cp -f $(TARGET).img dist_baremetal/app7l.bin
	@cp -f $(TARGET).img dist_baremetal/kernel7l.img
	@echo "  INST  dist_baremetal/app7l.bin (<= $(TARGET).img)"
endif

.PHONY: clean
clean:
	@echo "  CLEAN $(BUILDDIR)"
	@rm -rf $(BUILDDIR)
	@rm -f $(KERNEL_NAME).img $(KERNEL_NAME).elf $(KERNEL_NAME).lst $(KERNEL_NAME).map

.PHONY: clean-all
clean-all:
	@echo "  CLEAN build/"
	@rm -rf build/rpi*
	@rm -f kernel*.img kernel*.elf kernel*.lst kernel*.map

.PHONY: circle
circle:
	cd $(CIRCLEHOME) && ./configure -r $(RASPPI) --prefix arm-none-eabi- --kernel-max-size 32 -f
	cd $(CIRCLEHOME) && ./makeall
	@for d in lib/sched lib/sound lib/usb lib/input lib/fs \
		addon/SDCard addon/fatfs addon/linux addon/vc4/vchiq addon/vc4/sound; do \
		$(MAKE) -C $(CIRCLEHOME)/$$d; \
	done
	@mkdir -p $(CIRCLELIBDIR)
	@for p in Config.mk \
		lib/libcircle.a \
		lib/sched/libsched.a \
		lib/sound/libsound.a \
		lib/usb/libusb.a \
		lib/input/libinput.a \
		lib/fs/libfs.a \
		addon/SDCard/libsdcard.a \
		addon/fatfs/libfatfs.a \
		addon/linux/liblinuxemu.a \
		addon/vc4/vchiq/libvchiq.a \
		addon/vc4/sound/libvchiqsound.a; do \
		mkdir -p $(CIRCLELIBDIR)/$$(dirname $$p); \
		cp -a $(CIRCLEHOME)/$$p $(CIRCLELIBDIR)/$$p; \
	done
	@echo "  INST  Circle libs -> $(CIRCLELIBDIR)"
