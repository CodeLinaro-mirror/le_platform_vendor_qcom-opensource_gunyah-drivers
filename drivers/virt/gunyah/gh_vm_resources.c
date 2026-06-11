/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/gunyah/gh_rm_drv_oot.h>
#include "gh_vm_resources.h"

/* Maximum number of destination VMIDs */
#define GH_VM_RES_MAX_DST_VMIDS	8
#define GH_VM_IOMEM_MAX_REGIONS	32

static int gh_vm_iomem_setup(struct device *dev,
			     struct device_node *np)
{
	u8 acl_buf[offsetof(struct gh_acl_desc,
		   acl_entries[GH_VM_RES_MAX_DST_VMIDS + 1])] = {0};
	u8 sgl_buf[offsetof(struct gh_sgl_desc,
		   sgl_entries[GH_VM_IOMEM_MAX_REGIONS])] = {0};

	struct gh_acl_desc *acl = (struct gh_acl_desc *)acl_buf;
	struct gh_sgl_desc *sgl = (struct gh_sgl_desc *)sgl_buf;

	struct resource res;
	u32 label;
	u32 dst_vmids[GH_VM_RES_MAX_DST_VMIDS];
	u32 dst_perms[GH_VM_RES_MAX_DST_VMIDS];
	int ndst, nregs;
	int i, ret;
	gh_memparcel_handle_t handle;

	if (!of_device_is_available(np))
		return 0;

	ret = of_property_read_u32(np, "gunyah-label", &label);
	if (ret) {
		dev_err(dev, "IOMEM group missing gunyah-label\n");
		return ret;
	}

	ndst = of_property_count_elems_of_size(np,
					"qcom,dst-vmids",
					sizeof(u32));
	if (ndst <= 0 || ndst > GH_VM_RES_MAX_DST_VMIDS) {
		dev_err(dev, "invalid qcom,dst-vmids count %d\n", ndst);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np,
				"qcom,dst-vmids",
				dst_vmids, ndst);
	if (ret) {
		dev_err(dev, "Unable to parse qcom,dst-vmids for IOMEM group\n");
		return ret;
	}

	ret = of_property_read_u32_array(np,
				"qcom,dst-perms",
				dst_perms, ndst);
	if (ret) {
		dev_err(dev, "Unable to parse qcom,dst-perms for IOMEM group\n");
		return ret;
	}

	nregs = of_address_count(np);
	if (nregs <= 0 || nregs > GH_VM_IOMEM_MAX_REGIONS) {
		dev_err(dev, "iomem group reg entries out of range\n");
		return -EINVAL;
	}

	acl->n_acl_entries = ndst + 1;

	for (i = 0; i < ndst; i++) {
		acl->acl_entries[i].vmid  = dst_vmids[i] & 0xFFFF;
		acl->acl_entries[i].perms = dst_perms[i] & 0xFF;
	}

	acl->acl_entries[ndst].vmid  = QCOM_SCM_VMID_HLOS;
	acl->acl_entries[ndst].perms = QCOM_SCM_PERM_RW;

	sgl->n_sgl_entries = nregs;

	for (i = 0; i < nregs; i++) {
		ret = of_address_to_resource(np, i, &res);
		if (ret) {
			dev_err(dev, "failed to parse region [%d]\n", i);
			goto out;
		}

		sgl->sgl_entries[i].ipa_base = res.start;
		sgl->sgl_entries[i].size     = resource_size(&res);
	}

	ret = gh_rm_mem_share(GH_RM_MEM_TYPE_IO,
			      0,
			      label,
			      acl,
			      sgl,
			      NULL,
			      &handle);
	if (ret) {
		dev_err(dev,
			"gh_vm_iomem_setup failed for label: 0x%x ret=%d\n",
			label, ret);
		goto out;
	}

	dev_info(dev,
		 "gh_vm_iomem_setup successful for label: 0x%x ranges:%d dst-vms:%d handle:0x%x\n",
		 label, nregs, ndst, handle);

out:
	return ret;
}

static int gh_vm_iomem_group_setup(struct platform_device *pdev,
				   int iomem_ngroups,
				   struct device_node *np)
{
	struct device_node *grp;
	int i, ret;
	struct device *dev = &pdev->dev;

	for (i = 0; i < iomem_ngroups; i++) {
		grp = of_parse_phandle(np,
				       "qcom,vm-iomem-groups",
				       i);
		if (!grp) {
			dev_err(dev, "Unable to parse qcom,vm-iomem-groups\n");
			return -EINVAL;
		}

		ret = gh_vm_iomem_setup(dev, grp);
		of_node_put(grp);

		if (ret)
			return ret;
	}

	return 0;
}

static int gh_vm_irq_group_setup(struct platform_device * __maybe_unused pdev,
				 int __maybe_unused irq_ngroups,
				 struct device_node * __maybe_unused np)
{
	/* TODO: Implement logic for IRQ sharing */
	return 0;
}

static int gh_vm_sid_group_setup(struct platform_device * __maybe_unused pdev,
				 int __maybe_unused sid_ngroups,
				 struct device_node * __maybe_unused np)
{
	/* TODO: Implement logic for SID sharing */
	return 0;
}

static int gh_vm_resources_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int vm_iomem_ngroups, vm_irq_ngroups, vm_sid_ngroups;
	int ret = 0;

	vm_iomem_ngroups = of_count_phandle_with_args(np,
						      "qcom,vm-iomem-groups",
						      NULL);
	vm_irq_ngroups = of_count_phandle_with_args(np,
						    "qcom,vm-irq-groups",
						    NULL);
	vm_sid_ngroups = of_count_phandle_with_args(np,
						    "qcom,vm-sid-groups",
						    NULL);

	if (vm_iomem_ngroups > 0) {
		ret = gh_vm_iomem_group_setup(pdev, vm_iomem_ngroups, np);
		if (ret) {
			dev_err(dev, "gh_vm_iomem_setup failed with error: %d\n", ret);
			return ret;
		}
	}

	if (vm_irq_ngroups > 0) {
		ret = gh_vm_irq_group_setup(pdev, vm_irq_ngroups, np);
		if (ret) {
			dev_err(dev, "gh_vm_irq_setup failed with error: %d\n", ret);
			return ret;
		}
	}

	if (vm_sid_ngroups > 0) {
		ret = gh_vm_sid_group_setup(pdev, vm_sid_ngroups, np);
		if (ret) {
			dev_err(dev, "gh_vm_sid_setup failed with error: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id gh_vm_resources_of_match[] = {
	{ .compatible = "qcom,gh-vm-resources" },
	{}
};

static struct platform_driver gh_vm_resources_driver = {
	.probe = gh_vm_resources_probe,
	.driver = {
		.name = "gh-vm-resources",
		.of_match_table = gh_vm_resources_of_match,
	},
};

int gh_vm_resources_init(void)
{
	return platform_driver_register(&gh_vm_resources_driver);
}

void gh_vm_resources_exit(void)
{
	platform_driver_unregister(&gh_vm_resources_driver);
}
