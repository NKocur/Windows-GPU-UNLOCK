/* GPL-2.0-only. Experimental exact-machine mixed-GPU firmware port.
 * Full checks at release/ownership boundaries; allocation-free checks at the
 * transaction write boundaries. Companion access is PCI CONFIGURATION READ ONLY.
 * No GPU family, firmware, selector, target BME/DMA/FLR policy is broadened.
 */
#include "mixed-guard.h"
#include "machine-profile.h"
#include "mixed-policy.h"
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#ifndef GU_MIXED_COMPANION_GUARD
#error This module is only for the explicit mixed-GPU guarded port
#endif
#define MAX_HANDLES 1024U
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static BOOLEAN Bytes(const VOID *a,const VOID *b,UINTN n) {
 const UINT8 *x=a,*y=b;for(UINTN i=0;i<n;i++)if(x[i]!=y[i])return FALSE;return TRUE;
}
static BOOLEAN GuGuidEqual(const VOID *a,const VOID *b) {return Bytes(a,b,16);}
static BOOLEAN Passive(UINT32 a) {return a==1 || a==2 || a==4;}
static EFI_STATUS Interface(EFI_BOOT_SERVICES *bs,EFI_HANDLE h,EFI_GUID *guid,VOID **out) {
 *out=NULL;EFI_STATUS status=bs->HandleProtocol(h,guid,out);
 return status==EFI_SUCCESS && !*out?EFI_COMPROMISED_DATA:status;
}
static BOOLEAN PathMatches(const EFI_DEVICE_PATH_PROTOCOL *p,const UINT8 *expected,UINTN bytes) {
 if(!p || bytes<4 || bytes>128)return FALSE;
 const UINT8 *raw=(const VOID *)p;UINTN at=0;
 for(UINTN n=0;n<32 && at+4<=bytes;n++) {
  UINTN len=raw[at+2]+((UINTN)raw[at+3]<<8);
  if(len<4 || len>bytes-at)return FALSE;
  if(raw[at]==END_DEVICE_PATH_TYPE)return raw[at+1]==END_ENTIRE_DEVICE_PATH_SUBTYPE &&
    len==4 && at+4==bytes && Bytes(raw,expected,bytes);
  at+=len;
 }
 return FALSE;
}
typedef struct {
  EFI_SYSTEM_TABLE *Table;
  EFI_HANDLE Target,Companion,Bridge;
  EFI_PCI_IO_PROTOCOL *TargetIo,*CompanionIo,*BridgeIo;
  BOOLEAN Armed,Begun;
} MIXED_STATE;
static MIXED_STATE MixedState;
static EFI_BOOT_SERVICES *MixedBs;
static BOOLEAN MixedBusy,MixedReentered;
static UINT32 LastBridgeCommand=0xffffffffU;
static BOOLEAN LastBridgeRequired;
static EFI_STATUS LastBridgeStatus=EFI_NOT_STARTED;

static EFI_STATUS MixedReadCompanion(EFI_BOOT_SERVICES *bs,EFI_HANDLE h,EFI_PCI_IO_PROTOCOL *expected) {
  EFI_PCI_IO_PROTOCOL *io=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  EFI_STATUS s=Interface(bs,h,&PciGuid,(VOID **)&io);
  if(s!=EFI_SUCCESS || !io || io!=expected || !io->GetLocation || !io->Pci.Read)return EFI_NOT_READY;
  UINTN seg=0,bus=0,dev=0,fun=0;UINT32 cfg[64]={0};
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s!=EFI_SUCCESS || seg || bus!=4 || dev || fun)return EFI_NOT_READY;
  s=Interface(bs,h,&PathGuid,(VOID **)&path);
  if(s!=EFI_SUCCESS || !PathMatches(path,MixedExpectedPath,sizeof(MixedExpectedPath)))return EFI_NOT_READY;
  s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,cfg);
  if(s!=EFI_SUCCESS || !Bytes(cfg,MixedExpectedConfig,sizeof(cfg)))return EFI_NOT_READY;
  return EFI_SUCCESS;
}

