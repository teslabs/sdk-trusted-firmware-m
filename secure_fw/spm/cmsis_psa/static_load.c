/*
 * Copyright (c) 2021, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include <limits.h>
#include <stdint.h>
#include "lists.h"
#include "region.h"
#include "region_defs.h"
#include "spm_ipc.h"
#include "utilities.h"
#include "load/partition_defs.h"
#include "load/spm_load_api.h"
#include "load/service_defs.h"
#include "psa/client.h"

#include "secure_fw/partitions/tfm_service_list.inc"
#include "tfm_spm_db_ipc.inc"

/* Partition static data region */
REGION_DECLARE(Image$$, TFM_SP_STATIC_LIST, $$RO$$Base);
REGION_DECLARE(Image$$, TFM_SP_STATIC_LIST, $$RO$$Limit);

static uintptr_t ldinf_sa = PART_REGION_ADDR(TFM_SP_STATIC_LIST, $$RO$$Base);
static uintptr_t ldinf_ea = PART_REGION_ADDR(TFM_SP_STATIC_LIST, $$RO$$Limit);

/* Allocate runtime space for partition from the pool. Static allocation. */
static struct partition_t *tfm_allocate_partition_assuredly(void)
{
    static uint32_t partition_pool_pos = 0;
    struct partition_t *p_partition_allocated = NULL;

    if (partition_pool_pos >= g_spm_partition_db.partition_count) {
        tfm_core_panic();
    }

    p_partition_allocated = &g_spm_partition_db.partitions[partition_pool_pos];
    partition_pool_pos++;

    return p_partition_allocated;
}

/* Allocate runtime space for service from the pool. Static allocation. */
static struct service_t *tfm_allocate_service_assuredly(uint32_t service_count)
{
    static uint32_t service_pool_pos = 0;
    struct service_t *p_service_allocated = NULL;
    uint32_t num_of_services = sizeof(g_services) / sizeof(struct service_t);

    if (service_count == 0) {
        return NULL;
    } else if ((service_count > num_of_services) ||
               (service_pool_pos >= num_of_services) ||
               (service_pool_pos + service_count > num_of_services)) {
        tfm_core_panic();
    }

    p_service_allocated = &g_services[service_pool_pos];
    service_pool_pos += service_count;

    return p_service_allocated;
}

struct partition_t *load_a_partition_assuredly(void)
{
    struct partition_load_info_t *p_ptldinf;
    struct partition_t           *partition;

    if ((UINTPTR_MAX - ldinf_sa < sizeof(struct partition_load_info_t)) ||
        (ldinf_sa + sizeof(struct partition_load_info_t) >= ldinf_ea)) {
        return NULL;
    }

    p_ptldinf = (struct partition_load_info_t *)ldinf_sa;

    if ((UINTPTR_MAX - ldinf_sa < LOAD_INFSZ_BYTES(p_ptldinf)) ||
        (ldinf_sa + LOAD_INFSZ_BYTES(p_ptldinf) > ldinf_ea))   {
        tfm_core_panic();
    }

    /* Magic ensures data integrity */
    if ((p_ptldinf->psa_ff_ver & PARTITION_INFO_MAGIC_MASK)
        != PARTITION_INFO_MAGIC) {
        tfm_core_panic();
    }

    if ((p_ptldinf->psa_ff_ver & PARTITION_INFO_VERSION_MASK)
        > PSA_FRAMEWORK_VERSION) {
        tfm_core_panic();
    }

    if (!(p_ptldinf->flags & SPM_PART_FLAG_IPC)) {
        tfm_core_panic();
    }

    partition = tfm_allocate_partition_assuredly();
    partition->p_ldinf = p_ptldinf;

    ldinf_sa += LOAD_INFSZ_BYTES(p_ptldinf);

    return partition;
}

void load_services_assuredly(struct partition_t *p_partition,
                             struct service_t **list_head)
{
    uint32_t i, j;
    struct service_t *services;
    const struct partition_load_info_t *p_ptldinf;
    const struct service_load_info_t *p_servldinf;

    if (p_partition == NULL) {
        tfm_core_panic();
    }

    p_ptldinf = p_partition->p_ldinf;
    p_servldinf = (struct service_load_info_t *)LOAD_INFO_SERVICE(p_ptldinf);

    /*
     * 'services' CAN be NULL when no services, which is a rational result.
     * The loop won't go in the NULL case.
     */
    services = tfm_allocate_service_assuredly(p_ptldinf->nservices);
    for (i = 0; i < p_ptldinf->nservices && services; i++) {
        p_partition->signals_allowed |= p_servldinf[i].signal;
        services[i].p_ldinf = &p_servldinf[i];
        services[i].partition = p_partition;

        /* Populate the p_service of stateless_service_ref[] */
        if (SERVICE_IS_STATELESS(p_servldinf[i].flags)) {
            for (j = 0; j < STATIC_HANDLE_NUM_LIMIT; j++) {
                if (stateless_service_ref[j].sid == p_servldinf[i].sid) {
                    stateless_service_ref[j].p_service = &services[i];
                    break;
                }
            }
            /* Stateless service not found in tracking table */
            if (j >= STATIC_HANDLE_NUM_LIMIT) {
                tfm_core_panic();
            }
        }
        BI_LIST_INIT_NODE(&services[i].handle_list);
        BI_LIST_INIT_NODE(&services[i].list);

        if (list_head) {
            if (*list_head) {
                BI_LIST_INSERT_AFTER(&(*list_head)->list, &services[i].list);
            } else {
                *list_head = &services[i];
            }
        }
    }
}
