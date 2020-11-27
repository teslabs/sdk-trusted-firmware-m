/*
 * Copyright (c) 2018-2020, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>
#include "cmsis.h"
#include "fih.h"
#include "tfm_spm_hal.h"
#include "tfm_platform_core_api.h"
#include "target_cfg.h"
#include "Driver_MPC.h"
#include "mpu_armv8m_drv.h"
#include "region_defs.h"
#include "utilities.h"

/* Import MPC driver */
extern ARM_DRIVER_MPC Driver_SRAM1_MPC;

/* Get address of memory regions to configure MPU */
extern const struct memory_region_limits memory_regions;

struct mpu_armv8m_dev_t dev_mpu_s = { MPU_BASE };

#ifdef CONFIG_TFM_ENABLE_MEMORY_PROTECT
#define PARTITION_REGION_PERIPH_START   5
#define PARTITION_REGION_PERIPH_MAX_NUM 2

uint32_t periph_num_count = 0;
#endif /* CONFIG_TFM_ENABLE_MEMORY_PROTECT */

#ifdef TFM_FIH_PROFILE_ON
fih_int tfm_spm_hal_configure_default_isolation(
                  uint32_t partition_idx,
                  const struct platform_data_t *platform_data)
#else /* TFM_FIH_PROFILE_ON */
enum tfm_plat_err_t tfm_spm_hal_configure_default_isolation(
                  uint32_t partition_idx,
                  const struct platform_data_t *platform_data)
#endif /* TFM_FIH_PROFILE_ON */
{
    fih_int fih_rc = FIH_FAILURE;
    bool privileged = tfm_is_partition_privileged(partition_idx);
#if defined(CONFIG_TFM_ENABLE_MEMORY_PROTECT) && (TFM_LVL != 1)
    struct mpu_armv8m_region_cfg_t region_cfg;
#endif

    if (!platform_data) {
        FIH_RET(fih_int_encode(TFM_PLAT_ERR_INVALID_INPUT));
    }

#if defined(CONFIG_TFM_ENABLE_MEMORY_PROTECT) && (TFM_LVL != 1)
    if (!privileged) {
        region_cfg.region_nr = PARTITION_REGION_PERIPH_START + periph_num_count;
        periph_num_count++;
        if (periph_num_count >= PARTITION_REGION_PERIPH_MAX_NUM) {
            FIH_RET(fih_int_encode(TFM_PLAT_ERR_MAX_VALUE));
        }
        region_cfg.region_base = platform_data->periph_start;
        region_cfg.region_limit = platform_data->periph_limit;
        region_cfg.region_attridx = MPU_ARMV8M_MAIR_ATTR_DEVICE_IDX;
        region_cfg.attr_access = MPU_ARMV8M_AP_RW_PRIV_UNPRIV;
        region_cfg.attr_sh = MPU_ARMV8M_SH_NONE;
        region_cfg.attr_exec = MPU_ARMV8M_XN_EXEC_NEVER;

#ifdef TFM_FIH_PROFILE_ON
        FIH_CALL(mpu_armv8m_disable, fih_rc, &dev_mpu_s);

        FIH_CALL(mpu_armv8m_region_enable, fih_rc, &dev_mpu_s, &region_cfg);
        if (fih_not_eq(fih_rc, fih_int_encode(MPU_ARMV8M_OK))) {
            FIH_RET(fih_int_encode(TFM_PLAT_ERR_SYSTEM_ERR));
        }

        FIH_CALL(mpu_armv8m_enable, fih_rc, &dev_mpu_s,
                 PRIVILEGED_DEFAULT_ENABLE, HARDFAULT_NMI_ENABLE);
        if (fih_not_eq(fih_rc, fih_int_encode(MPU_ARMV8M_OK))) {
            FIH_RET(fih_int_encode(TFM_PLAT_ERR_SYSTEM_ERR));
        }
#else /* TFM_FIH_PROFILE_ON */
        mpu_armv8m_disable(&dev_mpu_s);

        if (mpu_armv8m_region_enable(&dev_mpu_s, &region_cfg)
            != MPU_ARMV8M_OK) {
            return TFM_PLAT_ERR_SYSTEM_ERR;
        }
        mpu_armv8m_enable(&dev_mpu_s, PRIVILEGED_DEFAULT_ENABLE,
                          HARDFAULT_NMI_ENABLE);
#endif /* TFM_FIH_PROFILE_ON */
    }
#endif /* defined(CONFIG_TFM_ENABLE_MEMORY_PROTECT) && (TFM_LVL != 1) */

    if (platform_data->periph_ppc_bank != PPC_SP_DO_NOT_CONFIGURE) {
#ifdef TFM_FIH_PROFILE_ON
        FIH_CALL(ppc_configure_to_secure, fih_rc,
                 platform_data->periph_ppc_bank,
                 platform_data->periph_ppc_loc);
        if (privileged) {
            FIH_CALL(ppc_clr_secure_unpriv, fih_rc,
                     platform_data->periph_ppc_bank,
                     platform_data->periph_ppc_loc);
        } else {
            FIH_CALL(ppc_en_secure_unpriv, fih_rc,
                     platform_data->periph_ppc_bank,
                     platform_data->periph_ppc_loc);
        }
#else /* TFM_FIH_PROFILE_ON */
        ppc_configure_to_secure(platform_data->periph_ppc_bank,
                                platform_data->periph_ppc_loc);
        if (privileged) {
            ppc_clr_secure_unpriv(platform_data->periph_ppc_bank,
                                  platform_data->periph_ppc_loc);
        } else {
            ppc_en_secure_unpriv(platform_data->periph_ppc_bank,
                                 platform_data->periph_ppc_loc);
        }
#endif /* TFM_FIH_PROFILE_ON */
    }

    fih_rc = fih_int_encode(TFM_PLAT_ERR_SUCCESS);
    FIH_RET(fih_rc);
}

