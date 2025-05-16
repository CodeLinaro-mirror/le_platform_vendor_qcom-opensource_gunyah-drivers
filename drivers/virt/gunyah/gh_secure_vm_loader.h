/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _GH_SECURE_VM_LOADER_H
#define _GH_SECURE_VM_LOADER_H

#include "gh_private.h"

struct gh_sec_vm_fw_mem {
	phys_addr_t fw_phys;
	void *fw_virt;
	ssize_t fw_size;
	bool is_static;
};

struct gh_sec_vm_dev {
	struct list_head list;
	const char *vm_name;
	struct device *dev;
	bool system_vm;
	struct gh_sec_vm_fw_mem *fw_mem_regions;
	unsigned int fw_mem_count;
	int pas_id;
	int vmid;
	unsigned int fw_index;
	struct gh_shmem *sh_mem_regions;
	unsigned int sh_mem_count;
	bool translation_required;
};

/*
 * secure vm loader APIs
 */
#if IS_ENABLED(CONFIG_GH_SECURE_VM_LOADER)
int gh_secure_vm_loader_init(void);
void gh_secure_vm_loader_exit(void);
long gh_vm_ioctl_set_fw_name(struct gh_vm *vm, unsigned long arg);
long gh_vm_ioctl_get_fw_name(struct gh_vm *vm, unsigned long arg);
long gh_vm_ioctl_get_mem_count(struct gh_vm *vm);
long gh_vm_ioctl_get_mem_region(struct gh_vm *vm, unsigned long arg);
int gh_secure_vm_loader_reclaim_fw(struct gh_vm *vm);
struct gh_sec_vm_dev *get_sec_vm_dev_by_name(const char *vm_name);
#else
static int gh_secure_vm_loader_init(void)
{
	return -EINVAL;
}
static void gh_secure_vm_loader_exit(void)
{
}
static inline long gh_vm_ioctl_set_fw_name(struct gh_vm *vm,
						unsigned long arg)
{
	return -EINVAL;
}
static inline long gh_vm_ioctl_get_fw_name(struct gh_vm *vm,
						unsigned long arg)
{
	return -EINVAL;
}
static inline long gh_vm_ioctl_get_mem_count(struct gh_vm *vm)
{
	return -EINVAL;
}
static inline long gh_vm_ioctl_get_mem_region(struct gh_vm *vm,
						unsigned long arg)
{
	return -EINVAL;
}
static inline int gh_secure_vm_loader_reclaim_fw(struct gh_vm *vm)
{
	return -EINVAL;
}
static inline struct gh_sec_vm_dev *get_sec_vm_dev_by_name(const char *vm_name)
{
	return NULL;
}
#endif

#endif /* _GH_SECURE_VM_LOADER_H */
