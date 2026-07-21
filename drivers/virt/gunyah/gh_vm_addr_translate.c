/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/spinlock.h>
#include "gh_vm_addr_translate.h"
#include <linux/gunyah/gh_vm_addr_translation.h>
#include <linux/interval_tree_generic.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

/**
 * Global list to store all instances of struct gh_vm_mem.
 * Each entry in this list represents a GVM memory used to do sanity check.
 */
static LIST_HEAD(gh_vm_mem_list);

/**
 * Global lock to protect access to both gh_vm_mem_list and the interval tree
 * inside each struct gh_vm_mem.
 */
static DEFINE_RWLOCK(gh_vm_mem_lock);

struct gh_vm_mem_region_map {
    struct rb_node rb;
    unsigned long addr_start;
    unsigned long addr_last;
    unsigned long size;
    unsigned long pvm_addr_start;
    unsigned long __subtree_last;
};

/**
 * struct gh_vm_mem - Represents a GVM memory used to do translate.
 * @root: Interval tree to store memory regions for this GVM.
 * @vmid: VMID of the GVM.
 * @list: List entry to link this struct with gh_vm_mem_list.
 * @is_valid: Flag to indicate if the GVM memory is valid to do translate.
 *                  true: memory in vm_mem_itree is valid.
 *                  false: no valid memory in vm_mem_itree.
 * @translation_required: Flag to indicate whether address translation from GVM IPA to PVM IPA is required.
 *                  true: GVM IPA and PVM IPA are not equal, translation is required.
 *                  false: GVM IPA and PVM IPA are equal, no translation needed.
 */
struct gh_vm_mem {
    struct rb_root_cached root;
    int vmid;
    struct list_head list;
    bool is_valid;
    bool translation_required;
};

#define START(map) ((map)->addr_start)
#define LAST(map) ((map)->addr_last)

/* Use an interval tree to speed up address translation from GVM addresses to PVM addresses. */
INTERVAL_TREE_DEFINE(struct gh_vm_mem_region_map, rb,
                     unsigned long, __subtree_last,
                     START, LAST, static inline, gh_vm_mem_itree);

static struct gh_vm_mem *gh_find_vm_mem_by_vmid(int vmid)
{
    struct gh_vm_mem *vm_mem;

    list_for_each_entry(vm_mem, &gh_vm_mem_list, list) {
        if (vm_mem->vmid == vmid) {
            return vm_mem;
        }
    }

    pr_err("vmid %d not found in gh_vm_mem_list\n", vmid);
    return NULL;
}

static int gh_vm_mem_add_region(struct gh_vm_mem *vm_mem, unsigned long addr_start,
                        unsigned long size, unsigned long pvm_addr_start)
{
    struct gh_vm_mem_region_map *map;
    int ret = 0;

    map = kzalloc(sizeof(*map), GFP_ATOMIC);
    if (!map)
        return -ENOMEM;

    map->addr_start = addr_start;
    if (check_add_overflow(map->addr_start, size - 1, &map->addr_last)) {
        pr_err("vmid %d overflow in adding memory region, start %lu and size %lu\n",
                vm_mem->vmid, addr_start, size);
        ret = -EINVAL;
        goto err_overflow;
    }
    map->size = size;
    map->pvm_addr_start = pvm_addr_start;

    gh_vm_mem_itree_insert(map, &vm_mem->root);

    return 0;

err_overflow:
    kfree(map);
    return ret;
}

static void gh_vm_mem_remove_map(struct gh_vm_mem *vm_mem,
                struct gh_vm_mem_region_map *map)
{
    gh_vm_mem_itree_remove(map, &vm_mem->root);
    kfree(map);
}

static struct gh_vm_mem_region_map *
    gh_vm_mem_iter_first(struct gh_vm_mem *vm_mem, unsigned long addr_start,
    unsigned long addr_last)
{
    return gh_vm_mem_itree_iter_first(&vm_mem->root, addr_start, addr_last);
}

