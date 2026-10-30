# Experimental RTX 3090 + V100 on MSI B550M

This branch adds a separate research overlay to Windows-GPU-UNLOCK. On 2026-10-30,
one guarded USB unlock returned directly to Windows with an ASUS RTX 3090 and
Tesla PG500-216/V100 both installed. Three subsequent tensor trials measured about
130 TFLOPS for FP16/BF16 inputs with FP32 accumulation. There is no locked baseline.

**This is one exact-machine result, not general support for multiple NVIDIA GPUs.**
The regular upstream sources and profile generator are unchanged.

## Tested configuration

| Component | Recorded configuration |
|---|---|
| Board / BIOS | MSI B550M PRO-VDH WIFI (MS-7C95), 2.N0 |
| CPU / display | Ryzen 9 3900X, no iGPU; 3090 is the display GPU |
| Target | ASUS RTX 3090 24 GiB; 10de:2204 / 1043:87af; 0000:2b:00.0 |
| Companion | Tesla PG500-216 / V100 32 GiB; 10de:1df0 / 10de:13f5; 0000:04:00.0 |
| Windows NVIDIA driver | 580.88, unchanged |
| Firmware display driver | NVIDIA GOP version 60009 |
| Exact base | 263741419ece69bcce73162ad5e98a742db8c6b5 |

GPU identity alone is insufficient: the profile also pins PROM hash, GOP code,
PCI topology/configuration and display ownership. Another board/BIOS/GPU topology
needs separate read-only capture, review, replay and hardware validation.
Do not substitute a hash or weaken checks to make a different machine pass.

## What changes

The overrides directory contains only files added/changed from the pinned base.
source-manifest.json hashes the exact source snapshot; its test-harness entry
identifies the latest full-transaction replay harness. Production runtime sources
are identical to the hardware-tested candidate. The build tool reconstructs them
in a new external workspace; normal upstream sources are not patched in place.

The mixed guard checks the exact target, a headless/unowned companion, the full
companion PCI configuration, PCI inventory and target ancestor path. Checks cover
release/handoff boundaries and allocation-free transaction boundaries including
DMA lifetime, target bus mastering and FLR. Companion accesses are PCI configuration
reads and protocol/ownership queries; there is no companion MMIO or PCI write.

The upstream bridge is checked read-only. The code requires a usable DMA path when
the target requests bus mastering. It does not repair the bridge directly.
If the native service does not provide the required path, execution stops before
firmware starts; uncertain cleanup retains resources and refuses successful handoff.
The original target transaction, selector policy and post-FLR quiet interval remain.

The wrapper requires an explicit **U** within 30 seconds before releasing display
ownership. R does not approve. Esc/timeout before release skips changes and starts
Windows. The later child Esc window occurs after release and stops; it does not
restore the display or continue to Windows. Failure after release may leave a
blank display and halt. There is no automatic retry or reset.

## Results and interpretation

| Mode | Median of three per-trial medians, TFLOPS |
|---|---:|
| FP32 pedantic | 28.20 |
| FP16 input / FP32 accumulation and output | 129.95 |
| BF16 input / FP32 accumulation and output | 130.06 |
| TF32 / FP32 accumulation and output | 68.20 |

All twenty child CHECK stages succeeded. One FLR completed; final status was 0
with clean=1, fault=0, retained=0 and maps=0. The loader reported successful Windows
handoff; the Windows boot timestamp followed the USB log write by 2.5 seconds.
Both GPUs remained recognized afterward on driver 580.88.

The successful transaction and tensor throughput are consistent with the unlock
remaining active in Windows. This is not a controlled before/after comparison.
The desktop was active (10% reported GPU utilization and 398 MiB VRAM before testing).
The benchmark uses cuBLAS 12.8.4 and torch 2.10.0+cu128; it is separate from the
upstream single-GPU CUDA 12.6 comparison protocol. It uses 4096x4096 GEMM,
two seconds warmup, thirty samples per mode and three sequential trials in one
process using one cuBLAS handle. Every output element was checked against 4096.
No application-speed, long-term stability, persistence after reset, or causal
speedup ratio is claimed.

Evidence: [hardware report](evidence/hardware-result.json),
[selected UEFI lines](evidence/uefi-success-excerpt.txt) and
[all timing samples and sanitized telemetry](evidence/tensor-benchmark.json).

The parent graph snapshots surround driver release, before the actual unlock;
they are not full pre/post-unlock snapshots. The post-cleanup cached bridge
command 0x100003 is not a DMA timeline. Child driver_release=0 means the parent
already performed the release. The tolerated Falcon pre-reset poll timeout is
followed by successful reset and transaction completion.

## Replaying and building

See [BUILD.md](BUILD.md). Host tests simulate all PCI/MMIO/DMA and firmware
execution in RAM. Source-only compilation never executes an unlock.
Private ROM/driver/graph fixtures and locally obtained pinned NVIDIA firmware are
required. They are not downloadable from this fork; without the private fixtures,
the complete replay/build refuses to proceed. This is a reproducible research
snapshot for the original captured inputs, not a ready-to-run package for other PCs.

Host coverage includes the retained baseline's 84 scenarios, the guarded core's
84 original plus nine extra boundary/path scenarios (34,164,202 assertions),
two complete release/Windows handoff replays (1,604,308 assertions each),
637 U-choice/flow checks and ten rejected unsafe build combinations. Assertions
include repeated model checks; a large count is not a hardware coverage claim.

No compiled EFI, installer, internal boot-entry changes or firmware assets are
distributed here. Building for a different Windows ESP produces a different
machine-specific loader. Private fixtures and build outputs belong outside Git.

## Recovery and boot-session scope

The tested procedure used a separate FAT32 USB, Secure Boot disabled, and an
unencrypted/unprotected Windows system volume. Save work and keep Windows Boot
Manager available. After a failure/halt, fully power off and select Windows Boot
Manager to boot normally. Do not reset between successful unlock and Windows.
The research scripts do not change firmware settings, encryption, BootOrder,
drivers or VBIOS. They do not stage any USB files. The tested U-required loader
was manually staged and booted; this fork does not provide a general installer.

Keep a backup of the prior USB loader and use the firmware's one-time boot menu.
Do not assume the unlock survives a reboot or reset. No default automatic boot
configuration has been enabled by this contribution.

## Attribution and licensing

Based on [impacifist/Windows-GPU-UNLOCK](https://github.com/impacifist/Windows-GPU-UNLOCK)
at the pinned commit above, with its rough2515-hub and Zagitta lineage.
New mixed-GPU research source and tools: NKocur and contributors, GPL-2.0-only.
Retain the root LICENSE, THIRD_PARTY_NOTICES.md, CHANGES.md and component notices.
NVIDIA firmware, PROM/BIOS dumps, loaded driver images and CUDA DLLs are excluded.
Original source comments retain their pre-test wording for hash fidelity; the
dated hardware evidence above supersedes those historical status comments.
