# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
# Copyright (c) 2019, Jianshen Liu

MOD := no_fscache_lp

KERNEL_PATH ?= /lib/modules/$(shell uname -r)/build

obj-m += $(MOD).o

.PHONY: all
all:
	make -C $(KERNEL_PATH) M=$(CURDIR) modules

.PHONY: debug
debug:
	make -C $(KERNEL_PATH) M=$(CURDIR) ccflags-y="-O0 -g" modules

.PHONY: clean
clean:
	make -C $(KERNEL_PATH) M=$(CURDIR) clean

.PHONY: insmod
insmod: $(MOD).ko
	sudo insmod $(MOD).ko

.PHONY: rmmod
rmmod: lp_sysfs_if = /sys/kernel/livepatch/$(MOD)
ifeq (,$(wildcard $(lp_sysfs_if)))
rmmod:
	@echo "Operation skipped due to kernel module is not loaded."
else
rmmod:
	while : ; do \
		transitioning="$$(cat /sys/kernel/livepatch/$(MOD)/transition)"; \
		[ "$$transitioning" == "0" ] && break; \
	done
	sudo rmmod $(MOD)
endif