static int gh_fill_pvm_addr(struct vm_addr_rgn_table *pvm_addr_rgn_table,
        unsigned long addr_start, unsigned long size, unsigned int *nents)
{
    unsigned int i = *nents;
    struct vm_addr_rgn *addr_regions = pvm_addr_rgn_table->regions;

    if (i >= pvm_addr_rgn_table->nents) {
        pr_err("overflow pvm_addr_rgn_table->nents %u nents %u\n",
            pvm_addr_rgn_table->nents, i);
        return -EOVERFLOW;
    }

    addr_regions[i].addr_start = addr_start;
    addr_regions[i].len = size;
    *nents += 1;

    return 0;
}

static int gh_translate_and_fill_pvm_addr(struct vm_addr_rgn_table *pvm_addr_rgn_tbl,
                                         struct gh_vm_mem_region_map *map,
                                         unsigned long gvm_addr_start,
                                         unsigned long addr_region_len,
                                         unsigned int *nents)
{
    unsigned long offset = gvm_addr_start - map->addr_start;
    unsigned long pvm_addr_start;

    if (check_add_overflow(offset, map->pvm_addr_start, &pvm_addr_start)) {
        pr_err("overflow computing pvm_addr_start: offset=%lx pvm_base=%lx\n",
               offset, map->pvm_addr_start);
        return -EINVAL;
    }

    return gh_fill_pvm_addr(pvm_addr_rgn_tbl, pvm_addr_start, addr_region_len, nents);
}

/**
 * @gvm_addr_rgn_tbl: Pointer to a vm_addr_rgn_table containing IPA entries
 *    from the GVM
 * @pvm_addr_rgn_tbl: Pointer to a vm_addr_rgn_table filled with translated
 *    PVM IPA entries
 * @vmid: VM identifier for the GVM owning the input IPA addresses
 * @output_in_pvm_addr_rgn_tbl: Pointer to a boolean flag. On return,
 *    *output_in_pvm_addr_rgn_tbl is set to true if pvm_addr_rgn_tbl contains
 *    valid translated entries. If false, the entries in pvm_addr_rgn_tbl
 *    should be considered invalid and gvm_addr_rgn_tbl should be used instead.
 *
 * This function translates each Intermediate Physical Address (IPA) entry in
 * @gvm_addr_rgn_tbl, which originates from a Guest VM (GVM), into
 * corresponding PVM IPA.
 *
 * As an optimization for the case where GVM IPA equals PVM IPA, the parameter
 * @output_in_pvm_addr_rgn_tbl is introduced. If
 * *output_in_pvm_addr_rgn_tbl is false, it indicates that GVM IPA and PVM IPA
 * are identical, and no translation is performed. In this case, the function
 * only verifies that the IPA entries in gvm_addr_rgn_tbl belong to the
 * specified GVM. Upon successful return, you should continue using the data in
 * gvm_addr_rgn_tbl.
 *
 * If *output_in_pvm_addr_rgn_tbl is true, it means GVM IPA differs from
 * PVM IPA, and translation has been performed. Upon successful return, you
 * should use the data in pvm_addr_rgn_tbl instead.
 *
 * Note that the number of entries in pvm_addr_rgn_tbl is always greater than
 * or equal to the number of entries in gvm_addr_rgn_tbl. This is because
 * translated PVM IPA regions are not merged, as merging memory regions can be
 * time-consuming and often yields limited benefit. You may choose to perform
 * merging yourself if needed.
 *
 * When allocating space for pvm_addr_rgn_tbl, we recommend planning for the
 * worst-case scenario, where the number of entries equals the number of pages
 * in gvm_addr_rgn_tbl.
 *
 * Return: >= 0 on success (number of valid entries), < 0 on failure
 */
