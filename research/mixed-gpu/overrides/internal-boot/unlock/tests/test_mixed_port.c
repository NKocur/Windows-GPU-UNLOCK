/* GPL-2.0-only. Actual guarded port.c, all devices and firmware services in RAM. */
#define main OriginalPortSuite
#include "test_port.c"
#undef main
#include "../mixed-guard.h"
#include "../mixed-policy.h"
#include "mixed-pci-fixture.h"
static EFI_SYSTEM_TABLE MixedTable;
static EFI_PCI_IO_PROTOCOL Others[sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0])];
static EFI_HANDLE TargetHandle,CompanionHandle;
static UINT32 CConfig[64],TargetInitial[64];
static UINT64 Cache;
static EFI_GUID PG=EFI_PCI_IO_PROTOCOL_GUID,DG=EFI_DEVICE_PATH_PROTOCOL_GUID;
static int Assertions,Forbidden,V100Reads,Reenter,Injected,QuietDrift,SwapTarget,SwapWrite;
static const char *Inject;
static VOID Need(int ok,const char *label) {Assertions++;if(!ok){fprintf(stderr,"FAIL %s at %d\n",label,Assertions);exit(1);}}
static VOID Drift(const char *where) {if(Inject && !strcmp(Inject,where) && !Injected){CConfig[12]^=1;Injected++;}}
static BOOLEAN EqualGuid(const EFI_GUID *a,const EFI_GUID *b) {return !memcmp(a,b,16);}
static UINTN OtherIndex(EFI_PCI_IO_PROTOCOL *io) {
 for(UINTN i=0;i<sizeof(Others)/sizeof(Others[0]);i++)if(io==&Others[i])return i;
 Need(0,"known other PCI protocol");return 0;
}
static EFI_STATUS EFIAPI OtherLocation(EFI_PCI_IO_PROTOCOL *io,UINTN *s,UINTN *b,UINTN *d,UINTN *f) {
 const UINTN *loc=MixedPciFixtures[OtherIndex(io)].Location;*s=loc[0];*b=loc[1];*d=loc[2];*f=loc[3];return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI TargetLocation(EFI_PCI_IO_PROTOCOL *io,UINTN *s,UINTN *b,UINTN *d,UINTN *f) {
 Need(io==&pci,"only exact target location");*s=0;*b=GU_TARGET_BUS;*d=*f=0;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NoOtherMem(EFI_PCI_IO_PROTOCOL *,EFI_PCI_IO_PROTOCOL_WIDTH,UINT8,UINT64,UINTN,VOID *);
static EFI_STATUS EFIAPI OtherRead(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *out) {
 UINTN index=OtherIndex(io);Need(w==EfiPciIoWidthUint32 && !off && (n==16 || n==64),"other devices configuration reads only");
 if(MixedPciFixtures[index].Handle==CompanionHandle) {
  V100Reads++;
  if(Reenter){Reenter=0;Need(GuMixedCompanionFast()!=EFI_SUCCESS,"recursive guard denied");}
  memcpy(out,CConfig,n*4);
  if(n==64 && SwapTarget){SwapTarget=0;Gu.pci=io;}
  if(n==64 && SwapWrite){SwapWrite=0;pci.Mem.Write=NoOtherMem;}
 }else {Need(n==16,"only companion can have full config read");memcpy(out,MixedPciFixtures[index].Config,64);}
 return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NoOtherWrite(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *data) {
 (void)io;(void)w;(void)off;(void)n;(void)data;Forbidden++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI NoOtherMem(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 off,UINTN n,VOID *data) {
 (void)io;(void)w;(void)bar;(void)off;(void)n;(void)data;Forbidden++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI NoOtherAttributes(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out) {
 (void)io;(void)op;(void)mask;(void)out;Forbidden++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI PciHandles(EFI_LOCATE_SEARCH_TYPE search,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **out) {
 (void)key;Need(search==ByProtocol && EqualGuid(g,&PG),"guard PCI enumeration only");
 *n=sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0]);*out=malloc(*n*sizeof(**out));
 for(UINTN i=0;i<*n;i++)(*out)[i]=MixedPciFixtures[i].Handle;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Protocol(EFI_HANDLE h,EFI_GUID *g,VOID **out) {
 *out=NULL;
 if(EqualGuid(g,&PG))for(UINTN i=0;i<sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0]);i++)if(h==MixedPciFixtures[i].Handle)*out=h==TargetHandle?(VOID *)&pci:(VOID *)&Others[i];
 if(EqualGuid(g,&DG) && h==TargetHandle)*out=(VOID *)MixedTargetPath;
 if(EqualGuid(g,&DG) && h==CompanionHandle)*out=(VOID *)MixedExpectedPath;
 return *out?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI Protos(EFI_HANDLE h,EFI_GUID ***out,UINTN *n) {
 Need(h==CompanionHandle,"only companion protocol ownership queried");
 *n=2;*out=malloc(2*sizeof(**out));(*out)[0]=&PG;(*out)[1]=&DG;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI OpenInfo(EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,UINTN *n) {
 (void)g;Need(h==CompanionHandle,"only companion opens queried");*n=0;*out=NULL;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Attr(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out) {
 Need(io==&pci,"attributes only target");
 if(op==EfiPciIoAttributeOperationGet){*out=Cache;return EFI_SUCCESS;}
 if(op==EfiPciIoAttributeOperationSupported){*out=0x700;return EFI_SUCCESS;}
 Need(mask==EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,"port attributes only BME");
 if(op==EfiPciIoAttributeOperationEnable){Cache|=mask;config[4]|=4;Drift("enable");return EFI_SUCCESS;}
 Need(op==EfiPciIoAttributeOperationDisable,"containment only BME disable");Cache&=~mask;config[4]&=(UINT8)~4U;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI HookAllocate(EFI_PCI_IO_PROTOCOL *io,EFI_ALLOCATE_TYPE t,EFI_MEMORY_TYPE m,UINTN n,VOID **cpu,UINT64 attrs) {
 EFI_STATUS s=allocate(io,t,m,n,cpu,attrs);Drift("allocate");return s;
}
static EFI_STATUS EFIAPI HookMap(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_OPERATION op,VOID *cpu,UINTN *n,EFI_PHYSICAL_ADDRESS *d,VOID **mapout) {
 EFI_STATUS s=map(io,op,cpu,n,d,mapout);Drift("map");return s;
}
static EFI_STATUS EFIAPI HookUnmap(EFI_PCI_IO_PROTOCOL *io,VOID *token) {EFI_STATUS s=unmap(io,token);Drift("unmap");return s;}
static EFI_STATUS EFIAPI HookFree(EFI_PCI_IO_PROTOCOL *io,UINTN n,VOID *cpu) {EFI_STATUS s=freebuffer(io,n,cpu);Drift("free");return s;}
static EFI_STATUS EFIAPI HookMemWrite(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 off,UINTN n,VOID *data) {
 EFI_STATUS s=writemem(io,w,bar,off,n,data);Drift("mmio");return s;
}
static EFI_STATUS EFIAPI HookConfigWrite(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *data) {
 EFI_STATUS s=writeconfig(io,w,off,n,data);Drift("pci");return s;
}
static EFI_STATUS EFIAPI HookStall(UINTN n) {
 EFI_STATUS s=stall(n);if(QuietDrift && n==100000){CConfig[12]^=1;Injected++;QuietDrift=0;}return s;
}
static VOID DisposeRamOnly(VOID) {
 /* Test process disposal only: these are malloc pointers and fake map tokens,
  * never real DMA. Production correctly retains them until physical power-off. */
 GU_DMA *entry=Gu.allocations;
 while(entry){GU_DMA *next=entry->next;if(entry->cpu)_aligned_free(entry->cpu);free(entry);entry=next;}
 Gu.allocations=NULL;
}
static VOID MixedSetup(VOID) {
 DisposeRamOnly();setup();GuMixedHostReset();Inject=NULL;Injected=Forbidden=Reenter=QuietDrift=SwapTarget=SwapWrite=0;V100Reads=0;
 memcpy(config,TargetInitial,256);memcpy(CConfig,MixedExpectedConfig,256);
 for(UINTN i=0;i<sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0]);i++){
  Others[i]=(EFI_PCI_IO_PROTOCOL){0};Others[i].GetLocation=OtherLocation;Others[i].Pci.Read=OtherRead;
  Others[i].Pci.Write=NoOtherWrite;Others[i].Mem.Read=Others[i].Mem.Write=Others[i].Io.Read=Others[i].Io.Write=NoOtherMem;Others[i].Attributes=NoOtherAttributes;
  if(MixedPciFixtures[i].Config[0]==GU_BOARD_ID)TargetHandle=MixedPciFixtures[i].Handle;
  if(MixedPciFixtures[i].Config[0]==0x1df010de)CompanionHandle=MixedPciFixtures[i].Handle;
 }
 pci.GetLocation=TargetLocation;pci.Attributes=Attr;pci.AllocateBuffer=HookAllocate;pci.Map=HookMap;pci.Unmap=HookUnmap;pci.FreeBuffer=HookFree;
 pci.Mem.Write=HookMemWrite;pci.Pci.Write=HookConfigWrite;
 bs.LocateHandleBuffer=PciHandles;bs.HandleProtocol=Protocol;bs.ProtocolsPerHandle=Protos;bs.OpenProtocolInformation=OpenInfo;bs.Stall=HookStall;
 MixedTable=(EFI_SYSTEM_TABLE){0};MixedTable.BootServices=&bs;
 Gu.handle=TargetHandle;Gu.image=(EFI_HANDLE)9999;Gu.bar_base=GU_BAR0_BASE;Gu.bar_size=0x1000000;
 Cache=(config[4]&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(config[4]&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
 Gu.prom_verified=TRUE;registers[0x82381c/4]=0x36061141;registers[0x823820/4]=7;
#ifdef GU_ENTRY_LATCHED
 Need(GuLatchEntrySelectors(registers[0x82381c/4],7),"entry selectors latched from this synthetic boot");
#endif
 Need(GuMixedBegin(&MixedTable)==EFI_SUCCESS,"exact two-GPU module preflight");
}
static VOID NoCompanionEffects(VOID) {Need(!Forbidden,"no non-target MMIO/IO/writes/attributes");}
static VOID Selectors(VOID) {
 GuWrite32(0x82381c,0x88888888);
#ifdef GU_ENTRY_LATCHED_SS1_FULL
 Need(GuSelectorPhase(GU_SELECTOR_PHASE_SS0_WRITTEN),"SS0 phase");Need(GuSelectorPhase(GU_SELECTOR_PHASE_SS1_WRITE),"SS1 phase");
#endif
 GuWrite32(0x823820,8);
#ifdef GU_ENTRY_LATCHED_SS1_FULL
 Need(GuSelectorPhase(GU_SELECTOR_PHASE_TARGET),"selector target phase");
#endif
 Need(!Gu.fault,"selector sequence remains valid");
}
int main(int argc,char **argv) {
 Need(argc==2,"actual target 256-byte config required");FILE *f=fopen(argv[1],"rb");
 Need(f && fread(TargetInitial,1,256,f)==256,"load actual target config");fclose(f);
 MixedSetup();GuWrite32(0x82381c,0x88888888);Need(fake.writes==1 && !Gu.fault,"clean target MMIO succeeds");NoCompanionEffects();
 for(int bit=0;bit<2048;bit++){
  MixedSetup();CConfig[bit/32]^=1U<<(bit%32);GuWrite32(0x82381c,0x88888888);
  Need(!fake.writes && Gu.fault && Gu.retained,"every companion bit blocks actual port MMIO");
  CConfig[bit/32]^=1U<<(bit%32);
  Need(GuMixedBegin(&MixedTable)!=EFI_SUCCESS,"same live session cannot rearm after companion refusal");NoCompanionEffects();
 }
 MixedSetup();Inject="mmio";GuWrite32(0x82381c,0x88888888);Need(fake.writes==1 && Gu.fault,"post-MMIO drift becomes fault");GuWrite32(0x82381c,0x88888888);Need(fake.writes==1,"no second MMIO after fault");
 MixedSetup();UINT16 ctl=*(UINT16 *)(config+0x80);Inject="pci";Need(GuConfigWrite(EfiPciIoWidthUint16,0x80,&ctl)!=EFI_SUCCESS && fake.pciwrites==1 && Gu.retained,"post-PCI drift retained");
 MixedSetup();Need(GuEnableMaster()==EFI_SUCCESS && (config[4]&4),"clean target BME enable");
 CConfig[12]^=1;GuWrite32(0x82381c,0x88888888);Need(Gu.fault && Gu.retained,"companion drift latches while BME enabled");
 int before=V100Reads;Need(GuClearMaster()==EFI_SUCCESS && !(config[4]&4) && Cache==0x300,"emergency BME clearing succeeds after companion fault");
 Need(V100Reads==before && !GuSafeToReturn(),"containment never touches companion; unsafe return stays blocked");
 MixedSetup();Inject="enable";Need(GuEnableMaster()!=EFI_SUCCESS && (config[4]&4) && Gu.master_attributes_pending,"post-enable fault tracked");
 Need(GuClearMaster()==EFI_SUCCESS && !(config[4]&4) && !Gu.master_attributes_pending && Gu.retained,"BME partial effect contained without authorizing return");
 VOID *cpu=NULL;UINT64 dma=0;
 MixedSetup();Need(GuDmaAllocate(4096,&cpu,&dma)==EFI_SUCCESS && GuDmaFree(cpu,dma)==EFI_SUCCESS,"clean DMA allocate/map/unmap/free");NoCompanionEffects();
 const char *allocFaults[]={"allocate","map"};
 for(int i=0;i<2;i++){
  MixedSetup();Inject=allocFaults[i];cpu=NULL;dma=0;
  Need(GuDmaAllocate(4096,&cpu,&dma)!=EFI_SUCCESS && !cpu && !dma && Gu.retained && Gu.allocations,"DMA resource recorded/retained after callback fault");
  Need(fake.allocs==1 && fake.maps==(UINTN)i && !fake.unmaps && !fake.frees,"no later mapping/free after allocation fault");
  Need(GuDmaFree(Gu.allocations->cpu,Gu.allocations->dma)!=EFI_SUCCESS && !fake.frees,"retained DMA cannot be freed");NoCompanionEffects();
 }
 MixedSetup();Need(GuDmaAllocate(4096,&cpu,&dma)==EFI_SUCCESS,"DMA before unmap fault");Inject="unmap";
 Need(GuDmaFree(cpu,dma)!=EFI_SUCCESS && Gu.retained && Gu.allocations && !Gu.allocations->mapped && !fake.frees,"unmap callback drift retains buffer");NoCompanionEffects();
 MixedSetup();Need(GuDmaAllocate(4096,&cpu,&dma)==EFI_SUCCESS,"DMA before free fault");GU_DMA *scratch=Gu.allocations;Inject="free";
 Need(GuDmaFree(cpu,dma)!=EFI_SUCCESS && Gu.retained && !Gu.allocations && !Gu.dma_count && fake.frees==1,"post-free callback fault cannot retry or double-free");free(scratch);NoCompanionEffects();
 MixedSetup();Need(GuSavePci()==EFI_SUCCESS,"save exact actual PCI state");Selectors();
 Need(GuFlr()==EFI_SUCCESS && fake.flr_seen && fake.delay>=100000 && !fake.config_too_early,"guarded FLR preserves mandatory 100ms quiet interval");
 Need(GuRestorePci()==EFI_SUCCESS && !memcmp(config+0x10,(UINT8 *)TargetInitial+0x10,24),"actual target PCI restore succeeds with guards");
 Need(GuFlr()!=EFI_SUCCESS && Gu.flrs==1,"FLR remains one-shot");NoCompanionEffects();
 MixedSetup();Need(GuSavePci()==EFI_SUCCESS,"save for reset-time companion drift");Selectors();QuietDrift=1;
 Need(GuFlr()!=EFI_SUCCESS && Gu.retained && fake.delay>=100000 && !fake.config_too_early,"reset-time drift blocks continuation after quiet interval");NoCompanionEffects();
 MixedSetup();SwapTarget=1;GuWrite32(0x82381c,0x88888888);
 Need(!fake.writes && !Forbidden && Gu.retained,"companion read callback cannot redirect Gu.pci into V100");
 MixedSetup();SwapWrite=1;GuWrite32(0x82381c,0x88888888);
 Need(!fake.writes && !Forbidden && Gu.retained,"companion read callback cannot replace target write method");
 MixedSetup();Reenter=1;GuWrite32(0x82381c,0x88888888);Need(!fake.writes && Gu.retained,"reentrant guard callback cannot authorize writes");
 DisposeRamOnly();printf("PASS guarded actual port: %d assertions; exact 2-GPU configuration, DMA/BME/FLR containment; RAM only\n",Assertions);return 0;
}
