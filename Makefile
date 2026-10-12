#---------------------------------------------------------------------------------
# RetroAchievements 3DS - Makefile
# Based on devkitARM / libctru template
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Project Configuration
#---------------------------------------------------------------------------------
TARGET          := RetroAchievements
BUILD           := build
SOURCES         := source
DATA            := data
INCLUDES        := include
GRAPHICS        := gfx
ROMFS           := romfs

APP_TITLE       := RetroAchievements 3DS
APP_DESCRIPTION := RetroAchievements client for Nintendo 3DS
APP_AUTHOR      := deathbygin
APP_VERSION     := 0.1.0

#---------------------------------------------------------------------------------
# Compilation Architecture & Flags
#---------------------------------------------------------------------------------
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations \
           -ffunction-sections -fdata-sections \
           $(ARCH)

CFLAGS  += $(INCLUDE) -D__3DS__ -DCURL_STATICLIB

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections

# Citra/3DS Libraries: order matters! citro2d -> citro3d -> png -> curl -> mbedtls -> z -> jansson -> ctru -> m
LIBS    := -lcitro2d -lcitro3d -lpng -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -ljansson -lctru -lm

# List of library roots (containing include/ and lib/)
LIBDIRS := $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# Build System Logic
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
    export LD   := $(CC)
else
    export LD   := $(CXX)
endif

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES   := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
    icons := $(wildcard *.png)
    ifneq (,$(findstring $(TARGET).png,$(icons)))
        export APP_ICON := $(subst \,/,$(TOPDIR)/$(TARGET).png)
    else
        ifneq (,$(findstring icon.png,$(icons)))
            export APP_ICON := $(subst \,/,$(TOPDIR)/icon.png)
        endif
    endif
else
    export APP_ICON := $(subst \,/,$(TOPDIR)/$(ICON))
endif

ifeq ($(strip $(NO_SMDH)),)
    export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
    export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

#---------------------------------------------------------------------------------
# CIA & Banner Toolchain Detection
#---------------------------------------------------------------------------------
ifeq ($(shell which makerom 2>/dev/null),)
    MAKEROM ?= $(DEVKITPRO)/tools/bin/makerom
else
    MAKEROM ?= makerom
endif

ifeq ($(shell which bannertool 2>/dev/null),)
    BANNERTOOL ?= $(DEVKITPRO)/tools/bin/bannertool
else
    BANNERTOOL ?= bannertool
endif

BANNER_AUDIO := $(wildcard audio.wav)
BANNER_AUDIO_FLAG := $(if $(BANNER_AUDIO),-a $(BANNER_AUDIO),)

.PHONY: all clean cia

all: $(BUILD) $(DEPSDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

cia: all banner.bnr $(TARGET).cia

banner.bnr: banner.png $(BANNER_AUDIO)
	@echo "Creating banner $@..."
	@$(BANNERTOOL) makebanner -i $< $(BANNER_AUDIO_FLAG) -o $@

$(TARGET).cia: $(TARGET).elf $(TARGET).smdh banner.bnr template.rsf
	@echo "Building CIA package $@..."
	@$(MAKEROM) -f cia -o $@ -DAPP_ENCRYPTED=false -rsf template.rsf -target t -exefslogo -elf $< -icon $(TARGET).smdh -banner banner.bnr
	@echo "Built CIA ... $@"

$(BUILD):
	@mkdir -p $@

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).smdh $(TARGET).cia banner.bnr $(TARGET).romfs

else

# Sub-make running inside $(BUILD)
$(OUTPUT).3dsx  : $(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf   : $(OFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
