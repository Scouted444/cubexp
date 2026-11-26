#---------------------------------------------------------------------------------
# CubeLinux boot loader - GameCube DOL (devkitPPC + libogc)
# Put the kernel ELF (dtbImage.gamecube) at data/kernel.bin before building.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/gamecube_rules

TARGET   := cubelinux
BUILD    := build
SHARED   := ../../source
DATA     := data

CFLAGS   = -g -O2 -Wall -Wno-format-truncation $(MACHDEP) $(INCLUDE)
LDFLAGS  = -g $(MACHDEP) -Wl,-Map,$(notdir $@).map
LIBS     := -logc -lm

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export VPATH   := $(CURDIR)/source $(CURDIR)/$(SHARED) $(CURDIR)/$(DATA)
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := main.c gfx.c icons.c font_data.c
BINFILES := kernel.bin

export LD := $(CC)
SFILES   := jump.S
export OFILES := $(addsuffix .o,$(BINFILES)) $(CFILES:.c=.o) $(SFILES:.S=.o)
export INCLUDE := -I$(CURDIR)/$(SHARED) -I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
export LIBPATHS := -L$(LIBOGC_LIB)

.DEFAULT_GOAL := $(BUILD)
.PHONY: $(BUILD) clean

$(BUILD):
	@test -f $(DATA)/kernel.bin || { echo "data/kernel.bin missing (build the kernel first)"; exit 1; }
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

else

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
