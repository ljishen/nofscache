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

# Read more about livepatch consistency model:
#	https://www.kernel.org/doc/Documentation/livepatch/livepatch.txt
.PHONY: check_state
check_state:
	@while : ; do							\
		transitioning="$$(cat $(mod_sysfs_if)/transition)";	\
		if [ "$$transitioning" = "1" ]; then			\
			echo "[INFO] Checking transition state...";	\
			sleep 2;					\
		else							\
			break;						\
		fi							\
	done

.PHONY: done
done:
	@echo "[INFO] Done!"

.PHONY: insmod
insmod: $(MOD).ko
	sudo insmod $(MOD).ko

.PHONY: install
install: insmod check_state done

.PHONY: uninstall
ifeq (,$(wildcard $(mod_sysfs_if)/enabled))
uninstall:
	@echo "[INFO] Operation skipped due to kernel module is not loaded."
else
.PHONY: disable
disable:
	-@echo 0 | sudo tee $(mod_sysfs_if)/enabled > /dev/null 2>&1

.PHONY: rmmod
rmmod: disable check_state
	sudo rmmod $(MOD)

uninstall: rmmod done
endif
