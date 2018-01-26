/** @file
  QEMU Glue Code for TPM Physical Presence Interface.

Copyright (c) 2018, IBM Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiDxe.h>

#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Guid/Tcg2PhysicalPresenceData.h>
#include <Library/Tcg2PhysicalPresenceLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/QemuFwCfgLib.h>

#include "QemuTPMPPI.h"

/* the following structure is shared between firmware and ACPI */
struct tpm_ppi {
    UINT8 ppin;            /*  0: 1 = initialized */
    UINT32 ppip;           /*  1: not used */
    UINT32 pprp;           /*  5: response from TPM; set by BIOS */
    UINT32 pprq;           /*  9: opcode; set by ACPI */
    UINT32 pprm;           /* 13: parameter for opcode; set by ACPI */
    UINT32 lppr;           /* 17: last opcode; set by BIOS */
    UINT32 fret;           /* 21: not used */
    UINT8 res1;            /* 25: reserved */
    UINT32 res2[4];        /* 26: reserved */
    UINT8 res3[213];       /* 42: reserved */
    UINT8 nextStep;        /* 255: next step after reboot */
    UINT8 func[256];       /* 256: per function implementation flags; set by BIOS */
/* actions OS should take to transition to the pre-OS env.; bits 0, 1 */
#define TPM_PPI_FUNC_ACTION_SHUTDOWN   (1 << 0)
#define TPM_PPI_FUNC_ACTION_REBOOT     (2 << 0)
#define TPM_PPI_FUNC_ACTION_VENDOR     (3 << 0)
#define TPM_PPI_FUNC_ACTION_MASK       (3 << 0)
/* whether function is blocked by BIOS settings; bits 2, 3, 4 */
#define TPM_PPI_FUNC_NOT_IMPLEMENTED     (0 << 2)
#define TPM_PPI_FUNC_BIOS_ONLY           (1 << 2)
#define TPM_PPI_FUNC_BLOCKED             (2 << 2)
#define TPM_PPI_FUNC_ALLOWED_USR_REQ     (3 << 2)
#define TPM_PPI_FUNC_ALLOWED_USR_NOT_REQ (4 << 2)
#define TPM_PPI_FUNC_MASK                (7 << 2)
} __attribute__((packed));

/* the following structure is for the fw_cfg file */
struct TPMPPIConfig {
    UINT32 tpmppi_address;
    UINT8 tpm_version;
    UINT8 tpmppi_version;
#define TPM_PPI_VERSION_1_30  1
} __attribute__((packed));

#define TPM_VERSION_1_2  1
#define TPM_VERSION_2    2

#define TPM_PPI_FLAGS_NR (TPM_PPI_FUNC_ACTION_REBOOT | \
                          TPM_PPI_FUNC_ALLOWED_USR_NOT_REQ)
#define TPM_PPI_FLAGS_R (TPM_PPI_FUNC_ACTION_REBOOT | \
                         TPM_PPI_FUNC_ALLOWED_USR_REQ)

static UINT8 tpm2_ppi_funcs[] = {
    [TCG2_PHYSICAL_PRESENCE_NO_ACTION] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_CLEAR] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_ENABLE_CLEAR] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_ENABLE_CLEAR_2] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_ENABLE_CLEAR_3] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_SET_PP_REQUIRED_FOR_CLEAR_TRUE] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_SET_PP_REQUIRED_FOR_CLEAR_FALSE] = TPM_PPI_FLAGS_R,
    [TCG2_PHYSICAL_PRESENCE_SET_PCR_BANKS] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_CHANGE_EPS] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_LOG_ALL_DIGESTS] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_ENABLE_BLOCK_SID] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_DISABLE_BLOCK_SID] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_SET_PP_REQUIRED_FOR_ENABLE_BLOCK_SID_FUNC_TRUE] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_SET_PP_REQUIRED_FOR_ENABLE_BLOCK_SID_FUNC_FALSE] = TPM_PPI_FLAGS_R,
    [TCG2_PHYSICAL_PRESENCE_SET_PP_REQUIRED_FOR_DISABLE_BLOCK_SID_FUNC_TRUE] = TPM_PPI_FLAGS_NR,
    [TCG2_PHYSICAL_PRESENCE_SET_PP_REQUIRED_FOR_DISABLE_BLOCK_SID_FUNC_FALSE] = TPM_PPI_FLAGS_R,
};

