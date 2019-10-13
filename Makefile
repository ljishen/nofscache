# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
# Copyright (c) 2019, Jianshen Liu <jliu120@ucsc.edu>

MOD := no_fscache
obj-m += $(MOD).o

KERNEL_PATH ?= /lib/modules/$(shell uname -r)/build
MOD_SYSFS_IF := /sys/kernel/livepatch/$(MOD)
MKFILE_DIR := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))

OLDEST_SUPPORTED_KERNEL := 4.12
KERNEL_RELEASE := $(shell uname -r)

$(MOD).ko: check_kernel
	make -C $(KERNEL_PATH) M=$(CURDIR) modules

# Code reference for the second check:
#	https://elixir.bootlin.com/linux/v5.3.6/source/mm/fadvise.c#L191
#
# We may need to take care of the other case that the kernel config file could
# present as /proc/config.gz if the kernel was compiled with CONFIG_IKCONFIG.
.PHONY: check_kernel
check_kernel:
	@if [  "$(KERNEL_RELEASE)" = "$$(printf "$(OLDEST_SUPPORTED_KERNEL)\n$(KERNEL_RELEASE)" | sort -V | head -n1)" ]; then	\
	    printf "[INFO] This module can only be installed on kernel version >= $(OLDEST_SUPPORTED_KERNEL)\n\n";		\
	    exit 1;														\
	fi

	@if ! cat /boot/config-$(KERNEL_RELEASE) | grep CONFIG_ADVISE_SYSCALLS=y >/dev/null 2>&1; then				\
		printf "[INFO] Kernel config CONFIG_ADVISE_SYSCALLS is disabled.\n\n";						\
		exit 1;														\
	fi

.PHONY: clean
clean:
	make -C $(KERNEL_PATH) M=$(CURDIR) clean

.PHONY: insmod
insmod: $(MOD).ko
	@if [ -f "$(MOD_SYSFS_IF)/enabled" ]; then					\
		echo "[INFO] Module $(MOD) is already inserted into the Linux Kernel.";	\
	else										\
		sudo insmod $(MOD).ko;							\
	fi

.PHONY: check_state
check_state:
	@# Sleep 2 seconds beforehand to allow nomral transition state finished.
	@sleep 2

	@sudo $(MKFILE_DIR)/ck_state.sh $(MOD) $(check_state)

.PHONY: install
install: check_state = 0
install: insmod check_state
	@echo "[INFO] Module $(MOD) is successfully installed."

.PHONY: debug_install
debug_install: export ccflags-y := -O0 -g
debug_install: install

.PHONY: uninstall
ifeq (,$(wildcard $(MOD_SYSFS_IF)/enabled))
uninstall:
	@printf "[INFO] Operation skipped due to kernel module $(MOD) is not loaded.\n\n"
else
.PHONY: enable
enable:
	@if [ "$$(cat $(MOD_SYSFS_IF)/enabled)" -eq 1 ]; then			\
		printf "[INFO] Module $(MOD) is already enabled.\n";		\
	else									\
		echo 1 | sudo tee $(MOD_SYSFS_IF)/enabled >/dev/null;		\
		printf "[INFO] Module $(MOD) is now enabled.\n";		\
	fi

.PHONY: disable
disable:
	@if [ "$$(cat $(MOD_SYSFS_IF)/enabled)" -eq 0 ]; then			\
		printf "[INFO] Module $(MOD) is already disabled.\n";		\
	else									\
		echo 0 | sudo tee $(MOD_SYSFS_IF)/enabled >/dev/null;		\
		printf "[INFO] Module $(MOD) is now disabled.\n";		\
	fi

.PHONY: rmmod
rmmod: check_state = 1
rmmod: disable check_state
	sudo rmmod $(MOD)

uninstall: rmmod
	@echo "[INFO] Module $(MOD) is successfully uninstalled."
endif
