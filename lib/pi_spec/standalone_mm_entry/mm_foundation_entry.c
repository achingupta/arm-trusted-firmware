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

#include <arch.h>
#include <arch_helpers.h>
#include <assert.h>
#include <bl_common.h>
#include <debug.h>
#include <errno.h>
#include <platform.h>
#include <platform_def.h>
#include <stdint.h>
#include <string.h>

/* UEFI/PI headers */
#include <pi_spec/ProcessorBind.h>
#include <pi_spec/UefiBaseTypes.h>
#include <pi_spec/UefiMultiPhase.h>
#include <pi_spec/PiBootMode.h>
#include <pi_spec/hoblib/PiHob.h>
#include <pi_spec/Guid/SmramMemoryReserve.h>
#include <pi_spec/Guid/MpInformation.h>
#include <pi_spec/standalone_mm_entry/standalone_mm_entry.h>

extern VOID EFIAPI CreateHobList(IN VOID   *MemoryBegin,
				  IN UINTN  MemoryLength,
				  IN VOID   *HobBase,
				  IN VOID   *StackBase);
extern VOID * EFIAPI GetFirstHob(IN UINT16 Type);
extern VOID * EFIAPI BuildGuidDataHob(IN CONST EFI_GUID *Guid,
				      IN VOID *Data,
				      IN UINTN DataLength);
extern VOID EFIAPI BuildFvHob(IN EFI_PHYSICAL_ADDRESS BaseAddress,
			      IN UINT64 Length);

#define MM_PAYLOAD_PCPU_STACK_SIZE	0x1000

EFI_GUID gEfiSmmPeiSmramMemoryReserveGuid = EFI_SMM_PEI_SMRAM_MEMORY_RESERVE;
EFI_GUID gMpInformationHobGuid            = MP_INFORMATION_GUID;

EFI_SMRAM_HOB_DESCRIPTOR_BLOCK mm_mem_regions = { 5, {{0}, {0}, {0}, {0}, {0}}};

MP_INFORMATION_HOB_DATA mpinfo_hob_data = {
	PLATFORM_CORE_COUNT,
	PLATFORM_CORE_COUNT,
	{
		[0] = {0x80000000, 			/* MPIDR */
		       PROCESSOR_AS_BSP_BIT |		/* Healthy, Enabled
		       PROCESSOR_ENABLED_BIT |		 * BSP */
		       PROCESSOR_HEALTH_STATUS_BIT,
		       {0, 0, 0}},			/* Package, Core, Thread
							 * ID to be filled at
							 * runtime */

		[1] = {0x80000001, 			/* MPIDR */
		       PROCESSOR_ENABLED_BIT | 		/* Healthy, Enabled AP */
		       PROCESSOR_HEALTH_STATUS_BIT,
		       {0, 0, 0}},

		[2] = {0x80000002,
		       PROCESSOR_ENABLED_BIT |
		       PROCESSOR_HEALTH_STATUS_BIT,
		       {0, 0, 0}},

		[3] = {0x80000003,
		       PROCESSOR_ENABLED_BIT |
		       PROCESSOR_HEALTH_STATUS_BIT,
		       {0, 0, 0}},

		[4] = {0x80000100,
		       PROCESSOR_ENABLED_BIT |
		       PROCESSOR_HEALTH_STATUS_BIT,
		       {0, 0, 0}},

		[5] = {0x80000101,
		       PROCESSOR_ENABLED_BIT |
		       PROCESSOR_HEALTH_STATUS_BIT,
		       {0, 0, 0}},

		[6] = {0x80000102,
		       PROCESSOR_ENABLED_BIT |
		       PROCESSOR_HEALTH_STATUS_BIT,
		       {0, 0, 0}},

		[7] = {0x80000103,
		       PROCESSOR_ENABLED_BIT |
		       PROCESSOR_HEALTH_STATUS_BIT,
		       {0, 0, 0}}
	}
};

