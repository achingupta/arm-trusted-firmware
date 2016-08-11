/*
 * Copyright (c) 2016, ARM Limited and Contributors. All rights reserved.
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

#include <assert.h>
#include <bl_common.h>
#include <context.h>
#include <context_mgmt.h>
#include <debug.h>
#include <platform.h>
#include <platform_def.h>
#include <string.h>
#include "mmd_private.h"

/*******************************************************************************
 * The following function sets up the S-EL0 execution environment. This involves
 * system register, page table, exception vectors and stack pointer setup.
 ******************************************************************************/
void mmd_init_sel0_context(const entry_point_info_t *mm_ep_info,
			   uintptr_t vbar_el1)
{
	cpu_context_t *ctx;
	unsigned int   linear_id = plat_my_core_pos();
	uintptr_t      sp_base;
	uint64_t       mair, tcr, ttbr;
	uint32_t       sctlr;

	assert(GET_SECURITY_STATE(mm_ep_info->h.attr) == SECURE);

	/* Setup the system register context for entry into S_EL0 */
	cm_init_my_context(mm_ep_info);

	/* Obtain a reference to the secure context */
	ctx = cm_get_context(GET_SECURITY_STATE(mm_ep_info->h.attr));
	assert(ctx);

	/* Set the S-EL1 exception vectors to the trampoline vector table */
	write_ctx_reg(get_sysregs_ctx(ctx), CTX_VBAR_EL1, vbar_el1);

	/* Calculate and set the stack base address for this CPU */
	sp_base = mm_ep_info->args.arg1
		+ ((linear_id + 1) * mm_ep_info->args.arg2);
	write_ctx_reg(get_gpregs_ctx(ctx), CTX_GPREG_SP_EL0, sp_base);
	INFO("mm_stub: mm payload stack base=%p\n", (void *) sp_base);

	/*
	 * Obtain the context for address translation for S-EL1 and save it in
	 * the system register context
	 */
	plat_prepare_mmu_context_el1(&mair, &tcr, &ttbr, &sctlr);

	sctlr |= read_ctx_reg(get_sysregs_ctx(ctx), CTX_SCTLR_EL1);
	mair  |= read_ctx_reg(get_sysregs_ctx(ctx), CTX_MAIR_EL1);
	tcr   |= read_ctx_reg(get_sysregs_ctx(ctx), CTX_TCR_EL1);
	ttbr  |= read_ctx_reg(get_sysregs_ctx(ctx), CTX_TTBR0_EL1);

	write_ctx_reg(get_sysregs_ctx(ctx), CTX_MAIR_EL1, mair);
	write_ctx_reg(get_sysregs_ctx(ctx), CTX_TCR_EL1, tcr);
	write_ctx_reg(get_sysregs_ctx(ctx), CTX_TTBR0_EL1, ttbr);
	write_ctx_reg(get_sysregs_ctx(ctx), CTX_SCTLR_EL1, sctlr);
}
