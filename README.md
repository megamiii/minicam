# minicam

A multi-threaded camera image signal processing (ISP) pipeline in modern C++, which turns RAW Bayer sensor data into finished RGB images.

minicam takes the raw output of a camera sensor and runs it through the stages a phone camera uses to produce a photo: black-level subtraction, demosaicing, white balance, color correction, tone mapping and denoising. The pipeline is designed like a simplified camera HAL. Each stage runs on its own thread, frames move through bounded queues, and frame memory comes from a reusable buffer pool, so several frames can be processed at once.

It is tested on real RAW bursts from the [Google HDR+ Burst Photography Dataset](https://hdrplusdata.org/), captured on Nexus and Pixel phones.

<!-- TODO: add a hero image: RAW input on the left, minicam output on the right -->

> **Status:** work in progress. Milestones 1 and 2 are done: all six ISP stages run in a single-threaded pipeline on synthetic RAW, are unit-tested, and demosaicing is benchmarked on the Kodak set. The multi-threaded camera-HAL design and real DNG input come next. See [Progress](#progress).

## Features

- **ISP stages implemented from scratch** (no OpenCV processing calls):
  - Black-level subtraction and normalization
  - Demosaicing: bilinear and Malvar-He-Cutler (2004)
  - Gray-world auto white balance
  - Color correction matrix
  - Gamma correction and tone mapping
  - Denoising (bilateral filter)
  - Optional HOG-based face detection
- **Camera-HAL-style system design**
  - Thread-per-stage pipeline connected by thread-safe bounded queues
  - Frame buffer pool, so no allocation per frame
  - Per-frame metadata (frame ID, timestamp, exposure, white balance gains)
  - Clean shutdown and error propagation
- **Inputs**
  - Real RAW/DNG files via LibRaw, tested on the Google HDR+ dataset
  - Synthetic RAW generated from any sRGB image, with known ground truth for testing
  - Stream mode for processing frame sequences and bursts
- **Engineering**
  - CMake build, GoogleTest unit tests, Google Benchmark performance tests
  - AddressSanitizer and ThreadSanitizer clean
  - GitHub Actions CI, clang-format, clang-tidy, Google C++ Style Guide

## Architecture

<!-- TODO: replace with the final diagram -->

```
 RAW input ──► [Black Level] ──► [Demosaic] ──► [White Balance] ──► [CCM] ──► [Tone Map] ──► [Denoise] ──► PNG output
     ▲            thread 1         thread 2         thread 3       thread 4     thread 5       thread 6        │
     │                                                                                                        │
     └──────────────────────────── frame buffers returned to the pool ◄───────────────────────────────────────┘

 Stages are connected by bounded, thread-safe queues. Each frame carries its metadata through the pipeline.
```

**Design decisions**

<!-- TODO: fill in as you build. Explain each choice in your own words. -->

- **Thread per stage:** _why this model, and the trade-off versus a thread pool over image tiles_
- **Bounded queues:** _how they provide back-pressure when a stage is slow_
- **Buffer pool:** _why per-frame allocation hurts latency, and how buffers are recycled_
- **Stage interface:** every stage implements `Stage::Process(Frame&)` and talks to other stages only through the `Frame` it is given. A `Frame` holds the metadata and one buffer per representation (`raw` uint16 mosaic, `bayer` normalized float mosaic, `rgb` linear float image). Because no stage knows about any other, a `Pipeline` is just an ordered `std::vector<std::unique_ptr<Stage>>`, and adding, removing or reordering stages only changes that list. The one rule is that a stage's input buffer must have been produced by an earlier stage; if it was not, the stage returns an error.
- **Stage order:** black level → demosaic → white balance → CCM → tone map → denoise.
  - The CCM is a change of color basis, so it runs on linear data, before the tone curve.
  - Denoising runs *after* tone mapping. Shot noise grows with brightness, and the sRGB curve roughly evens out the noise level (it acts like a variance-stabilizing transform), so one bilateral range sigma fits shadows and highlights alike. Production ISPs instead denoise RAW data early, using a calibrated per-ISO noise model.
  - White balance runs on demosaiced RGB. Many ISPs apply it to the mosaic before demosaicing, because Malvar's cross-channel correction assumes balanced channels. The stage interface makes that reordering easy to try.
- **Errors without exceptions:** following the Google C++ Style Guide, nothing throws. Stages return a `Status`, and the pipeline stops at the first failure and reports the frame id and stage name, for example `frame 7, stage 'demosaic_bilinear': expected a 1-channel Bayer mosaic`.

## Results

<!-- TODO: add before/after images for each stage, using a burst from the HDR+ dataset -->

| Stage | Before | After |
|---|---|---|
| Demosaic (bilinear vs. Malvar) | _image_ | _image_ |
| White balance | _image_ | _image_ |
| Tone mapping | _image_ | _image_ |

**Demosaicing quality** on the Kodak set (24 images mosaicked through RGGB, compared with the originals; mean CPSNR, 10-pixel border excluded)

| Method | sRGB domain (dB) | Linear domain (dB) |
|---|---|---|
| Bilinear | 30.25 | 30.38 |
| Malvar-He-Cutler | **35.79** | **34.71** |
| Gain | +5.54 | +4.33 |

- **Sanity check.** In the sRGB domain (the usual literature protocol), Malvar gains 5.54 dB over bilinear, in line with the improvement of over 5.5 dB reported in the Malvar-He-Cutler paper. It wins on all 24 images in both domains.
- **Smaller gain in the linear domain.** The paper's correction gains were tuned on gamma-encoded images. In linear data, edges in bright regions produce much larger Laplacian values than the same edges in dark regions, so a fixed gain fits less well. Since minicam demosaics linear data, this is an argument for re-tuning the gains on linear data (or applying the correction in a gamma-encoded domain), which would be a measurable follow-up.
- **Hardest images:** kodim08 (fine shutters and brick texture, 29.20 dB) and kodim13 (detailed rocks and water, 29.56 dB). Their high-frequency detail is close to the Bayer sampling limit, where every linear demosaicer aliases.
- The per-image table is in [docs/results/kodak_demosaic.md](docs/results/kodak_demosaic.md). It is generated by `demosaic_eval` (see [Test and benchmark](#test-and-benchmark)). PSNR values are only comparable within a domain.

Measured so far on the procedural test scene used in the unit tests (256×192, 10-bit synthetic RAW, no noise, 2-pixel border excluded):

| Method | Full scene | Smooth region |
|---|---|---|
| Bilinear | 20.30 dB | 70.76 dB |
| Malvar-He-Cutler | 24.37 dB | 69.56 dB |

- The smooth-region scores are near the theoretical limit set by 10-bit quantization (about 70.4 dB for 959 levels), so both methods are effectively exact on smooth content.
- The full scene is dominated by aliasing on 3-pixel-wide stripes. That is where Malvar's cross-channel correction gains 4.06 dB.
- In the smooth region Malvar is 1.2 dB *worse*. Its Laplacian term is a high-pass filter, so it slightly amplifies quantization noise. This sharpness-versus-noise trade-off is why real ISPs denoise before or jointly with demosaicing.

**Denoising** (unit test, test scene in display values plus Gaussian noise σ = 0.03): the bilateral filter (σ_s = 2, σ_r = 0.08) raises PSNR from **30.42 dB to 41.31 dB**.

## Performance

Measured with Google Benchmark on _TODO: CPU, cores, RAM, OS, compiler_. Image size: _TODO_.

| Stage | Before optimization | After optimization | Change | Optimization |
|---|---|---|---|---|
| _TODO_ | _ms/frame_ | _ms/frame_ | _x faster_ | _e.g., cache-friendly layout_ |
| _TODO_ | _ms/frame_ | _ms/frame_ | _x faster_ | _e.g., tiled multithreading, SIMD_ |

| Pipeline | Throughput (FPS) | Latency p50 | Latency p99 |
|---|---|---|---|
| Single-threaded | _TODO_ | _TODO_ | _TODO_ |
| Multi-threaded | _TODO_ | _TODO_ | _TODO_ |

**Profiling notes:** _TODO: what perf showed and what you changed because of it._

## Getting started

### Requirements

- Linux, CMake 3.20+, a C++20 compiler (GCC 11+ or Clang 14+)
- LibRaw, OpenCV (I/O and display only), GoogleTest, Google Benchmark

On Ubuntu 22.04:

```bash
sudo apt install build-essential cmake ninja-build pkg-config libraw-dev libopencv-dev
```

GoogleTest (and later Google Benchmark) are downloaded at a pinned version by CMake, so local builds and CI use the same version.

### Build

```bash
git clone https://github.com/megamiii/minicam.git
cd minicam
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Get test data

The HDR+ dataset is large, so it is not stored in this repository. Download the subset (153 bursts, about 37 GiB) or only a few bursts with `gsutil`, following the instructions at [hdrplusdata.org](https://hdrplusdata.org/dataset.html).

```bash
./scripts/download_hdrplus.sh   # TODO: downloads a few sample bursts into data/
```

To generate synthetic RAW from any image:

```bash
./build/make_synthetic_raw input.jpg data/synthetic.raw
# Optional: CFA pattern and sensor noise (variance = shot * signal + read^2)
./build/make_synthetic_raw input.jpg data/noisy.raw --cfa RGGB --shot-noise 0.002 --read-noise 0.01 --seed 1
```

The tool undoes the sRGB gamma, keeps one color per pixel in the chosen Bayer pattern, and stores 10-bit values with black level 64 and white level 1023 in a small `.raw` container that also holds the frame metadata.

### Run

Currently `minicam` reads synthetic `.raw` files and runs the full single-threaded ISP. It prints the time of each stage and the white balance gains it applied.

```bash
./build/minicam --input data/synthetic.raw --output out.png

# Every stage can be selected or turned off, e.g. for before/after images:
./build/minicam --input data/synthetic.raw --output out.png     --demosaic bilinear|malvar --wb gray-world|metadata|off --ccm on|off     --tone reinhard|clip --exposure 0.5 --denoise on|off     --sigma-spatial 2 --sigma-range 0.08
```

Planned (DNG input via LibRaw and stream mode are not implemented yet):

```bash
# Process a single RAW/DNG file
./build/minicam --input data/<burst>/payload_N000.dng --output out.png

# Process a whole burst in stream mode
./build/minicam --input data/<burst>/ --output out/ --stream
```

### Test and benchmark

```bash
ctest --test-dir build --output-on-failure
./build/minicam_bench   # planned

# Demosaicing quality on the Kodak set (24 PNGs, ~15 MB, into data/kodak/)
./scripts/download_kodak.sh
./build/demosaic_eval data/kodak
```

`download_kodak.sh` currently fails because the host refuses non-browser downloads. In that case it prints instructions for saving the 24 images from [r0k.us/graphics/kodak](https://r0k.us/graphics/kodak/) by hand.

`demosaic_eval` samples each image through an RGGB mosaic with an ideal sensor (no noise or quantization), reconstructs it with both methods, and reports CPSNR (PSNR over all three channels) with a 10-pixel border excluded. It does this in two domains: directly on the 8-bit sRGB values, which is the usual protocol in the literature, and on linearized values, as in minicam's pipeline.

## Project structure

<!-- TODO: update as bench/ and CI are added -->

```
minicam/
├── include/minicam/      # Public headers: Status, Image, Frame, Stage, Pipeline, CFA, color, metrics
│   ├── stages/           # Black level, demosaic, white balance, CCM, tone map, denoise
│   └── io/               # RAW container, synthetic RAW, image file I/O
├── src/
│   ├── core/             # Status, CFA, color transfer, PSNR
│   ├── stages/           # ISP stage implementations
│   ├── pipeline/         # Pipeline (threading, queues and buffer pool come later)
│   └── io/               # .raw reader/writer, synthetic RAW generator, OpenCV file I/O
├── tools/                # minicam CLI, make_synthetic_raw, demosaic_eval
├── tests/                # GoogleTest unit tests and procedural test scenes
├── bench/                # (planned) Google Benchmark performance tests
├── scripts/              # Dataset download (Kodak; HDR+ planned)
└── docs/                 # README images and generated result tables
```

`minicam_core` (stages, pipeline, synthetic RAW) has no OpenCV dependency. OpenCV is linked only into `minicam_io`, which decodes and encodes image files.

## Progress

- [x] **Milestone 1:** CMake skeleton, `Status`/`Image`/`Frame`/`Stage`, single-threaded `Pipeline`, black-level stage, bilinear demosaicing, synthetic RAW tool, PSNR metric, 43 unit tests (also clean under AddressSanitizer + UndefinedBehaviorSanitizer)
- [x] **Milestone 2:** Malvar-He-Cutler demosaicing, gray-world AWB, color correction matrix, auto exposure + Reinhard tone mapping + sRGB, bilateral denoising, full pipeline in the CLI with per-stage timing, `demosaic_eval` tool, 76 unit tests (also clean under ASan + UBSan)
  - [x] Kodak demosaicing results: Malvar +5.54 dB over bilinear (sRGB domain)
- [ ] **Milestone 3:** camera-HAL-style system: bounded queues, buffer pool, thread-per-stage pipeline, shutdown and error propagation
- [ ] **Milestone 4:** real data: LibRaw DNG loader, HDR+ download script, stream mode
- [ ] **Milestone 5:** benchmarks, optimizations, profiling
- [ ] **Milestone 6:** CI, sanitizers in CI, clang-tidy, HOG face detection

## Roadmap

- [ ] Burst merging (align and merge several frames, in the spirit of HDR+)
- [ ] Lens shading correction
- [ ] SIMD versions of more stages
- [ ] Android version using the Camera2 `RAW_SENSOR` format

## References

- H. S. Malvar, L. He, R. Cutler. "High-quality linear interpolation for demosaicing of Bayer-patterned color images." ICASSP 2004.
- C. Tomasi, R. Manduchi. "Bilateral filtering for gray and color images." ICCV 1998.
- E. Reinhard, M. Stark, P. Shirley, J. Ferwerda. "Photographic tone reproduction for digital images." ACM Transactions on Graphics (SIGGRAPH), 2002.
- G. Buchsbaum. "A spatial processor model for object colour perception." Journal of the Franklin Institute, 1980 (gray-world assumption).
- N. Dalal, B. Triggs. "Histograms of Oriented Gradients for Human Detection." CVPR 2005.
- S. W. Hasinoff et al. "Burst photography for high dynamic range and low-light imaging on mobile cameras." ACM Transactions on Graphics (SIGGRAPH Asia), 2016.

## Data and license

Test images come from the [HDR+ Burst Photography Dataset](https://hdrplusdata.org/) by Google, released under [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). Any images from the dataset shown in this README are shared under the same license. If you use the dataset, cite:

```bibtex
@article{hasinoff2016burst,
  author  = {Samuel W. Hasinoff and Dillon Sharlet and Ryan Geiss and Andrew Adams and
             Jonathan T. Barron and Florian Kainz and Jiawen Chen and Marc Levoy},
  title   = {Burst photography for high dynamic range and low-light imaging on mobile cameras},
  journal = {ACM Transactions on Graphics (Proc. SIGGRAPH Asia)},
  volume  = {35},
  number  = {6},
  year    = {2016},
}
```

Demosaicing quality is measured on the [Kodak Lossless True Color Image Suite](https://r0k.us/graphics/kodak/) (PhotoCD PCD0992), which Eastman Kodak released for unrestricted use. These images are not stored in this repository either.

The minicam source code is released under the MIT License. See [LICENSE](LICENSE).