static void
Tcg2PhysicalPresenceLibQEMUCreate(UINT8 *base)
{
  EFI_TCG2_PHYSICAL_PRESENCE_FLAGS  Flags;
  EFI_STATUS                        Status;
  UINTN                             DataSize;

  /* copy default flags that need adjustments */
  CopyMem (base, tpm2_ppi_funcs, sizeof(tpm2_ppi_funcs));

  DataSize = sizeof (EFI_TCG2_PHYSICAL_PRESENCE_FLAGS);
  Status = gRT->GetVariable (
                  TCG2_PHYSICAL_PRESENCE_FLAGS_VARIABLE,
                  &gEfiTcg2PhysicalPresenceGuid,
                  NULL,
                  &DataSize,
                  &Flags
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "[TPM2] Get physical presence flags variable failed, Status = %r\n", Status));
  } else {
    if (Flags.PPFlags & TCG2_BIOS_TPM_MANAGEMENT_FLAG_PP_REQUIRED_FOR_CLEAR) {
      base[TCG2_PHYSICAL_PRESENCE_CLEAR] =
      base[TCG2_PHYSICAL_PRESENCE_ENABLE_CLEAR] =
      base[TCG2_PHYSICAL_PRESENCE_ENABLE_CLEAR_2] =
      base[TCG2_PHYSICAL_PRESENCE_ENABLE_CLEAR_3] = TPM_PPI_FLAGS_R;
    }
    if (Flags.PPFlags & TCG2_BIOS_TPM_MANAGEMENT_FLAG_PP_REQUIRED_FOR_CHANGE_PCRS) {
      base[TCG2_PHYSICAL_PRESENCE_SET_PCR_BANKS] = TPM_PPI_FLAGS_R;
    }
    if (Flags.PPFlags & TCG2_BIOS_TPM_MANAGEMENT_FLAG_PP_REQUIRED_FOR_CHANGE_EPS) {
      base[TCG2_PHYSICAL_PRESENCE_CHANGE_EPS] = TPM_PPI_FLAGS_R;
    }
    if (Flags.PPFlags & TCG2_BIOS_STORAGE_MANAGEMENT_FLAG_PP_REQUIRED_FOR_ENABLE_BLOCK_SID) {
      base[TCG2_PHYSICAL_PRESENCE_ENABLE_BLOCK_SID] = TPM_PPI_FLAGS_R;
    }
    if (Flags.PPFlags & TCG2_BIOS_STORAGE_MANAGEMENT_FLAG_PP_REQUIRED_FOR_DISABLE_BLOCK_SID) {
      base[TCG2_PHYSICAL_PRESENCE_DISABLE_BLOCK_SID] = TPM_PPI_FLAGS_R;
    }
  }
}

