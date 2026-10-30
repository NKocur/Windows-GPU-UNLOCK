#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Public evidence/source checks; no GPU work, firmware execution or private inputs."""
import ast,hashlib,json,math,pathlib,re,statistics
HERE=pathlib.Path(__file__).resolve().parent
def load(name):
    return json.loads((HERE/name).read_text(encoding="utf-8-sig"))
def require(ok,message):
    if not ok:raise RuntimeError(message)
def main():
    manifest=load("source-manifest.json")
    for item in manifest["hardware_tested_sources"]:
        p=HERE/"overrides"/item["path"]
        require(hashlib.sha256(p.read_bytes()).hexdigest()==item["sha256"],"Source changed: "+item["path"])
    for p in HERE.rglob("*.py"):
        ast.parse(p.read_text(encoding="utf-8-sig"),filename=p.name)
    data=load("evidence/tensor-benchmark.json")
    names=["fp32_pedantic","fp16_tensor_fp32_accumulate","bf16_tensor_fp32_accumulate","tf32_tensor_fp32_accumulate"]
    require(data["context"]["dimension"]==4096 and len(data["trials"])==3,"Wrong trial protocol")
    for trial in data["trials"]:
        require([m["name"] for m in trial["measurements"]]==names,"Wrong mode order")
        for measurement in trial["measurements"]:
            samples=measurement["sample_ms"]
            require(len(samples)==30 and all(math.isfinite(x) and x>0 for x in samples),"Invalid timings")
            median=statistics.median(samples)
            require(math.isclose(median,measurement["median_ms"],rel_tol=1e-12),"Timing median changed")
            require(math.isclose(2*4096**3/(median*1e9),measurement["tflops"],rel_tol=1e-12),"TFLOPS arithmetic mismatch")
            require(measurement["all_output_elements_checked"] is True,"Unchecked outputs")
    for item in data["summary"]:
        values=[next(m["tflops"] for m in t["measurements"] if m["name"]==item["name"]) for t in data["trials"]]
        require(math.isclose(statistics.median(values),item["median_tflops"],rel_tol=1e-12),"Summary mismatch")
    excerpt=(HERE/"evidence/uefi-success-excerpt.txt").read_text()
    require(len(re.findall(r"^CHECK stage=[a-z0-9-]+ status=0$",excerpt,re.M))==20,"Missing preflight success")
    require(excerpt.count("FINAL status=0 stage=transaction attempted=1 driver_release=0 clean=1")==1,"Missing clean final result")
    guard=(HERE/"overrides/internal-boot/unlock/mixed-guard.c").read_text()
    guard=re.sub(r"/\*.*?\*/|//[^\n]*","",guard,flags=re.S)
    require(not re.search(r"->(?:Mem|Io|Attributes|Pci\.Write|Map|Unmap|AllocateBuffer|FreeBuffer|DisconnectController|SetVariable)\b",guard),"Companion/bridge write call introduced")
    for p in HERE.rglob("*"):
        if not p.is_file():continue
        require(p.suffix.lower() not in (".efi",".exe",".dll",".bin",".obj",".pdb",".zip",".key",".pem"),"Unexpected binary/private artifact")
        text=p.read_text(encoding="utf-8-sig")
        require(not re.search(r"GPU-[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}",text),"Unredacted GPU UUID")
        require(not re.search(r"(?<![A-Za-z0-9])[A-Za-z]:[\\/]",text),"Absolute Windows path in public extension: "+str(p.relative_to(HERE)))
        require(not re.search(r"gh[pousr]_[A-Za-z0-9]{20,}",text),"Possible GitHub credential")
    print("PASS: source hashes, Python syntax, all 360 timing samples, summaries, selected UEFI evidence and public-file hygiene.")
if __name__=="__main__":
    main()
