/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _GUNYAH_INFO_H
#define _GUNYAH_INFO_H

#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>

enum gunyah_info_owner {
  /* clang-format off */
	GUNYAH_INFO_OWNER_INVALID	= 0,
	GUNYAH_INFO_OWNER_HYP		= 1,
	GUNYAH_INFO_OWNER_ROOTVM	= 2,
	GUNYAH_INFO_OWNER_RM		= 3,
	GUNYAH_INFO_OWNER_QCRM		= 16,
	/* clang-format on */
};

void *gunyah_get_info(u16 owner, u16 id, size_t *size);

int gh_fill_irq_fwspec_params(u32 virq, struct irq_fwspec *fwspec);

enum gh_error gh_hypercall_addrspace_find_info_area(unsigned long *ipa,
	unsigned long *size);
#endif /* _GUNYAH_INFO_H */
