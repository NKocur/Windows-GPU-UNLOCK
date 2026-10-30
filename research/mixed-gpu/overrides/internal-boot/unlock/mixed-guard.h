/* GPL-2.0-only. Exact mixed-GPU experimental companion guard. */
#pragma once
#include <Uefi.h>
#include <Protocol/PciIo.h>
EFI_STATUS GuMixedBegin(EFI_SYSTEM_TABLE *Table);
EFI_STATUS GuMixedVerify(EFI_SYSTEM_TABLE *Table);
EFI_STATUS GuMixedFast(EFI_HANDLE Target,EFI_PCI_IO_PROTOCOL *Io,BOOLEAN RequireBar);
EFI_STATUS GuMixedCompanionFast(VOID);
EFI_STATUS GuMixedCheckpoint(VOID);
EFI_STATUS GuMixedDmaPath(BOOLEAN RequireBusMaster);
EFI_STATUS GuMixedContainTarget(EFI_HANDLE Target,EFI_PCI_IO_PROTOCOL *Io);
#if defined(GU_MIXED_RELEASE_ONLY) && defined(GU_EXPERIMENTAL_3090)
#error Release-only and unlock transaction must remain separate builds
#endif
#ifdef GU_MIXED_PORT_HOST_TEST
#if __STDC_HOSTED__ != 1
#error Port transaction integration is host-only
#endif
VOID GuMixedHostReset(VOID);
#endif

/* Cached observation only; this function performs no firmware/GPU access. */
VOID GuMixedDmaObservation(UINT32 *Command,BOOLEAN *Required,EFI_STATUS *Status);
