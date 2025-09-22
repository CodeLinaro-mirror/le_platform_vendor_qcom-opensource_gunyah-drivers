/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __GH_DEVICE_LEND_H
#define __GH_DEVICE_LEND_H

#ifdef CONFIG_GH_DEVICE_LEND

int device_lend(struct gh_vm *vm, struct gh_shdev *shdev);
int device_reclaim(struct gh_shdev *shdev);
int gh_vm_shared_device_probe (struct gh_sec_vm_dev *sec_vm_dev);


#else
static inline int device_lend(struct gh_vm *vm, struct gh_shdev *shdev) {
	return -EINVAL;
}

static inline int device_reclaim(struct gh_shdev *shdev){
	return -EINVAL;
}

static inline int gh_vm_shared_device_probe (struct gh_sec_vm_dev *sec_vm_dev) {
	return 0;
}
#endif

#endif
