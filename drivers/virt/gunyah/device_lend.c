/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "gh_rm_drv_private.h"
#include "gh_private.h"


int rm_device_find_handle(struct gh_shdev *sh_dev) {
	int gh_ret, ret = 0;
	struct gh_device_find_handle_req_payload *req_buf;
	struct gh_device_find_handle_resp_payload *resp_buf;
	struct device_resource_descriptor *resource_desp;
	size_t resp_payload_size, req_payload_size;

	req_payload_size = sizeof(*req_buf);
	req_buf = kzalloc(req_payload_size, GFP_KERNEL);
	if (!req_buf)
		return -ENOMEM;

	resource_desp = &req_buf->descriptor;
	if (sh_dev->type == RESOURCE_TYPE_IOMEM) {
		resource_desp->type = DEVICE_RESOURCE_DESCRIPTOR_MMIO_TYPE;
		resource_desp->data[0] = sh_dev->size;
		resource_desp->data[1] = sh_dev->addr_low;
		resource_desp->data[2] = sh_dev->addr_high;
	} else if (sh_dev->type == RESOURCE_TYPE_IRQ) {
		resource_desp->type = DEVICE_RESOURCE_DESCRIPTOR_IRQ_TYPE;
		resource_desp->data[0] = sh_dev->irq;
	} else {
		pr_err("%s: Resource descriptor type is not support: %d\n",
			__func__, sh_dev->type);
		ret = -EINVAL;
		goto out;
	}


	resp_buf = gh_rm_call(GH_RM_RPC_MSG_ID_CALL_VM_DEVICE_FIND_HANDLE,
							req_buf, req_payload_size, &resp_payload_size, &gh_ret);

	if (gh_ret || IS_ERR_OR_NULL(resp_buf)) {
		ret = PTR_ERR(resp_buf);
		pr_err("%s: Find the device handle failed with err: %d\n",
			__func__, ret);
		goto out;
	}

	sh_dev->device_handle = resp_buf->device_handle;
	pr_info("%s: Find the device handle: 0x%x\n",
			__func__, sh_dev->device_handle);

out:
	kfree(req_buf);

	return ret;
}

int rm_device_lend(gh_vmid_t vmid, u32 handle) {
	int gh_ret, ret = 0;
	struct gh_device_lend_req_payload *req_buf;
	void *resp_payload;
	size_t resp_payload_size, req_payload_size;

	req_payload_size = sizeof(*req_buf);
	req_buf = kzalloc(req_payload_size, GFP_KERNEL);
	if (!req_buf)
		return -ENOMEM;

	req_buf->device_handle = handle;
	req_buf->vmid = vmid;
	req_buf->flags = GH_VM_DEVICE_LEND_UNMAP_FLAG;

	resp_payload = gh_rm_call(GH_RM_RPC_MSG_ID_CALL_VM_DEVICE_LEND, req_buf,
							req_payload_size, &resp_payload_size, &gh_ret);

	if (IS_ERR(resp_payload)) {
		pr_err("%s: Unable to lend device, RM response: %d\n", __func__,
			PTR_ERR(resp_payload));
		ret = PTR_ERR(resp_payload);
		goto out;
	}

	if (gh_ret) {
		pr_err("%s: Device_lend returned error: %d\n", __func__,
			gh_ret);
		ret = gh_ret;
		goto out;
	}

	if (resp_payload_size) {
		pr_err("%s: Invalid size received for Device lend: %u\n",
			__func__, resp_payload_size);
		ret = -EINVAL;
		goto out;
	}

out:
	kfree(req_buf);
	return ret;

}

