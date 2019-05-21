# SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
# Copyright (c) 2019, Jianshen Liu

MOD := no_fscache
obj-m += $(MOD).o

KERNEL_PATH ?= /lib/modules/$(shell uname -r)/build

mod_sysfs_if = /sys/kernel/livepatch/$(MOD)

$(MOD).ko:
	make -C $(KERNEL_PATH) M=$(CURDIR) modules

.PHONY: debug
debug: export ccflags-y := -O0 -g
debug: $(MOD).ko

.PHONY: clean
clean:
	make -C $(KERNEL_PATH) M=$(CURDIR) clean

.PHONY: insmod
insmod: $(MOD).ko
	sudo insmod $(MOD).ko

.PHONY: rmmod
ifeq (,$(wildcard $(mod_sysfs_if)/enabled))
rmmod:
	@echo "Operation skipped due to kernel module is not loaded."
else
rmmod:
	-@echo 0 | sudo tee $(mod_sysfs_if)/enabled > /dev/null 2>&1
	while : ; do \
		transitioning="$$(cat $(mod_sysfs_if)/transition)"; \
		[ "$$transitioning" = "0" ] && break; \
	done
	sudo rmmod $(MOD)
endif
