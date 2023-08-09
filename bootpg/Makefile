# boot program build makefile
# Thorsten Brehm, 2023

BOOTLOADER     = $(wildcard *.asm)
BOOTLOADER_H   = $(addprefix bin/,$(BOOTLOADER:.asm=.h))
BOOTLOADER_BIN = $(addprefix bin/,$(BOOTLOADER:.asm=.bin))
BL_ARDUINO_H   = $(addprefix ../Apple2Arduino/, $(BOOTLOADER:.asm=.h))

# include file with firmware version
include ../version.mk

FW_MAJOR := $(word 1,$(subst ., ,$(FW_VERSION)))
FW_MINOR := $(word 2,$(subst ., ,$(FW_VERSION)))
FW_MAINT := $(word 3,$(subst ., ,$(FW_VERSION)))

VERSION_INFO := -DFW_MAJOR=$(FW_MAJOR) -DFW_MINOR=$(FW_MINOR) -DFW_MAINT=$(FW_MAINT)

all: check $(BOOTLOADER_H)

bin/%.h: bin/%.bin tools/file2header.py
	@echo "Generating Arduino header file $(notdir $<)..."
	@python3 tools/file2header.py --arduino bootblocks --binary --file $< > $@
	@cp $@ ../Apple2Arduino/$(notdir $@)
	@echo ""

bin/%.bin: bin/%.o
	@echo "Linking $@..."
	@ld65 -t none $< -o $@

bin/%.o: %.asm Makefile | bin
	@echo "Assembling $<..."
	@ca65 $< -o $@ $(VERSION_INFO) --listing bin/$(<:.asm=.lst) --list-bytes 255

.PRECIOUS: $(BOOTLOADER_BIN)

bin:
	@echo "Making build directory..."
	@mkdir bin

clean:
	@echo "Wiping generated files..."
	@rm -f bin/* $(BL_ARDUINO_H)

check:
	@(python3 -V > /dev/null 2> /dev/null) || (echo "ERROR: Python3 installation is required to convert binaries!"; exit 1)


disk: bin/bootpg_flex_v6.bin
	cp _Master.dsk bin/testdisk.dsk
	cat bin/bootpg_flex_v6.bin | java -jar tools/AppleCommander-1.3.5.13-ac.jar -p bin/testdisk.dsk BOOTPG bin