struct tpm_ppi *
EFIAPI
Tcg2PhysicalPresenceLibQEMUInitPPI(void)
{
  struct TPMPPIConfig  tpm_ppi_config;
  struct tpm_ppi       *TPM_PPI;
  EFI_STATUS           Status;
  FIRMWARE_CONFIG_ITEM FwCfgItem;
  UINTN                FwCfgSize;

  Status = QemuFwCfgFindFile("etc/tpm/ppi", &FwCfgItem, &FwCfgSize);
  if (EFI_ERROR(Status)) {
    return NULL;
  }

  if (FwCfgSize < sizeof(tpm_ppi_config)) {
    return NULL;
  }

  QemuFwCfgSelectItem (FwCfgItem);
  QemuFwCfgReadBytes(sizeof(tpm_ppi_config), &tpm_ppi_config);

  TPM_PPI = (struct tpm_ppi *)(unsigned long)tpm_ppi_config.tpmppi_address;
  if (!TPM_PPI)
    return NULL;
;
  DEBUG ((EFI_D_INFO, "[TPM2] !!! TPM_PPI = %x !!!\n", TPM_PPI));

  ZeroMem(&TPM_PPI->func, sizeof(TPM_PPI->func));

  switch (tpm_ppi_config.tpmppi_version) {
  case TPM_PPI_VERSION_1_30:
      switch (tpm_ppi_config.tpm_version) {
      case TPM_VERSION_1_2:
          // CopyMem (&TPM_PPI->func, tpm12_ppi_funcs, sizeof(tpm12_ppi_funcs));
          break;
      case TPM_VERSION_2:
          Tcg2PhysicalPresenceLibQEMUCreate((UINT8 *)&TPM_PPI->func);
          break;
      }
      break;
  }

  if (!TPM_PPI->ppin) {
      TPM_PPI->ppin = 1;
      TPM_PPI->pprq = TCG2_PHYSICAL_PRESENCE_NO_ACTION;
      TPM_PPI->lppr = TCG2_PHYSICAL_PRESENCE_NO_ACTION;
      TPM_PPI->nextStep = TCG2_PHYSICAL_PRESENCE_NO_ACTION;
  }
  return TPM_PPI;
}

VOID
EFIAPI
Tcg2PhysicalPresenceLibQEMUPre(struct tpm_ppi *TPM_PPI)
{
  EFI_TCG2_PHYSICAL_PRESENCE        TcgPpData;
  EFI_STATUS                        Status;
  UINTN                             DataSize;

  if (TPM_PPI->pprq != TCG2_PHYSICAL_PRESENCE_NO_ACTION) {
    DEBUG ((EFI_D_ERROR, "[TPM2] Getting PPRQ from PPI device\n"));
    /* Get the request and parameter from the user */
    TcgPpData.PPRequest = TPM_PPI->pprq;
    /* avoid endless reboots upon resets */
    TPM_PPI->pprq = TCG2_PHYSICAL_PRESENCE_NO_ACTION;
    TcgPpData.PPRequestParameter = TPM_PPI->pprm;

    DataSize = sizeof (EFI_TCG2_PHYSICAL_PRESENCE);
    Status = gRT->SetVariable (
                    TCG2_PHYSICAL_PRESENCE_VARIABLE,
                    &gEfiTcg2PhysicalPresenceGuid,
                    EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                    DataSize,
                    &TcgPpData
                     );
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "[TPM2] Set physical presence variable failed, Status = %r\n", Status));
    }
  }
}

VOID
EFIAPI
Tcg2PhysicalPresenceLibQEMUPost(struct tpm_ppi *TPM_PPI)
{
  EFI_TCG2_PHYSICAL_PRESENCE        TcgPpData;
  EFI_STATUS                        Status;
  UINTN                             DataSize;

  DataSize = sizeof (EFI_TCG2_PHYSICAL_PRESENCE);
  Status = gRT->GetVariable (
                  TCG2_PHYSICAL_PRESENCE_VARIABLE,
                  &gEfiTcg2PhysicalPresenceGuid,
                  NULL,
                  &DataSize,
                  &TcgPpData
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "[TPM2] Get physical presence variable failed, Status = %r\n", Status));
  } else {
    TcgPpData.PPRequest = TCG2_PHYSICAL_PRESENCE_NO_ACTION;
    TPM_PPI->pprq = TcgPpData.PPRequest;
    TPM_PPI->lppr = TcgPpData.LastPPRequest;
    TPM_PPI->pprp = TcgPpData.PPResponse;

    DataSize = sizeof (EFI_TCG2_PHYSICAL_PRESENCE);
    Status = gRT->SetVariable (
                    TCG2_PHYSICAL_PRESENCE_VARIABLE,
                    &gEfiTcg2PhysicalPresenceGuid,
                    EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                    DataSize,
                    &TcgPpData
                     );
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "[TPM2] Set physical presence variable failed, Status = %r\n", Status));
    }
  }
}
