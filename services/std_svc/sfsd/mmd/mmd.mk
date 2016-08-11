#
# Copyright (c) 2016, ARM Limited and Contributors. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# Neither the name of ARM nor the names of its contributors may be used
# to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

MMD_DIR			:=	services/std_svc/sfsd/mmd

INCLUDES		+=	-Iinclude/lib/pi_spec/

BL2_SOURCES		+=	lib/pi_spec/hoblib/Hob.c		\
				lib/pi_spec/standalone_mm_entry/mm_foundation_entry.c

SPD_SOURCES		:=	services/std_svc/sfsd/mmd/mmd_common.c			\
				services/std_svc/sfsd/mmd/mmd_helpers.S			\
				services/std_svc/sfsd/mmd/mmd_sel0_context_mgmt.c		\
				services/std_svc/sfsd/mmd/mmd_main.c

ifeq (${TRUSTED_BOARD_BOOT},1)
        $(error "Please disable TRUSTED_BOARD_BOOT to include support for SFS MM payload binary")
endif

ifneq (${SFS_PAYLOAD},)
        $(eval $(call MAKE_TOOL_ARGS,32,${SFS_PAYLOAD},sfs-fw))
else
        $(error "Please provide the path to a MM SFS payload binary i.e. SFS_PAYLOAD=<path to payload>")
endif

# Include MM Shim's Makefile. For the time being it will masquerade as a BL32 image
BL32_ROOT		:=	bl32/mm_stub
include ${BL32_ROOT}/mm_stub.mk

# Let the top-level Makefile know that we intend to build the SP from source
NEED_BL32		:=	yes
