/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _GH_VM_ADDR_TRANSLATE_H
#define _GH_VM_ADDR_TRANSLATE_H

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>

#include "gh_secure_vm_loader.h"

#if IS_ENABLED(CONFIG_GH_SECURE_VM_LOADER)
int gh_gvm_mem_translate_init(struct gh_sec_vm_dev *sec_vm_dev);
void gh_gvm_mem_translate_deinit(struct gh_sec_vm_dev *sec_vm_dev);
void gh_gvm_mem_translate_remove_mem_regions(struct gh_sec_vm_dev *sec_vm_dev);
int gh_gvm_mem_translate_add_mem_regions(struct gh_sec_vm_dev *sec_vm_dev);
#else // IS_ENABLED(CONFIG_GH_SECURE_VM_LOADER)
static inline int gh_gvm_mem_translate_init(struct gh_sec_vm_dev *sec_vm_dev)
{
	return -EINVAL;
}

static inline void gh_gvm_mem_translate_deinit(struct gh_sec_vm_dev *sec_vm_dev) {}

static inline void gh_gvm_mem_translate_remove_mem_regions(struct gh_sec_vm_dev *sec_vm_dev) {}

static inline int gh_gvm_mem_translate_add_mem_regions(struct gh_sec_vm_dev *sec_vm_dev)
{
	return -EINVAL;
}
#endif // IS_ENABLED(CONFIG_GH_SECURE_VM_LOADER)

#endif // _GH_VM_ADDR_TRANSLATE_H