int device_lend(struct gh_vm *vm, struct gh_shdev *sh_dev) {
	int ret = 0;

	ret = rm_device_find_handle(sh_dev);
	if (ret){
		pr_err("%s: Device_Find returned error: %d\n", __func__, ret);
		return ret;
	}

	ret = rm_device_lend(vm->vmid, sh_dev->device_handle);
	if (ret)
		pr_err("%s: Device_Lend returned error: %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(device_lend);

int device_reclaim(struct gh_shdev *sh_dev) {
	int gh_ret, ret = 0;
	struct gh_device_reclaim_req_payload *req_buf;
	void *resp_payload;
	size_t resp_payload_size, req_payload_size;
	req_payload_size = sizeof(*req_buf);
	req_buf = kzalloc(req_payload_size, GFP_KERNEL);
        if (!req_buf)
                return -ENOMEM;

	req_buf->device_handle = sh_dev->device_handle;
	req_buf->flags = GH_VM_DEVICE_LEND_UNMAP_FLAG;
	resp_payload = gh_rm_call(GH_RM_RPC_MSG_ID_CALL_VM_DEVICE_RECLAIM, req_buf,
						req_payload_size, &resp_payload_size, &gh_ret);
	if (IS_ERR(resp_payload)) {
                pr_err("%s: Unable to reclaim device, RM response: %d\n", __func__,
                        PTR_ERR(resp_payload));
                ret = PTR_ERR(resp_payload);
                goto out;
        }

        if (gh_ret) {
                pr_err("%s: Device_reclaim returned error: %d\n", __func__,
                        gh_ret);
                ret = gh_ret;
                goto out;
        }

        if (resp_payload_size) {
                pr_err("%s: Invalid size received for Device reclaim: %u\n",
                        __func__, resp_payload_size);
                ret = -EINVAL;
                goto out;
        }

out:
        kfree(req_buf);
        return ret;
}
EXPORT_SYMBOL(device_reclaim);

int gh_vm_shared_device_probe(struct gh_sec_vm_dev *sec_vm_dev)
{
	struct device *dev = sec_vm_dev->dev;
	struct device_node *node;
	struct resource res;
	struct gh_shdev *shdev;
	int irq;

	if (!of_find_property(dev->of_node, "shared-devices", NULL)) {
		sec_vm_dev->sh_dev_count = 0;
		return 0;
	}

	sec_vm_dev->sh_dev_count = of_count_phandle_with_args(dev->of_node, "shared-devices", NULL);

	if (!sec_vm_dev->sh_dev_count) {
		dev_err(dev, "No shared devices are specified\n");
		return -EINVAL;
	}

	shdev = devm_kcalloc(dev, sec_vm_dev->sh_dev_count,
					sizeof(struct gh_shdev), GFP_KERNEL);
	if (!shdev) {
		sec_vm_dev->sh_dev_count = 0;
		return -ENOMEM;
	}

	for (int i = 0; i < sec_vm_dev->sh_dev_count; i++) {
		node = of_parse_phandle(dev->of_node, "shared-devices", i);
		if (!node) {
			dev_err(dev, "DT error getting \"shared-devices\"\n");
			sec_vm_dev->sh_dev_count = 0;
			of_node_put(node);
			return -EINVAL;
		}

		if (!of_address_to_resource(node, 0, &res)) {
			shdev[i].type = RESOURCE_TYPE_IOMEM;
			shdev[i].addr_low = (u32)(res.start & 0xFFFFFFFF);
			shdev[i].addr_high = (u32)((res.start >> 32) & 0xFFFFFFFF);
			shdev[i].size = (size_t)resource_size(&res);
		} else {
			irq = of_irq_get(node, 0);
			if (irq < 0) {
				dev_err(dev, "Can not find the resource of the shared device\n");
				sec_vm_dev->sh_dev_count = 0;
				of_node_put(node);
				return -EINVAL;
			}
			struct irq_data *data = irq_get_irq_data(irq);
			if (!data) {
				dev_err(dev, "Failed to get irq data of shared device\n");
				sec_vm_dev->sh_dev_count = 0;
				of_node_put(node);
				return -EINVAL;
			}
			shdev[i].irq = irqd_to_hwirq(data);
			shdev[i].type = RESOURCE_TYPE_IRQ;
		}
		of_node_put(node);
	}
	sec_vm_dev->sh_dev = shdev;
	return 0;
}
EXPORT_SYMBOL(gh_vm_shared_device_probe);
