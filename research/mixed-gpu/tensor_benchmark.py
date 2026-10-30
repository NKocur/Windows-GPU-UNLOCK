# SPDX-License-Identifier: GPL-2.0-only
"""Read-only ordinary cuBLAS workload. No firmware, tuning or driver changes.
Uses existing torch only for memory, events and full output validation.
Not the repository's single-GPU benchmark protocol; no before/after baseline.
"""
def main():
    import ctypes as C
    import hashlib, json, os, pathlib, statistics, subprocess, time
    ROOT = pathlib.Path(__file__).resolve().parent
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-pci",default="00000000:2B:00.0")
    parser.add_argument("--output",required=True,type=pathlib.Path,help="Private result directory outside this checkout")
    args=parser.parse_args()
    ROOT=args.output.resolve()
    checkout=pathlib.Path(__file__).resolve().parents[2]
    if ROOT==checkout or checkout in ROOT.parents:
        raise RuntimeError("Keep private benchmark output outside Git")
    ROOT.mkdir(parents=True,exist_ok=True)
    devices=subprocess.check_output(["nvidia-smi","--query-gpu=name,uuid,pci.bus_id","--format=csv,noheader,nounits"],text=True).strip().splitlines()
    matches=[row.split(", ") for row in devices if row.split(", ")[0]=="NVIDIA GeForce RTX 3090" and row.split(", ")[2].lower()==args.target_pci.lower()]
    if len(matches)!=1: raise RuntimeError("Expected one RTX 3090 at the requested PCI address")
    UUID=matches[0][1]
    if os.environ.get("NVIDIA_TF32_OVERRIDE"):
        raise RuntimeError("Refusing NVIDIA_TF32_OVERRIDE")
    os.environ["CUDA_VISIBLE_DEVICES"] = UUID
    import torch

    FIELDS = "name,uuid,pci.bus_id,driver_version,utilization.gpu,memory.used,temperature.gpu,power.limit,clocks.max.sm,clocks.max.memory"
    def telemetry():
        return subprocess.check_output(["nvidia-smi", "--query-gpu="+FIELDS, "--format=csv,noheader,nounits"], text=True).strip().splitlines()
    before = telemetry()
    rows = [line.split(", ") for line in before]
    target = [r for r in rows if r[1] == UUID]
    if len(rows)!=2 or len(target)!=1 or target[0][0]!="NVIDIA GeForce RTX 3090" or target[0][2].lower()!=args.target_pci.lower():
        raise RuntimeError("Unexpected hardware inventory")
    if int(target[0][4])>15 or int(target[0][5])>2048 or int(target[0][6])>65:
        raise RuntimeError("GPU busy or hot; nothing stopped")
    if torch.cuda.device_count()!=1 or torch.cuda.get_device_name(0)!="NVIDIA GeForce RTX 3090":
        raise RuntimeError("CUDA target selection failed")
    torch.cuda.set_device(0)
    libdir = pathlib.Path(torch.__file__).parent/"lib"
    dllpath=libdir/"cublas64_12.dll"
    dll = C.WinDLL(str(dllpath))
    def bind(name, args):
        f=getattr(dll,name); f.argtypes=args; f.restype=C.c_int; return f
    P=C.c_void_p; I=C.c_int
    create=bind("cublasCreate_v2",[C.POINTER(P)])
    destroy=bind("cublasDestroy_v2",[P])
    setmath=bind("cublasSetMathMode",[P,I])
    setstream=bind("cublasSetStream_v2",[P,P])
    getversion=bind("cublasGetVersion_v2",[P,C.POINTER(I)])
    gemm=bind("cublasGemmEx",[P,I,I,I,I,I,P,P,I,I,P,I,I,P,P,I,I,I,I])
    def check(code):
        if code: raise RuntimeError("cuBLAS status "+str(code))
    handle=P()
    check(create(C.byref(handle)))
    version=I(); check(getversion(handle,C.byref(version)))
    check(setstream(handle,P(torch.cuda.current_stream().cuda_stream)))
    folder=ROOT/time.strftime("%Y%m%d-%H%M%S")
    folder.mkdir(exist_ok=False)
    context={"schema":1,"note":"Current-state exploratory benchmark; no locked baseline; desktop display activity permitted up to 15%; not repository comparison protocol",
    "gpu_uuid":UUID,"gpu_pci":args.target_pci,"torch":torch.__version__,"torch_cuda":torch.version.cuda,"cublas":version.value,
    "source_sha256":hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),"dll_path":str(dllpath),
    "before":before,"dimension":4096,"samples":30,"warmup_seconds":2,"trials":3,
    "output_validation":"Every output element equals 4096; FP32 accumulation and output"}
    (folder/"context.json").write_text(json.dumps(context,indent=2))
    n=4096
    modes=[("fp32_pedantic",torch.float32,0,2,69,-1),
           ("fp16_tensor_fp32_accumulate",torch.float16,2,1,68,99),
           ("bf16_tensor_fp32_accumulate",torch.bfloat16,14,1,68,99),
           ("tf32_tensor_fp32_accumulate",torch.float32,0,3,77,99)]
    trials=[]
    try:
        for trial in range(1,4):
            measurements=[]
            for name,dtype,datatype,math,compute,algo in modes:
                a=torch.ones((n,n),device="cuda",dtype=dtype)
                b=torch.ones_like(a)
                out=torch.empty((n,n),device="cuda",dtype=torch.float32)
                alpha=C.c_float(1); beta=C.c_float(0)
                check(setmath(handle,math))
                def call():
                    check(gemm(handle,0,0,n,n,n,C.byref(alpha),P(a.data_ptr()),datatype,n,
                               P(b.data_ptr()),datatype,n,C.byref(beta),P(out.data_ptr()),0,n,compute,algo))
                torch.cuda.synchronize()
                starttime=time.perf_counter()
                while time.perf_counter()-starttime<2:
                    call(); torch.cuda.synchronize()
                start=torch.cuda.Event(enable_timing=True)
                stop=torch.cuda.Event(enable_timing=True)
                samples=[]
                for _ in range(30):
                    start.record(); call(); stop.record(); stop.synchronize()
                    samples.append(start.elapsed_time(stop))
                if not torch.all(out==float(n)).item():
                    raise RuntimeError("Full matrix output mismatch: "+name)
                median=statistics.median(samples)
                r={"name":name,"median_ms":median,"tflops":2*n**3/(median*1e9),"all_output_elements_checked":True,"sample_ms":samples}
                measurements.append(r)
                print(f"trial {trial} {name}: {r['tflops']:.2f} TFLOPS",flush=True)
                del a,b,out
            after=telemetry()
            targetafter=[r.split(", ") for r in after if UUID in r][0]
            if targetafter[:4]!=target[0][:4] or targetafter[7:]!=target[0][7:]:
                raise RuntimeError("GPU identity/settings changed")
            record={"trial":trial,"measurements":measurements,"after":after}
            (folder/f"trial-{trial}.json").write_text(json.dumps(record,indent=2))
            trials.append(record)
    finally:
        check(destroy(handle))
    summary=[{"name":mode[0],"median_tflops":statistics.median([next(m["tflops"] for m in t["measurements"] if m["name"]==mode[0]) for t in trials])} for mode in modes]
    (folder/"summary.json").write_text(json.dumps(summary,indent=2))
    print(json.dumps({"results":str(folder),"summary":summary}),flush=True)

if __name__ == "__main__":
    main()
