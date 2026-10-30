# Build and replay the mixed-GPU research snapshot

These tools are for the recorded exact profile. They do not discover or admit
other mixed-GPU systems. Runtime guards are necessary but are not proof that
another system can safely run it. The generic upstream profile generator remains
single-NVIDIA-only.

## Prerequisites

- Windows x64; Python 3.9+.
- clang and lld-link 20.1.8, plus installed MSVC/Windows SDK host link support.
- This branch at its pinned upstream base.
- Five locally obtained firmware assets matching internal-boot/unlock/assets.json.
  Follow the upstream [firmware guidance](../../docs/ASSETS.en.md); no driver
  installation is needed to obtain build inputs. If that documentation moves,
  consult the upstream firmware/assets guide.
- Original private capture fixtures matching private-fixtures-manifest.json.

The original two capture sets are not distributed: they contain loaded NVIDIA
driver bytes and PROM dumps. A public reader can inspect all modified source,
read the evidence and run public_checks.py, but cannot claim the complete replay
passed without the real private inputs. There is no synthetic-fixture fallback.
Changing the fixture manifest/profile to admit another machine is new research,
not validation of this result.

The private input directory layout is:

~~~text
mixed-private/
  case1/
    config.bin
    pci-fixture.h
    replay-fixture.h
    PROM.BIN
    IMG24.BIN
  case2/
    config.bin
    pci-fixture.h
    replay-fixture.h
    PROM.BIN
    IMG24.BIN
~~~

The config files are the target's full 256-byte PCI capture. PCI/replay headers
are the original complete inventory and handle/ownership graph fixtures.
Keep this directory and every output workspace outside the Git checkout.

## Public checks without private firmware

From the checkout root:

~~~powershell
python research/mixed-gpu/public_checks.py
~~~

This checks source hashes, selected firmware evidence, benchmark arithmetic and
privacy rules. It does not run GPU work or claim a full firmware/host replay.

## Complete host replay

Use a new external workspace on each invocation. Replace tool and private input
paths with your local paths. PowerShell accepts these commands on one line:

~~~powershell
python research/mixed-gpu/replay_and_build.py --mode host --fixtures ../mixed-private --assets ../firmware-assets --work ../mixed-host-output --clang ../LLVM/bin/clang.exe
~~~

All fixture bytes and pinned firmware asset hashes are checked before any tests.
The tool runs the real retained vendor core, compatibility and guarded port
against RAM-backed EFI/PCI/MMIO/DMA services; it never executes GPU firmware.
It also tests the display-release/Windows-handoff flow against both complete
original ownership graphs and rejects ten unsafe build combinations.

A successful invocation writes tests/results.json plus full host logs. It refuses
an existing output directory rather than overwriting it.

## Compile both EFI applications

This also reruns the complete host suite. Supply your actual existing Windows
EFI System Partition GUID; it is read and checked against the current system.
Do not paste that GUID into an issue or commit it.

~~~powershell
$esp = @(Get-Partition | Where-Object IsSystem)
if ($esp.Count -ne 1) { throw 'Expected one existing Windows ESP' }
python research/mixed-gpu/replay_and_build.py --mode build --fixtures ../mixed-private --assets ../firmware-assets --work ../mixed-build-output --clang ../LLVM/bin/clang.exe --lld-link ../LLVM/bin/lld-link.exe --windows-esp-guid $esp[0].Guid
~~~

Output:

- build/child/UNLOCK-HEADLESS.EFI
- build/loader/BOOTX64.EFI
- build/build-report.json, linker maps and PE verification reports.

The loader hashes and checks the exact child bytes before loading it. Both
applications retain the narrow build/profile gates and explicit U-before-release
policy. No boot entry is registered and no USB is changed by compilation.

The public tool is a packaging/replay adaptation of the original local build
scripts. Its outputs must be reviewed independently. Public build reproduction
and hashes are recorded separately in evidence/publication-validation.json.
A newly built image is not another hardware success until actually booted.

## USB layout and operator procedure

This contribution deliberately provides no generalized USB writer or internal
installer. Only a reviewed, exact-profile local build should be staged to a
separate spare FAT32 USB. Preserve the previous loader and verify read-back
hashes. Never format or write an existing Windows EFI partition for this.

The tested USB layout was:

~~~text
EFI/BOOT/BOOTX64.EFI
EFI/WindowsGPUUnlock/UNLOCK-HEADLESS.EFI
RTX3090-UNLOCK/ASSETS/
  booter_load_ga102.bin
  v67-ga102-610.43.03.bin
  gsp_ga10x.bin
  gsp_boot_ga102.bin
  gsp_boot_desc_ga102.bin
  NVIDIA-LICENSE
  NOTICE.md
~~~

Keep the license/notice accompanying your locally obtained firmware. The fork
does not provide or redistribute those files. The directory/file spelling must
match the source contract.

The successful original run used a full shutdown, then power-on and the MSI F11
one-time USB boot menu, followed by U at the initial prompt. Both cards stayed
installed. The display may blank while the NVIDIA GOP is detached. After success,
the loader starts Windows directly without resetting.

Initial Esc/timeout skips release/unlock and boots Windows. A later child cancel
or a failure after release stops and may leave the display blank. There is no
automatic retry. If it halts, fully power off and choose Windows Boot Manager.
Do not use Reset to transition from a successful unlock into Windows; session
state may be lost. Preserve normal Windows boot as a recovery path.

## Optional tensor benchmark

This is separate from firmware testing. The public harness requires an existing
Windows Python environment with torch and its CUDA 12 cuBLAS DLLs. It uses no
chat-app interfaces or services and does not install dependencies. The
historical measurement used torch 2.10.0+cu128 and cuBLAS 12.8.4.

~~~powershell
python research/mixed-gpu/tensor_benchmark.py --target-pci 00000000:2B:00.0 --output ../private-tensor-results
~~~

It selects the matching 3090 through its dynamically discovered UUID, refuses
nonempty NVIDIA_TF32_OVERRIDE, and runs ordinary CUDA math only. It requires two
detected NVIDIA GPUs, target usage at most 15%, target VRAM at most 2048 MiB and
target temperature at most 65 C. No process is killed or model unloaded.
The 15% allowance admits desktop activity, so this is an exploratory measurement.
For a useful before/after comparison, use the same harness, libraries, driver,
tuning and comparable background/temperature conditions in both states.

Output may contain private UUIDs and local library paths; do not upload it raw.
The published historical timing arrays are sanitized separately.
