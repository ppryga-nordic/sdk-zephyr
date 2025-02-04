/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/toolchain.h>
#include <zephyr/pm/policy.h>
#include <zephyr/arch/common/pm_s2ram.h>
#include <hal/nrf_resetinfo.h>
#include <hal/nrf_memconf.h>
#include <hal/nrf_cache.h>
#include <zephyr/cache.h>
#include <power.h>
#include <soc_lrcconf.h>
#include "soc.h"
#include "pm_s2ram.h"

extern sys_snode_t soc_node;

//#define USE_CACHE_STORE_RESTORE
//#define USE_CACHE_STORE_RESTORE_WFI

#if !defined(CONFIG_SOC_NRF54H20_CPURAD) || (defined(CONFIG_SOC_NRF54H20_CPURAD) && !defined(USE_CACHE_STORE_RESTORE) && !defined(USE_CACHE_STORE_RESTORE_WFI))
#define USE_CACHE_DISABLE_STRATEGY
#elif defined(CONFIG_SOC_NRF54H20_CPURAD) && defined(USE_CACHE_STORE_RESTORE)
#define USE_CACHE_STORE_RESTORE_STRATEGY
#elif defined(CONFIG_SOC_NRF54H20_CPURAD) && defined(USE_CACHE_STORE_RESTORE_WFI)
#define USE_CACHE_STORE_RESTORE_STRATEGY_WFI
#else
#error Unupported CACHE stratedy
#endif

static void common_suspend(void)
{
#if defined(USE_CACHE_STORE_RESTORE_STRATEGY)
		nrf_cache_profiling_counters_clear(NRF_ICACHE);
		nrf_cache_profiling_set(NRF_ICACHE, true);
		nrf_cache_task_trigger(NRF_DCACHE, NRF_CACHE_TASK_SAVE);
		//stop profiling before move on. That data must be != 0 to give an informaation if power down happened
		nrf_cache_profiling_set(NRF_ICACHE, false);
		nrf_cache_task_trigger(NRF_ICACHE, NRF_CACHE_TASK_SAVE);
#endif /* USE_CACHE_STORE_RESTORE_STRATEGY */

	if (IS_ENABLED(CONFIG_DCACHE)) {
#if defined(USE_CACHE_DISABLE_STRATEGY)
		/* Flush, disable and power down DCACHE */
		sys_cache_data_flush_all();
		sys_cache_data_disable();
		nrf_memconf_ramblock_control_enable_set(NRF_MEMCONF, RAMBLOCK_POWER_ID,
							RAMBLOCK_CONTROL_BIT_DCACHE, false);
#endif /* USE_CACHE_DISABLE_STRATEGY */
	}

	if (IS_ENABLED(CONFIG_ICACHE)) {
#if defined(USE_CACHE_DISABLE_STRATEGY)
		/* Disable and power down ICACHE */
		sys_cache_instr_disable();
		nrf_memconf_ramblock_control_enable_set(NRF_MEMCONF, RAMBLOCK_POWER_ID,
							RAMBLOCK_CONTROL_BIT_ICACHE, false);
#endif /* USE_CACHE_DISABLE_STRATEGY */
	}

#if defined(USE_CACHE_DISABLE_STRATEGY)
	soc_lrcconf_poweron_release(&soc_node, NRF_LRCCONF_POWER_DOMAIN_0);
#endif /* USE_CACHE_DISABLE_STRATEGY */
}

static void common_resume(void)
{
#if defined(USE_CACHE_STORE_RESTORE_STRATEGY)
	if (NRF_ICACHE->PROFILING.READS == 0) {
		nrf_cache_task_trigger(NRF_ICACHE, NRF_CACHE_TASK_RESTORE);
		nrf_cache_task_trigger(NRF_DCACHE, NRF_CACHE_TASK_RESTORE);
	} else {
		nrf_cache_enable(NRF_ICACHE);
		nrf_cache_enable(NRF_DCACHE);
	}
#endif /* USE_CACHE_STORE_RESTORE_STRATEGY */

	if (IS_ENABLED(CONFIG_ICACHE)) {
#if defined(USE_CACHE_DISABLE_STRATEGY)
		/* Power up and re-enable ICACHE */
		nrf_memconf_ramblock_control_enable_set(NRF_MEMCONF, RAMBLOCK_POWER_ID,
							RAMBLOCK_CONTROL_BIT_ICACHE, true);
		sys_cache_instr_enable();
#endif /* USE_CACHE_DISABLE_STRATEGY */
	}

	if (IS_ENABLED(CONFIG_DCACHE)) {
#if defined(USE_CACHE_DISABLE_STRATEGY)
		/* Power up and re-enable DCACHE */
		nrf_memconf_ramblock_control_enable_set(NRF_MEMCONF, RAMBLOCK_POWER_ID,
							RAMBLOCK_CONTROL_BIT_DCACHE, true);
		sys_cache_data_enable();
#endif /* USE_CACHE_STORE_RESTORE_STRATEGY */
	}

#if defined(USE_CACHE_DISABLE_STRATEGY)
	soc_lrcconf_poweron_request(&soc_node, NRF_LRCCONF_POWER_DOMAIN_0);
#endif /* USE_CACHE_DISABLE_STRATEGY */
}