int mm_foundation_entry_prepare(meminfo_t *payload_mem_info,
				image_info_t *payload_image_info,
				entry_point_info_t *payload_ep_info)
{
	EFI_HOB_HANDOFF_INFO_TABLE *hoblist;
	EFI_SMRAM_HOB_DESCRIPTOR_BLOCK *mm_regions_hob_data;
	unsigned int mm_payload_stack_size, mm_mem_regions_size;
	uintptr_t mm_payload_stack_base;
	int index;

	/* Calculate the base addess of the stacks and the total size */
	mm_payload_stack_size = PLATFORM_CORE_COUNT * MM_PAYLOAD_PCPU_STACK_SIZE;
	mm_payload_stack_base =
		payload_mem_info->total_base + payload_mem_info->total_size;
	mm_payload_stack_base -= mm_payload_stack_size;

	/* Reduce the stack size from the remaining free memory */
	payload_mem_info->free_size -= mm_payload_stack_size;

	INFO("BL2: ====SFS_PAYLOAD Information==== \n");
	INFO("BL2: image base=%p\n", (void *) payload_image_info->image_base);
	INFO("BL2: total base=%p\n", (void *) payload_mem_info->total_base);
	INFO("BL2: free base=%p\n",  (void *) payload_mem_info->free_base);
	INFO("BL2: stack base=%p\n",  (void *) mm_payload_stack_base);

	INFO("BL2: image size=0x%x\n", payload_image_info->image_size);
	INFO("BL2: total size=0x%x\n",
		     (unsigned int) payload_mem_info->total_size);
	INFO("BL2: free size=0x%x\n",
	     (unsigned int) payload_mem_info->free_size);
	INFO("BL2: stack size=0x%x\n", mm_payload_stack_size);

	mm_mem_regions.Descriptor[0].PhysicalStart = payload_image_info->image_base;
	mm_mem_regions.Descriptor[0].CpuStart = payload_image_info->image_base;
	mm_mem_regions.Descriptor[0].PhysicalSize = payload_image_info->image_size;
	mm_mem_regions.Descriptor[0].RegionState = EFI_CACHEABLE | EFI_ALLOCATED;

	mm_mem_regions.Descriptor[1].PhysicalStart = mm_payload_stack_base;
	mm_mem_regions.Descriptor[1].CpuStart = mm_payload_stack_base;
	mm_mem_regions.Descriptor[1].PhysicalSize = mm_payload_stack_size;
	mm_mem_regions.Descriptor[1].RegionState = EFI_CACHEABLE | EFI_ALLOCATED;

	mm_mem_regions.Descriptor[2].PhysicalStart = payload_mem_info->free_base;
	mm_mem_regions.Descriptor[2].CpuStart = payload_mem_info->free_base;
	mm_mem_regions.Descriptor[2].PhysicalSize = payload_mem_info->free_size;
	mm_mem_regions.Descriptor[2].RegionState = EFI_CACHEABLE | EFI_ALLOCATED;

	mm_mem_regions.Descriptor[3].PhysicalStart = payload_mem_info->free_base;
	mm_mem_regions.Descriptor[3].CpuStart = payload_mem_info->free_base;
	mm_mem_regions.Descriptor[3].PhysicalSize = payload_mem_info->free_size;
	mm_mem_regions.Descriptor[3].RegionState = EFI_CACHEABLE;

	// TODO: Use the resource descriptor HOB instead?
	mm_mem_regions.Descriptor[4].PhysicalStart = ARM_SECURE_NS_DRAM1_BASE;
	mm_mem_regions.Descriptor[4].CpuStart = ARM_SECURE_NS_DRAM1_BASE;
	mm_mem_regions.Descriptor[4].PhysicalSize = ARM_SECURE_NS_DRAM1_SIZE;
	mm_mem_regions.Descriptor[4].RegionState = EFI_CACHEABLE |
						   EFI_ALLOCATED |
						   EFI_SECURE_NS_MEM;

	/* Create a hoblist with a PHIT and EOH */
	CreateHobList((void *) payload_mem_info->total_base,
		      payload_mem_info->total_size,
		      (void *) payload_mem_info->free_base,
		      (void *) mm_payload_stack_base);

	/* Get a reference to the PHIT hob */
	hoblist = (EFI_HOB_HANDOFF_INFO_TABLE *) GetFirstHob(EFI_HOB_TYPE_HANDOFF);

	/*
	 * Calculate the size of free memory consumed by the SRAM descriptors
	 * that describe the regions of memory reserved for MM payload use.
	 */
	mm_mem_regions_size =
		sizeof(mm_mem_regions.NumberOfSmmReservedRegions);
	mm_mem_regions_size +=
		mm_mem_regions.NumberOfSmmReservedRegions *
		sizeof(EFI_SMRAM_DESCRIPTOR);

	INFO("BL2: mm_mem_regions=%p\n",  (void *) &mm_mem_regions);
	INFO("BL2: mm_mem_regions_size=0x%x\n", mm_mem_regions_size);

	/*
	 * Create a GUIDed HOB with SRAM ranges & update the region descriptor
	 * to point to the copied data
	 */
	mm_regions_hob_data = BuildGuidDataHob(&gEfiSmmPeiSmramMemoryReserveGuid,
					       (void *) &mm_mem_regions,
					       mm_mem_regions_size);

	/*
	 * Populate the Location.Core field with the linear index of each CPU in
	 * the MP_INFORMATION_HOB_DATA.
	 * TODO: Assume non-MT CPUs for the time being. MT CPUs should have
	 * their Location.Thread field populated.
	 */
	assert(mpinfo_hob_data.NumberOfProcessors == PLATFORM_CORE_COUNT);
	for (index = 0; index < PLATFORM_CORE_COUNT; index++) {
		UINT64 mpidr;
		UINT32 *cpu;

		mpidr = mpinfo_hob_data.ProcessorInfoBuffer[index].ProcessorId;
		cpu = &mpinfo_hob_data.ProcessorInfoBuffer[index].Location.Core;

		*cpu = plat_core_pos_by_mpidr(mpidr);
	}
	INFO("BL2: size of mpinfo_hob_data = 0x%lu\n", sizeof(mpinfo_hob_data));
	BuildGuidDataHob(&gMpInformationHobGuid,
			 (void *) &mpinfo_hob_data,
			 sizeof(mpinfo_hob_data));

	/* Create a FV hob with the address of the BFV */
	BuildFvHob(payload_image_info->image_base,
		   payload_image_info->image_size);

	/*
	 * Update the size of the memory region (2) marked as reserved for the
	 * Hoblist.
	 */
	mm_regions_hob_data->Descriptor[2].PhysicalSize =
		hoblist->EfiFreeMemoryBottom - payload_mem_info->free_base;

	INFO("BL2: hoblist base=%p\n",  (void *) hoblist);
	INFO("BL2: hoblist size=0x%llx\n",
	     mm_regions_hob_data->Descriptor[2].PhysicalSize);

	/*
	 * Update the size & base of the free memory after discounting the
	 * memory occupied by the hoblist.
	 */
	mm_regions_hob_data->Descriptor[3].PhysicalStart =
		hoblist->EfiFreeMemoryBottom;
	mm_regions_hob_data->Descriptor[3].CpuStart =
		hoblist->EfiFreeMemoryBottom;
	mm_regions_hob_data->Descriptor[3].PhysicalSize =
		hoblist->EfiFreeMemoryTop - hoblist->EfiFreeMemoryBottom;

	INFO("BL2: free memory base=%p\n",
	     (void *) mm_regions_hob_data->Descriptor[3].PhysicalStart);
	INFO("BL2: free memory size=0x%llx\n",
	     mm_regions_hob_data->Descriptor[3].PhysicalSize);

	INFO("BL2: =============================== \n");

	/* Create entry point information for the MM payload */
	payload_ep_info->args.arg0 = (unsigned long long) hoblist;
	payload_ep_info->args.arg1 = mm_payload_stack_base;
	payload_ep_info->args.arg2 = MM_PAYLOAD_PCPU_STACK_SIZE;
	payload_ep_info->args.arg3 = payload_mem_info->total_base;
	payload_ep_info->args.arg4 = payload_mem_info->total_size;

	return 0;
}
