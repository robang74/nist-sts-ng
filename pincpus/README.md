# Phase 7 — persistent worker CPU affinity

This patch adds default-on Linux CPU affinity to the existing iteration worker pool.

## Why this is a small scheduler change, not a new thread pool

The current `testBits()` worker is already persistent. It repeatedly:

1. locks the shared mutex;
2. claims the next `iterationsMissing` entry;
3. reads that stream into its worker-private buffer;
4. unlocks the mutex;
5. prepares packed data and runs the selected tests;
6. loops back to claim another stream.

Thus work is already dynamically distributed: when a worker finishes an iteration, it claims the next available one. Threads are created once per file-backed run, not once per iteration. This patch preserves that scheduler and pins each persistent worker to one allowed logical CPU.

## Changes

- `src/Makefile`: enables `-D_USE_PIN_CPUS` by default. Use `make PIN_CPUS=0` to build without pinning.
- `src/utils/defs.h`: records the CPU assigned to each worker.
- `src/utils/parse_args.c`: when the feature is enabled on Linux, derives the default worker count and the `-T` warning threshold from the calling process's allowed CPU affinity mask. This observes `taskset` and cpuset restrictions.
- `src/utils/utilities.c`: snapshots the allowed CPU IDs, assigns workers round-robin across them, and pins each worker once at startup with `pthread_setaffinity_np()`.

The affinity code is guarded by both `_USE_PIN_CPUS` and `__linux__`. Other platforms keep their existing scheduler behavior and build without Linux affinity calls.

If affinity discovery or setting fails, the program warns and continues without pinning. An explicit `-T` greater than the available CPU count still works; workers are assigned round-robin, so CPUs can be shared.

## Apply

From the repository root:

```sh
git apply --check speedup/phase7/phase7-cpu-affinity.patch
git apply speedup/phase7/phase7-cpu-affinity.patch
git diff --check
cd src && make clean && make
```

To build the comparison version without affinity:

```sh
cd src && make clean && make PIN_CPUS=0
```

## Verify and benchmark

Run the same workload with and without pinning, changing no other options. The example below uses the Phase 5 tests; substitute your usual test selection and input when measuring the full workload:

```sh
# Default: pinning enabled
make clean && make
time ./sts -v 3 -t 8,9 -T 8 -i 1000 -S 1048576 -F r -w pin-on input.bin

# Comparison: pinning disabled
make clean && make PIN_CPUS=0
time ./sts -v 3 -t 8,9 -T 8 -i 1000 -S 1048576 -F r -w pin-off input.bin
```

`-v 3` enables the project's `DBG_MED` messages, which show worker-to-CPU assignments and successful affinity operations. Run each case more than once, alternate the order, and compare medians. A desktop workload can make hard pinning slower as well as faster because it constrains Linux's normal load balancing; retain the default only if measured results justify it.

## Scope and limitations

- CPU IDs are logical CPU IDs from the process's allowed affinity mask, not physical-core IDs. With SMT enabled, two logical CPUs may share a physical core.
- With the default thread count, workers match the number of allowed logical CPUs (capped by the number of bitstreams). A custom `-T` larger than that count shares CPUs round-robin.
- The fixed `cpu_set_t` interface represents CPU IDs up to `CPU_SETSIZE - 1` (normally 1023). If affinity discovery fails, the program safely falls back to unpinned workers.
- Pinning improves CPU locality opportunities; it does not guarantee NUMA memory placement or a speedup. The input file is still read through the existing mutex-protected stream, and the Linux scheduler can still preempt a pinned worker on its assigned CPU.
- Non-Linux builds do not execute affinity code, even though the Makefile macro is present; their previous scheduling behavior is preserved.

A full repository build and end-to-end benchmark must be run on the target checkout. This patch has not been built against the user's local working tree in this environment.