int gh_vm_addr_translate(struct vm_addr_rgn_table *gvm_addr_rgn_tbl,
                                    struct vm_addr_rgn_table *pvm_addr_rgn_tbl,
                                    int vmid, bool *output_in_pvm_addr_rgn_tbl)
{
    struct gh_vm_mem *vm_mem;
    struct gh_vm_mem_region_map *map = NULL;
    int i = 0, ret = 0;
    struct vm_addr_rgn *addr_region;
    unsigned long gvm_addr_start, gvm_addr_last, size, offset, addr_region_len, partial_size;
    unsigned int nents = 0;

    if (!gvm_addr_rgn_tbl) {
        pr_err("null pointer of gvm_addr_rgn_tbl\n");
        return -EINVAL;
    }

    if (!output_in_pvm_addr_rgn_tbl) {
        pr_err("null pointer of output_in_pvm_addr_rgn_tbl\n");
        return -EINVAL;
    }

    if (gvm_addr_rgn_tbl->nents < 1) {
        pr_err("The nents of vm_addr_rgn_table is small than 1\n");
        return -EINVAL;
    }

    read_lock(&gh_vm_mem_lock);
    vm_mem = gh_find_vm_mem_by_vmid(vmid);
    if (!vm_mem) {
        ret = -ENODEV;
        goto end;
    }

    if (!vm_mem->is_valid) {
        pr_err("no valid memory region in VM %d\n", vmid);
        ret = -EINVAL;
        goto end;
    }

    if (vm_mem->translation_required && !pvm_addr_rgn_tbl) {
        pr_err("null pointer of pvm_addr_rgn_tbl\n");
        ret = -EINVAL;
        goto end;
    }

    if (vm_mem->translation_required && pvm_addr_rgn_tbl->nents < 1) {
        pr_err("The nents of pvm_addr_rgn_tbl is small than 1\n");
        ret = -EINVAL;
        goto end;
    }

    for_each_addr_rgntlb_region(gvm_addr_rgn_tbl, addr_region, i) {
        addr_region_len = addr_region->len;
        if (addr_region_len < 1) {
            pr_err("addr_region->len is %lu\n", addr_region_len);
            ret = -EINVAL;
            goto end;
        }

        gvm_addr_start = addr_region->addr_start;
        if (check_add_overflow(gvm_addr_start, addr_region_len - 1, &gvm_addr_last)) {
            pr_err("vmid %d overflow in gvm addr range, start %lx len %lu\n",
                vmid, gvm_addr_start, addr_region_len);
            ret = -EINVAL;
            goto end;
        }

        /*
         * After hitting a mapping node, we cache the corresponding mapping.
         * When translating subsequent GVM address regions, we first check
         * whether the cached mapping is hit.
         */
        if (map != NULL && gvm_addr_start >= map->addr_start &&
            gvm_addr_last <= map->addr_last) {
            pr_debug("transltion cache hit: vmid=%d gvm_addr=[%lx, %lx] map=[%lx, %lx]\n",
                vmid, gvm_addr_start, gvm_addr_last, map->addr_start, map->addr_last);

            if (vm_mem->translation_required) {
                ret = gh_translate_and_fill_pvm_addr(pvm_addr_rgn_tbl, map, gvm_addr_start, addr_region_len, &nents);
                if (ret)
                    goto end;
            }

            continue;
        }

        while (1) {
            map = gh_vm_mem_iter_first(vm_mem, gvm_addr_start, gvm_addr_last);
            if (!map) {
                pr_err("start %lx end %lx not found in VM %d\n",
                    gvm_addr_start, gvm_addr_last, vmid);
                ret = -EFAULT;
                goto end;
            } else if (map->addr_start > gvm_addr_start) {
                pr_err("VM %d overlap memory region start %lx is less than start %lx\n",
                    vmid, map->addr_start, gvm_addr_start);
                ret = -EFAULT;
                goto end;
            }

            if (gvm_addr_last > map->addr_last) {
                pr_debug("VM %d overlap memory region end %lx is big than end %lx\n",
                    vmid, map->addr_last, gvm_addr_last);

                partial_size = map->size - (gvm_addr_start - map->addr_start);
                if (vm_mem->translation_required) {
                    ret = gh_translate_and_fill_pvm_addr(pvm_addr_rgn_tbl, map, gvm_addr_start, partial_size, &nents);
                    if (ret)
                        goto end;
                }

                addr_region_len -= partial_size;
                gvm_addr_start = map->addr_last + 1;
                continue;
            }

            if (vm_mem->translation_required) {
                ret = gh_translate_and_fill_pvm_addr(pvm_addr_rgn_tbl, map, gvm_addr_start, addr_region_len, &nents);
                if (ret)
                    goto end;
            }

            break;
        }
    }

    *output_in_pvm_addr_rgn_tbl = vm_mem->translation_required;

    if (*output_in_pvm_addr_rgn_tbl)
        ret = (int)nents;
    else
        ret = (int)gvm_addr_rgn_tbl->nents;

end:
    read_unlock(&gh_vm_mem_lock);
    return ret;
}
EXPORT_SYMBOL_GPL(gh_vm_addr_translate);

