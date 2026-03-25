# SPDX-License-Identifier: GPL-2.0

GUNYAH_ROOT=$(M)
KBUILD_OPTIONS += GUNYAH_ROOT=$(GUNYAH_ROOT)

obj-y += arch/arm64/gunyah/
obj-y += drivers/virt/gunyah/
obj-y += drivers/tty/hvc/

all: clean modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean
