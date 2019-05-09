/*
 * Copyright (c) 2017-2018, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <arch_helpers.h>
#include <bl31/bl31.h>
#include <bl31/ehf.h>
#include <bl31/interrupt_mgmt.h>
#include <common/debug.h>
#include <common/runtime_svc.h>
#include <lib/el3_runtime/context_mgmt.h>
#include <lib/smccc.h>
#include <lib/spinlock.h>
#include <lib/utils.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <plat/common/platform.h>
#include <services/sprt_svc.h>
#include <smccc_helpers.h>
#if ENABLE_SPCI_ALPHA2
#include <services/spci_alpha2.h>
#endif
#include "spm_private.h"

/*******************************************************************************
 * Secure Partition context information.
 ******************************************************************************/
sp_context_t sp_ctx_array[PLAT_SPM_MAX_PARTITIONS];

/* Last Secure Partition last used by the CPU */
sp_context_t *cpu_sp_ctx[PLATFORM_CORE_COUNT];

void spm_cpu_set_sp_ctx(unsigned int linear_id, sp_context_t *sp_ctx)
{
	assert(linear_id < PLATFORM_CORE_COUNT);

	cpu_sp_ctx[linear_id] = sp_ctx;
}

sp_context_t *spm_cpu_get_sp_ctx(unsigned int linear_id)
{
	assert(linear_id < PLATFORM_CORE_COUNT);

	return cpu_sp_ctx[linear_id];
}

/*******************************************************************************
 * Functions to keep track of how many requests a Secure Partition has received
 * and hasn't finished.
 ******************************************************************************/
void spm_sp_request_increase(sp_context_t *sp_ctx)
{
	spin_lock(&(sp_ctx->request_count_lock));
	sp_ctx->request_count++;
	spin_unlock(&(sp_ctx->request_count_lock));
}

void spm_sp_request_decrease(sp_context_t *sp_ctx)
{
	spin_lock(&(sp_ctx->request_count_lock));
	sp_ctx->request_count--;
	spin_unlock(&(sp_ctx->request_count_lock));
}

/* Returns 0 if it was originally 0, -1 otherwise. */
int spm_sp_request_increase_if_zero(sp_context_t *sp_ctx)
{
	int ret = -1;

	spin_lock(&(sp_ctx->request_count_lock));
	if (sp_ctx->request_count == 0U) {
		sp_ctx->request_count++;
		ret = 0U;
	}
	spin_unlock(&(sp_ctx->request_count_lock));

	return ret;
}

/*******************************************************************************
 * This function returns a pointer to the context of the Secure Partition that
 * handles the service specified by an UUID. It returns NULL if the UUID wasn't
 * found.
 ******************************************************************************/
sp_context_t *spm_sp_get_by_uuid(const uint32_t (*svc_uuid)[4])
{
	unsigned int i;

	for (i = 0U; i < PLAT_SPM_MAX_PARTITIONS; i++) {

		sp_context_t *sp_ctx = &sp_ctx_array[i];

		if (sp_ctx->is_present == 0) {
			continue;
		}

		struct sp_rd_sect_service *rdsvc;

		for (rdsvc = sp_ctx->rd.service; rdsvc != NULL;
		     rdsvc = rdsvc->next) {
			uint32_t *rd_uuid = (uint32_t *)(rdsvc->uuid);

			if (memcmp(rd_uuid, svc_uuid, sizeof(rd_uuid)) == 0) {
				return sp_ctx;
			}
		}
	}

	return NULL;
}

/*******************************************************************************
 * Set state of a Secure Partition context.
 ******************************************************************************/
void sp_state_set(sp_context_t *sp_ptr, sp_state_t state)
{
	spin_lock(&(sp_ptr->state_lock));
	sp_ptr->state = state;
	spin_unlock(&(sp_ptr->state_lock));
}

/*******************************************************************************
 * Wait until the state of a Secure Partition is the specified one and change it
 * to the desired state.
 ******************************************************************************/
void sp_state_wait_switch(sp_context_t *sp_ptr, sp_state_t from, sp_state_t to)
{
	int success = 0;

	while (success == 0) {
		spin_lock(&(sp_ptr->state_lock));

		if (sp_ptr->state == from) {
			sp_ptr->state = to;

			success = 1;
		}

		spin_unlock(&(sp_ptr->state_lock));
	}
}

/*******************************************************************************
 * Check if the state of a Secure Partition is the specified one and, if so,
 * change it to the desired state. Returns 0 on success, -1 on error.
 ******************************************************************************/
int sp_state_try_switch(sp_context_t *sp_ptr, sp_state_t from, sp_state_t to)
{
	int ret = -1;

	spin_lock(&(sp_ptr->state_lock));

	if (sp_ptr->state == from) {
		sp_ptr->state = to;

		ret = 0;
	}

	spin_unlock(&(sp_ptr->state_lock));

	return ret;
}

/*******************************************************************************
 * This function takes an SP context pointer and performs a synchronous entry
 * into it.
 ******************************************************************************/
uint64_t spm_sp_synchronous_entry(sp_context_t *sp_ctx, int can_preempt)
{
	uint64_t rc;
	unsigned int linear_id = plat_my_core_pos();

	assert(sp_ctx != NULL);

	/* Assign the context of the SP to this CPU */
	spm_cpu_set_sp_ctx(linear_id, sp_ctx);
	cm_set_context(&(sp_ctx->cpu_ctx), SECURE);

	/* Restore the context assigned above */
	cm_el1_sysregs_context_restore(SECURE);
	cm_set_next_eret_context(SECURE);

	/* Invalidate TLBs at EL1. */
	tlbivmalle1();
	dsbish();

#if !ENABLE_SPCI_ALPHA2
	if (can_preempt == 1) {
		enable_intr_rm_local(INTR_TYPE_NS, SECURE);
	} else {
		disable_intr_rm_local(INTR_TYPE_NS, SECURE);
	}
#endif

	/* Enter Secure Partition */
	rc = spm_secure_partition_enter(&sp_ctx->c_rt_ctx);

	/* Save secure state */
	cm_el1_sysregs_context_save(SECURE);

	return rc;
}

/*******************************************************************************
 * This function returns to the place where spm_sp_synchronous_entry() was
 * called originally.
 ******************************************************************************/
__dead2 void spm_sp_synchronous_exit(uint64_t rc)
{
	/* Get context of the SP in use by this CPU. */
	unsigned int linear_id = plat_my_core_pos();
	sp_context_t *ctx = spm_cpu_get_sp_ctx(linear_id);

	/*
	 * The SPM must have initiated the original request through a
	 * synchronous entry into the secure partition. Jump back to the
	 * original C runtime context with the value of rc in x0;
	 */
	spm_secure_partition_exit(ctx->c_rt_ctx, rc);

	panic();
}

#if !ENABLE_SPCI_ALPHA2
/*******************************************************************************
 * This function is the handler registered for Non secure interrupts by the SPM.
 * It validates the interrupt and upon success arranges entry into the normal
 * world for handling the interrupt.
 ******************************************************************************/
static uint64_t spm_ns_interrupt_handler(uint32_t id, uint32_t flags,
					  void *handle, void *cookie)
{
	/* Check the security state when the exception was generated */
	assert(get_interrupt_src_ss(flags) == SECURE);

	spm_sp_synchronous_exit(SPM_SECURE_PARTITION_PREEMPTED);
}
#endif

/*******************************************************************************
 * Jump to each Secure Partition for the first time.
 ******************************************************************************/
static int32_t spm_init(void)
{
	uint64_t rc = 0;
	sp_context_t *ctx;

	for (unsigned int i = 0U; i < PLAT_SPM_MAX_PARTITIONS; i++) {

		ctx = &sp_ctx_array[i];

		if (ctx->is_present == 0) {
			continue;
		}

		INFO("Secure Partition %u init...\n", i);

		ctx->state = SP_STATE_RESET;

		rc = spm_sp_synchronous_entry(ctx, 0);
#if ENABLE_SPCI_ALPHA2
		if (rc == SPCI_SUCCESS) {
#else
		if (rc != SPRT_YIELD_AARCH64) {
#endif
			ERROR("Unexpected return value 0x%llx\n", rc);
			panic();
		}

		ctx->state = SP_STATE_IDLE;

		INFO("Secure Partition %u initialized.\n", i);
	}

	return rc;
}

/*******************************************************************************
 * Initialize contexts of all Secure Partitions.
 ******************************************************************************/
#if ENABLE_SPCI_ALPHA2
static void spci_msg_buf_init(spci_buf_t **msg_buf_ptr,
			      buf_t *buf_ptr)
{
	spci_buf_hdr_t *msg_buf_hdr;

	VERBOSE("Message buffer : 0x%lx \n", (unsigned long) buf_ptr);

	/* Zero the buffer memory */
	memset((void *) buf_ptr, 0, PAGE_SIZE);

	/* Assign reference to this buffer for this SP */
	*msg_buf_ptr = (spci_buf_t *) buf_ptr;

	/* Initialise the buffer header */
	msg_buf_hdr = &(*msg_buf_ptr)->hdr;
	msg_buf_hdr->state = SPCI_BUF_STATE_EMPTY;
	msg_buf_hdr->page_count = sizeof(buf_t) >> PAGE_SIZE_SHIFT ;
	memcpy((void *) msg_buf_hdr->signature,
	       SPCI_BUF_SIGNATURE,
	       MAX_SIG_LENGTH);

	/*
	 * Flush the buffer memory so that the target SP can see the buffer
	 * header when its MMU is not enabled.
	 */
	flush_dcache_range((uintptr_t) buf_ptr, PAGE_SIZE);

	return;
}

static void spci_msg_bufs_init(sp_context_t *ctx, unsigned int ss,
			       rxtx_buf_ptr rxtx_buf_pool_start)
{
	uint16_t id;
	spci_buf_t **msg_buf_ptr;


	assert(ctx != NULL);
	assert(ctx->sp_id < PLAT_SPM_MAX_PARTITIONS);
	assert(rxtx_buf_pool_start != 0UL);

	id = ctx->sp_id;

	/* Assign and initialise a RX buffer for this partition */
	msg_buf_ptr = &ctx->msg_bufs[ss][SPCI_BUF_RX];
	spci_msg_buf_init(msg_buf_ptr,
			  &rxtx_buf_pool_start[id]->buf[SPCI_BUF_RX]);


	/* Assign and initialise a TX buffer for this partition */
	msg_buf_ptr = &ctx->msg_bufs[ss][SPCI_BUF_TX];
	spci_msg_buf_init(msg_buf_ptr,
			  &rxtx_buf_pool_start[id]->buf[SPCI_BUF_TX]);

	return;
}

void mem_reg_desc_init(spci_mem_reg_desc_t *desc,
			      uint64_t addr,
			      uint16_t pg_cnt,
			      uint32_t attr)
{
	assert(desc != NULL);

	VERBOSE("desc: 0x%lx, offset of : 0x%lx\n", (unsigned long) desc,
		offsetof(spci_msg_sp_init_t, mem_regs));

	desc->address = addr;
	desc->page_count = pg_cnt;
	desc->attributes = attr;

	return;
}

void init_msg_mem_reg_add(spci_msg_sp_init_t *msg,
				 uint64_t addr,
				 uint16_t pg_cnt,
				 uint32_t attr)
{
	spci_mem_reg_desc_t *desc;

	assert(msg != NULL);

	VERBOSE("msg: 0x%lx, offset of : 0x%lx\n", (unsigned long) msg,
		offsetof(spci_msg_sp_init_t, mem_regs));

	desc = msg->mem_regs;

	mem_reg_desc_init(&desc[msg->mem_reg_count], addr, pg_cnt, attr);
	msg->mem_reg_count++;

	return;
}

static void init_msg_mem_reg_dump(spci_msg_sp_init_t *msg)
{
	spci_mem_reg_desc_t *desc;
	unsigned int ctr;

	assert(msg != NULL);

	desc = msg->mem_regs;

	for (ctr = 0; ctr < msg->mem_reg_count; ctr++) {
		VERBOSE("[%d]: sp_mem_reg = %p\n", ctr, (void *) &desc[ctr]);
		VERBOSE("|->address       = %p\n", (void *) desc[ctr].address);
		VERBOSE("|->page_count    = 0x%x\n", desc[ctr].page_count);
		VERBOSE("|->attr          = 0x%x\n", desc[ctr].attributes);
	}

	return;
}

static void spci_init_msg_create(spci_buf_t *rx_buf_ptr, uint16_t sp_id)
{
	spci_msg_hdr_t *sp_init_msg_hdr;
	spci_arch_msg_hdr_t *sp_init_msg_arch_hdr;
	spci_msg_sp_init_t *sp_init_msg;
	struct sp_rd_sect_mem_region *rdmem;
	sp_context_t *ctx;
	unsigned int ctr0, ctr1;
	uint64_t addr;
	uint16_t pg_cnt;
	uint32_t attr;

	/* Populate the common message header in RX buffer*/
	sp_init_msg_hdr = (spci_msg_hdr_t *) &rx_buf_ptr->buf;
	sp_init_msg_hdr->version =
		SPCI_MSG_VER(SPCI_MSG_VER_MAJ, SPCI_MSG_VER_MIN);
	sp_init_msg_hdr->flags =
		SPCI_MSG_TYPE(SPCI_MSG_TYPE_ARCH);
	sp_init_msg_hdr->target_sp = sp_id;

	/* Populate the arch. message header */
	sp_init_msg_arch_hdr =
		(spci_arch_msg_hdr_t *) sp_init_msg_hdr->payload;
	sp_init_msg_arch_hdr->type =
		SPCI_ARCH_MSG_TYPE(SPCI_ARCH_MSG_TYPE_SP_INIT);

	/* Populate the SP init. message header */
	sp_init_msg =(spci_msg_sp_init_t *)
		sp_init_msg_arch_hdr->payload;
	sp_init_msg->version = INIT_MSG_VER(INIT_MSG_VER_MAJ,
					    INIT_MSG_VER_MIN);
	sp_init_msg->mem_reg_count = 0;

	/*
	 * Populate regions specified in SP's resource
	 * description.
	 * TODO: Do a bounds check before copying each region.
	 */
	ctx = &sp_ctx_array[sp_id];
	for (rdmem = ctx->rd.mem_region; rdmem != NULL; rdmem = rdmem->next) {
		/* Size of memory region must be page aligned */
		assert ((rdmem->size & (PAGE_SIZE - 1)) == 0U);

		addr = rdmem->base;
		pg_cnt = (rdmem->size) >> PAGE_SIZE_SHIFT;
		attr = SPCI_MEM_REG_IMP(rdmem->imp_def_attr);

		VERBOSE("RD mem region addr: 0x%llx \n", addr);
		VERBOSE("RD mem region attr: 0x%x \n", attr);
		VERBOSE("RD mem region pgcn: 0x%x \n", pg_cnt);

		/* Copy region attributes into message */
		init_msg_mem_reg_add(sp_init_msg, addr, pg_cnt, attr);
	}

	/* Describe secure and non-secure RX/TX buffers for this partition */
	for (ctr0 = 0; ctr0 < SPCI_MAX_SEC_STATES; ctr0++)
		for (ctr1 = 0; ctr1 < SPCI_MAX_BUFS; ctr1++) {
			spci_buf_t *buf;

			buf = ctx->msg_bufs[ctr0][ctr1];
			if (!buf)
				continue;
			addr = (uint64_t) buf;
			pg_cnt = buf->hdr.page_count;
			attr = SPCI_MEM_REG_ARCH(ctr1, ctr0,		\
						 SPCI_MEM_REG_ARCH_GRAN_4K);

			VERBOSE("SPCI buf addr: 0x%llx \n", addr);
			VERBOSE("SPCI buf attr: 0x%x \n", attr);
			VERBOSE("SPCI buf pgcn: 0x%x \n", pg_cnt);

			init_msg_mem_reg_add(sp_init_msg, addr, pg_cnt, attr);
		}

	init_msg_mem_reg_dump(sp_init_msg);

	sp_init_msg_hdr->length = sizeof(spci_arch_msg_hdr_t);
	sp_init_msg_hdr->length += sizeof(spci_msg_sp_init_t);
	sp_init_msg_hdr->length += sp_init_msg->mem_reg_count *
		sizeof(spci_mem_reg_desc_t);
	VERBOSE("SP init message length = 0x%x bytes \n",
		sp_init_msg_hdr->length);

	rx_buf_ptr->hdr.state = SPCI_BUF_STATE_FULL;

	/*
	 * Flush the message contents so that the target SP can see it without
	 * the MMU enabled
	 */
	flush_dcache_range((uintptr_t) rx_buf_ptr,
			   sizeof(spci_buf_t) + sp_init_msg_hdr->length);

	return;
}

int32_t spm_setup(void)
{
	int rc;
	sp_context_t *ctx;
	void *rd_base;
	size_t rd_size;
	unsigned int i = 0U;
	entry_point_info_t *sp_ep_info;
	uintptr_t rd_base_align;
	uintptr_t rd_size_align;
	uint32_t ep_attr;
	uint32_t sel2_enabled, mode = MODE_EL1;

	/*
	 * Check if SEL2 has been enabled. In this case, this SPM will only
	 * dispatch SPCI requests to the SPM in SEL2.
	 *
	 * TODO: Only a jump into SEL2 is supported right now. Do the rest
	 * later.
	 */
	sel2_enabled = read_scr_el3() & SCR_EEL2_BIT;
	if (sel2_enabled)
		mode = MODE_EL2;

	sp_ep_info = bl31_plat_get_next_image_ep_info(SECURE);
	if (!sp_ep_info) {
		WARN("No SP provided by BL2 boot loader, Booting device"
			" without SP initialization. SMC`s destined for SP"
			" will return SMC_UNK\n");
		return 1;
	}

	/* Under no circumstances will this parameter be 0 */
	assert (sp_ep_info->pc != 0U);

	/*
	 * Check if BL32 ep_info has a reference to 'tos_fw_config' in which
	 * case there is a S-EL1 SP instead of multiple S-EL0 SPs.
	 */
	if (sp_ep_info->args.arg0 == 0U || sp_ep_info->args.arg2 == 0U) {
		ERROR("Invalid or absent 'tos_fw_config' \n");
		panic();
	}

	/* Obtain whereabouts of BL32 resource description */
	rd_base = (void *) sp_ep_info->args.arg0;
	rd_size = sp_ep_info->args.arg2;

	rd_base_align = page_align((uintptr_t) rd_base, DOWN);
	rd_size_align = page_align((uintptr_t) rd_size, UP);

	/* Map the RD memory in the SPM translation regime first */
	VERBOSE("RD base : 0x%lx \n", rd_base_align);
	VERBOSE("RD size : 0x%lx \n", rd_size_align);
	rc = mmap_add_dynamic_region((unsigned long long) rd_base_align,
				     (uintptr_t) rd_base_align,
				     rd_size_align,
				     MT_RO_DATA);
	if (rc < 0) {
		ERROR("Error while mapping RD blob (%d).\n", rc);
		panic();
	}

	ctx = &sp_ctx_array[i];

	/* Size of RD can be read from DTB itself */
	rc = plat_spm_sp_rd_load(&ctx->rd,
				 (void *) sp_ep_info->args.arg0,
				 0);
	if (rc < 0) {
		ERROR("Error while loading RD blob.\n");
		panic();
	}

	/* Assign an identity to this partition */
	ctx->sp_id = i;

	/* Initialize the secure RX/TX buffers for this SP */
	VERBOSE("SPCI buffer pool base : 0x%lx \n", SPCI_MSG_BUFS_SEC_START);
	spci_msg_bufs_init(ctx, SECURE,
			   (rxtx_buf_ptr) SPCI_MSG_BUFS_SEC_START);

	/* Initialize the non-secure RX/TX buffers for this SP */
	VERBOSE("SPCI buffer pool base : 0x%llx \n", SPCI_MSG_BUFS_NSEC_START);
	spci_msg_bufs_init(ctx, !SECURE,
			   (rxtx_buf_ptr) SPCI_MSG_BUFS_NSEC_START);

	if (rc < 0) {
		ERROR("Error (0x%x) in secure msg buf init.\n", rc);
		panic();
	}

	VERBOSE("Creating SP init message\n");
	spci_init_msg_create(ctx->msg_bufs[SECURE][SPCI_BUF_RX], i);

	/* Initialise an entrypoint to set up the CPU context */
	ep_attr = SECURE | EP_ST_ENABLE;
	if (read_sctlr_el3() & SCTLR_EE_BIT)
		ep_attr |= EP_EE_BIG;
	SET_PARAM_HEAD(sp_ep_info, PARAM_EP, VERSION_1, ep_attr);
	assert (sp_ep_info->pc == BL32_BASE);
	sp_ep_info->spsr = SPSR_64(mode, MODE_SP_ELX,
				   DISABLE_ALL_EXCEPTIONS);

	zeromem(&sp_ep_info->args, sizeof(sp_ep_info->args));

	/*
	 * SPCI_MSG_RECV returns SUCCESS with a message in the
	 * secure RX buffer
	 */
	sp_ep_info->args.arg0 = SPCI_SUCCESS;
	sp_ep_info->args.arg1 = (u_register_t) ctx->msg_bufs[SECURE][SPCI_BUF_RX];

	/*
	 * Initialise S-EL1 SP context with this entry point
	 * information
	 */
	cm_setup_context(&(ctx->cpu_ctx), sp_ep_info);

	ctx->is_present = 1;

	INFO("S-EL1 Secure Partition %u setup done.\n", i);

	/* Register init function for deferred init.  */
	bl31_register_bl32_init(&spm_init);

	return 0;
}
#else
int32_t spm_setup(void)
{
	int rc;
	sp_context_t *ctx;
	void *sp_base, *rd_base;
	size_t sp_size, rd_size;
	uint64_t flags = 0U;

	/* Disable MMU at EL1 (initialized by BL2) */
	disable_mmu_icache_el1();

	/*
	 * Non-blocking services can be interrupted by Non-secure interrupts.
	 * Register an interrupt handler for NS interrupts when generated while
	 * the CPU is in secure state. They are routed to EL3.
	 */
	set_interrupt_rm_flag(flags, SECURE);

	uint64_t rc_int = register_interrupt_type_handler(INTR_TYPE_NS,
				spm_ns_interrupt_handler, flags);
	if (rc_int) {
		ERROR("SPM: Failed to register NS interrupt handler with rc = %llx\n",
		      rc_int);
		panic();
	}

	/*
	 * Setup all Secure Partitions.
	 */
	unsigned int i = 0U;

	while (1) {
		rc = plat_spm_sp_get_next_address(&sp_base, &sp_size,
						&rd_base, &rd_size);
		if (rc < 0) {
			/* Reached the end of the package. */
			break;
		}

		if (i >= PLAT_SPM_MAX_PARTITIONS) {
			ERROR("Too many partitions in the package.\n");
			panic();
		}

		ctx = &sp_ctx_array[i];

		assert(ctx->is_present == 0);

		/* Initialize context of the SP */
		INFO("Secure Partition %u context setup start...\n", i);

		/* Assign translation tables context. */
		ctx->xlat_ctx_handle = spm_sp_xlat_context_alloc();

		/* Save location of the image in physical memory */
		ctx->image_base = (uintptr_t)sp_base;
		ctx->image_size = sp_size;

		rc = plat_spm_sp_rd_load(&ctx->rd, rd_base, rd_size);
		if (rc < 0) {
			ERROR("Error while loading RD blob.\n");
			panic();
		}

		spm_sp_setup(ctx);

		ctx->is_present = 1;

		INFO("Secure Partition %u setup done.\n", i);

		i++;
	}

	if (i == 0U) {
		ERROR("No present partitions in the package.\n");
		panic();
	}

	/* Register init function for deferred init.  */
	bl31_register_bl32_init(&spm_init);

	return 0;
}
#endif	/* ENABLE_SPCI_ALPHA2 */
