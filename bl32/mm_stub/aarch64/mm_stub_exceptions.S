/*
 * Copyright (c) 2013-2016, ARM Limited and Contributors. All rights reserved.
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

#include <bl_common.h>
#include <arch.h>
#include <asm_macros.S>

	.globl	mm_stub_exceptions

	/* -----------------------------------------------------
	 * mm_stub exception handlers.
	 * -----------------------------------------------------
	 */
	.section	.vectors, "ax"; .align 11

	.align	7
mm_stub_exceptions:
	/* -----------------------------------------------------
	 * Current EL with _sp_el0 : 0x0 - 0x180. No exceptions
	 * are expected and treated as irrecoverable errors.
	 * -----------------------------------------------------
	 */
sync_exception_sp_el0:
	bl	plat_panic_handler
	check_vector_size sync_exception_sp_el0

	.align	7

irq_sp_el0:
	bl	plat_panic_handler
	check_vector_size irq_sp_el0

	.align	7
fiq_sp_el0:
	bl	plat_panic_handler
	check_vector_size fiq_sp_el0

	.align	7
serror_sp_el0:
	bl	plat_panic_handler
	check_vector_size serror_sp_el0


	/* -----------------------------------------------------
	 * Current EL with SPx: 0x200 - 0x380. Only IRQs/FIQs
	 * are expected and handled
	 * -----------------------------------------------------
	 */
	.align	7
sync_exception_sp_elx:
	bl	plat_panic_handler
	check_vector_size sync_exception_sp_elx

	.align	7
irq_sp_elx:
	bl	plat_panic_handler
	check_vector_size irq_sp_elx

	.align	7
fiq_sp_elx:
	bl	plat_panic_handler
	check_vector_size fiq_sp_elx

	.align	7
serror_sp_elx:
	bl	plat_panic_handler
	check_vector_size serror_sp_elx


	/* -----------------------------------------------------
	 * Lower EL using AArch64 : 0x400 - 0x580. No exceptions
	 * are handled since mm_stub does not implement a lower EL
	 * -----------------------------------------------------
	 */
	.align	7
sync_exception_aarch64:
	msr	daifclr, #DAIF_ABT_BIT

	msr	tpidr_el1, x30
	mrs	x30, esr_el1
	ubfx	x30, x30, #ESR_EC_SHIFT, #ESR_EC_LENGTH

	cmp	x30, #EC_AARCH64_SVC
	b.eq 	do_smc

	cmp	x30, #EC_AARCH32_SVC
	b.ne	plat_panic_handler

	/* ---------------------------------------------
	 * Tell MMD that we are done initialising
	 * ---------------------------------------------
	 */
do_smc:
	mrs	x30, tpidr_el1
	smc	#0
	b	plat_panic_handler
	check_vector_size sync_exception_aarch64

	.align	7
irq_aarch64:
	bl	plat_panic_handler
	check_vector_size irq_aarch64

	.align	7
fiq_aarch64:
	bl	plat_panic_handler
	check_vector_size fiq_aarch64

	.align	7
serror_aarch64:
	bl	plat_panic_handler
	check_vector_size serror_aarch64


	/* -----------------------------------------------------
	 * Lower EL using AArch32 : 0x600 - 0x780. No exceptions
	 * handled since the mm_stub does not implement a lower EL.
	 * -----------------------------------------------------
	 */
	.align	7
sync_exception_aarch32:
	bl	plat_panic_handler
	check_vector_size sync_exception_aarch32

	.align	7
irq_aarch32:
	bl	plat_panic_handler
	check_vector_size irq_aarch32

	.align	7
fiq_aarch32:
	bl	plat_panic_handler
	check_vector_size fiq_aarch32

	.align	7
serror_aarch32:
	bl	plat_panic_handler
	check_vector_size serror_aarch32
	.align	7
