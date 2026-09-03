# Benchmarking on Linux (from zero Linux experience)

You develop on macOS; the honest latency numbers come from Linux on the x86 box (the 7600X).
This is a copy-paste runbook. Start with **WSL2** — it's the least friction; graduate to native
Linux only if you want the last bit of tail precision.

The **head-to-head ratios** (`structbench`) and **throughput** (`bench`) are valid on WSL2. Only the
absolute sub-100ns *tail* percentiles need native Linux + RDTSC + CPU pinning (the last section).

---

## Path A — WSL2 (recommended first)

### 1. Install WSL2 (once)
Open **PowerShell as Administrator** on Windows and run:
```
wsl --install
```
Reboot when it asks. It installs Ubuntu and prompts you to create a Linux username/password
(remember the password — it's for `sudo`). After reboot, search "Ubuntu" in the Start menu to open a
Linux terminal. Everything below runs *in that Ubuntu terminal*.

### 2. Install the build tools (once)
```
sudo apt update
sudo apt install -y build-essential cmake git gh
```
(`build-essential` = the C++ compiler + make; `cmake` = our build system; `gh` = GitHub login.)

### 3. Get the code
The repo is private, so log in first (opens a browser flow — pick GitHub.com, HTTPS, "login with a
web browser", paste the one-time code):
```
gh auth login
gh repo clone ryanh24/ryan-har-lob-matching-sim
cd ryan-har-lob-matching-sim
```

### 4. Build (Release — optimized)
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 5. Run the benchmarks
```
./build/structbench 5000000     # hand-rolled vs std:: (the head-to-head; look at the ratios)
./build/bench 2000000 200000    # full engine: throughput + per-op latency
```
And re-run the correctness tests to confirm the Linux build is sound:
```
for t in matching avl index parser snapshot generator; do ./build/${t}_test; done
```

That's a complete result set: **the ratios and throughput are your real numbers.** Record them for
the writeup.

---

## Path B — native Linux (later, for pristine tails)

Only needed if you want the absolute p99.9/max tail numbers to be noise-free. Dual-boot Ubuntu
alongside Windows (installer: shrink a partition, boot from a USB stick, follow prompts — plenty of
guides). Then, in that native Ubuntu, repeat steps 2–5 above, plus the tuning below.

### Tuning for low-noise measurement
Run these before benchmarking (they reduce scheduler/frequency jitter):
```
sudo cpupower frequency-set -g performance      # lock CPU to max clock (apt install linux-tools-common linux-tools-$(uname -r) first)
taskset -c 2 ./build/bench 5000000 500000       # pin the run to one core (core 2 here)
```
For the cleanest tails you'd also isolate a core (`isolcpus=2` kernel arg) and enable hugepages —
document these in the writeup as "further tuning," they're incremental.

### RDTSC (absolute per-op cycles) — a small code addition, when you're ready
`bench`/`structbench` currently time with `std::chrono` (fine for ratios + throughput). For
low-overhead absolute per-op timing on x86, swap the timer for `__rdtsc()` (`#include <x86intrin.h>`)
and convert cycles→ns using the measured TSC frequency. Left as a follow-up — the ratios don't need
it, and it's x86-only.

---

## What to report in the writeup
- **`structbench` ratios** — "my `OrderIndex` is N× `std::unordered_map`, my `PriceTree` is M×
  `std::map`," with one line on *why* (open addressing + backshift; pooled AVL).
- **`bench` throughput + per-op p50/p99/p99.9** — state the platform (WSL2 vs native), the workload
  mix, warmup discarded, and that distributions (not averages) are reported.
- The macOS↔Linux split itself is worth a sentence: develop/validate on macOS, measure on Linux.
