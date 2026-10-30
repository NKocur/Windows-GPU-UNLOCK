/* GPL-2.0-only. Host process, synthetic EFI services backed by complete captures.
 * Captured firmware code bytes are hashed as data, NEVER executed.
 */
#include "test_headless_full.c"
#include "release-only-flow.h"
#define PCI_COUNT (sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0]))
static EFI_PCI_IO_PROTOCOL MixedIos[PCI_COUNT],Replacement;
static const UINT8 CapturedBridgePath[]={2,1,12,0,208,65,3,10,0,0,0,0,1,1,6,0,1,3,127,255,4,0};
static const UINT32 CapturedBridgeConfig[16]={0x14831022U,0x100003U,0x6040000U,0x810010U,0x0U,0x0U,0x2b2b00U,0x2000e1e1U,0xfc00fb00U,0x1f10001U,0x70U,0x78U,0x0U,0x50U,0x0U,0x1000ffU};
static UINT32 BridgeModelConfig[16];
static int BridgeFault;

static const UINT8 MixedMockTargetPath[]={2,1,12,0,208,65,3,10,0,0,0,0,1,1,6,0,1,3,1,1,6,0,0,0,127,255,4,0};
static UINT32 CompanionConfig[64];
static UINT8 CompanionPath[sizeof(FixtureV100Path)];
static int MixedFault,Injected,ForbiddenCalls,ReadCalls,ConfigBit;
static const char *EventFault,*BoundaryFault;
static int StopFault;
static BOOLEAN FreeFault,FreeArmed;
static UINTN V100Index;
enum {F_NONE,F_MISSING,F_DUPLICATE,F_EXTRA_DISPLAY,F_LOCATION,F_SUBSYSTEM,
 F_PATH,F_PROTOCOL,F_READ_ERROR,F_READ_WARNING,F_CONFIG_BIT,F_OWNER,F_MISSING_PATH,F_NULL_PCI,F_COMPANION_GOP};
