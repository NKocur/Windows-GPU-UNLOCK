/* GPL-2.0-only. Real stock core + compat + guarded port; firmware/services in RAM.
 * Native parent propagation is a MOCK parameter, not a claim about MSI firmware.
 */
#define main CapturedBaselineSuite
#include "test_core_captured.c"
#undef main
#include "../mixed-guard.h"
#include "../mixed-policy.h"
#include "mixed-pci-fixture.h"
static EFI_PCI_IO_PROTOCOL Other[sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0])];
static EFI_SYSTEM_TABLE MixedTable;
static EFI_HANDLE TargetHandle,V100Handle,BridgeHandle;
static UINT32 V100Config[64],BridgeConfig[16];
static EFI_GUID PciId=EFI_PCI_IO_PROTOCOL_GUID,PathId=EFI_DEVICE_PATH_PROTOCOL_GUID;
static unsigned FullChecks,Forbidden,Injected,GuardFault;
static int NativeParentEnable;
enum {NO_FAULT,AFTER_MAP,AFTER_ENABLE,DURING_FWSEC,BEFORE_FLR,DURING_RESET,AT_UNMAP,BRIDGE_DURING_FWSEC,BRIDGE_AFTER_ENABLE};
static int SameGuid(const EFI_GUID *a,const EFI_GUID *b){return !memcmp(a,b,16);}
static void Need(int ok,const char *name){FullChecks++;if(!ok){fprintf(stderr,"Mixed assertion: %s fault=%u\n",name,GuardFault);fail(name,__FILE__,__LINE__);}}
static void Mutate(VOID){if(!Injected){V100Config[12]^=1;Injected++;}}
static UINTN OtherIndex(EFI_PCI_IO_PROTOCOL *io){
 for(UINTN i=0;i<sizeof(Other)/sizeof(Other[0]);i++)if(io==&Other[i])return i;
 Need(0,"known PCI protocol");return 0;
}
static EFI_STATUS EFIAPI Location(EFI_PCI_IO_PROTOCOL *io,UINTN *s,UINTN *b,UINTN *d,UINTN *f){
 if(io==&pci){*s=GU_TARGET_SEGMENT;*b=GU_TARGET_BUS;*d=*f=0;return EFI_SUCCESS;}
 const UINTN *a=MixedPciFixtures[OtherIndex(io)].Location;*s=a[0];*b=a[1];*d=a[2];*f=a[3];return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ReadPci(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *out){
 if(io==&pci){
  if(n==1)return read_config(io,w,off,n,out);
  Need(w==EfiPciIoWidthUint32 && !off && (n==16 || n==64),"bounded target bulk reads");
  for(UINTN i=0;i<n;i++){EFI_STATUS s=read_config(io,w,(UINT32)i*4,1,(UINT32 *)out+i);if(s!=EFI_SUCCESS)return s;}
  return EFI_SUCCESS;
 }
 UINTN index=OtherIndex(io);EFI_HANDLE h=MixedPciFixtures[index].Handle;
 Need(w==EfiPciIoWidthUint32 && !off && (n==16 || (h==V100Handle && n==64)),"non-target config reads only");
 const UINT32 *data=h==V100Handle?V100Config:h==BridgeHandle?BridgeConfig:MixedPciFixtures[index].Config;
 memcpy(out,data,n*4);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ForbiddenConfig(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *out){
 (void)io;(void)w;(void)off;(void)n;(void)out;Forbidden++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI ForbiddenMem(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 off,UINTN n,VOID *out){
 (void)io;(void)w;(void)bar;(void)off;(void)n;(void)out;Forbidden++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI ForbiddenAttrs(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out){
 (void)io;(void)op;(void)mask;(void)out;Forbidden++;return EFI_ACCESS_DENIED;
}
static EFI_STATUS EFIAPI LocatePci(EFI_LOCATE_SEARCH_TYPE search,EFI_GUID *guid,VOID *key,UINTN *n,EFI_HANDLE **out){
 (void)key;Need(search==ByProtocol && SameGuid(guid,&PciId),"PCI enumeration only");
 *n=sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0]);
 EFI_STATUS status=allocate_pool(EfiLoaderData,*n*sizeof(**out),(VOID **)out);if(status!=EFI_SUCCESS)return status;
 for(UINTN i=0;i<*n;i++)(*out)[i]=MixedPciFixtures[i].Handle;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI HandlePci(EFI_HANDLE h,EFI_GUID *guid,VOID **out){
 *out=NULL;
 if(SameGuid(guid,&PciId))for(UINTN i=0;i<sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0]);i++)
  if(h==MixedPciFixtures[i].Handle)*out=h==TargetHandle?(VOID *)&pci:(VOID *)&Other[i];
 if(SameGuid(guid,&PathId)){
  if(h==TargetHandle)*out=(VOID *)MixedTargetPath;
  if(h==V100Handle)*out=(VOID *)MixedExpectedPath;
  if(h==BridgeHandle)*out=(VOID *)MixedExpectedBridgePath;
 }
 return *out?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI Protocols(EFI_HANDLE h,EFI_GUID ***out,UINTN *n){
 Need(h==V100Handle,"only companion ownership enumerated");*n=2;
 EFI_STATUS s=allocate_pool(EfiLoaderData,2*sizeof(**out),(VOID **)out);if(s!=EFI_SUCCESS)return s;
 (*out)[0]=&PciId;(*out)[1]=&PathId;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Opens(EFI_HANDLE h,EFI_GUID *guid,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,UINTN *n){
 Need(h==V100Handle && (SameGuid(guid,&PciId)||SameGuid(guid,&PathId)),"companion protocol open query");
 *n=0;*out=NULL;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NativeAttributes(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out){
 EFI_STATUS s=master_attributes(io,op,mask,out);
 if(s!=EFI_SUCCESS)return s;
 if(op==EfiPciIoAttributeOperationEnable){
  if(NativeParentEnable)BridgeConfig[1]|=4; /* hypothesized firmware-service effect */
  if(GuardFault==AFTER_ENABLE)Mutate();
  if(GuardFault==BRIDGE_AFTER_ENABLE && !Injected){BridgeConfig[1]&=~4U;Injected++;}
 }
 if(op==EfiPciIoAttributeOperationDisable){
  if(NativeParentEnable)BridgeConfig[1]&=~4U;
  if(GuardFault==BEFORE_FLR && fake.sec2_starts && !fake.flrs)Mutate();
 }
 return s;
}
static EFI_STATUS EFIAPI MixedMap(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_OPERATION op,VOID *cpu,UINTN *n,EFI_PHYSICAL_ADDRESS *dma,VOID **mapping){
 EFI_STATUS s=map_buffer(io,op,cpu,n,dma,mapping);
 if(s==EFI_SUCCESS && GuardFault==AFTER_MAP && fake.maps==48)Mutate();
 return s;
}
static EFI_STATUS EFIAPI MixedUnmap(EFI_PCI_IO_PROTOCOL *io,VOID *mapping){
 EFI_STATUS s=unmap_buffer(io,mapping);
 if(s==EFI_SUCCESS && GuardFault==AT_UNMAP)Mutate();return s;
}
static EFI_STATUS EFIAPI MixedReadMem(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 off,UINTN n,VOID *out){
 EFI_STATUS s=read_memory(io,w,bar,off,n,out);
 if(s==EFI_SUCCESS && fake.gsp_starts && !Injected){
  if(GuardFault==DURING_FWSEC)Mutate();
  if(GuardFault==BRIDGE_DURING_FWSEC){BridgeConfig[1]&=~4U;Injected++;}
 }
 return s;
}
static EFI_STATUS EFIAPI MixedStall(UINTN delay){
 EFI_STATUS s=stall(delay);
 if(GuardFault==DURING_RESET && fake.flrs && fake.delay_since_flr>=100000)Mutate();
 return s;
}
static void MixedSetup(enum scenario mode,unsigned fault,int parentEnable,UINT32 selector){
 test_entry_ss0=selector;test_entry_ss1=7;setup(mode);GuMixedHostReset();
 GuardFault=fault;NativeParentEnable=parentEnable;Injected=Forbidden=0;
 memcpy(V100Config,MixedExpectedConfig,sizeof(V100Config));memcpy(BridgeConfig,MixedExpectedBridgeConfig,sizeof(BridgeConfig));
 for(UINTN i=0;i<sizeof(MixedPciFixtures)/sizeof(MixedPciFixtures[0]);i++){
  Other[i]=(EFI_PCI_IO_PROTOCOL){0};Other[i].GetLocation=Location;Other[i].Pci.Read=ReadPci;
  Other[i].Pci.Write=ForbiddenConfig;Other[i].Mem.Read=Other[i].Mem.Write=Other[i].Io.Read=Other[i].Io.Write=ForbiddenMem;Other[i].Attributes=ForbiddenAttrs;
  const MIXED_PCI_FIXTURE *f=&MixedPciFixtures[i];
  if(f->Config[0]==GU_BOARD_ID)TargetHandle=f->Handle;
  if(f->Config[0]==0x1df010de)V100Handle=f->Handle;
  if(!f->Location[0] && !f->Location[1] && f->Location[2]==3 && f->Location[3]==1)BridgeHandle=f->Handle;
 }
 pci.GetLocation=Location;pci.Pci.Read=ReadPci;pci.Attributes=NativeAttributes;pci.Map=MixedMap;pci.Unmap=MixedUnmap;pci.Mem.Read=MixedReadMem;
 bs.LocateHandleBuffer=LocatePci;bs.HandleProtocol=HandlePci;bs.ProtocolsPerHandle=Protocols;bs.OpenProtocolInformation=Opens;bs.Stall=MixedStall;
 MixedTable=(EFI_SYSTEM_TABLE){0};MixedTable.BootServices=&bs;Gu.handle=TargetHandle;Gu.image=(EFI_HANDLE)9999;
 Need(GuMixedBegin(&MixedTable)==EFI_SUCCESS,"observed post-release mixed topology admitted to RAM test");
 Need(!(BridgeConfig[1]&4),"bridge begins in actual observed disabled state");
}
static int Execute(VOID){
 UINT64 mask=0xffffffffULL;struct pci_dev device={.dev={.dma_mask=&mask,.coherent_dma_mask=0xffffffffULL},.pm_cap=Gu.pm_cap,.refs=1};
 struct gpu_unlock_610_shared shared={0};
 int result=gpu_unlock_610_load_shared(&device,&shared);Need(!result,"actual verified stock assets load");
 result=gpu_unlock_610_unlock(&device,&shared);
 gpu_unlock_610_release_shared(&shared);
 return result;
}
static void Common(VOID){
 Need(!Forbidden,"zero non-target MMIO/IO/PCI writes or Attributes calls");
 Need(!memcmp(V100Config,MixedExpectedConfig,sizeof(V100Config)) || Injected,"companion config stays exact except intentional fault");
 Need(fake.gsp_starts<=1 && fake.sec2_starts<=1 && fake.flrs<=1,"one-shot firmware/reset execution");
 Need(!fake.pio_reads && !fake.pio_writes && !fake.dmem_reads,"forbidden legacy firmware access absent");
}
static void CheckSuccess(int result){
 Need(!result && GuSafeToReturn() && Gu.started && Gu.quiesced && !Gu.fault && !Gu.retained,"complete guarded core succeeds cleanly");
 Need(fake.gsp_starts==1 && fake.sec2_starts==1 && fake.flrs==1 && fake.radix_bytes==0x5053000,"both actual firmware publication checks and reset exercised");
 Need(!fake.live_maps && !fake.live_buffers && !Gu.allocations && !fake.pool_live,"all DMA/CPU resources cleaned");
 Need(fake.unmaps==fake.maps && fake.frees==fake.buffer_count,"all mappings and buffers released exactly");
 Need(registers[SS0/4]==0x88888888 && registers[SS1/4]==8,"guarded selector target reached");
 Need(fake.delay_since_flr>=100000,"mandatory FLR quiet interval preserved");
 unsigned writes=0;for(UINTN i=0;i<fake.trace_count;i++)if(!fake.write_trace[i][0] && fake.write_trace[i][2]==SS1){Need(fake.write_trace[i][3]==8,"only allowed SS1 value");writes++;}
 Need(writes==1,"SS1 written exactly once");Common();
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);Need(argc==4,"PROM, stock assets and captured config required");load_fixture_assets(argv[1],argv[2]);
 FILE *f=fopen(argv[3],"rb");Need(f && fread(core_captured_config,1,256,f)==256,"actual captured target config");fclose(f);core_use_capture=TRUE;
 const UINT32 observed[]={0x36061141U,0x36261141U,0x34261041U};
 for(UINTN selector=0;selector<3;selector++)for(int mode=SUCCESS;mode<SCENARIO_COUNT;mode++){
  MixedSetup((enum scenario)mode,NO_FAULT,1,observed[selector]);int result=Execute();
  if(mode==SUCCESS || mode==FWSEC_RUNNING || mode==PIO_UNAVAILABLE)CheckSuccess(result);
  else if(mode==BME_READBACK_FAIL){
   Need(result && !Gu.started && Gu.retained && !GuSafeToReturn(),"guard-read uncertainty retains resources before GPU start");
   Need(!fake.gsp_starts && !fake.sec2_starts && !fake.flrs && !fake.mmio_writes,"bus-master read fault cannot start firmware");
  }else if(mode==MAP_FAIL || mode==MAP_PARTIAL || mode==CORE_ENTRY_SS0 || mode==CORE_ENTRY_SS1 || mode==BEFORE_FW_SS0 || mode==BEFORE_FW_SS1){
   Need(result && !Gu.started && GuSafeToReturn() && !Gu.retained,"known clean pre-start failure releases resources");
   Need(!fake.live_maps && !fake.live_buffers && !Gu.allocations && !fake.pool_live,"clean failure leaves no resources");
   Need(!fake.gsp_starts && !fake.sec2_starts && !fake.flrs && !fake.mmio_writes,"clean pre-start failure leaves GPU untouched");
  }else if(mode==UNMAP_FAIL){
   Need(Gu.retained && Gu.quiesced && !GuSafeToReturn() && fake.unmaps==1 && !fake.frees,"failed unmap keeps all buffers reserved");
  }else{
   Need(result && Gu.started && !Gu.quiesced && Gu.retained && !GuSafeToReturn(),"post-start error cannot authorize return or cleanup");
   Need(fake.live_maps && fake.live_buffers && Gu.allocations && !fake.unmaps && !fake.frees,"unsafe DMA resources retained");
  }
  Need(GuClearMaster()==EFI_SUCCESS && !(config[4]&4),"app-level target BME containment works even after latched fault");
  Common();printf("guarded core selector=%08x scenario=%s result=%d safe=%u retained=%u maps=%zu starts=%zu/%zu FLR=%zu PASS\n",
   observed[selector],case_names[mode],result,GuSafeToReturn(),Gu.retained,fake.live_maps,fake.gsp_starts,fake.sec2_starts,fake.flrs);
  dispose_host_fixture();
 }
 for(unsigned fault=AFTER_MAP;fault<=BRIDGE_AFTER_ENABLE;fault++){
  MixedSetup(SUCCESS,fault,1,observed[2]);int result=Execute();Need(Injected==1,"transaction boundary fault actually injected");
  Need(!GuSafeToReturn() && Gu.retained,"injected companion/upstream failure blocks successful return");
  if(fault==AFTER_MAP || fault==AFTER_ENABLE || fault==BRIDGE_AFTER_ENABLE)Need(!fake.gsp_starts && !fake.sec2_starts && !fake.flrs,"early fault starts no GPU firmware");
  if(fault==DURING_FWSEC || fault==BRIDGE_DURING_FWSEC)Need(fake.gsp_starts==1 && !fake.sec2_starts && !fake.flrs,"FWSEC-time fault blocks later engine/reset");
  if(fault==BEFORE_FLR)Need(!fake.flrs,"late companion fault blocks FLR");
  if(fault==DURING_RESET)Need(fake.flrs==1 && fake.delay_since_flr>=100000,"post-reset fault observed only after quiet interval");
  if(fault==AT_UNMAP)Need(fake.unmaps==1 && !fake.frees && Gu.quiesced,"unmap-time fault blocks buffer free");
  Need(GuClearMaster()==EFI_SUCCESS && !(config[4]&4),"target BME still contained after boundary fault");
  Common();printf("guarded boundary=%u result=%d retained=%u maps=%zu starts=%zu/%zu FLR=%zu PASS\n",fault,result,Gu.retained,fake.live_maps,fake.gsp_starts,fake.sec2_starts,fake.flrs);
  dispose_host_fixture();
 }
 MixedSetup(SUCCESS,NO_FAULT,0,observed[2]);int result=Execute();
 Need(result && !Gu.started && !fake.gsp_starts && !fake.sec2_starts && !fake.flrs && !fake.mmio_writes,"unchanged disabled upstream bridge blocks firmware before any engine starts");
 Need(Gu.retained && !GuSafeToReturn() && !(BridgeConfig[1]&4),"no bridge repair or false success when firmware does not propagate BME");
 UINT32 bridgeCommand=0;BOOLEAN bridgeRequired=FALSE;EFI_STATUS bridgeStatus=EFI_SUCCESS;
 GuMixedDmaObservation(&bridgeCommand,&bridgeRequired,&bridgeStatus);
 Need(bridgeRequired && bridgeCommand==0x00100003U && bridgeStatus==EFI_NOT_READY,"cached diagnostic identifies disabled upstream path without another read");
 Need(GuClearMaster()==EFI_SUCCESS && !(config[4]&4),"disabled bridge failure still contains endpoint BME");
 Common();dispose_host_fixture();
 printf("PASS full guarded transaction: %u assertions, %u original scenarios + %u boundary/path scenarios; all hardware/firmware execution simulated\n",FullChecks,SCENARIO_COUNT*3,(unsigned)BRIDGE_AFTER_ENABLE+1);
 for(UINTN i=0;i<5;i++)free((VOID *)GuAssets[i].firmware.data);
 return 0;
}
