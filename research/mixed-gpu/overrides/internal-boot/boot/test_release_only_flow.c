/* GPL-2.0-only. State machine and exact choice logic, RAM services only. */
#include "release-only-flow.h"
#include "release-only-choice.h"
#include <stdio.h>
#include <stdlib.h>
typedef struct {EFI_STATUS prepare,choose,pre,release,post,verify,windows;BOOLEAN skip;int releases,captures,boots,skips,records,recordFault;} TEST;
static int Checks,KeyMode,KeyReads,Stalls,Resets;
static void Check(int ok,const char *m){Checks++;if(!ok){fprintf(stderr,"FAIL %s\n",m);exit(1);}}
static EFI_STATUS P(VOID *v){return ((TEST *)v)->prepare;}
static EFI_STATUS C(VOID *v,BOOLEAN *s){TEST *t=v;*s=t->skip;return t->choose;}
static EFI_STATUS G(VOID *v){TEST *t=v;return ++t->captures==1?t->pre:t->post;}
static EFI_STATUS R(VOID *v){TEST *t=v;t->releases++;return t->release;}
static EFI_STATUS V(VOID *v){return ((TEST *)v)->verify;}
static EFI_STATUS S(VOID *v){TEST *t=v;t->skips++;return t->windows;}
static EFI_STATUS W(VOID *v){TEST *t=v;t->boots++;return t->windows;}
static EFI_STATUS L(VOID *v,const char *m,EFI_STATUS s){(void)m;(void)s;TEST *t=v;return ++t->records==t->recordFault?EFI_DEVICE_ERROR:EFI_SUCCESS;}
static EFI_STATUS Run(TEST *t){const GU_RELEASE_ONLY_OPS ops={t,P,C,G,R,V,S,W,L};return GuReleaseOnlyFlow(&ops);}
static EFI_STATUS EFIAPI Reset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,BOOLEAN x){(void)p;(void)x;Resets++;return KeyMode==5?EFI_DEVICE_ERROR:EFI_SUCCESS;}
static EFI_STATUS EFIAPI Key(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *p,EFI_INPUT_KEY *k){
 (void)p;KeyReads++;if(KeyMode==4)return EFI_WARN_UNKNOWN_GLYPH;
 if(KeyMode==1){k->UnicodeChar='R';return EFI_SUCCESS;}
 if(KeyMode==2){k->UnicodeChar=27;return EFI_SUCCESS;}
 if(KeyMode==3 && KeyReads==7){k->UnicodeChar='r';return EFI_SUCCESS;}
 return EFI_NOT_READY;
}
static EFI_STATUS EFIAPI Stall(UINTN n){Check(n==100000,"bounded 100ms choice poll");Stalls++;return EFI_SUCCESS;}
int main(VOID) {
 TEST t={0};Check(Run(&t)==EFI_ABORTED && t.releases==1 && t.captures==2 && t.boots==1 && !t.skips,"release-only success without an unlock callback");
 t=(TEST){.skip=TRUE};Check(Run(&t)==EFI_ABORTED && !t.releases && !t.captures && !t.boots && t.skips==1,"skip has no device changes");
 EFI_STATUS bad[]={EFI_DEVICE_ERROR,EFI_ACCESS_DENIED,EFI_WARN_UNKNOWN_GLYPH};
 for(UINTN i=0;i<3;i++){
  t=(TEST){.prepare=bad[i]};Check(Run(&t)==bad[i] && !t.releases && !t.boots && !t.skips,"prepare refuses");
  t=(TEST){.choose=bad[i]};Check(Run(&t)==bad[i] && !t.releases && !t.boots && !t.skips,"choice refuses");
  t=(TEST){.pre=bad[i]};Check(Run(&t)==bad[i] && !t.releases && !t.boots && !t.skips,"pre-capture refuses");
  t=(TEST){.release=bad[i]};Check(Run(&t)==bad[i] && t.releases==1 && t.captures==2 && !t.boots && !t.skips,"partial release captures evidence but no retry/fallback");
  t=(TEST){.post=bad[i]};Check(Run(&t)==bad[i] && t.releases==1 && !t.boots && !t.skips,"post capture refuses");
  t=(TEST){.verify=bad[i]};Check(Run(&t)==bad[i] && t.releases==1 && !t.boots && !t.skips,"verification refuses");
 }
 for(int i=1;i<=3;i++){t=(TEST){.recordFault=i};Check(Run(&t)==EFI_DEVICE_ERROR && !t.boots && !t.skips,"every release log failure blocks Windows");}
 Check(GuReleaseOnlyFlow(NULL)==EFI_INVALID_PARAMETER,"invalid ops refuse");
 EFI_BOOT_SERVICES bs={0};EFI_SIMPLE_TEXT_INPUT_PROTOCOL input={0};EFI_SYSTEM_TABLE table={0};
 input.Reset=Reset;input.ReadKeyStroke=Key;bs.Stall=Stall;table.BootServices=&bs;table.ConIn=&input;
 for(KeyMode=0;KeyMode<=5;KeyMode++){
  KeyReads=Stalls=Resets=0;BOOLEAN skip=FALSE;EFI_STATUS status=GuReleaseOnlyChoose(&table,&skip);
  if(KeyMode==0)Check(status==EFI_SUCCESS && skip && KeyReads==300 && Stalls==300,"timeout skips, never auto-releases");
  if(KeyMode==1)Check(status==EFI_SUCCESS && !skip && KeyReads==1 && !Stalls,"uppercase R expressly approves");
  if(KeyMode==2)Check(status==EFI_SUCCESS && skip && KeyReads==1 && !Stalls,"Esc skips");
  if(KeyMode==3)Check(status==EFI_SUCCESS && !skip && KeyReads==7 && Stalls==6,"lowercase r expressly approves after wait");
  if(KeyMode==4)Check(status==EFI_WARN_UNKNOWN_GLYPH && skip && KeyReads==1,"input warning cannot approve release");
  if(KeyMode==5)Check(status==EFI_DEVICE_ERROR && skip && !KeyReads,"input reset error cannot approve release");
 }
 printf("PASS release-only flow/choice: %d checks; no GPU/firmware access\n",Checks);return 0;
}
