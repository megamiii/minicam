# Profiling notes

Machine: Intel Core i7-10510U (4 cores / 8 threads, 1.8 GHz base, up to 4.9 GHz turbo, 8 MB L3), 7.7 GiB RAM available to WSL2 (Ubuntu 22.04, kernel 6.18 WSL2), GCC 11.4, `-O3`. Frame size 4048×3036 (12.3 MP, the HDR+ sensor size), synthetic input from `minicam_bench`.

## Method

- Profiled build: same flags as Release plus `-g -fno-omit-frame-pointer`, so the profile shows the code that is actually benchmarked.
- `perf record -F 2000 -g` / `perf report --no-children` for where time goes, and `perf stat` for instructions per cycle (IPC), cache and branch behavior. On WSL2 the `linux-tools-generic` `perf` binary (5.15) works for user-space sampling and hardware counters.
- `perf` samples the whole benchmark process. Scene generation (`sinf`, Mersenne Twister, `MosaicLinearRgb`) and the stages that prepare a stage's input appear in the profile too and were ignored when reading it.

## Findings before optimization

| Stage | Baseline ms/frame | What `perf` showed |
|---|---|---|
| Bilateral denoise | 12,439 | `expf` ≈ 40% of the stage (one call per neighbor, 81 per pixel, ~1 billion per frame); the rest is the 9×9 loop itself. IPC 2.11, branch misses 0.41%, 0.999 CPUs used: compute-bound on one core. |
| Tone map | 1,103 | `powf` from the sRGB encoding ≈ 60% of the stage (3 calls per pixel). The log-average called *double* `log` (`__ieee754_log`) because the constant `kLogDelta` is a `double`, which promoted every call. IPC 1.83, one core. |
| Malvar demosaic | 470 | Not profiled separately; reading the code, every one of the 13 neighbor reads per pixel goes through `Reflect()` (two compares), although only the outer 2 pixels need mirroring. |

The baseline numbers above come from the first benchmark run (`bench/stages_baseline.txt`).

## Measurement noise

The laptop's turbo frequency depends on temperature, and back-to-back long runs varied a lot: the same reference bilateral code measured 21.3 s right after other long benchmarks and 13.6–13.8 s after a 60 s cool-down. Because of that, every before/after comparison was measured **in one run, after a 60 s cool-down, with `--benchmark_enable_random_interleaving`**, as the median of 3 repetitions (`bench/*_variants.txt`). Before/after numbers are only compared within the same run.

## What changed because of it

| Stage | Change | Before | After (1 thread) | After (8 threads) |
|---|---|---|---|---|
| Bilateral | 4097-entry table of `exp(-d²/2σ_r²)` over the squared color distance (zero entry past the 10⁻⁴ cutoff, so no branch) + row-band threads | 13,617 ms | 7,184 ms (1.90×) | 2,591 ms (5.26×) |
| Tone map | Interpolated 4097-entry sRGB table instead of `powf`, float `log` with per-row double sums, row-band threads | 983 ms | 417 ms (2.36×) | 161 ms (6.1×) |
| Malvar | Mirroring only on the 2-pixel border, interior reads through five row pointers, row-band threads | 301 ms | 132 ms (2.28×) | 55.3 ms (5.4×) |

Accuracy:
- The bilateral table differs from `exp()` by at most 2.0×10⁻⁵ (110.7 dB PSNR).
- The sRGB table's maximum error is 1.6×10⁻⁵, about 0.4% of an 8-bit step, matching the h²/8·|f''| estimate.
- The Malvar fast path is bit-identical to the reference.
- All three are bit-identical for any thread count. Tone mapping needed a fixed summation order to get there: one double sum per row, rows added in order.

Why 8 threads give about 2.8–3× over 1 thread rather than 8×: the CPU has 4 physical cores (the other 4 are hyper-threads sharing execution units), and its turbo clock drops when all cores are busy.

## After optimization

Re-profiling the optimized tone map (1 thread): no `powf` remains. The pass that applies the curve and encodes takes about 62% of the stage, the log pass about 25%, and the serial `nth_element` for the white-point percentile about 9%. That is not worth optimizing yet; with 8 threads its share grows, because it is the only serial part.
