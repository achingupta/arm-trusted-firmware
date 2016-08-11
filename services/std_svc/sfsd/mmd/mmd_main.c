/*
 * Copyright (c) 2013-2015, ARM Limited and Contributors. All rights reserved.
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


/*******************************************************************************
 * This is the Secure Payload Dispatcher (SPD). The dispatcher is meant to be a
 * plug-in component to the Secure Monitor, registered as a runtime service. The
 * SPD is expected to be a functional extension of the Secure Payload (SP) that
 * executes in Secure EL1. The Secure Monitor will delegate all SMCs targeting
 * the Trusted OS/Applications range to the dispatcher. The SPD will either
 * handle the request locally or delegate it to the Secure Payload. It is also
 * responsible for initialising and maintaining communication with the SP.
 ******************************************************************************/
#include <arch_helpers.h>
#include <assert.h>
#include <bl_common.h>
#include <bl31.h>
#include <context_mgmt.h>
#include <debug.h>
#include <errno.h>
#include <platform.h>
#include <runtime_svc.h>
#include <stddef.h>
#include <string.h>
#include <uuid.h>
#include <mmd.h>
#include "mmd_private.h"

/*******************************************************************************
 * Array to keep track of per-cpu Secure Payload state
 ******************************************************************************/
mm_context_t mm_context[MMD_CORE_COUNT];

/*******************************************************************************
 * Array to keep track of handlers registered for global events.
 ******************************************************************************/
uintptr_t mmd_global_event_handlers[EVENT_FID_MAX];

/*******************************************************************************
 * Array to keep track of handlers registered for per-cpu events.
 ******************************************************************************/
uintptr_t mmd_local_event_handlers[MMD_CORE_COUNT][EVENT_FID_MAX];

int32_t mmd_init(void);

/*******************************************************************************
 * Secure Payload Dispatcher setup. The SPD finds out the SP entrypoint and type
 * (aarch32/aarch64) if not already known and initialises the context for entry
 * into the SP for its initialisation.
 ******************************************************************************/
int32_t mmd_setup(void)
{
	entry_point_info_t *mm_ep_info;
	uint32_t linear_id;

	linear_id = plat_my_core_pos();

	/*
	 * Get information about the Secure Payload (BL32) image. Its
	 * absence is a critical failure.  TODO: Add support to
	 * conditionally include the SPD service
	 */
	mm_ep_info = bl31_plat_get_next_image_ep_info(SECURE);
	if (!mm_ep_info) {
		WARN("No MM provided by BL2 boot loader, Booting device"
			" without MM initialization. SMC`s destined for MM"
			" will return SMC_UNK\n");
		return 1;
	}

	/*
	 * If there's no valid entry point for SP, we return a non-zero value
	 * signalling failure initializing the service. We bail out without
	 * registering any handlers
	 */
	if (!mm_ep_info->pc)
		return 1;

	/*
	 * We could inspect the SP image and determine its execution
	 * state i.e whether AArch32 or AArch64. Assuming it's AArch64
	 * for the time being.
	 */
	mmd_init_mm_ep_state(mm_ep_info, &mm_context[linear_id]);

	/*
	 * All MMD initialization done. Now register our init function with
	 * BL31 for deferred invocation
	 */
	bl31_register_bl32_init(&mmd_init);
	return 0;
}

/*******************************************************************************
 * This function passes control to the Secure Payload image (BL32) for the first
 * time on the primary cpu after a cold boot. It assumes that a valid secure
 * context has already been created by mmd_setup() which can be directly used.
 * It also assumes that a valid non-secure context has been initialised by PSCI
 * so it does not need to save and restore any non-secure state. This function
 * performs a synchronous entry into the Secure payload. The SP passes control
 * back to this routine through a SMC.
 ******************************************************************************/
