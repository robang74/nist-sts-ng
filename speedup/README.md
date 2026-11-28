## Manyfolds Speeding Up

The patched `phase6` + `workload1` computes **24x** (raw estimation) faster on average for the full testsuite (aka all the tests active in the standard mix for NIST check)

- `Intel(R) Core(TM) i5-8365U CPU @ 1.60GHz` (energy saving mode)

The main idea behind this work is what have been done on github by

- [AndreiBalan-dev/NIST-Statistical-Test-Suite/tree/speed-up-assess](https://github.com/AndreiBalan-dev/NIST-Statistical-Test-Suite/tree/speed-up-assess)

The optimization principle has been transferred with the support of ChatGPT which artifacts have been saved in `speedup` folder for each of the six phases.

### Performance increase

> [!NOTE]
> 
> About the number of threads, note that they are N workers plus the parent that is in charge of preparing the jobs queue.

A single confrontation on a single system is merely indicative and cannot be taken as a conclusion of the speedup achievement. On the other hand, a **24x** speedup cannot be overlooked, either.

* `8x`: `530.8 / 29.83` = **`17.8x`** (`real`), `4057.6 / 171.70` = **`23.6x`** (`user`)
* `1x`: `183.3 / 9.704` = **`18.9x`** (`real`), ` 183.1 /   9.72` = **`18.9x`** (`user`) 

Moreover, it has been speedup by another `7.64 / 5.76 = 1.33x` by achieving a more effective CPU allocation on computational workload, while the I/O workload appears negligible in all the cases.

The manyfolds performance increase revealed a second order bottleneck: the workers management. Although it is related to reading data, it is not about I/O but about threads mutually blocking during the read. Which is necessary when reading from `stdin`, not when reading from a seekable file that can be accessed concurrently.

---

### SPEEDUP

* ratio `real` vs iterations: ` 29.83` ms (`-T8`), `70.89` ms (`-T1`), `2.38x`
* ratio `user` vs iterations: `171.70` ms (`-T8`), `70.43` ms (`-T1`), `2.44x`

#### 8x multi-thread

* ratio `user / real` is `171.704 / 29.826` = **5.76**
* expected max effectiveness: 6 threads on 8 cores

```sh
roberto@x280[8]:~/robang74/nist-sts-ng$
time ./sts -v 1 -I 10 -s -i 1000 -T 8 -S 1048576 -F r
    -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
```
```console
Testing data from file:
    /home/roberto/robang74/uchaosys/kdev/data32.bin.01
Start of init phase
End of init phase

Start of iterate phase
Completed iteration   1 of 1000 at 2026-11-27 16:57:49
Completed iteration  11 of 1000 at 2026-11-27 16:57:49
Completed iteration  21 of 1000 at 2026-11-27 16:57:50
Completed iteration  31 of 1000 at 2026-11-27 16:57:50
Completed iteration  41 of 1000 at 2026-11-27 16:57:50
...
Completed iteration 991 of 1000 at 2026-11-27 16:58:18
...
Execution completed!
Check the result.txt file for the results

real  0m 29.826s
user  2m 51.704s
sys   0m  0.279s
```

#### Single-thread

* ratio `user / real` is `9.015 / 9.074` = **99.35%**
* entirely computational with a minimal I/O workload

```sh
roberto@x280[8]:~/robang74/nist-sts-ng$
time ./sts -v 1 -I 10 -s -i 1000 -T 1 -S 1048576 -F r
    -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
```
```console
Testing data from file:
    /home/roberto/robang74/uchaosys/kdev/data32.bin.01
Start of init phase
End of init phase

Start of iterate phase
Completed iteration   1 of 128 at 2026-11-27 17:05:29
Completed iteration  11 of 128 at 2026-11-27 17:05:29
Completed iteration  21 of 128 at 2026-11-27 17:05:30
Completed iteration  31 of 128 at 2026-11-27 17:05:31
Completed iteration  41 of 128 at 2026-11-27 17:05:31
...
Completed iteration 121 of 128 at 2026-11-27 17:05:37
...
Execution completed!
Check the result.txt file for the results

real  0m 9.074s
user  0m 9.015s
sys   0m 0.036s
```

---

### ORIGINAL

* ratio `real` vs iterations: ` 530.8` ms (`-T8`), `1666.1` ms (`-T1`), `3.14x`
* ratio `user` vs iterations: `4057.6` ms (`-T8`), `1664.8` ms (`-T1`), `0.41x`

#### 8x multi-thread

* ratio `user / real` is `4057.6 / 530.80` = **7.64**
* expected max effectiveness: 8 threads on 8 cores

```sh
roberto@x280[2]:~/robang74/nist-sts-ng$
time ./sts -v 1 -I 10 -s -i 1000 -T 8 -S 1048576 -F r
    -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
```
```console
Testing data from file:
    /home/roberto/robang74/uchaosys/kdev/data32.bin.01
Start of init phase
End of init phase

Start of iterate phase
Completed iteration   1 of 1000 at 2026-11-27 11:42:53
Completed iteration  11 of 1000 at 2026-11-27 11:42:56
Completed iteration  21 of 1000 at 2026-11-27 11:42:58
Completed iteration  31 of 1000 at 2026-11-27 11:43:02
Completed iteration  41 of 1000 at 2026-11-27 11:43:10
...
Completed iteration 991 of 1000 at 2026-11-27 11:51:37
End of iterate phase
...
Execution completed!
Check the result.txt file for the results

real   8m 50.809s
user  67m 37.590s
sys    0m  3.246s
```

#### Single-thread

* ratio `user / real` is `183.1 / 183.3` = **99.91%**
* totally computational with a negligible I/O workload

```sh
roberto@x280[8]:~/robang74/nist-sts-ng$
time ./sts -v 1 -I 10 -s -i 1000 -T 1 -S 1048576 -F r
    -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
```
```console
Testing data from file:
    /home/roberto/robang74/uchaosys/kdev/data32.bin.01
Start of init phase
End of init phase

Start of iterate phase
Completed iteration   1 of 128 at 2026-11-27 17:06:26
Completed iteration  11 of 128 at 2026-11-27 17:06:40
Completed iteration  21 of 128 at 2026-11-27 17:06:55
Completed iteration  31 of 128 at 2026-11-27 17:07:11
Completed iteration  41 of 128 at 2026-11-27 17:07:28
...
Completed iteration 121 of 128 at 2026-11-27 17:09:46
...
Execution completed!
Check the result.txt file for the results

real  3m 33.267s
user  3m 33.100s
sys   0m  0.093s
```

---

### WORKLOAD

On mobile CPUs the thermal throttling has a relevant impact on sustained computational workload performance, clearly.

* ratio warm / cold on `real` is `27.591 / 22.940` = **1.20** slower 
* ratio warm / cold on `user` is `210.40 / 173.12` = **1.22** slower 

While a better workload balancing in terms of completely independent access to the input by `pread()` restored the computational effectiveness to a value nearly the cores engaged.

* ratio `user / real` is `173.12 / 22.940` = **7.55** (cold)
* ratio `user / real` is `210.40 / 27.591` = **7.63** (warm)

This kind of I/O optimization has a little effect when data comes from `stdin`, but the standard input I/O can be heavily cached by the kernel and by the application, both.

#### Cold-CPU start

```sh
roberto@x280[1]:~/robang74/nist-sts-ng$
time ./sts -v 1 -I 10 -s -i 1000 -T 8 -S 1048576 -F r
    -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
```
```console
Testing data from file:
    /home/roberto/robang74/uchaosys/kdev/data32.bin.01
...
Start of iterate phase
Completed iteration   1 of 1000 at 2026-11-28 11:07:36
Completed iteration  11 of 1000 at 2026-11-28 11:07:36
Completed iteration  21 of 1000 at 2026-11-28 11:07:36
Completed iteration  31 of 1000 at 2026-11-28 11:07:36
Completed iteration  41 of 1000 at 2026-11-28 11:07:36
...
Completed iteration 991 of 1000 at 2026-11-28 11:07:59
...
Execution completed!
Check the result.txt file for the results

real  0m 22.940s
user  2m 53.115s
sys   0m  0.356s
```

#### Warn-CPU start

```sh
roberto@x280[1]:~/robang74/nist-sts-ng$
time ./sts -v 1 -I 10 -s -i 1000 -T 8 -S 1048576 -F r
    -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
```
```console
Testing data from file:
    /home/roberto/robang74/uchaosys/kdev/data32.bin.01
...
Start of iterate phase
Completed iteration   1 of 1000 at 2026-11-28 11:08:03
Completed iteration  11 of 1000 at 2026-11-28 11:08:03
Completed iteration  21 of 1000 at 2026-11-28 11:08:03
Completed iteration  31 of 1000 at 2026-11-28 11:08:03
Completed iteration  41 of 1000 at 2026-11-28 11:08:03
...
Completed iteration 991 of 1000 at 2026-11-28 11:08:31
...
Execution completed!
Check the result.txt file for the results

real  0m 27.591s
user  3m 30.401s
sys   0m  0.387s
```
