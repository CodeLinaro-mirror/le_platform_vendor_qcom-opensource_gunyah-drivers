/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _GH_VM_ADDR_TRANSLATION_H
#define _GH_VM_ADDR_TRANSLATION_H

#include <linux/err.h>
#include <linux/slab.h>

struct vm_addr_rgn {
    unsigned long addr_start;
    unsigned long len;
};

struct vm_addr_rgn_table {
    struct vm_addr_rgn *regions;
    unsigned int nents;
};

#define for_each_addr_region(addr_regions, addr_region, nr, __i)   \
    for (__i, addr_region = (addr_regions); __i < nr; __i++, addr_region++)

#define for_each_addr_rgntlb_region(addr_rgn_table, addr_region, i)    \
    for_each_addr_region((addr_rgn_table)->regions, addr_region, (addr_rgn_table)->nents, i)

static inline struct vm_addr_rgn_table *gh_vm_addr_rgn_table_alloc(unsigned int nents)
{
    struct vm_addr_rgn_table *vm_addr_table;

    vm_addr_table = kzalloc(sizeof(*vm_addr_table), GFP_KERNEL);
    if (!vm_addr_table)
        return ERR_PTR(-ENOMEM);

    vm_addr_table->regions = kvzalloc(sizeof(struct vm_addr_rgn) * nents, GFP_KERNEL);
    if (!vm_addr_table->regions) {
        kfree(vm_addr_table);
        return ERR_PTR(-ENOMEM);
    }

    vm_addr_table->nents = nents;

    return vm_addr_table;
}

static inline void gh_vm_addr_rgn_table_free(struct vm_addr_rgn_table *vm_addr_table)
{
    kvfree(vm_addr_table->regions);
    kfree(vm_addr_table);
}

int gh_vm_addr_translate(struct vm_addr_rgn_table *gvm_addr_rgn_tbl,
                                    struct vm_addr_rgn_table *pvm_addr_rgn_tbl,
                                    int vmid, bool *output_in_pvm_addr_rgn_tbl);

#endif // _GH_VM_ADDR_TRANSLATION_H
