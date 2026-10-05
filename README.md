# minicam

A multi-threaded camera image signal processing (ISP) pipeline in modern C++, which turns RAW Bayer sensor data into finished RGB images.

minicam takes the raw output of a camera sensor and runs it through the stages a phone camera uses to produce a photo: black-level subtraction, demosaicing, white balance, color correction, tone mapping and denoising. The pipeline is designed like a simplified camera HAL. Each stage runs on its own thread, frames move through bounded queues, and frame memory comes from a reusable buffer pool, so several frames can be processed at once.

It is tested on real RAW bursts from the [Google HDR+ Burst Photography Dataset](https://hdrplusdata.org/), captured on Nexus and Pixel phones.

<!-- TODO: add a hero image: RAW input on the left, minicam output on the right -->

> **Status:** work in progress. Milestone 1 is done: project skeleton, a single-threaded pipeline, black-level correction, bilinear demosaicing and synthetic RAW generation, all tested. See [Progress](#progress).

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
- **Errors without exceptions:** following the Google C++ Style Guide, nothing throws. Stages return a `Status`, and the pipeline stops at the first failure and reports the frame id and stage name, for example `frame 7, stage 'demosaic_bilinear': expected a 1-channel Bayer mosaic`.

## Results

<!-- TODO: add before/after images for each stage, using a burst from the HDR+ dataset -->

| Stage | Before | After |
|---|---|---|
| Demosaic (bilinear vs. Malvar) | _image_ | _image_ |
| White balance | _image_ | _image_ |
| Tone mapping | _image_ | _image_ |

**Demosaicing quality** (synthetic RAW against ground truth)

| Method | PSNR (dB) |
|---|---|
| Bilinear | _TODO_ |
| Malvar-He-Cutler | _TODO_ |

So far, measured only on the procedural test scene used in the unit tests (256×192, 10-bit, no noise, 2-pixel border excluded), bilinear demosaicing scores **70.76 dB** on the smooth gradient region and **20.30 dB** on the full scene. The smooth-region score matches the theoretical limit set by 10-bit quantization (about 70.4 dB for 959 levels), so bilinear is effectively exact on smooth content. The full-scene score is dominated by aliasing on 3-pixel-wide stripes, which is the weakness Malvar-He-Cutler is meant to reduce.

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

Currently `minicam` reads synthetic `.raw` files and runs black level + bilinear demosaicing. There is no tone mapping stage yet, so the output PNG holds linear values and looks darker than a normal photo.

```bash
./build/minicam --input data/synthetic.raw --output out.png
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
./build/minicam_bench
```

## Project structure

<!-- TODO: update as bench/, scripts/ and CI are added -->

```
minicam/
├── include/minicam/      # Public headers: Status, Image, Frame, Stage, Pipeline, CFA, color, metrics
│   ├── stages/           # Stage headers (black level, demosaic, ...)
│   └── io/               # RAW container, synthetic RAW, image file I/O
├── src/
│   ├── core/             # Status, CFA, color transfer, PSNR
│   ├── stages/           # ISP stage implementations
│   ├── pipeline/         # Pipeline (threading, queues and buffer pool come later)
│   └── io/               # .raw reader/writer, synthetic RAW generator, OpenCV file I/O
├── tools/                # minicam CLI and make_synthetic_raw
├── tests/                # GoogleTest unit tests and procedural test scenes
├── bench/                # (planned) Google Benchmark performance tests
├── scripts/              # (planned) dataset download
└── docs/images/          # README images
```

`minicam_core` (stages, pipeline, synthetic RAW) has no OpenCV dependency. OpenCV is linked only into `minicam_io`, which decodes and encodes image files.

## Progress

- [x] **Milestone 1:** CMake skeleton, `Status`/`Image`/`Frame`/`Stage`, single-threaded `Pipeline`, black-level stage, bilinear demosaicing, synthetic RAW tool, PSNR metric, 43 unit tests (also clean under AddressSanitizer + UndefinedBehaviorSanitizer)
- [ ] **Milestone 2:** remaining ISP stages (Malvar demosaic, gray-world AWB, CCM, tone mapping, bilateral denoise)
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

The minicam source code is released under the MIT License. See [LICENSE](LICENSE).