void MPC_Handler(void)
{
    /* Clear MPC interrupt flag and pending MPC IRQ */
    Driver_SRAM1_MPC.ClearInterrupt();
    NVIC_ClearPendingIRQ(MPC_IRQn);

    /* Print fault message and block execution */
    ERROR_MSG("Oops... MPC fault!!!");

    /* Inform TF-M core that isolation boundary has been violated */
    tfm_access_violation_handler();
}

void PPC_Handler(void)
{
    /*
     * Due to an issue on the FVP, the PPC fault doesn't trigger a
     * PPC IRQ which is handled by the PPC_handler.
     * In the FVP execution, this code is not execute.
     */

    /* Clear PPC interrupt flag and pending PPC IRQ */
    ppc_clear_irq();
    NVIC_ClearPendingIRQ(PPC_IRQn);

    /* Print fault message*/
    ERROR_MSG("Oops... PPC fault!!!");

    /* Inform TF-M core that isolation boundary has been violated */
    tfm_access_violation_handler();
}

uint32_t tfm_spm_hal_get_ns_VTOR(void)
{
    return memory_regions.non_secure_code_start;
}

uint32_t tfm_spm_hal_get_ns_MSP(void)
{
    return *((uint32_t *)memory_regions.non_secure_code_start);
}

uint32_t tfm_spm_hal_get_ns_entry_point(void)
{
    return *((uint32_t *)(memory_regions.non_secure_code_start+ 4));
}

enum tfm_plat_err_t tfm_spm_hal_set_secure_irq_priority(IRQn_Type irq_line,
                                                        uint32_t priority)
{
    uint32_t quantized_priority = priority >> (8U - __NVIC_PRIO_BITS);
    NVIC_SetPriority(irq_line, quantized_priority);
    return TFM_PLAT_ERR_SUCCESS;
}

void tfm_spm_hal_clear_pending_irq(IRQn_Type irq_line)
{
    NVIC_ClearPendingIRQ(irq_line);
}

void tfm_spm_hal_enable_irq(IRQn_Type irq_line)
{
    NVIC_EnableIRQ(irq_line);
}

void tfm_spm_hal_disable_irq(IRQn_Type irq_line)
{
    NVIC_DisableIRQ(irq_line);
}

enum irq_target_state_t tfm_spm_hal_set_irq_target_state(
                                           IRQn_Type irq_line,
                                           enum irq_target_state_t target_state)
{
    uint32_t result;

    if (target_state == TFM_IRQ_TARGET_STATE_SECURE) {
        result = NVIC_ClearTargetState(irq_line);
    } else {
        result = NVIC_SetTargetState(irq_line);
    }

    if (result) {
        return TFM_IRQ_TARGET_STATE_NON_SECURE;
    } else {
        return TFM_IRQ_TARGET_STATE_SECURE;
    }
}

enum tfm_plat_err_t tfm_spm_hal_enable_fault_handlers(void)
{
    return enable_fault_handlers();
}

enum tfm_plat_err_t tfm_spm_hal_system_reset_cfg(void)
{
    return system_reset_cfg();
}

#ifdef TFM_FIH_PROFILE_ON
fih_int tfm_spm_hal_init_debug(void)
{
    fih_int fih_rc = FIH_FAILURE;

    FIH_CALL(init_debug, fih_rc);

    FIH_RET(fih_rc);
}
#else /* TFM_FIH_PROFILE_ON */
enum tfm_plat_err_t tfm_spm_hal_init_debug(void)
{
    return init_debug();
}
#endif /* TFM_FIH_PROFILE_ON */

enum tfm_plat_err_t tfm_spm_hal_nvic_interrupt_target_state_cfg(void)
{
    return nvic_interrupt_target_state_cfg();
}

enum tfm_plat_err_t tfm_spm_hal_nvic_interrupt_enable(void)
{
    return nvic_interrupt_enable();
}

#ifdef TFM_FIH_PROFILE_ON
fih_int tfm_spm_hal_verify_isolation_hw(void)
{
    fih_int fih_rc = FIH_INT_INIT(TFM_PLAT_ERR_SYSTEM_ERR);

    FIH_CALL(verify_isolation_hw, fih_rc);
    if (fih_not_eq(fih_rc, FIH_SUCCESS)) {
        FIH_PANIC;
    }

    FIH_RET(fih_int_encode(TFM_PLAT_ERR_SUCCESS));
}
#endif /* TFM_FIH_PROFILE_ON */
