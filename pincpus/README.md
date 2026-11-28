## Persistent worker CPU affinity, p.1

This patch adds default-on Linux CPU affinity to the existing iteration worker pool.

### Why this is a small scheduler change, not a new thread pool

The current `testBits()` worker is already persistent. It repeatedly:

1. locks the shared mutex;
2. claims the next `iterationsMissing` entry;
3. reads that stream into its worker-private buffer;
4. unlocks the mutex;
5. prepares packed data and runs the selected tests;
6. loops back to claim another stream.

Thus work is already dynamically distributed: when a worker finishes an iteration, it claims the next available one. Threads are created once per file-backed run, not once per iteration. This patch preserves that scheduler and pins each persistent worker to one allowed logical CPU.

### Changes

- `src/Makefile`: enables `-D_USE_PIN_CPUS` by default. Use `make PIN_CPUS=0` to build without pinning.
- `src/utils/defs.h`: records the CPU assigned to each worker.
- `src/utils/parse_args.c`: when the feature is enabled on Linux, derives the default worker count and the `-T` warning threshold from the calling process's allowed CPU affinity mask. This observes `taskset` and cpuset restrictions.
- `src/utils/utilities.c`: snapshots the allowed CPU IDs, assigns workers round-robin across them, and pins each worker once at startup with `pthread_setaffinity_np()`.

The affinity code is guarded by both `_USE_PIN_CPUS` and `__linux__`. Other platforms keep their existing scheduler behavior and build without Linux affinity calls.

If affinity discovery or setting fails, the program warns and continues without pinning. An explicit `-T` greater than the available CPU count still works; workers are assigned round-robin, so CPUs can be shared.

### Apply

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

### Scope and limitations

- CPU IDs are logical CPU IDs from the process's allowed affinity mask, not physical-core IDs. With SMT enabled, two logical CPUs may share a physical core.
- With the default thread count, workers match the number of allowed logical CPUs (capped by the number of bitstreams). A custom `-T` larger than that count shares CPUs round-robin.
- The fixed `cpu_set_t` interface represents CPU IDs up to `CPU_SETSIZE - 1` (normally 1023). If affinity discovery fails, the program safely falls back to unpinned workers.
- Pinning improves CPU locality opportunities; it does not guarantee NUMA memory placement or a speedup. The input file is still read through the existing mutex-protected stream, and the Linux scheduler can still preempt a pinned worker on its assigned CPU.
- Non-Linux builds do not execute affinity code, even though the Makefile macro is present; their previous scheduling behavior is preserved.

A full repository build and end-to-end benchmark must be run on the target checkout. This patch has not been built against the user's local working tree in this environment.

---

## Persistent worker CPU affinity, p.2

> However, the main problem seems to be the lack of use of the GNU
> extension in libpthread pthread_attr_setaffinity_np(). The thread
> starts on the CPU the Linux scheduler has decided is the most suitable,
> then the code potentially switches on a (n%CPUS) core, and this kills
> performance, expecially on short/light tasks.
> 
> [...]

**The phase-7 patch leaves that precise weakness.**

It calls `pthread_setaffinity_np()` from inside `testBits()`, after the thread has started. The kernel therefore gets the first opportunity to schedule the worker; the affinity call may then migrate it to a different CPU.

The better solution is to use `pthread_attr_setaffinity_np()` on the thread attributes **before** `pthread_create()`. The new worker then inherits the requested affinity from its creation, avoiding that initial migration.

For systems where that GNU extension is unavailable, the fallback should check the worker's actual CPU with `sched_getcpu()` before deciding whether to migrate it. In particular, when it is already running on an allowed CPU, it should pin itself there rather than unnecessarily switching to the CPU assigned by the round-robin policy.

### Phase 8 patch, what changes

| Environment                   | Behaviour                                                                                         |
| ----------------------------- | ------------------------------------------------------------------------------------------------- |
| Linux + glibc + `_GNU_SOURCE` | Uses `pthread_attr_setaffinity_np()` before `pthread_create()`                                    |
| Linux + musl                  | Uses `sched_getcpu()` to identify the worker's actual starting CPU, then pins it there if allowed |
| Fallback CPU unavailable      | Uses the previously assigned CPU                                                                  |
| Affinity operation fails      | Warns and continues without pinning                                                               |

There are also two implementation details worth highlighting.

**1. Independent thread attributes.**

I create and destroy a separate `pthread_attr_t` for each worker. This avoids accidentally inheriting the previous worker's CPU affinity when changing the attributes for the next worker.

**2. No unnecessary migration in the fallback.**

The worker checks its current CPU against its allowed affinity mask. When that CPU is available, it uses that CPU as its affinity target instead of blindly switching to `cpu_list[i % cpu_count]`.

The proper GNU feature-test macro is `_GNU_SOURCE` (not `__GNU_SOURCE`). Importantly, musl does not provide `pthread_attr_setaffinity_np()`, even when `_GNU_SOURCE` is enabled. 

---

## Persistent worker CPU affinity, p.3

> How thread_args[i].cpu_id can be positive if soon before is set to -1?
> 
> [...]

### How the scheduler works

The critical correction is that `cpu_id = -1` was initialized immediately before the attribute-affinity test. Consequently, the original `if (cpu_id >= 0)` could never succeed.  The new implementation uses:

`cpu_owner[cpu_id] = thread_id`, with `-1` indicating a free CPU.

#### Path 1 — `pthread_attr_setaffinity_np()` available

1. The parent finds the first free CPU.
2. It reserves that CPU in `cpu_owner`.
3. It sets affinity in the thread attributes.
4. It creates the worker.

The worker starts with its affinity already established. No initial `sched_getcpu()` check or migration is necessary.

#### Path 2 — attribute affinity unavailable

1. The worker starts under the kernel scheduler.
2. It obtains its initial CPU using `sched_getcpu()`.
3. Under the mutex, it checks whether that CPU is free.
4. If occupied, it reserves the first free CPU.
5. It calls `pthread_setaffinity_np()` to pin itself there.

This performs the CPU check only once per worker, not once per iteration.

#### Worker termination

I made one additional correction to your proposed lifecycle: ownership is released by the parent *after `pthread_join()` succeeds*, rather than just before the worker calls `pthread_exit()`.

This guarantees that the CPU slot is not reassigned while the previous thread is still executing its termination path.

### Changes to the project

The patch also addresses three integration details:

- `PIN_CPUS=1` is now the default; use `make PIN_CPUS=0` to disable affinity.

- The Makefile probes the two GNU affinity interfaces independently. It doesn't assume that `_GNU_SOURCE` guarantees both are implemented. 

- The thread limit is finalized after the `-d` directory has finished updating the number of bitstreams. Both the default worker count and explicit `-T` requests are limited to available logical CPUs. Linux CPU availability also respects the process's affinity mask, including `taskset` restrictions.

#### The important architectural point

STS creates its worker pool once, and each worker processes multiple iterations from the shared queue. Therefore, this is a startup scheduler with persistent CPU ownership, not a scheduler that needs to assign a CPU for every iteration. That keeps the scheduling overhead out of the performance-critical loop.

