# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
# Copyright (c) 2019, Jianshen Liu

MOD := no_fscache
obj-m += $(MOD).o

KERNEL_PATH ?= /lib/modules/$(shell uname -r)/build
MOD_SYSFS_IF := /sys/kernel/livepatch/$(MOD)

OLDEST_SUPPORTED_KERNEL := 4.12
KERNEL_RELEASE := $(shell uname -r)

$(MOD).ko: check_kernel
	make -C $(KERNEL_PATH) M=$(CURDIR) modules

.PHONY: check_kernel
check_kernel:
	@if [  "$(KERNEL_RELEASE)" = "$$(printf "$(OLDEST_SUPPORTED_KERNEL)\n$(KERNEL_RELEASE)" | sort -V | head -n1)" ]; then	\
	    printf "[INFO] This module can only be installed on kernel version >= $(OLDEST_SUPPORTED_KERNEL)\n\n";		\
	    exit 1;														\
	fi

.PHONY: debug
debug: export ccflags-y := -O0 -g
debug: $(MOD).ko

.PHONY: clean
clean:
	make -C $(KERNEL_PATH) M=$(CURDIR) clean

.PHONY: insmod
insmod: $(MOD).ko
	sudo insmod $(MOD).ko

# Read more about livepatch consistency model:
#	https://www.kernel.org/doc/Documentation/livepatch/livepatch.txt
.PHONY: check_state
check_state:
	@while : ; do							\
		transitioning="$$(cat $(MOD_SYSFS_IF)/transition)";	\
		if [ "$$transitioning" = "1" ]; then			\
			echo "[INFO] Checking transition state...";	\
			sleep 2;					\
		else							\
			break;						\
		fi							\
	done

.PHONY: install
install: insmod check_state
	@echo "[INFO] successfully installed"

.PHONY: uninstall
ifeq (,$(wildcard $(MOD_SYSFS_IF)/enabled))
uninstall:
	@echo "[INFO] Operation skipped due to kernel module is not loaded."
else
.PHONY: enable
enable:
	-echo 1 | sudo tee $(MOD_SYSFS_IF)/enabled > /dev/null 2>&1

.PHONY: disable
disable:
	-echo 0 | sudo tee $(MOD_SYSFS_IF)/enabled > /dev/null 2>&1

.PHONY: rmmod
rmmod: disable check_state
	sudo rmmod $(MOD)

uninstall: rmmod
	@echo "[INFO] successfully uninstalled"
endif
