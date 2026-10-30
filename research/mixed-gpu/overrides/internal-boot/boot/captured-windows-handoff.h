/* Unmodified function bodies extracted from pinned upstream handoff.c; HOST-ONLY mocks. */
static EFI_STATUS ReportVerification(EFI_STATUS s) {
  const GU_VERIFY_DIAGNOSTIC *d=GuLastVerifyDiagnostic();
  if(d->ConfigDiff) {
    ScreenStatus("PCI config mismatch byte offset",d->Offset);
    ScreenStatus("PCI config expected DWORD",d->Expected);
    ScreenStatus("PCI config observed DWORD",d->Observed);
    if(Log) {
      (void)Record(NULL,"verify-config-byte-offset",d->Offset);
      (void)Record(NULL,"verify-config-expected-dword",d->Expected);
      (void)Record(NULL,"verify-config-observed-dword",d->Observed);
    }
  }
  EFI_STATUS written=Record(NULL,d->Stage,s);
  return s==EFI_SUCCESS?written:s;
}
static EFI_STATUS FullWindows(VOID *Context) {
  (void)Context;Say(L"Unlock returned success. Checking Windows handoff, no reset.\r\n");
  EFI_STATUS s=Record(NULL,"windows-preclose-verify-begin",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
  s=GuVerifyReleased(Table,FALSE);
  s=ReportVerification(s);if(s!=EFI_SUCCESS)return s;
  s=Record(NULL,"windows-close-files-next-then-final-verify-and-StartImage",EFI_SUCCESS);if(s!=EFI_SUCCESS)return s;
  EFI_STATUS logStatus,rootStatus;
  s=CloseBootFiles(&logStatus,&rootStatus);
  ScreenStatus("windows-close-log",logStatus);
  ScreenStatus("windows-close-root",rootStatus);if(s!=EFI_SUCCESS)return s;
  Say(L"Final fresh verification, then Windows StartImage. Failures below identify the stopping stage.\r\n");
  s=GuVerifyReleased(Table,FALSE);
  if(s!=EFI_SUCCESS){ScreenStatus("windows-final-verify",s);return ReportVerification(s);}
  /* No file/console IO between the final graph check and StartImage. */
  s=Table->BootServices->StartImage(WindowsImage,NULL,NULL);
  ScreenStatus("windows-StartImage-returned",s);return s;
}
