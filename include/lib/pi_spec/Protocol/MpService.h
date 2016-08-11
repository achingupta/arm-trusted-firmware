/** @file
  When installed, the MP Services Protocol produces a collection of services
  that are needed for MP management.

  The MP Services Protocol provides a generalized way of performing following tasks:
    - Retrieving information of multi-processor environment and MP-related status of
      specific processors.
    - Dispatching user-provided function to APs.
    - Maintain MP-related processor status.

  The MP Services Protocol must be produced on any system with more than one logical
  processor.

  The Protocol is available only during boot time.

  MP Services Protocol is hardware-independent. Most of the logic of this protocol
  is architecturally neutral. It abstracts the multi-processor environment and
  status of processors, and provides interfaces to retrieve information, maintain,
  and dispatch.

  MP Services Protocol may be consumed by ACPI module. The ACPI module may use this
  protocol to retrieve data that are needed for an MP platform and report them to OS.
  MP Services Protocol may also be used to program and configure processors, such
  as MTRR synchronization for memory space attributes setting in DXE Services.
  MP Services Protocol may be used by non-CPU DXE drivers to speed up platform boot
  by taking advantage of the processing capabilities of the APs, for example, using
  APs to help test system memory in parallel with other device initialization.
  Diagnostics applications may also use this protocol for multi-processor.

Copyright (c) 2006 - 2010, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under
the terms and conditions of the BSD License that accompanies this distribution.
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

  @par Revision Reference:
  This Protocol is defined in the UEFI Platform Initialization Specification 1.2,
  Volume 2:Driver Execution Environment Core Interface.

**/

#ifndef _MP_SERVICE_PROTOCOL_H_
#define _MP_SERVICE_PROTOCOL_H_

///
/// This bit is used in the StatusFlag field of EFI_PROCESSOR_INFORMATION and
/// indicates whether the processor is playing the role of BSP. If the bit is 1,
/// then the processor is BSP. Otherwise, it is AP.
///
#define PROCESSOR_AS_BSP_BIT         0x00000001

///
/// This bit is used in the StatusFlag field of EFI_PROCESSOR_INFORMATION and
/// indicates whether the processor is enabled. If the bit is 1, then the
/// processor is enabled. Otherwise, it is disabled.
///
#define PROCESSOR_ENABLED_BIT        0x00000002

///
/// This bit is used in the StatusFlag field of EFI_PROCESSOR_INFORMATION and
/// indicates whether the processor is healthy. If the bit is 1, then the
/// processor is healthy. Otherwise, some fault has been detected for the processor.
///
#define PROCESSOR_HEALTH_STATUS_BIT  0x00000004

///
/// Structure that describes the pyhiscal location of a logical CPU.
///
typedef struct {
  ///
  /// Zero-based physical package number that identifies the cartridge of the processor.
  ///
  UINT32  Package;
  ///
  /// Zero-based physical core number within package of the processor.
  ///
  UINT32  Core;
  ///
  /// Zero-based logical thread number within core of the processor.
  ///
  UINT32  Thread;
} EFI_CPU_PHYSICAL_LOCATION;

///
/// Structure that describes information about a logical CPU.
///
typedef struct {
  ///
  /// The unique processor ID determined by system hardware.  For IA32 and X64,
  /// the processor ID is the same as the Local APIC ID. Only the lower 8 bits
  /// are used, and higher bits are reserved.  For IPF, the lower 16 bits contains
  /// id/eid, and higher bits are reserved.
  ///
  UINT64                     ProcessorId;
  ///
  /// Flags indicating if the processor is BSP or AP, if the processor is enabled
  /// or disabled, and if the processor is healthy. Bits 3..31 are reserved and
  /// must be 0.
  ///
  /// <pre>
  /// BSP  ENABLED  HEALTH  Description
  /// ===  =======  ======  ===================================================
  ///  0      0       0     Unhealthy Disabled AP.
  ///  0      0       1     Healthy Disabled AP.
  ///  0      1       0     Unhealthy Enabled AP.
  ///  0      1       1     Healthy Enabled AP.
  ///  1      0       0     Invalid. The BSP can never be in the disabled state.
  ///  1      0       1     Invalid. The BSP can never be in the disabled state.
  ///  1      1       0     Unhealthy Enabled BSP.
  ///  1      1       1     Healthy Enabled BSP.
  /// </pre>
  ///
  UINT32                     StatusFlag;
  ///
  /// The physical location of the processor, including the physical package number
  /// that identifies the cartridge, the physical core number within package, and
  /// logical thread number within core.
  ///
  EFI_CPU_PHYSICAL_LOCATION  Location;
} EFI_PROCESSOR_INFORMATION;

#endif
