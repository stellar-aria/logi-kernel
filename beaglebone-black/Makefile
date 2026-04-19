KERNELDIR ?= /lib/modules/$(shell uname -r)/build
OUT_DIR ?= $(CURDIR)/out
R1_DIR := $(CURDIR)/logibone_r1
R1_DTS := $(R1_DIR)/BB-BONE-LOGIBONE-00R1.dts
R1_DTBO := $(OUT_DIR)/BB-BONE-LOGIBONE-00R1.dtbo

.PHONY: all modules dtbos package clean

all: package

modules:
	$(MAKE) -C $(R1_DIR) KERNELDIR=$(KERNELDIR) CC=$(CC) LLVM=$(LLVM)

dtbos: $(R1_DTBO)

$(R1_DTBO): $(R1_DTS)
	mkdir -p $(OUT_DIR)
	dtc -O dtb -o $@ -b 0 -@ $<

package: modules dtbos
	mkdir -p $(OUT_DIR)
	cp -f $(R1_DIR)/logibone_r1_dm.ko $(OUT_DIR)/
	cp -f $(R1_DIR)/logibone_r1_dma.ko $(OUT_DIR)/
	cp -f $(R1_DTS) $(OUT_DIR)/

clean:
	$(MAKE) -C $(R1_DIR) clean
	rm -rf $(OUT_DIR)
