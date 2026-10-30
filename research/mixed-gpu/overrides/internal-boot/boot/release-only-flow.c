/* GPL-2.0-only. Once release begins, failure never boots, retries or falls back. */
#include "release-only-flow.h"
EFI_STATUS GuReleaseOnlyFlow(const GU_RELEASE_ONLY_OPS *o) {
 if(!o || !o->Prepare || !o->Choose || !o->Capture || !o->Release ||
    !o->Verify || !o->WindowsSkip || !o->WindowsAfter || !o->Record)return EFI_INVALID_PARAMETER;
 EFI_STATUS s=o->Prepare(o->Context);if(s!=EFI_SUCCESS)return s;
 BOOLEAN skip=TRUE;s=o->Choose(o->Context,&skip);if(s!=EFI_SUCCESS)return s;
 if(skip) {
  s=o->Record(o->Context,"release-only-skipped-no-device-changes",EFI_SUCCESS);
  if(s==EFI_SUCCESS)s=o->WindowsSkip(o->Context);
  return s==EFI_SUCCESS?EFI_ABORTED:s;
 }
 s=o->Capture(o->Context);if(s!=EFI_SUCCESS)return s;
 EFI_STATUS released=o->Release(o->Context);
 EFI_STATUS captured=o->Capture(o->Context);
 s=o->Record(o->Context,"release-only-no-unlock-result",released);
 if(released!=EFI_SUCCESS)return released;
 if(captured!=EFI_SUCCESS)return captured;
 if(s!=EFI_SUCCESS)return s;
 s=o->Record(o->Context,"release-only-verify-before-windows",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
 s=o->Verify(o->Context);if(s!=EFI_SUCCESS)return s;
 s=o->Record(o->Context,"release-only-start-windows-no-unlock",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
 s=o->WindowsAfter(o->Context);
 return s==EFI_SUCCESS?EFI_ABORTED:s;
}
