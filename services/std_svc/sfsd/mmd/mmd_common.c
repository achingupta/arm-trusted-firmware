/*
 * Copyright (c) 2013-2014, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch_helpers.h>
#include <assert.h>
#include <bl_common.h>
#include <context_mgmt.h>
#include <string.h>
#include "mmd_private.h"

/*******************************************************************************
 * Replace the S-EL1 re-entry information with S-EL0 re-entry
 * information
 ******************************************************************************/
void mmd_setup_next_eret_into_sel0(cpu_context_t *secure_context)
{
	unsigned long elr_el1;
	unsigned int spsr_el1;

	assert(secure_context == cm_get_context(SECURE));
	elr_el1 = read_elr_el1();
	spsr_el1 = read_spsr_el1();

	cm_set_elr_spsr_el3(SECURE, elr_el1, spsr_el1);
	return;
}

/*******************************************************************************
 * Given a secure payload entrypoint info pointer, entry point PC, register
 * width, cpu id & pointer to a context data structure, this function will
 * initialize mm context and entry point info for the secure payload
 ******************************************************************************/
void mmd_init_mm_ep_state(struct entry_point_info *mm_ep_info,
			  mm_context_t *mm_ctx)
{
	uint32_t ep_attr;

	/* Passing a NULL context is a critical programming error */
	assert(mm_ctx);
	assert(mm_ep_info);

	/* Associate this context with the cpu specified */
	mm_ctx->mpidr = read_mpidr_el1();
	mm_ctx->state = 0;
	set_mm_pstate(mm_ctx->state, MM_PSTATE_OFF);
	clr_std_smc_active_flag(mm_ctx->state);

	cm_set_context(&mm_ctx->cpu_ctx, SECURE);

	/* initialise an entrypoint to set up the CPU context */
	ep_attr = SECURE | EP_ST_ENABLE | EP_UCME_ENABLE | EP_UFPE_ENABLE;
	if (read_sctlr_el3() & SCTLR_EE_BIT)
		ep_attr |= EP_EE_BIG;
	SET_PARAM_HEAD(mm_ep_info, PARAM_EP, VERSION_1, ep_attr);
}

/*******************************************************************************
 * This function takes an SP context pointer and:
 * 1. Applies the S-EL1 system register context from mm_ctx->cpu_ctx.
 * 2. Saves the current C runtime state (callee saved registers) on the stack
 *    frame and saves a reference to this state.
 * 3. Calls el3_exit() so that the EL3 system and general purpose registers
 *    from the mm_ctx->cpu_ctx are used to enter the secure payload image.
 ******************************************************************************/
uint64_t mmd_synchronous_sp_entry(mm_context_t *mm_ctx)
{
	uint64_t rc;

	assert(mm_ctx != NULL);
	assert(mm_ctx->c_rt_ctx == 0);

	/* Apply the Secure EL1 system register context and switch to it */
	assert(cm_get_context(SECURE) == &mm_ctx->cpu_ctx);
	cm_el1_sysregs_context_restore(SECURE);
	cm_set_next_eret_context(SECURE);

	rc = mmd_enter_sp(&mm_ctx->c_rt_ctx);
#if DEBUG
	mm_ctx->c_rt_ctx = 0;
#endif

	return rc;
}


/*******************************************************************************
 * This function takes an SP context pointer and:
 * 1. Saves the S-EL1 system register context tp mm_ctx->cpu_ctx.
 * 2. Restores the current C runtime state (callee saved registers) from the
 *    stack frame using the reference to this state saved in mmd_enter_sp().
 * 3. It does not need to save any general purpose or EL3 system register state
 *    as the generic smc entry routine should have saved those.
 ******************************************************************************/
void mmd_synchronous_sp_exit(mm_context_t *mm_ctx, uint64_t ret)
{
	assert(mm_ctx != NULL);

	/* Save the Secure EL1 system register context */
	assert(cm_get_context(SECURE) == &mm_ctx->cpu_ctx);
	cm_el1_sysregs_context_save(SECURE);

	mmd_setup_next_eret_into_sel0(&mm_ctx->cpu_ctx);

	assert(mm_ctx->c_rt_ctx != 0);
	mmd_exit_sp(mm_ctx->c_rt_ctx, ret);

	/* Should never reach here */
	assert(0);
}
