# Ferox Hardware and Accelerator Support

This document describes how Ferox detects host hardware, selects an accelerator
target, and applies hardware-aware runtime defaults.

## Current Model

Ferox now has an accelerator selection layer in `src/server/hardware_profile.c`.
It detects the local host, picks a target profile, and applies default runtime
tuning before the server creates the threadpool and atomic simulation world.

Current targets:

- `cpu`: default fallback on systems without a supported Apple or AMD GPU.
- `apple`: selected automatically on Apple Silicon hosts and tuned for unified
  memory plus low-overhead scheduling.
- `amd`: selected automatically on hosts where an AMD GPU is detected and tuned
  for throughput-oriented batch execution.

Important: the simulation still executes through the existing lock-free CPU
atomic path today. Apple and AMD targets currently select host-side tuning and
reporting, not full GPU kernel offload.

Current default workload profile used by the main server binary:

- world: `400x200`
- initial colonies: `50`
- tick rate: `100 ms`
- threads: auto-detected logical CPUs unless overridden

## Runtime Controls

CLI:

- `./build/src/server/ferox_server --print-hardware`
- `./build/src/server/ferox_server --accelerator auto`
- `./build/src/server/ferox_server --accelerator cpu`
- `./build/src/server/ferox_server --accelerator apple`
- `./build/src/server/ferox_server --accelerator amd`

Environment:

- `FEROX_ACCELERATOR=auto|cpu|apple|amd`
- `FEROX_THREADPOOL_PROFILE`
- `FEROX_ATOMIC_SERIAL_INTERVAL`
- `FEROX_ATOMIC_FRONTIER_DENSE_PCT`
- `FEROX_ATOMIC_USE_FRONTIER`

When Ferox applies a hardware profile, it only sets defaults for the tuning env
vars that are not already defined by the user.

## Tuning Policy

### CPU target

- selected when no Apple or AMD accelerator is detected, or when `cpu` is
  requested explicitly.
- uses `latency` threadpool profile on small hosts (`<= 4` logical CPUs).
- uses `balanced` threadpool profile on larger hosts.
- keeps atomic defaults at `serial_interval=5` and `frontier_dense_pct=15`.

### Apple target

- selected automatically on Apple Silicon.
- uses `latency` threadpool profile.
- sets `serial_interval=4`.
- sets `frontier_dense_pct=18`.
- marks the host as unified-memory capable.

### AMD target

- selected automatically when an AMD GPU is detected.
- uses `throughput` threadpool profile.
- sets `serial_interval=6`.
- sets `frontier_dense_pct=12`.
- checks for common Linux compute runtime signals such as `/dev/kfd`,
  `/opt/rocm`, `ROCM_PATH`, and `/etc/OpenCL/vendors`.

## Detection Notes

Host detection currently uses:

- `uname` for OS and architecture.
- `sysconf(_SC_NPROCESSORS_ONLN)` for logical CPU count.
- `/proc/cpuinfo` on Linux for CPU vendor text.
- `/sys/class/drm/card*/device/vendor` on Linux for GPU vendor detection.
- Apple Silicon compile/runtime detection on macOS arm64.

## Development Host Audit

Observed on the current Linux development host on 2026-03-08:

- OS: Linux 6.12 x86_64
- CPU: Intel Xeon E-2124 @ 3.30 GHz
- Logical CPUs: 4
- Memory: 15 GiB RAM
- GPU: Matrox G200eH3 (`mgag200`)
- Compute runtimes detected: none
- Resulting Ferox target: `cpu`

The current machine does not have a usable Apple or AMD compute GPU, so Ferox
correctly falls back to CPU-only tuning here.

## Where Future GPU Offload Fits

The current selection layer is meant to sit in front of future backend work.
The main integration points are:

- `src/server/hardware_profile.c` for target selection and policy
- `src/shared/atomic_types.h` for backend-friendly data layout
- `src/server/atomic_sim.c` for the spread/age/tick execution pipeline

When a real Metal, HIP, or OpenCL execution backend is added, it should reuse
the same target selection and reporting layer instead of inventing a separate
runtime switch.
