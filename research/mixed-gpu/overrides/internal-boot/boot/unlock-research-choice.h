/* GPL-2.0-only. Explicit U required; Esc/timeout leaves GPUs untouched. */
#pragma once
#include <Protocol/SimpleTextIn.h>
static EFI_STATUS GuUnlockResearchChoose(EFI_SYSTEM_TABLE *t,BOOLEAN *skip) {
 if(!t || !t->BootServices || !t->BootServices->Stall || !t->ConIn ||
    !t->ConIn->Reset || !t->ConIn->ReadKeyStroke || !skip)return EFI_INVALID_PARAMETER;
 *skip=TRUE;
 EFI_STATUS s=t->ConIn->Reset(t->ConIn,FALSE);if(s!=EFI_SUCCESS)return s;
 for(UINTN i=0;i<300;i++) {
  EFI_INPUT_KEY key={0};s=t->ConIn->ReadKeyStroke(t->ConIn,&key);
  if(s==EFI_SUCCESS) {
   if(key.ScanCode==0x17 || key.UnicodeChar==27)return EFI_SUCCESS;
   if(key.UnicodeChar=='u' || key.UnicodeChar=='U'){*skip=FALSE;return EFI_SUCCESS;}
  }else if(s!=EFI_NOT_READY)return s;
  s=t->BootServices->Stall(100000);if(s!=EFI_SUCCESS)return s;
 }
 return EFI_SUCCESS;
}