static EFI_STATUS MixedReadBridge(EFI_BOOT_SERVICES *bs,EFI_HANDLE handle,EFI_PCI_IO_PROTOCOL *expected,BOOLEAN master) {
 LastBridgeCommand=0xffffffffU;

 EFI_PCI_IO_PROTOCOL *io=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
 EFI_STATUS status=Interface(bs,handle,&PciGuid,(VOID **)&io);
 if(status!=EFI_SUCCESS || !io || io!=expected || !io->GetLocation || !io->Pci.Read)return EFI_NOT_READY;
 UINTN seg=0,bus=0,dev=0,fun=0;UINT32 cfg[16]={0};
 status=io->GetLocation(io,&seg,&bus,&dev,&fun);
 if(status!=EFI_SUCCESS || seg || bus || dev!=3 || fun!=1)return EFI_NOT_READY;
 status=Interface(bs,handle,&PathGuid,(VOID **)&path);
 if(status!=EFI_SUCCESS || !PathMatches(path,MixedExpectedBridgePath,sizeof(MixedExpectedBridgePath)))return EFI_NOT_READY;
 status=io->Pci.Read(io,EfiPciIoWidthUint32,0,16,cfg);
 if(status!=EFI_SUCCESS)return status;
 LastBridgeCommand=cfg[1];
 for(UINTN i=0;i<16;i++) {
  UINT32 mask=i==1?~4U:~0U;
  if((cfg[i]&mask)!=(MixedExpectedBridgeConfig[i]&mask))return EFI_NOT_READY;
 }
 if(master && !(cfg[1]&4U))return EFI_NOT_READY;
 return EFI_SUCCESS;
}
static EFI_STATUS MixedNoOwner(EFI_BOOT_SERVICES *bs,EFI_HANDLE h) {
  EFI_GUID **protocols=NULL;UINTN count=0;
  EFI_STATUS s=bs->ProtocolsPerHandle(h,&protocols,&count);
  if(s==EFI_SUCCESS && (!protocols || !count || count>256))s=EFI_BAD_BUFFER_SIZE;
  BOOLEAN hasPci=FALSE,hasPath=FALSE;
  for(UINTN i=0;s==EFI_SUCCESS && i<count;i++) {
    if(!protocols[i]){s=EFI_COMPROMISED_DATA;break;}
    if(GuGuidEqual(protocols[i],&PciGuid))hasPci=TRUE;
    if(GuGuidEqual(protocols[i],&PathGuid))hasPath=TRUE;
    if(GuGuidEqual(protocols[i],&GopGuid)){s=EFI_ACCESS_DENIED;break;}
    for(UINTN j=0;j<i;j++)if(GuGuidEqual(protocols[i],protocols[j]))s=EFI_COMPROMISED_DATA;
    if(s!=EFI_SUCCESS)break;
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN n=0;
    s=bs->OpenProtocolInformation(h,protocols[i],&entries,&n);
    if(s==EFI_SUCCESS && (n>512 || (n && !entries)))s=EFI_BAD_BUFFER_SIZE;
    for(UINTN j=0;s==EFI_SUCCESS && j<n;j++)if(!Passive(entries[j].Attributes))s=EFI_ACCESS_DENIED;
    if(entries && bs->FreePool(entries)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  }
  if(protocols && bs->FreePool(protocols)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  if(s==EFI_SUCCESS && (!hasPci || !hasPath))s=EFI_NOT_FOUND;
  return s;
}
/* Full PCI enumeration, not a vendor-only filter; unknown displays fail closed.
 * Other PCI function identities are not rewritten or used as unlock targets.
 */
static EFI_STATUS MixedScan(EFI_SYSTEM_TABLE *t,BOOLEAN begin) {
  if(!t || !t->BootServices)return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *bs=t->BootServices;
  if(!bs->LocateHandleBuffer || !bs->FreePool || !bs->HandleProtocol ||
     !bs->ProtocolsPerHandle || !bs->OpenProtocolInformation)return EFI_UNSUPPORTED;
  EFI_HANDLE *handles=NULL,rtx=NULL,v100=NULL;EFI_PCI_IO_PROTOCOL *rtxIo=NULL,*v100Io=NULL;
  UINTN n=0,displays=0,targets=0,companions=0,bridges=0;
  EFI_HANDLE bridge=NULL;EFI_PCI_IO_PROTOCOL *bridgeIo=NULL;
  EFI_STATUS s=bs->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&n,&handles);
  if(s==EFI_SUCCESS && (!handles || n!=MIXED_EXPECTED_PCI_COUNT || n>MAX_HANDLES))s=EFI_BAD_BUFFER_SIZE;
  for(UINTN i=0;s==EFI_SUCCESS && i<n;i++) {
    if(!handles[i]){s=EFI_COMPROMISED_DATA;break;}
    for(UINTN j=0;j<i;j++)if(handles[j]==handles[i])s=EFI_COMPROMISED_DATA;
    if(s!=EFI_SUCCESS)break;
    EFI_PCI_IO_PROTOCOL *io=NULL;UINT32 cfg[16]={0};UINTN seg=0,bus=0,dev=0,fun=0;
    s=Interface(bs,handles[i],&PciGuid,(VOID **)&io);
    if(s!=EFI_SUCCESS || !io || !io->GetLocation || !io->Pci.Read){s=EFI_NOT_READY;break;}
    s=io->GetLocation(io,&seg,&bus,&dev,&fun);
    if(s==EFI_SUCCESS)s=io->Pci.Read(io,EfiPciIoWidthUint32,0,16,cfg);
    if(s!=EFI_SUCCESS)break;
    if(!seg && !bus && dev==3 && fun==1) {
      bridges++;bridge=handles[i];bridgeIo=io;
    }
    if((cfg[2]>>24)==3) {
      displays++;
      if(cfg[0]==GU_BOARD_ID && cfg[11]==GU_BOARD_SUBSYSTEM &&
         cfg[2]==0x030000a1U && !seg && bus==GU_TARGET_BUS && !dev && !fun) {
        targets++;rtx=handles[i];rtxIo=io;
        if(cfg[4]!=(UINT32)GU_BAR0_BASE || (begin && (cfg[1]&6U)!=2U))s=EFI_UNSUPPORTED;
      } else if(cfg[0]==MixedExpectedConfig[0] && cfg[11]==MixedExpectedConfig[11] &&
                cfg[2]==MixedExpectedConfig[2] && !seg && bus==4 && !dev && !fun) {
        companions++;v100=handles[i];v100Io=io;
      } else s=EFI_UNSUPPORTED;
    }
  }
  if(handles && bs->FreePool(handles)!=EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  if(s==EFI_SUCCESS && (displays!=2 || targets!=1 || companions!=1 || bridges!=1 || rtx==v100 || rtxIo==v100Io))s=EFI_NOT_FOUND;
  if(s==EFI_SUCCESS && !begin && (MixedState.Table!=t || MixedState.Target!=rtx ||
     MixedState.Companion!=v100 || MixedState.TargetIo!=rtxIo || MixedState.CompanionIo!=v100Io || MixedState.Bridge!=bridge || MixedState.BridgeIo!=bridgeIo))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS)s=MixedNoOwner(bs,v100);
  /* No allocation/log callback after this final companion read and before return. */
  if(s==EFI_SUCCESS)s=MixedReadBridge(bs,bridge,bridgeIo,FALSE);
  if(s==EFI_SUCCESS)s=MixedReadCompanion(bs,v100,v100Io);
  if(s==EFI_SUCCESS && (t->BootServices!=bs || MixedReentered))s=EFI_NOT_READY;
  if(s==EFI_SUCCESS && begin) {
    MixedState=(MIXED_STATE){.Table=t,.Target=rtx,.Companion=v100,.TargetIo=rtxIo,.CompanionIo=v100Io,.Bridge=bridge,.BridgeIo=bridgeIo,.Armed=TRUE,.Begun=TRUE};
    MixedBs=bs;
  }
  return s;
}

static EFI_STATUS MixedLeave(EFI_STATUS s) {
 if(MixedReentered)s=EFI_NOT_READY;
 if(s!=EFI_SUCCESS)MixedState.Armed=FALSE;
 MixedBusy=FALSE;return s;
}
static EFI_STATUS MixedEnter(BOOLEAN containment) {
 if(MixedBusy){MixedReentered=TRUE;MixedState.Armed=FALSE;return EFI_NOT_READY;}
 if(!MixedState.Begun || !MixedState.Table || !MixedBs ||
    MixedState.Table->BootServices!=MixedBs || (!containment && !MixedState.Armed)) {
  MixedState.Armed=FALSE;return EFI_NOT_READY;
 }
 MixedBusy=TRUE;MixedReentered=FALSE;return EFI_SUCCESS;
}
EFI_STATUS GuMixedBegin(EFI_SYSTEM_TABLE *t) {
 if(MixedBusy){MixedReentered=TRUE;MixedState.Armed=FALSE;return EFI_NOT_READY;}
 if(MixedState.Begun)return MixedState.Armed?GuMixedVerify(t):EFI_NOT_READY;
 MixedState=(MIXED_STATE){.Begun=TRUE};MixedBusy=TRUE;MixedReentered=FALSE;
 return MixedLeave(MixedScan(t,TRUE));
}
EFI_STATUS GuMixedVerify(EFI_SYSTEM_TABLE *t) {
 EFI_STATUS s=MixedEnter(FALSE);if(s!=EFI_SUCCESS)return s;
 if(t!=MixedState.Table)s=EFI_NOT_READY;
 else s=MixedScan(t,FALSE);
 return MixedLeave(s);
}
static EFI_STATUS MixedTargetRead(EFI_HANDLE h,EFI_PCI_IO_PROTOCOL *expected,BOOLEAN bar,BOOLEAN freshContainment) {
 if(h!=MixedState.Target || !expected || (!freshContainment && expected!=MixedState.TargetIo))return EFI_NOT_READY;
 EFI_PCI_IO_PROTOCOL *io=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
 EFI_STATUS s=Interface(MixedBs,h,&PciGuid,(VOID **)&io);
 if(s!=EFI_SUCCESS || io!=expected || !io->GetLocation || !io->Pci.Read)return EFI_NOT_READY;
 UINTN seg=0,bus=0,dev=0,fun=0;UINT32 cfg[16]={0};
 s=io->GetLocation(io,&seg,&bus,&dev,&fun);
 if(s!=EFI_SUCCESS || seg!=GU_TARGET_SEGMENT || bus!=GU_TARGET_BUS || dev || fun)return EFI_NOT_READY;
 s=Interface(MixedBs,h,&PathGuid,(VOID **)&path);
 if(s!=EFI_SUCCESS || !PathMatches(path,MixedTargetPath,sizeof(MixedTargetPath)))return EFI_NOT_READY;
 s=io->Pci.Read(io,EfiPciIoWidthUint32,0,16,cfg);
 if(s!=EFI_SUCCESS || cfg[0]!=GU_BOARD_ID || cfg[11]!=GU_BOARD_SUBSYSTEM ||
    cfg[2]!=0x030000a1U || ((cfg[3]>>16)&0x7fU))return EFI_NOT_READY;
 if(bar && (cfg[4]!=(UINT32)GU_BAR0_BASE || !(cfg[1]&2U)))return EFI_NOT_READY;
 return EFI_SUCCESS;
}
EFI_STATUS GuMixedFast(EFI_HANDLE h,EFI_PCI_IO_PROTOCOL *io,BOOLEAN bar) {
 EFI_STATUS s=MixedEnter(FALSE);if(s!=EFI_SUCCESS)return s;
 s=MixedTargetRead(h,io,bar,FALSE);
 if(s==EFI_SUCCESS)s=MixedReadCompanion(MixedBs,MixedState.Companion,MixedState.CompanionIo);
 return MixedLeave(s);
}
/* Only the companion is read here: valid during the target's reset quiet time. */
EFI_STATUS GuMixedCompanionFast(VOID) {
 EFI_STATUS s=MixedEnter(FALSE);if(s!=EFI_SUCCESS)return s;
 return MixedLeave(MixedReadCompanion(MixedBs,MixedState.Companion,MixedState.CompanionIo));
}
/* Emergency target containment stays available after a companion refusal.
 * This API grants no new transaction writes: the caller may only clear BME.
 */
EFI_STATUS GuMixedContainTarget(EFI_HANDLE h,EFI_PCI_IO_PROTOCOL *io) {
 EFI_STATUS s=MixedEnter(TRUE);if(s!=EFI_SUCCESS)return s;
 return MixedLeave(MixedTargetRead(h,io,FALSE,TRUE));
}
#ifdef GU_MIXED_PORT_HOST_TEST
VOID GuMixedHostReset(VOID) {LastBridgeCommand=0xffffffffU;LastBridgeRequired=FALSE;LastBridgeStatus=EFI_NOT_STARTED;MixedState=(MIXED_STATE){0};MixedBs=NULL;MixedBusy=MixedReentered=FALSE;}
#endif

EFI_STATUS GuMixedCheckpoint(VOID) {return GuMixedVerify(MixedState.Table);}

/* Observe the upstream DMA path only. Never call bridge Attributes or write PCI.
 * If native target BME enable leaves the ancestor disabled, refuse firmware/DMA.
 */
EFI_STATUS GuMixedDmaPath(BOOLEAN master) {
 EFI_STATUS status=MixedEnter(FALSE);if(status!=EFI_SUCCESS)return status;
 LastBridgeRequired=master;
 status=MixedReadBridge(MixedBs,MixedState.Bridge,MixedState.BridgeIo,master);
 LastBridgeStatus=status;
 if(status==EFI_SUCCESS)status=MixedReadCompanion(MixedBs,MixedState.Companion,MixedState.CompanionIo);
 return MixedLeave(status);
}

VOID GuMixedDmaObservation(UINT32 *command,BOOLEAN *required,EFI_STATUS *status) {
 if(command)*command=LastBridgeCommand;
 if(required)*required=LastBridgeRequired;
 if(status)*status=LastBridgeStatus;
}
