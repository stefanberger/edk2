/** @file
  QEMU Glue Code for TPM Physical Presence Interface.

  Copyright (c) 2018, IBM Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:

  QemuTPMPPI.h

Abstract:

  Head file for QEMU specific TPM PPI code

**/

#ifndef _PLATFORM_SPECIFIC_QEMU_TPMPPI_H_
#define _PLATFORM_SPECIFIC_QEMU_TPMPPI_H_

struct tpm_ppi;
struct tpm_ppi *
EFIAPI
Tcg2PhysicalPresenceLibQEMUInitPPI(void);

VOID
EFIAPI
Tcg2PhysicalPresenceLibQEMUPre(struct tpm_ppi *);

VOID
EFIAPI
Tcg2PhysicalPresenceLibQEMUPost(struct tpm_ppi *);

#endif // _PLATFORM_SPECIFIC_QEMU_TPMPPI_H_
