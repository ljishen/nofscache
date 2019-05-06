MOD := no_fscache

KERNEL_PATH ?= /lib/modules/$(shell uname -r)/build

obj-m += $(MOD).o

all:
	make -C $(KERNEL_PATH) M=$(CURDIR) modules

clean:
	make -C $(KERNEL_PATH) M=$(CURDIR) clean

insmod: $(MOD).ko
	sudo insmod $(MOD).ko

rmmod:
	sudo rmmod $(MOD)