static void gh_vm_mem_iter_remove(struct gh_vm_mem *vm_mem, unsigned long addr_start, unsigned long addr_last)
{
    struct gh_vm_mem_region_map *map;

    while ((map = gh_vm_mem_iter_first(vm_mem, addr_start, addr_last)))
        gh_vm_mem_remove_map(vm_mem, map);
}

static int gh_gvm_mem_add_share_mem(struct gh_sec_vm_dev *sec_vm_dev)
{
    struct gh_vm_mem *vm_mem;
    int i, ret;

    write_lock(&gh_vm_mem_lock);
    vm_mem = gh_find_vm_mem_by_vmid(sec_vm_dev->vmid);
    if (!vm_mem) {
        ret = -EINVAL;
        goto err_vmid;
    }

    for (i = 0; i < sec_vm_dev->sh_mem_count; i++) {
        if (sec_vm_dev->sh_mem_regions[i].base == 0) {
            pr_err("vmid %d sh_mem_regions[%d].base is 0\n",
                    vm_mem->vmid, i);
            ret = -EINVAL;
            goto err_vm_mem_list;
        }

        /**
         * Assumption: GVM IPA = PVM IPA
         * If GVM IPA ≠ PVM IPA, we need to fill in the correct PVM IPA corresponding to the GVM IPA
         */
        ret = gh_vm_mem_add_region(vm_mem, sec_vm_dev->sh_mem_regions[i].base,
            sec_vm_dev->sh_mem_regions[i].size, sec_vm_dev->sh_mem_regions[i].base);
        if (ret) {
            goto err_vm_mem_list;
        }
    }
    write_unlock(&gh_vm_mem_lock);

    return 0;

err_vm_mem_list:
    gh_vm_mem_iter_remove(vm_mem, 0, ULONG_MAX);
err_vmid:
    write_unlock(&gh_vm_mem_lock);
    return ret;
}

static int gh_gvm_mem_add_fw_mem(struct gh_sec_vm_dev *sec_vm_dev)
{
    struct gh_vm_mem *vm_mem;
    int i, ret;

    write_lock(&gh_vm_mem_lock);
    vm_mem = gh_find_vm_mem_by_vmid(sec_vm_dev->vmid);
    if (!vm_mem) {
        ret = -EINVAL;
        goto err_vmid;
    }

    for (i = 0; i < sec_vm_dev->fw_mem_count; i++) {
        if (sec_vm_dev->fw_mem_regions[i].fw_phys == 0) {
            pr_err("vmid %d fw_mem_regions[%d].fw_phys is 0\n", sec_vm_dev->vmid, i);
            ret = -EINVAL;
            goto err_vm_mem_list;
        }

        /**
         * Assumption: GVM IPA = PVM IPA
         * If GVM IPA ≠ PVM IPA, we need to fill in the correct PVM IPA corresponding to the GVM IPA
         */
        ret = gh_vm_mem_add_region(vm_mem, sec_vm_dev->fw_mem_regions[i].fw_phys,
            sec_vm_dev->fw_mem_regions[i].fw_size, sec_vm_dev->fw_mem_regions[i].fw_phys);
        if (ret) {
            goto err_vm_mem_list;
        }
    }
    write_unlock(&gh_vm_mem_lock);

    return 0;

err_vm_mem_list:
    gh_vm_mem_iter_remove(vm_mem, 0, ULONG_MAX);
err_vmid:
    write_unlock(&gh_vm_mem_lock);
    return ret;
}

