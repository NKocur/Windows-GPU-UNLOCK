#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Rebuild/replay the exact MSI B550M / ASUS 3090 / PG500-216 research snapshot.
Host tests and EFI compilation only. Never stages USB, changes boot, or runs GPU code.
Private captured ROM/driver/graph fixtures and firmware assets are required.
"""
import argparse, hashlib, json, pathlib, shutil, struct, subprocess, uuid

EXT = pathlib.Path(__file__).resolve().parent
REPO = EXT.parents[1]
PIN = "263741419ece69bcce73162ad5e98a742db8c6b5"
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def require(ok, message):
    if not ok:
        raise RuntimeError(message)
def run(argv, log=None):
    p=subprocess.run([str(a) for a in argv],capture_output=True,text=True)
    if log:
        log.write_text(p.stdout+p.stderr,encoding="utf-8")
    require(p.returncode==0,"Command failed: "+str(argv[0])+"\n"+(p.stdout+p.stderr)[-3500:])
    return p.stdout
def write_json(path, value):
    path.write_text(json.dumps(value,indent=2)+"\n",encoding="utf-8")
def guid_c(value):
    b=value.bytes_le
    return "{0x%08x,0x%04x,0x%04x,{%s}}"%(struct.unpack("<I",b[:4])[0],struct.unpack("<H",b[4:6])[0],struct.unpack("<H",b[6:8])[0],",".join("0x%02x"%x for x in b[8:]))
def platform_header(boot, windows):
    # Zero internal GUID is the spare-USB contract, not an internal install.
    (boot/"platform-config.h").write_text(
        "/* Local build only; never publish partition GUIDs. */\n"
        "#define GU_INTERNAL_PARTITION_INITIALIZER "+guid_c(uuid.UUID(int=0))+"\n"
        "#define GU_WINDOWS_PARTITION_INITIALIZER "+guid_c(windows)+"\n")
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode",choices=("host","build"),default="host")
    parser.add_argument("--fixtures",required=True,type=pathlib.Path,help="Private case1/case2 folders; see manifest")
    parser.add_argument("--assets",required=True,type=pathlib.Path,help="Locally obtained five pinned NVIDIA assets")
    parser.add_argument("--work",required=True,type=pathlib.Path,help="New empty output path OUTSIDE this Git checkout")
    parser.add_argument("--clang",required=True,type=pathlib.Path)
    parser.add_argument("--lld-link",type=pathlib.Path)
    parser.add_argument("--windows-esp-guid",type=uuid.UUID)
    args=parser.parse_args()
    work=args.work.resolve()
    require(not work.exists(),"Output must not already exist; no files are overwritten")
    require(REPO.resolve() not in work.parents and work!=REPO.resolve(),"Output must be outside the publication checkout")
    require(args.clang.is_file(),"clang not found")
    require(args.fixtures.resolve()!=REPO.resolve() and REPO.resolve() not in args.fixtures.resolve().parents,"Private fixtures must stay outside Git")
    if args.mode=="build":
        require(args.lld_link and args.lld_link.is_file() and args.windows_esp_guid,"Build requires lld-link and your Windows ESP GUID")
        # Read only: the caller must supply the actual existing Windows ESP.
        command="@(Get-Partition | Where-Object IsSystem | ForEach-Object { $_.Guid }) | ConvertTo-Json -Compress"
        actual=json.loads(run(["powershell.exe","-NoProfile","-Command",command]).lstrip("\ufeff"))
        actual=actual if isinstance(actual,list) else [actual]
        require(len(actual)==1 and uuid.UUID(actual[0])==args.windows_esp_guid,"Windows ESP does not match; no build")
    require(run(["git","-C",REPO,"rev-parse",PIN]).strip()==PIN,"Pinned base commit unavailable")
    require(not run(["git","-C",REPO,"diff",PIN,"--","internal-boot","deps/edk2"]).strip(),"Upstream source changed; this snapshot is not validated against it")
    manifest=json.loads((EXT/"source-manifest.json").read_text(encoding="utf-8-sig"))
    require(manifest["upstream_commit"]==PIN,"Unexpected source base")
    for item in manifest["hardware_tested_sources"]:
        require(digest(EXT/"overrides"/item["path"])==item["sha256"],"Hardware-tested override changed: "+item["path"])
    fixtures=json.loads((EXT/"private-fixtures-manifest.json").read_text())
    for case in fixtures["cases"]:
        for name,spec in case["files"].items():
            p=args.fixtures/case["case"]/name
            require(p.is_file() and p.stat().st_size==spec["bytes"] and digest(p)==spec["sha256"],"Missing/different private fixture: "+case["case"]+"/"+name)
    assets=json.loads((REPO/"internal-boot/unlock/assets.json").read_text())
    for spec in assets:
        p=args.assets/spec["Name"]
        require(p.is_file() and p.stat().st_size==spec["Bytes"] and digest(p)==spec["Sha256"],"Missing/different firmware asset: "+spec["Name"])
    work.mkdir(parents=True)
    source=work/"src"
    # Reconstruct source without binaries, captures, generated configs or Git internals.
    for p in (REPO/"internal-boot").rglob("*"):
        if not p.is_file() or p.suffix not in (".c",".h",".py",".json"):
            continue
        relative=p.relative_to(REPO)
        if any(part in ("build","local","__pycache__","deps","install") for part in relative.parts[1:-1]):
            continue
        if p.name in ("platform-config.h","generated.h","profile-local.h"):
            continue
        out=source/relative
        out.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,out)
    for item in manifest["hardware_tested_sources"]:
        out=source/item["path"]
        out.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(EXT/"overrides"/item["path"],out)
    boot=source/"internal-boot/boot"
    u=source/"internal-boot/unlock"
    platform_header(boot,args.windows_esp_guid or uuid.UUID(int=0))
    (boot/"generated.h").write_text("/* Not used by this headless research build. */\n")
    incs=["-I",REPO/"deps/edk2/MdePkg/Include","-I",REPO/"deps/edk2/MdePkg/Include/X64"]
    out=work/"tests";out.mkdir()
    clang=args.clang
    # Exactly the U/skip logic compiled into the research wrapper.
    choice=out/"u-choice.exe"
    run([clang,"-std=c11","-O1","-Wall","-Wextra","-Werror","-fshort-wchar",*incs,boot/"test_unlock_choice.c",boot/"release-only-flow.c","-o",choice])
    result=run([choice],out/"u-choice.txt")
    require("637 checks" in result,"Unexpected choice protocol")
    print(result.strip(),flush=True)
    # Full release/Windows handoff replay for each original cold capture.
    loader_assertions=[]
    for case in fixtures["cases"]:
        name=case["case"]
        inputs=args.fixtures/name
        fixture_dir=boot/"build/r2";fixture_dir.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(inputs/"replay-fixture.h",fixture_dir/"replay-fixture.h")
        shutil.copyfile(inputs/"pci-fixture.h",boot/"pci-fixture.h")
        exe=out/(name+"-loader.exe")
        flags=["-std=c11","-O1","-Wall","-Wextra","-Werror","-fshort-wchar","-D_CRT_SECURE_NO_WARNINGS",
            "-DGU_PROFILE_LOCAL","-DGU_HEADLESS_RESEARCH","-DGU_SINGLE_RTX_CONSOLE","-DGU_FULL_UNLOCK",
            "-DGU_AUTOMATIC_BOOT","-DGU_MIXED_PORT_HOST_TEST","-DGU_MIXED_COMPANION_GUARD"]
        run([clang,*flags,*incs,boot/"test_mixed.c",boot/"console-flow.c",boot/"full-flow.c",boot/"release-only-flow.c",
             u/"checks.c",u/"sha256.c",u/"mixed-guard.c","-o",exe])
        result=run([exe,inputs/"IMG24.BIN",inputs/"PROM.BIN",inputs/"config.bin"],out/(name+"-loader.txt"))
        require("1604308 assertions" in result,"Unexpected loader replay assertions")
        loader_assertions.append(1604308)
        print(name+": "+result.strip().splitlines()[-1],flush=True)
    # Real stock core + compatibility + production guarded UEFI port; RAM only.
    first=args.fixtures/"case1"
    shutil.copyfile(first/"pci-fixture.h",u/"tests/mixed-pci-fixture.h")
    flags=["-std=c11","-O1","-Wall","-Wextra","-Werror","-Wno-unused-parameter","-Wno-unused-function",
        "-D_CRT_SECURE_NO_WARNINGS","-DGU_PROFILE_LOCAL","-DGU_MIXED_PORT_HOST_TEST","-DGU_MIXED_COMPANION_GUARD",
        "-DGU_EXPERIMENTAL_3090","-DGU_ENTRY_LATCHED_SS1_FULL","-DGU_UEFI_PORT","-D__KERNEL__","-DGU_TARGET_PCI_ID=0x220410deU",
        "-I",u/"compat",*incs]
    shared=[u/"checks.c",u/"compat.c",*[u/("vendor/"+s+".c") for s in ("unlock_610","prep_610","layout_610","vbios_610","gfw_eval")]]
    baseline=out/"baseline.exe"
    run([clang,*flags,u/"tests/test_core_captured.c",u/"baseline-port.c",*shared,"-o",baseline])
    config=struct.unpack("<64I",(first/"config.bin").read_bytes())
    text=out/"target-config.txt"
    text.write_text("".join("config[%02x]="%(i*4)+" ".join("%08x"%v for v in config[i:i+4])+"\n" for i in range(0,64,4)))
    result=run([baseline,first/"PROM.BIN",args.assets,text],out/"baseline.txt")
    require("84 real core + compat + UEFI port scenarios passed" in result,"Baseline incomplete")
    print(result.strip().splitlines()[-1],flush=True)
    guarded=out/"guarded.exe"
    run([clang,*flags,u/"tests/test_full_mixed.c",u/"port.c",u/"mixed-guard.c",*shared,"-o",guarded])
    result=run([guarded,first/"PROM.BIN",args.assets,first/"config.bin"],out/"guarded.txt")
    require("34164202 assertions, 84 original scenarios + 9 boundary/path scenarios" in result,"Guarded replay incomplete")
    print(result.strip().splitlines()[-1],flush=True)
    live=["-std=c11","-Wall","-Wextra","-Werror","-fshort-wchar","-ffreestanding",
        "-DGU_PROFILE_LOCAL","-DGU_MIXED_LIVE_RESEARCH","-DGU_MIXED_COMPANION_GUARD","-DGU_AUTOMATIC_BOOT",
        "-DGU_HEADLESS_RESEARCH","-DGU_USB_MEDIA","-DGU_EXPERIMENTAL_3090","-DGU_ENTRY_LATCHED_SS1_FULL",
        "-DGU_INTERNAL_BOOT","-DGU_UEFI_PORT","-D__KERNEL__","-DGU_TARGET_PCI_ID=0x220410deU"]
    cincip=["-I",u/"compat",*incs]
    run([clang,*live,*cincip,"-fsyntax-only",u/"app.c"])
    unsafe=[
      ("missing companion guard",["-DGU_MIXED_COMPANION_GUARD"],[]),
      ("missing USB contract",["-DGU_USB_MEDIA"],[]),
      ("missing selector policy",["-DGU_ENTRY_LATCHED_SS1_FULL"],[]),
      ("missing internal child contract",["-DGU_INTERNAL_BOOT"],[]),
      ("missing headless policy",["-DGU_HEADLESS_RESEARCH"],[]),
      ("missing research admission",["-DGU_MIXED_LIVE_RESEARCH"],[]),
      ("host switch in firmware",[],["-DGU_MIXED_PORT_HOST_TEST"]),
      ("release-only in unlock",[],["-DGU_MIXED_RELEASE_ONLY"]),
      ("override segment",[],["-DGU_TARGET_SEGMENT=1"]),
      ("override identity",[],["-DGU_BOARD_ID=0x1df010deU"])]
    for name,remove,add in unsafe:
        p=subprocess.run([str(x) for x in [clang,*[f for f in live if f not in remove],*add,*cincip,"-fsyntax-only",u/"app.c"]],capture_output=True,text=True)
        require(p.returncode!=0 and "error:" in p.stderr,"Unsafe build accepted: "+name)
    report={"scope":"Host replay only; no GPU or UEFI code executed on hardware","u_choice_checks":637,
        "loader_assertions":loader_assertions,"baseline_scenarios":84,"guarded_assertions":34164202,
        "guarded_original_scenarios":84,"guarded_extra_scenarios":9,"unsafe_build_refusals":[x[0] for x in unsafe],
        "clang_sha256":digest(clang),"source_manifest_sha256":digest(EXT/"source-manifest.json")}
    write_json(out/"results.json",report)
    print("PASS: ten unsafe build combinations refused.",flush=True)
    if args.mode=="host":
        return
    common=["-target","x86_64-pc-windows-msvc","-std=c11","-ffreestanding","-fno-builtin","-fno-stack-protector",
        "-mno-red-zone","-fshort-wchar","-Os","-Wall","-Wextra","-Werror","-DGU_PROFILE_LOCAL",
        "-DGU_MIXED_LIVE_RESEARCH","-DGU_MIXED_COMPANION_GUARD","-DGU_AUTOMATIC_BOOT","-DGU_HEADLESS_RESEARCH","-DGU_USB_MEDIA"]
    childdefs=["-DGU_EXPERIMENTAL_3090","-DGU_ENTRY_LATCHED_SS1_FULL","-DGU_INTERNAL_BOOT","-DGU_UEFI_PORT","-D__KERNEL__","-DGU_TARGET_PCI_ID=0x220410deU"]
    build=work/"build";build.mkdir()
    def compile_efi(kind,sources,defines,name):
        folder=build/kind;folder.mkdir()
        objects=[]
        for file in sources:
            obj=folder/(file.stem+".obj")
            run([clang,*common,*defines,*cincip,"-c",file,"-o",obj])
            objects.append(obj)
        exe=folder/name
        mapfile=folder/(kind+".map")
        run([args.lld_link,"/nologo","/nodefaultlib","/subsystem:efi_application","/entry:efi_main","/machine:x64","/base:0",
             "/dynamicbase","/nxcompat","/timestamp:0","/opt:ref","/opt:icf","/map:"+str(mapfile),"/out:"+str(exe),*objects])
        run([__import__("sys").executable,"-I",REPO/"internal-boot/unlock/verify_pe.py",exe,mapfile,"--output",folder/"pe-verification.json"])
        return exe
    childnames=["app","app_io","ownership","format","runtime","sha256","checks","port","compat","mixed-guard",
        "vendor/unlock_610","vendor/prep_610","vendor/layout_610","vendor/vbios_610","vendor/gfw_eval"]
    child=compile_efi("child",[u/(n+".c") for n in childnames],childdefs,"UNLOCK-HEADLESS.EFI")
    contract=boot/"build/headless";contract.mkdir(parents=True,exist_ok=True)
    (contract/"child-contract.h").write_text("/* Exact verified child; local build. */\n#define GU_UNLOCK_BYTES "+str(child.stat().st_size)+'U\n#define GU_UNLOCK_SHA256 "'+digest(child)+'"\n')
    loaders=[boot/(n+".c") for n in ("handoff","full-release","full-flow","diagnostic")]
    loaders += [u/(n+".c") for n in ("checks","sha256","mixed-guard")]
    loaders += [source/"internal-boot/efi_runtime.c"]
    loader=compile_efi("loader",loaders,["-DGU_FULL_UNLOCK","-DGU_SINGLE_RTX_CONSOLE"],"BOOTX64.EFI")
    write_json(build/"build-report.json",{"host_tests":report,"hardware_tested_this_build":False,"usb_staged":False,"requires_U":True,
        "child_sha256":digest(child),"child_bytes":child.stat().st_size,"loader_sha256":digest(loader),"loader_bytes":loader.stat().st_size})
    print("Built and PE-verified both EFI images. Nothing staged, booted or installed.",flush=True)
    print("Child SHA256: "+digest(child),flush=True)
    print("Loader SHA256: "+digest(loader),flush=True)
if __name__=="__main__":
    main()
