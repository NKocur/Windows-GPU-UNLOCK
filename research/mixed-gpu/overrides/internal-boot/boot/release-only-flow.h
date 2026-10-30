/* GPL-2.0-only. No unlock callback exists in this release-only flow. */
#pragma once
#include "prepare-console.h"
typedef struct {
 VOID *Context;
 EFI_STATUS (*Prepare)(VOID *);
 EFI_STATUS (*Choose)(VOID *,BOOLEAN *);
 EFI_STATUS (*Capture)(VOID *);
 EFI_STATUS (*Release)(VOID *);
 EFI_STATUS (*Verify)(VOID *);
 EFI_STATUS (*WindowsSkip)(VOID *);
 EFI_STATUS (*WindowsAfter)(VOID *);
 GU_PREPARE_LOG Record;
} GU_RELEASE_ONLY_OPS;
EFI_STATUS GuReleaseOnlyFlow(const GU_RELEASE_ONLY_OPS *Ops);