void nrf_poweroff(void)
{
	nrf_resetinfo_resetreas_local_set(NRF_RESETINFO, 0);
	nrf_resetinfo_restore_valid_set(NRF_RESETINFO, false);

#if !defined(CONFIG_SOC_NRF54H20_CPURAD)
	/* Disable retention */
	nrf_lrcconf_retain_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_MAIN, false);
	nrf_lrcconf_retain_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_DOMAIN_0, false);
#endif
	common_suspend();

	nrf_lrcconf_task_trigger(NRF_LRCCONF010, NRF_LRCCONF_TASK_SYSTEMOFFREADY);

	__set_BASEPRI(0);
	__ISB();
	__DSB();
	
	__WFI();

	CODE_UNREACHABLE;
}

static void s2idle_enter(uint8_t substate_id)
{
	switch (substate_id) {
	case 0:
		/* Substate for idle with cache powered on - not implemented yet. */
		break;
	case 1: /* Substate for idle with cache retained - not implemented yet. */
		break;
	case 2: /* Substate for idle with cache disabled. */
#if !defined(CONFIG_SOC_NRF54H20_CPURAD)
		soc_lrcconf_poweron_request(&soc_node, NRF_LRCCONF_POWER_MAIN);
#endif
		common_suspend();
		break;
	default: /* Unknown substate. */
		return;
	}

	__set_BASEPRI(0);
	__ISB();
	__DSB();
#if defined(USE_CACHE_STORE_RESTORE_STRATEGY_WFI)
		nrf_cache_profiling_counters_clear(NRF_ICACHE);
		nrf_cache_profiling_set(NRF_ICACHE, true);

		nrf_cache_task_trigger(NRF_DCACHE, NRF_CACHE_TASK_SAVE);
		//stop profiling before move on. That data must be != 0 to give an informaation if power down happened
		nrf_cache_profiling_set(NRF_ICACHE, false);
		nrf_cache_task_trigger(NRF_ICACHE, NRF_CACHE_TASK_SAVE);
#endif /* USE_CACHE_STORE_RESTORE_STRATEGY_WFI */
	__WFI();
}

static void s2idle_exit(uint8_t substate_id)
{
	switch (substate_id) {
	case 0:
		/* Substate for idle with cache powered on - not implemented yet. */
		break;
	case 1: /* Substate for idle with cache retained - not implemented yet. */
		break;
	case 2: /* Substate for idle with cache disabled. */
		common_resume();
#if !defined(CONFIG_SOC_NRF54H20_CPURAD)
		soc_lrcconf_poweron_release(&soc_node, NRF_LRCCONF_POWER_MAIN);
#endif
	default: /* Unknown substate. */
		return;
	}
}

#if defined(CONFIG_PM_S2RAM)
/* Resume domain after local suspend to RAM. */
static void s2ram_exit(void)
{
	common_resume();
#if !defined(CONFIG_SOC_NRF54H20_CPURAD)
	/* Re-enable domain retention. */
	nrf_lrcconf_retain_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_DOMAIN_0, true);
#endif
}

/* Function called during local domain suspend to RAM. */
static int sys_suspend_to_ram(void)
{
	/* Set intormation which is used on domain wakeup to determine if resume from RAM shall
	 * be performed.
	 */
	nrf_resetinfo_resetreas_local_set(NRF_RESETINFO,
					  NRF_RESETINFO_RESETREAS_LOCAL_UNRETAINED_MASK);
	nrf_resetinfo_restore_valid_set(NRF_RESETINFO, true);

#if !defined(CONFIG_SOC_NRF54H20_CPURAD)
	/* Disable retention */
	nrf_lrcconf_retain_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_DOMAIN_0, false);
#endif
	common_suspend();

	__set_BASEPRI(0);
	__ISB();
	__DSB();
	__WFI();
	/*
	 * We might reach this point is k_cpu_idle returns (there is a pre sleep hook that
	 * can abort sleeping.
	 */
	return -EBUSY;
}

static void s2ram_enter(void)
{
	/*
	 * Save the CPU context (including the return address),set the SRAM
	 * marker and power off the system.
	 */
	if (soc_s2ram_suspend(sys_suspend_to_ram)) {
		return;
	}
}
#endif /* defined(CONFIG_PM_S2RAM) */

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	if (state == PM_STATE_SUSPEND_TO_IDLE) {
		__disable_irq();
		s2idle_enter(substate_id);
		/* Resume here. */
#if defined(USE_CACHE_STORE_RESTORE_STRATEGY_WFI)
	if (NRF_ICACHE->PROFILING.READS == 0) {
		nrf_cache_task_trigger(NRF_ICACHE, NRF_CACHE_TASK_RESTORE);
		nrf_cache_task_trigger(NRF_DCACHE, NRF_CACHE_TASK_RESTORE);
	} else {
		nrf_cache_enable(NRF_ICACHE);
		nrf_cache_enable(NRF_DCACHE);
	}
#endif /* USE_CACHE_STORE_RESTORE_STRATEGY_WFI */
		s2idle_exit(substate_id);
		__enable_irq();
	}
#if defined(CONFIG_PM_S2RAM)
	else if (state == PM_STATE_SUSPEND_TO_RAM) {
		__disable_irq();
		s2ram_enter();
		/* On resuming or error we return exactly *HERE* */
		s2ram_exit();
		__enable_irq();
	}
#endif
	else {
		k_cpu_idle();
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	irq_unlock(0);
}
