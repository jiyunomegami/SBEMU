TARGET := output/sbemu.exe
CC := i586-pc-msdosdjgpp-gcc
CXX := i586-pc-msdosdjgpp-g++
DEBUG ?= 0
YSBEMU_CONFIG_UTIL ?= 0
USE_LINUX_PCI ?= 1

VERSION ?= $(shell git describe --tags)

INCLUDES := -I./mpxplay -I./sbemu -I./drivers/include
DEFINES := -D__DOS__ -DSBEMU -DDEBUG=$(DEBUG) -DYSBEMU_CONFIG_UTIL=$(YSBEMU_CONFIG_UTIL) -DUSE_LINUX_PCI=$(USE_LINUX_PCI) -DMAIN_SBEMU_VER=\"$(VERSION)\"
CFLAGS := -fcommon -march=i386 -Os $(INCLUDES) $(DEFINES)
LDFLAGS := -lstdc++ -lm

ifeq ($(DEBUG),0)
LDFLAGS += -s
CFLAGS += -DNDEBUG
endif

ifeq ($(V),1)
SILENTCMD :=
SILENTMSG := @true
else
SILENTCMD := @
SILENTMSG := @printf
endif

VPATH += .
VPATH += sbemu
VPATH += sbemu/dpmi

all: $(TARGET)

CARDS_SRC := mpxplay/au_cards/ac97_def.c \
	     mpxplay/au_cards/au_base.c \
	     mpxplay/au_cards/au_cards.c \
	     mpxplay/au_cards/dmairq.c \
	     mpxplay/au_cards/pcibios.c \
	     mpxplay/au_cards/sc_e1371.c \
	     mpxplay/au_cards/sc_ich.c \
	     mpxplay/au_cards/sc_cmi.c \
	     mpxplay/au_cards/sc_inthd.c \
	     mpxplay/au_cards/sc_sbl24.c \
	     mpxplay/au_cards/sc_sbliv.c \
	     mpxplay/au_cards/sc_via82.c \
		 mpxplay/au_cards/sc_ctxfi.c \
		 mpxplay/au_cards/sc_emu10k1x.c \
		 mpxplay/au_cards/sc_trident.c \
		 mpxplay/au_cards/sc_null.c \
		 mpxplay/au_cards/sc_ymf.c \

CTXFI_SRC := drivers/ctxfi/ctsrc.c \
             drivers/ctxfi/ctresource.c \
             drivers/ctxfi/ctmixer.c \
             drivers/ctxfi/ctimap.c \
             drivers/ctxfi/ctamixer.c \
             drivers/ctxfi/ctatc.c \
             drivers/ctxfi/cttimer.c \
             drivers/ctxfi/ctdaio.c \
             drivers/ctxfi/ctpcm.c \
             drivers/ctxfi/cthardware.c \
             drivers/ctxfi/ctvmem.c \
             drivers/ctxfi/cthw20k1.c \
             drivers/ctxfi/cthw20k2.c \

EMU10K1_SRC := drivers/emu10k1/emu10k1x.c \

TRIDENT_SRC := drivers/trident/trident_main.c \
               drivers/trident/trident_memory.c \

SBEMU_SRC := sbemu/dbopl.cpp \
	     sbemu/opl3emu.cpp \
	     sbemu/pic.c \
	     sbemu/sbemu.c \
	     sbemu/untrapio.c \
	     sbemu/vdma.c \
	     sbemu/virq.c \
	     sbemu/serial.c \
	     sbemu/dpmi/xms.c \
	     sbemu/dpmi/dpmi.c \
	     sbemu/dpmi/dbgutil.c \
	     sbemu/dpmi/dpmi_dj2.c \
	     sbemu/dpmi/dpmi_tsr.c \
	     sbemu/dpmi/djgpp/gormcb.c \
		 sbemu/dpmi/djgpp/gopint.c \
	     main.c \
	     qemm.c \
	     utility.c \
	     hdpmipt.c \

PCI_SRC = drivers/pci/kernel.c \
          drivers/pci/bitmap.c \
          drivers/pci/find_bit.c \
          drivers/pci/klist.c \
          drivers/pci/probe.c \
          drivers/pci/bus.c \
          drivers/pci/resource.c \
          drivers/pci/setup-res.c \
          drivers/pci/setup-bus.c \
          drivers/pci/pci.c \
          drivers/pci/access.c \
          drivers/pci/host-bridge.c \
          drivers/pci/search.c \
          drivers/pci/direct.c \
          drivers/pci/bus_numa.c \
          drivers/pci/common.c \
          drivers/pci/pcimain.c \

LINUX_DRIVERS_SRC := $(PCI_SRC) $(CTXFI_SRC) $(EMU10K1_SRC) $(TRIDENT_SRC)
SRC := $(LINUX_DRIVERS_SRC) $(CARDS_SRC) $(SBEMU_SRC)
OBJS := $(patsubst %.cpp,output/%.o,$(patsubst %.c,output/%.o,$(SRC)))

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(SILENTMSG) "LINK\t$@\n"
	$(SILENTCMD)$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

output/drivers/pci/%.o: drivers/pci/%.c
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) $(CFLAGS) -DUSE_LINUX_PCIBIOS=1 -c $< -o $@

output/%.o: %.c
	@mkdir -p $(dir $@)
	$(SILENTMSG) "CC\t$@\n"
	$(SILENTCMD)$(CC) $(CFLAGS) -c $< -o $@

output/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(SILENTCMD)$(SILENTMSG) "CXX\t$@\n"
	$(SILENTCMD)$(CXX) $(CFLAGS) -c $< -o $@

clean:
	$(SILENTMSG) "CLEAN\n"
	$(SILENTCMD)$(RM) $(OBJS)

distclean: clean
	$(SILENTMSG) "DISTCLEAN\n"
	$(SILENTCMD)$(RM) $(TARGET)