/*
 * Currently, only two types of GVM memory regions are registered for address translation:
 *   1. GVM share memory
 *   2. GVM system memory
 *
 * If additional memory region types need to be supported in the future, add the
 * corresponding registration calls here.
 *
 * Note: virtio vring memory regions are intentionally excluded, as clients are
 * not expected to pass virtio vring addresses for translation.
 */
int gh_gvm_mem_translate_add_mem_regions(struct gh_sec_vm_dev *sec_vm_dev)
{
    int ret;
    struct gh_vm_mem *vm_mem;


    ret = gh_gvm_mem_add_share_mem(sec_vm_dev);
    if (ret)
        return ret;
    ret = gh_gvm_mem_add_fw_mem(sec_vm_dev);
    if (ret)
        return ret;

    write_lock(&gh_vm_mem_lock);
    vm_mem = gh_find_vm_mem_by_vmid(sec_vm_dev->vmid);
    if (!vm_mem) {
        ret = -EINVAL;
        goto err_vmid;
    }

    vm_mem->is_valid = true;

err_vmid:
    write_unlock(&gh_vm_mem_lock);
    return ret;
}

void gh_gvm_mem_translate_remove_mem_regions(struct gh_sec_vm_dev *sec_vm_dev)
{
    struct gh_vm_mem *vm_mem;

    pr_info("vmid %d\n", sec_vm_dev->vmid);
    write_lock(&gh_vm_mem_lock);

    vm_mem = gh_find_vm_mem_by_vmid(sec_vm_dev->vmid);
    if (vm_mem) {
        gh_vm_mem_iter_remove(vm_mem, 0, ULONG_MAX);
        vm_mem->is_valid = false;
    } else
        pr_info("vmid %d not found\n", sec_vm_dev->vmid);

    write_unlock(&gh_vm_mem_lock);
}

int gh_gvm_mem_translate_init(struct gh_sec_vm_dev *sec_vm_dev)
{
    struct gh_vm_mem *vm_mem, *tmp_vm_mem;
    int ret;

    pr_info("vmid %d\n", sec_vm_dev->vmid);
    vm_mem = kzalloc(sizeof(*vm_mem), GFP_KERNEL);
    if (!vm_mem)
        return -ENOMEM;

    vm_mem->root = RB_ROOT_CACHED;
    vm_mem->vmid = sec_vm_dev->vmid;
    vm_mem->is_valid = false;
    vm_mem->translation_required =
        sec_vm_dev->translation_required;
    INIT_LIST_HEAD(&vm_mem->list);

    write_lock(&gh_vm_mem_lock);
    list_for_each_entry(tmp_vm_mem, &gh_vm_mem_list, list) {
        if (tmp_vm_mem->vmid == vm_mem->vmid) {
            pr_err("Duplicate vmid %d\n", vm_mem->vmid);
            ret = -EINVAL;
            goto err_vm_mem;
        }
    }

    list_add_tail(&vm_mem->list, &gh_vm_mem_list);
    write_unlock(&gh_vm_mem_lock);

    return 0;

err_vm_mem:
    write_unlock(&gh_vm_mem_lock);
    kfree(vm_mem);
    return ret;
}

/**
 * gh_gvm_mem_translate_deinit - reset gh_vm_mem of vm (free all entries)
 * @sec_vm_dev: the vm to be reset
 */
void gh_gvm_mem_translate_deinit(struct gh_sec_vm_dev *sec_vm_dev)
{
    struct gh_vm_mem *tmp_vm_mem;
    bool vmid_found = false;

    pr_info("vmid %d\n", sec_vm_dev->vmid);
    write_lock(&gh_vm_mem_lock);
    list_for_each_entry(tmp_vm_mem, &gh_vm_mem_list, list) {
        if (tmp_vm_mem->vmid == sec_vm_dev->vmid) {
            list_del(&tmp_vm_mem->list);
            vmid_found = true;
            break;
        }
    }
    write_unlock(&gh_vm_mem_lock);

    if (vmid_found) {
        gh_vm_mem_iter_remove(tmp_vm_mem, 0, ULONG_MAX);
        kfree(tmp_vm_mem);
    } else
        pr_err("vmid %d not found\n", sec_vm_dev->vmid);
}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah VM IPA translate");