int32_t mmd_init(void)
{
	uint32_t linear_id = plat_my_core_pos();
	mm_context_t *mm_ctx = &mm_context[linear_id];
	entry_point_info_t *mm_entry_point;
	uint64_t rc;

	/*
	 * Get information about the Secure Payload (BL32) image. Its
	 * absence is a critical failure.
	 */
	mm_entry_point = bl31_plat_get_next_image_ep_info(SECURE);
	assert(mm_entry_point);

	/* Setup the S-EL1/0 context */
	mmd_init_sel0_context(mm_entry_point, BL32_BASE);

	/*
	 * Arrange for an entry into the test secure payload. It will be
	 * returned via MM_ENTRY_DONE case
	 */
	rc = mmd_synchronous_sp_entry(mm_ctx);
	assert(rc == 0);

	return rc;
}


uint64_t mmd_ns_smc_handler(uint32_t smc_fid,
			    uint64_t x1,
			    uint64_t x2,
			    uint64_t x3,
			    uint64_t x4,
			    void *cookie,
			    void *handle,
			    uint64_t flags)
{
	unsigned int linear_id = plat_my_core_pos();
	uintptr_t handler;
	mm_context_t *mm_ctx = &mm_context[linear_id];
	assert(handle == cm_get_context(NON_SECURE));

	switch (smc_fid) {
	case MM_GET_NS_BUFFER_AARCH32:
	case MM_GET_NS_BUFFER_AARCH64:
		SMC_RET3(handle, 0,
			 ARM_SECURE_NS_DRAM1_BASE,
			 ARM_SECURE_NS_DRAM1_SIZE);

	case MM_COMMUNICATE_AARCH32:
	case MM_COMMUNICATE_AARCH64:
		/*
		 * Obtain a reference to the registered handler and check if the
		 * handler is present.
		 */
		handler = mmd_global_event_handlers[EVENT_FID_MM_COMMUNICATE_SMC];
		if (!handler)
			SMC_RET1(handle, ENOTSUP);

		/* Save the Normal world context */
		cm_el1_sysregs_context_save(NON_SECURE);

		/*
		 * TODO: In case of a preallocated normal world buffer, it
		 * should be possible to perform a bounds check on it here in
		 * EL3 instead of the MM entry point.
		 */

		/*
		 * Restore the normal world context and prepare for entry in
		 * S-EL0
		 */
		assert(&mm_ctx->cpu_ctx == cm_get_context(SECURE));
		cm_el1_sysregs_context_restore(SECURE);
		cm_set_elr_el3(SECURE, handler);
		cm_set_next_eret_context(SECURE);

		/*
		 * TODO: Print a warning if X2 is not NULL since that is the
		 * recommended approach
		 */
		SMC_RET3(&mm_ctx->cpu_ctx,
			 EVENT_ID_MAKE(EVENT_FID_MM_COMMUNICATE_SMC,
				       EVENT_TYPE_GLOBAL),
			 plat_my_core_pos(),
			 x1);

	default:
		break;
	}

	return SMC_UNK;
}

unsigned int mmd_verify_event_id_request(unsigned int event_id, uintptr_t **entry)

{
	unsigned int fid, linear_id = plat_my_core_pos();

	if (entry == NULL)
		return EINVAL;

	/* Check whether the FID is within range */
	fid = event_fid_get(event_id);
	if (fid >= EVENT_FID_MAX)
		return EINVAL;

	/* Get a pointer to the handler entry */
	if (event_type_get(event_id) == EVENT_TYPE_GLOBAL)
		*entry = &mmd_global_event_handlers[fid];
	else
		*entry = &mmd_local_event_handlers[linear_id][fid];

	return 0;
}


