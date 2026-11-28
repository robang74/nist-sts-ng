## Speeding up results

The patched phase6 computes 20x (on average) faster on all tests impacted.

- `Intel(R) Core(TM) i5-8365U CPU @ 1.60GHz` (energy saving mode)

The main idea behind this work is what have been done on github by

- [AndreiBalan-dev/NIST-Statistical-Test-Suite/tree/speed-up-assess](https://github.com/AndreiBalan-dev/NIST-Statistical-Test-Suite/tree/speed-up-assess)

The optimization principle has been transferred with the support of ChatGPT
which artifacts have been saved in `speedup` folder for each of the six phases.

---

### SPEEDUP

#### 8x multi-thread

* ratio `user / real` is `171.704 / 29.826` = **5.76**
* expected max effectiveness: 6 threads on 8 cores

```sh
roberto@x280[8]:~/robang74/nist-sts-ng$
time ./sts -v 1 -I 10 -s -i 1000 -S 1048576
    -F r -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
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
time ./sts -v 1 -I 10 -s -i 128 -T 1 -S 1048576
    -F r -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
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

#### 8x multi-thread

* ratio `user / real` is `4057.6 / 530.80` = **7.64**
* expected max effectiveness: 8 threads on 8 cores

```sh
roberto@x280[2]:~/robang74/nist-sts-ng$
time ./sts -v 1 -I 10 -s -i 1000 -S 1048576
    -F r -w test-02 ~/robang74/uchaosys/kdev/data32.bin.01
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
time ./sts -v 1 -I 10 -s -i 128 -T 1 -S 1048576
    -F r -w test-10 ~/robang74/uchaosys/kdev/data32.bin.01
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
