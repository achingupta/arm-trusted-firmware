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

#ifndef __MMD_H__
#define __MMD_H__

/*******************************************************************************
 * Defines for runtime services function ids
 ******************************************************************************/
#define MM_COMMUNICATE_AARCH32		0x84000040
#define MM_EVENT_REGISTER_AARCH32	0x84000041
#define MM_EVENT_UNREGISTER_AARCH32	0x84000042
#define MM_EVENT_GET_CONTEXT_AARCH32	0x84000043
#define MM_EVENT_MAP_MEMORY_AARCH32	0x84000044
#define MM_EVENT_UNMAP_MEMORY_AARCH32	0x84000045
#define MM_EVENT_COMPLETE_AARCH32	0x84000046
#define MM_INIT_COMPLETE_AARCH32	0x84000047
#define MM_GET_NS_BUFFER_AARCH32	0x84000048

#define MM_COMMUNICATE_AARCH64		0xC4000040
#define MM_EVENT_REGISTER_AARCH64	0xC4000041
#define MM_EVENT_UNREGISTER_AARCH64	0xC4000042
#define MM_EVENT_GET_CONTEXT_AARCH64	0xC4000043
#define MM_EVENT_MAP_MEMORY_AARCH64	0xC4000044
#define MM_EVENT_UNMAP_MEMORY_AARCH64	0xC4000045
#define MM_EVENT_COMPLETE_AARCH64	0xC4000046
#define MM_INIT_COMPLETE_AARCH64	0xC4000047
#define MM_GET_NS_BUFFER_AARCH64	0xC4000048

#define EVENT_TYPE_MASK			0x1
#define EVENT_TYPE_SHIFT		0x4
#define EVENT_TYPE_GLOBAL		1
#define EVENT_TYPE_LOCAL		0
#define event_type_get(e)		(((e) >> EVENT_TYPE_SHIFT) &	\
					 EVENT_TYPE_MASK)
#define event_type_set(e, t)		(((e) & ~(EVENT_TYPE_MASK << 	\
						  EVENT_TYPE_SHIFT)) |	\
					 (t & EVENT_TYPE_MASK))

#define EVENT_FID_MASK			0xf
#define EVENT_FID_SHIFT			0x0
#define event_fid_get(e)		(((e) >> EVENT_FID_SHIFT) &	\
					 EVENT_FID_MASK)

#define EVENT_FID_MM_COMMUNICATE_SMC	0x0
#define EVENT_FID_MAX			0x1

#define EVENT_ID_MAKE(fid, t)		((((t) & EVENT_TYPE_MASK) << 	\
					 EVENT_TYPE_SHIFT) | 		\
					 (((fid) & EVENT_FID_MASK) <<	\
					  EVENT_FID_SHIFT))


#ifndef __ASSEMBLY__
#include <stdint.h>

/* MM setup function */
int mmd_setup(void);
uint64_t mmd_smc_handler(uint32_t smc_fid,
			 uint64_t x1,
			 uint64_t x2,
			 uint64_t x3,
			 uint64_t x4,
			 void *cookie,
			 void *handle,
			 uint64_t flags);
#endif /*__ASSEMBLY__*/
#endif /* __MMD_H__ */