uint64_t mmd_secure_smc_handler(uint32_t smc_fid,
				uint64_t x1,
				uint64_t x2,
				uint64_t x3,
				uint64_t x4,
				void *cookie,
				void *handle,
				uint64_t flags)
{
	unsigned int linear_id = plat_my_core_pos();
	unsigned int ret;
	uintptr_t *entry;
	mm_context_t *mm_ctx = &mm_context[linear_id];
	cpu_context_t *ns_cpu_context;

	assert(handle == cm_get_context(SECURE));

	/*
	 * TODO: Upon entry from S-EL1, always set the ELR_EL3 and SPSR_EL3 to
	 * ELR_EL1 and SPSR_EL1. This ensures that the next secure ERET is into
	 * S-EL0 instead of S-EL1
	 */

	switch (smc_fid) {
	/*
	 * This function ID is used by the MM Foundation to indicate it has
	 * finished initialising itself after a cold boot
	 */
	case MM_INIT_COMPLETE_AARCH32:
	case MM_INIT_COMPLETE_AARCH64:
		/*
		 * SP reports completion. The SPD must have initiated
		 * the original request through a synchronous entry
		 * into the SP. Jump back to the original C runtime
		 * context.
		 */
		mmd_synchronous_sp_exit(mm_ctx, x1);

	case MM_EVENT_COMPLETE_AARCH32:
	case MM_EVENT_COMPLETE_AARCH64:
		/*
		 * This is the result from the MM image of an
		 * earlier request. The results are in x1-x3. Copy it
		 * into the non-secure context, save the secure state
		 * and return to the non-secure state.
		 */
		assert(handle == cm_get_context(SECURE));
		cm_el1_sysregs_context_save(SECURE);
		mmd_setup_next_eret_into_sel0(handle);

		/* Get a reference to the non-secure context */
		ns_cpu_context = cm_get_context(NON_SECURE);
		assert(ns_cpu_context);

		/* Restore non-secure state */
		cm_el1_sysregs_context_restore(NON_SECURE);
		cm_set_next_eret_context(NON_SECURE);

		/* Return to normal world */
		SMC_RET1(ns_cpu_context, x1);

		break;

	case MM_EVENT_UNMAP_MEMORY_AARCH32:
	case MM_EVENT_UNMAP_MEMORY_AARCH64:
		break;

	case MM_EVENT_MAP_MEMORY_AARCH32:
	case MM_EVENT_MAP_MEMORY_AARCH64:
		break;

	case MM_EVENT_GET_CONTEXT_AARCH32:
	case MM_EVENT_GET_CONTEXT_AARCH64:
		break;

	case MM_EVENT_UNREGISTER_AARCH32:
	case MM_EVENT_UNREGISTER_AARCH64:
		ret = mmd_verify_event_id_request(x1, &entry);

		/* If the entry is already allocated then de-allocate it. */
		if (!ret && *entry)
			*entry = 0;

		mmd_setup_next_eret_into_sel0(handle);
		SMC_RET1(handle, ret);

	case MM_EVENT_REGISTER_AARCH32:
	case MM_EVENT_REGISTER_AARCH64:
		ret = mmd_verify_event_id_request(x1, &entry);

		/*
		 * Check if the entry is already allocated else allocate it.
		 * TODO: Do a bounds check on the handler pointer
		 */
		if (!ret && !*entry)
			*entry = x2;

		mmd_setup_next_eret_into_sel0(handle);
		SMC_RET1(handle, ret);

	default:
		break;
	}

	SMC_RET1(handle, SMC_UNK);
}

/*******************************************************************************
 * This function is responsible for handling all SMCs in the Trusted OS/App
 * range from the non-secure state as defined in the SMC Calling Convention
 * Document. It is also responsible for communicating with the Secure payload
 * to delegate work and return results back to the non-secure state. Lastly it
 * will also return any information that the secure payload needs to do the
 * work assigned to it.
 ******************************************************************************/
uint64_t mmd_smc_handler(uint32_t smc_fid,
			 uint64_t x1,
			 uint64_t x2,
			 uint64_t x3,
			 uint64_t x4,
			 void *cookie,
			 void *handle,
			 uint64_t flags)
{
	uint32_t ns;

	/* Determine which security state this SMC originated from */
	ns = is_caller_non_secure(flags);
	if (ns)
		return mmd_ns_smc_handler(smc_fid, x1, x2, x3, x4,
					  cookie, handle, flags);
	else
		return mmd_secure_smc_handler(smc_fid, x1, x2, x3, x4,
					      cookie, handle, flags);
}
