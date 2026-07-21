// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) "gunyah: " fmt

#include <linux/arm-smccc.h>
#include <linux/gunyah.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/gunyah/gunyah_info.h>

#define GH_HYPERCALL_ADDRSPACE_FIND_INFO_AREA \
ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64, \
					ARM_SMCCC_OWNER_VENDOR_HYP, \
					(0x806a))

struct gunyah_info_desc {
	__le16 id;
	__le16 owner;
	__le32 size;
	__le32 offset;
#define INFO_DESC_VALID		BIT(31)
	__le32 flags;
};

static void *info_area;

/**
 * gh_hypercall_addrspace_find_info_area() - Find the IPA and size of the info area
 * @ipa: Filled with the IPA of the info area
 * @size: Filled with the size of the info area
 *
 * See also:
 * https://github.com/quic/gunyah-hypervisor/blob/develop/docs/api/gunyah_api.md#address-space-management
 */
enum gh_error gh_hypercall_addrspace_find_info_area(unsigned long *ipa, unsigned long *size)
{
	struct arm_smccc_res res = { 0 };

	arm_smccc_1_1_hvc(GH_HYPERCALL_ADDRSPACE_FIND_INFO_AREA, 0, &res);
	if (res.a0 == GH_ERROR_OK) {
		*ipa = res.a1;
		*size = res.a2;
	}

	return res.a0;
}
EXPORT_SYMBOL_GPL(gh_hypercall_addrspace_find_info_area);

int gh_fill_irq_fwspec_params(u32 virq, struct irq_fwspec *fwspec)
{
	/* Assume that Gunyah gave us an SPI or ESPI; defensively check it */
	if (WARN(virq < 32, "Unexpected virq: %d\n", virq)) {
		return -EINVAL;
	} else if (virq <= 1019) {
		fwspec->param_count = 3;
		fwspec->param[0] = 0; /* GIC_SPI */
		fwspec->param[1] = virq - 32; /* virq 32 -> SPI 0 */
		fwspec->param[2] = IRQ_TYPE_EDGE_RISING;
	} else if (WARN(virq < 4096, "Unexpected virq: %d\n", virq)) {
		return -EINVAL;
	} else if (virq < 5120) {
		fwspec->param_count = 3;
		fwspec->param[0] = 2; /* GIC_ESPI */
		fwspec->param[1] = virq - 4096; /* virq 4096 -> ESPI 0 */
		fwspec->param[2] = IRQ_TYPE_EDGE_RISING;
	} else {
		WARN(1, "Unexpected virq: %d\n", virq);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(gh_fill_irq_fwspec_params);

void *gunyah_get_info(u16 owner, u16 id, size_t *size)
{
	struct gunyah_info_desc *desc = info_area;
	__le16 le_owner = cpu_to_le16(owner);
	__le16 le_id = cpu_to_le16(id);

	if (!desc)
		return ERR_PTR(-ENOENT);

	for (desc = info_area; le32_to_cpu(desc->offset); desc++) {
		if (!(le32_to_cpu(desc->flags) & INFO_DESC_VALID))
			continue;

    /* make sure updated memory information is fetched */
		mb();

		if (le_owner == desc->owner && le_id == desc->id) {
			if (size)
				*size = le32_to_cpu(desc->size);
			return info_area + le32_to_cpu(desc->offset);
		}
	}
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL_GPL(gunyah_get_info);

static int __init gunyah_info_init(void)
{
	unsigned long info_ipa, info_size;
	enum gh_error gh_err;

	gh_err = gh_hypercall_addrspace_find_info_area(&info_ipa, &info_size);

	/* ignore errors for compatibility with gh without info_area support */
	if (gh_err != GH_ERROR_OK)
		return 0;

	info_area = memremap(info_ipa, info_size, MEMREMAP_WB);
	if (!info_area) {
		pr_err("Failed to map addrspace info area\n");
		return -ENOMEM;
	}

	return 0;
}
core_initcall(gunyah_info_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah Information Driver");