static BOOLEAN MixedMockCompanionGop(VOID) {return MixedFault==F_COMPANION_GOP;}
static VOID Drift(VOID) {CompanionConfig[12]^=1;Injected++;}
static VOID MixedMockEvent(const char *event) {
  if(EventFault && !strcmp(EventFault,event) && !Injected)Drift();
  if(FreeFault && !strcmp(event,"detach-nvidia-gop-intent"))FreeArmed=TRUE;
}
static VOID MixedMockBoundary(const char *boundary) {
  if(BoundaryFault && !strcmp(BoundaryFault,boundary) && !Injected &&
     (strcmp(boundary,"restore-tpl") || Calls==2))Drift();
}
static VOID MixedMockStopped(int calls) {
 if(calls==3){BridgeModelConfig[1]&=~4U;if(BridgeFault==2)BridgeModelConfig[1]^=2U;}
 if(calls==StopFault && !Injected)Drift();
}
static VOID MixedMockFree(VOID) {if(FreeArmed && !Injected)Drift();}
static UINTN MixedIndex(EFI_PCI_IO_PROTOCOL *io) {
  for(UINTN i=0;i<PCI_COUNT;i++)if(io==&MixedIos[i] || (io==&Replacement && i==V100Index))return i;
  Check(FALSE,"PCI mock index exists");return 0;
}
static EFI_STATUS EFIAPI MixedLocation(EFI_PCI_IO_PROTOCOL *io,UINTN *seg,UINTN *bus,UINTN *dev,UINTN *fun) {
  UINTN i=MixedIndex(io);const UINTN *a=MixedPciFixtures[i].Location;
  *seg=a[0];*bus=a[1];*dev=a[2];*fun=a[3];
  if(i==V100Index && MixedFault==F_LOCATION)(*bus)++;
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI MixedRead(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 at,UINTN n,VOID *out) {
  UINTN i=MixedIndex(io);ReadCalls++;
  Check(w==EfiPciIoWidthUint32 && at==0 && (n==16 || (i==V100Index && n==64)),"PCI mock only bounded configuration reads");
  if(i==V100Index) {
    if(MixedFault==F_READ_ERROR)return EFI_DEVICE_ERROR;
    if(MixedFault==F_READ_WARNING)return EFI_WARN_UNKNOWN_GLYPH;
    memcpy(out,CompanionConfig,n*4);
  } else {
    const UINTN *location=MixedPciFixtures[i].Location;
    if(!location[0] && !location[1] && location[2]==3 && location[3]==1)memcpy(out,BridgeModelConfig,n*4);
    else memcpy(out,MixedPciFixtures[i].Config,n*4);
    if(MixedFault==F_EXTRA_DISPLAY && i==0)((UINT32 *)out)[2]=0x03000000;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ForbiddenPciWrite(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 at,UINTN n,VOID *out) {
  (void)io;(void)w;(void)at;(void)n;(void)out;ForbiddenCalls++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI ForbiddenMemIo(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 at,UINTN n,VOID *out) {
  (void)io;(void)w;(void)bar;(void)at;(void)n;(void)out;ForbiddenCalls++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI ForbiddenAttributes(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out) {
  (void)io;(void)op;(void)mask;(void)out;ForbiddenCalls++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS MixedMockLocate(UINTN *n,EFI_HANDLE **out) {
  *n=PCI_COUNT-(MixedFault==F_MISSING?1:0);*out=malloc(*n*sizeof(**out));
  UINTN count=0;
  for(UINTN i=0;i<PCI_COUNT;i++) {
    if(MixedFault==F_MISSING && MixedPciFixtures[i].Handle==FX_V100_PCI)continue;
    (*out)[count++]=MixedPciFixtures[i].Handle;
  }
  Check(count==*n,"all captured PCI functions enumerated");
  if(MixedFault==F_DUPLICATE)(*out)[0]=(*out)[1];
  return EFI_SUCCESS;
}
static EFI_STATUS MixedMockHandle(EFI_HANDLE h,EFI_GUID *g,VOID **out) {
  if(h==FX_RtxPci){if(GuGuidEqual(g,&PathGuid)){*out=(VOID *)MixedMockTargetPath;return EFI_SUCCESS;}return EFI_NOT_FOUND;}
  for(UINTN i=0;i<PCI_COUNT;i++)if(MixedPciFixtures[i].Handle==h) {
    const UINTN *location=MixedPciFixtures[i].Location;
    if(GuGuidEqual(g,&PathGuid) && !location[0] && !location[1] && location[2]==3 && location[3]==1) {
      *out=(VOID *)CapturedBridgePath;return EFI_SUCCESS;
    }
    if(GuGuidEqual(g,&PciGuid)) {
      if(h==FX_V100_PCI && MixedFault==F_NULL_PCI){*out=NULL;return EFI_SUCCESS;}
      *out=h==FX_V100_PCI && MixedFault==F_PROTOCOL?(VOID *)&Replacement:(VOID *)&MixedIos[i];
      return EFI_SUCCESS;
    }
    if(h==FX_V100_PCI && GuGuidEqual(g,&PathGuid)) {
      if(MixedFault==F_MISSING_PATH)return EFI_UNSUPPORTED;
      *out=CompanionPath;return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}
static VOID MixedMockReset(VOID) {
  GuMixedHostReset();
  Injected=ForbiddenCalls=ReadCalls=0;FreeArmed=FALSE;
  memcpy(CompanionConfig,FixtureV100FullConfig,sizeof(CompanionConfig));
  memcpy(CompanionPath,FixtureV100Path,sizeof(CompanionPath));
  memcpy(BridgeModelConfig,CapturedBridgeConfig,sizeof(BridgeModelConfig));BridgeModelConfig[1]|=4U;
  if(BridgeFault==1)BridgeModelConfig[4]^=1U;
  for(UINTN i=0;i<PCI_COUNT;i++) {
    MixedIos[i]=(EFI_PCI_IO_PROTOCOL){0};MixedIos[i].Pci.Read=MixedRead;MixedIos[i].GetLocation=MixedLocation;
    MixedIos[i].Pci.Write=ForbiddenPciWrite;
    MixedIos[i].Mem.Read=MixedIos[i].Mem.Write=MixedIos[i].Io.Read=MixedIos[i].Io.Write=ForbiddenMemIo;
    MixedIos[i].Attributes=ForbiddenAttributes;
    if(MixedPciFixtures[i].Handle==FX_V100_PCI)V100Index=i;
  }
  Replacement=MixedIos[V100Index];
  if(MixedFault==F_SUBSYSTEM)CompanionConfig[11]^=1;
  if(MixedFault==F_CONFIG_BIT)CompanionConfig[ConfigBit/32]^=1U<<(ConfigBit%32);
  if(MixedFault==F_PATH)CompanionPath[17]^=1;
  if(MixedFault==F_OWNER)Live.Edges[Live.Count++]=(EDGE){FX_V100_PCI,FX_NvAgent,FX_V100_PCI,PciGuid,EFI_OPEN_PROTOCOL_BY_DRIVER,1};
}
static VOID ClearFaults(VOID) {
  MixedFault=F_NONE;EventFault=BoundaryFault=NULL;StopFault=0;FreeFault=FALSE;
  FullScenario=0;EnableCalls=ClaimCalls=CloseCalls=0;CapacityProbe=FALSE;
}
static VOID NoCompanionEffects(VOID) {Check(ForbiddenCalls==0,"zero companion MMIO, IO, config writes or Attributes calls");}
static VOID EarlyTests(VOID) {
  const int faults[]={F_MISSING,F_DUPLICATE,F_EXTRA_DISPLAY,F_LOCATION,F_SUBSYSTEM,F_PATH,
    F_READ_ERROR,F_READ_WARNING,F_OWNER,F_MISSING_PATH,F_NULL_PCI,F_COMPANION_GOP};
  for(UINTN i=0;i<sizeof(faults)/sizeof(faults[0]);i++) {
    ClearFaults();MixedFault=faults[i];
    Check(Run(0)!=EFI_SUCCESS && Calls==0,"initial companion/inventory fault blocks every disconnect");NoCompanionEffects();
  }
  /* Exhaust every bit of the actual 256-byte companion configuration. */
  for(ConfigBit=0;ConfigBit<2048;ConfigBit++) {
    ClearFaults();MixedFault=F_CONFIG_BIT;
    Check(Run(0)!=EFI_SUCCESS && Calls==0,"every companion config bit is pinned before any detach");NoCompanionEffects();
  }
}
static VOID LateTests(VOID) {
  const char *events[]={"detach-rtx-textout-intent","detach-rtx-textout-return",
    "detach-rtx-graphics-console-return","detach-nvidia-gop-intent","detach-nvidia-return",
    "pci-decode-restore-intent-io-mem-only","full-release-ready-for-original-unlock"};
  const int calls[]={0,1,2,2,3,3,3};
  for(UINTN i=0;i<sizeof(events)/sizeof(events[0]);i++) {
    ClearFaults();EventFault=events[i];
    Check(RunFull(0)!=EFI_SUCCESS && Injected==1,"log-callback companion drift is refused");
    Check(Calls==calls[i],"next driver disconnect blocked after companion drift");
    Check(EnableCalls==(i==6?1:0),"decode restore never follows earlier companion fault");
    if(i==5)Check(ClaimCalls==1 && CloseCalls==1,"held target claim closed after pre-enable companion fault");
    NoCompanionEffects();
  }
  for(int stop=1;stop<=3;stop++) {
    ClearFaults();StopFault=stop;
    Check(RunFull(0)!=EFI_SUCCESS && Injected==1 && Calls==stop && !EnableCalls,"stop-callback drift blocks next mutation");NoCompanionEffects();
  }
  const char *boundaries[]={"claim","enable","close","restore-tpl"};
  for(UINTN i=0;i<sizeof(boundaries)/sizeof(boundaries[0]);i++) {
    ClearFaults();BoundaryFault=boundaries[i];
    Check(RunFull(0)!=EFI_SUCCESS && Injected==1,"claim/enable/close/TPL callback drift cannot succeed");
    if(i==0)Check(ClaimCalls==1 && CloseCalls==1 && !EnableCalls,"exclusive target claim closed on guard refusal");
    if(i==1 || i==2)Check(EnableCalls==1 && CloseCalls==1,"post-effect fault stays failure and closes claim");
    if(i==3)Check(Calls==2 && !ClaimCalls && !EnableCalls,"TPL return callback drift blocks NV stop");
    NoCompanionEffects();
  }
  ClearFaults();FreeFault=TRUE;
  Check(RunFull(0)!=EFI_SUCCESS && Injected==1 && Calls==2 && !EnableCalls,"allocation/free callback drift caught before NV stop");NoCompanionEffects();
}
static VOID FinalTests(VOID) {
  for(ConfigBit=0;ConfigBit<2048;ConfigBit++) {
    ClearFaults();Check(RunFull(0)==EFI_SUCCESS,"fresh released state for post-release config mutation");
    CompanionConfig[ConfigBit/32]^=1U<<(ConfigBit%32);
    Check(GuVerifyReleased(LastTable,FALSE)!=EFI_SUCCESS,"every post-release companion bit drift blocks handoff");
    CompanionConfig[ConfigBit/32]^=1U<<(ConfigBit%32);
    Check(GuVerifyReleased(LastTable,FALSE)!=EFI_SUCCESS,"companion fault remains latched for this session");NoCompanionEffects();
  }
  ClearFaults();Check(RunFull(0)==EFI_SUCCESS,"new session clears old refusal only through complete preflight");
  MixedFault=F_PROTOCOL;
  Check(GuVerifyReleased(LastTable,FALSE)!=EFI_SUCCESS,"same config under replaced PCI protocol is refused");NoCompanionEffects();
  ClearFaults();Check(RunFull(0)==EFI_SUCCESS,"new session for post-release owner fault");
  Live.Edges[Live.Count++]=(EDGE){FX_V100_PCI,FX_NvAgent,FX_V100_PCI,PciGuid,EFI_OPEN_PROTOCOL_EXCLUSIVE,1};
  Check(GuVerifyReleased(LastTable,FALSE)!=EFI_SUCCESS,"new companion exclusive owner blocks final handoff");NoCompanionEffects();
  Live.Count--;
  Check(GuVerifyReleased(LastTable,FALSE)!=EFI_SUCCESS,"companion ownership fault remains latched even if owner disappears");
  ClearFaults();Check(RunFull(0)==EFI_SUCCESS,"last clean complete release replay");
  Check(Calls==3 && EnableCalls==1 && ClaimCalls==1 && CloseCalls==1,"only exact RTX release and IO/MEM restore operations");
  Check(!memcmp(CompanionConfig,FixtureV100FullConfig,sizeof(CompanionConfig)),"companion full PCI configuration unchanged");NoCompanionEffects();
}

/* Real full-flow state machine and exact FullWindows/ReportVerification bodies.
 * Unlock and EFI StartImage are test counters, never executable firmware.
 */
static int FlowFault,FlowStarts,FlowUnlocks,FlowSkipStarts,FlowCaptures,FlowCloses,FlowSays,FlowScreens;
static EFI_HANDLE MixedWindowsImage=(EFI_HANDLE)77777;
static VOID *MixedHandoffLog=(VOID *)1;
static EFI_STATUS MixedFlowRecord(VOID *ctx,const char *event,EFI_STATUS status) {
  (void)ctx;(void)status;
  const char *events[]={NULL,NULL,"automatic-unlock-start-Esc-cancels",NULL,
    "original-unlock-return","full-windows-start","windows-preclose-verify-begin",
    "verify-complete","windows-close-files-next-then-final-verify-and-StartImage"};
  if(FlowFault>0 && FlowFault<(int)(sizeof(events)/sizeof(events[0])) &&
     events[FlowFault] && !strcmp(event,events[FlowFault]) && !Injected)Drift();
  return EFI_SUCCESS;
}
static VOID MixedSay(CHAR16 *message) {(void)message;FlowSays++;if(FlowFault==11 && FlowSays==2 && !Injected)Drift();}
static VOID MixedScreenStatus(const char *label,EFI_STATUS status) {
  (void)label;(void)status;FlowScreens++;
  if(FlowFault==12 && FlowScreens==2 && !Injected)Drift();
}
static EFI_STATUS MixedCloseBootFiles(EFI_STATUS *log,EFI_STATUS *root) {
  FlowCloses++;*log=*root=EFI_SUCCESS;
  if(FlowFault==9 && !Injected)Drift();
  if(FlowFault==10){*log=EFI_DEVICE_ERROR;return EFI_DEVICE_ERROR;}
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI MixedStartImage(EFI_HANDLE image,UINTN *size,CHAR16 **data) {
  Check(image==MixedWindowsImage && !size && !data,"only expected mocked Windows image starts");
  FlowStarts++;return EFI_SUCCESS;
}
#define Log MixedHandoffLog
#define Table LastTable
#define Record MixedFlowRecord
#define Say MixedSay
#define ScreenStatus MixedScreenStatus
#define CloseBootFiles MixedCloseBootFiles
#define WindowsImage MixedWindowsImage
#include "captured-windows-handoff.h"
#undef Log
#undef Table
#undef Record
#undef Say
#undef ScreenStatus
#undef CloseBootFiles
#undef WindowsImage
static EFI_STATUS MixedOpsPrepare(VOID *ctx) {(void)ctx;return EFI_SUCCESS;}
static EFI_STATUS MixedOpsChoose(VOID *ctx,BOOLEAN *skip) {(void)ctx;*skip=FlowFault==14;return EFI_SUCCESS;}
static EFI_STATUS MixedOpsCapture(VOID *ctx) {
  (void)ctx;FlowCaptures++;
  if(FlowFault==1 && FlowCaptures==2 && !Injected)Drift();
  return EFI_SUCCESS;
}
static EFI_STATUS MixedOpsRelease(VOID *ctx) {
  (void)ctx;
  if(FlowFault==15)EventFault="detach-nvidia-gop-intent";
  EFI_STATUS status=RunFull(0);
  LastTable->BootServices->StartImage=MixedStartImage;
  return status;
}
static EFI_STATUS MixedOpsVerify(VOID *ctx) {(void)ctx;return GuVerifyReleased(LastTable,TRUE);}
static EFI_STATUS MixedOpsUnlock(VOID *ctx) {
  (void)ctx;FlowUnlocks++;
  if(FlowFault==3 && !Injected)Drift();
  return FlowFault==13?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS MixedOpsSkip(VOID *ctx) {(void)ctx;FlowSkipStarts++;return EFI_SUCCESS;}
static EFI_STATUS MixedOpsWindows(VOID *ctx) {return FullWindows(ctx);}
static EFI_STATUS RunHandoff(int fault) {
  ClearFaults();Calls=0;FlowFault=fault;
  FlowStarts=FlowUnlocks=FlowSkipStarts=FlowCaptures=FlowCloses=FlowSays=FlowScreens=0;
  const GU_FULL_FLOW_OPS ops={NULL,MixedOpsPrepare,MixedOpsChoose,MixedOpsCapture,MixedOpsRelease,
    MixedOpsVerify,MixedOpsUnlock,MixedOpsSkip,MixedOpsWindows,MixedFlowRecord};
  return GuFullFlow(&ops);
}
static VOID HandoffTests(VOID) {
  Check(RunHandoff(0)==EFI_ABORTED && FlowStarts==1 && FlowUnlocks==1 &&
    FlowCloses==1 && !FlowSkipStarts,"real clean Windows handoff reaches only mocked expected StartImage");
  NoCompanionEffects();
  for(int fault=1;fault<=12;fault++) {
    Check(RunHandoff(fault)!=EFI_SUCCESS && FlowStarts==0 && !FlowSkipStarts,
      "companion/log/file/console handoff fault prevents Windows and fallback");
    if(fault!=10)Check(Injected==1,"handoff companion mutation actually injected");
    if(fault<=2)Check(FlowUnlocks==0,"pre-unlock drift blocks mocked unlock");
    else Check(FlowUnlocks==1,"post-unlock handoff never retries mocked unlock");
    NoCompanionEffects();
  }
  Check(RunHandoff(13)==EFI_DEVICE_ERROR && FlowUnlocks==1 && !FlowStarts &&
    !FlowSkipStarts && !FlowCloses,"unlock callback error never hands off to Windows or fallback");
  Check(RunHandoff(14)==EFI_ABORTED && FlowSkipStarts==1 && !FlowStarts &&
    !FlowUnlocks && !Calls && !FlowCaptures,"explicit skip has no release, unlock or GPU operation");
  Check(RunHandoff(15)!=EFI_SUCCESS && !FlowStarts && !FlowUnlocks &&
    !FlowSkipStarts && Calls==2,"mixed-GPU release refusal blocks both unlock and Windows fallback");
  NoCompanionEffects();
  Check(RunHandoff(0)==EFI_ABORTED && FlowStarts==1,"independent clean handoff remains possible");
}


static EFI_STATUS RunRealReleaseOnly(int fault) {
 ClearFaults();Calls=0;FlowFault=fault;
 FlowStarts=FlowUnlocks=FlowSkipStarts=FlowCaptures=FlowCloses=FlowSays=FlowScreens=0;
 const GU_RELEASE_ONLY_OPS ops={NULL,MixedOpsPrepare,MixedOpsChoose,MixedOpsCapture,MixedOpsRelease,
   MixedOpsVerify,MixedOpsSkip,MixedOpsWindows,MixedFlowRecord};
 return GuReleaseOnlyFlow(&ops);
}
static VOID ReleaseOnlyTests(VOID) {
 Check(RunRealReleaseOnly(0)==EFI_ABORTED && FlowStarts==1 && !FlowUnlocks && FlowCaptures==2,"real guarded release-only flow never invokes unlock");
 const int bad[]={1,7,8,9,10,11,12,15};
 for(UINTN i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
  Check(RunRealReleaseOnly(bad[i])!=EFI_SUCCESS && !FlowStarts && !FlowUnlocks && !FlowSkipStarts,
    "release-only partial/callback/close faults cannot reach Windows or unlock");
  NoCompanionEffects();
 }
 Check(RunRealReleaseOnly(14)==EFI_ABORTED && FlowSkipStarts==1 && !FlowUnlocks && !Calls,"release-only explicit skip has no GPU operation");
}

static VOID UpstreamReleaseTests(VOID) {
 ClearFaults();BridgeFault=1;
 Check(Run(0)!=EFI_SUCCESS && !Calls,"wrong upstream bridge config blocks every detach");
 BridgeFault=0;ClearFaults();BridgeFault=2;
 Check(RunFull(0)!=EFI_SUCCESS && Calls==3 && !EnableCalls && !ClaimCalls,"unexpected upstream mutation after NVIDIA stop blocks restore/unlock");
 BridgeFault=0;ClearFaults();
 Check(RunFull(0)==EFI_SUCCESS && !(BridgeModelConfig[1]&4U),"observed upstream BME clear is read/recorded without bridge write");
}
int main(int argc,char **argv) {
  ClearFaults();
  Check(GuCapturedLegacySuite(argc,argv)==0,"original no-EDID release suite passes with companion guard");
  ClearFaults();EarlyTests();LateTests();FinalTests();HandoffTests();ReleaseOnlyTests();UpstreamReleaseTests();
  printf("PASS mixed-GPU guard replay: %d assertions including real Windows handoff; %u captured PCI functions, %u handles; no hardware access\n",Checks,(unsigned)PCI_COUNT,(unsigned)FX_HANDLES);
  return 0;
}
