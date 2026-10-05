# Demosaicing quality on the Kodak set

Generated with:

```bash
./build/demosaic_eval data/kodak
```

Setup:
- The 24 images of the [Kodak Lossless True Color Image Suite](https://r0k.us/graphics/kodak/), 768×512 or 512×768, 8-bit sRGB.
- Each image is sampled through an RGGB mosaic with an ideal sensor (no noise, no quantization), reconstructed, and compared with the original.
- The metric is CPSNR: PSNR over all three channels with peak 1.0, computed on float output with a 10-pixel border excluded.

Domains:
- **sRGB:** the 8-bit sRGB values are mosaicked directly. This is the usual protocol in the demosaicing literature.
- **linear:** the sRGB curve is undone first, as in minicam's pipeline, where demosaicing runs on linear sensor data.

PSNR values are only comparable *within* a domain, because the same pixel error counts differently in linear and in gamma-encoded values.

PSNR in dB, RGGB, border 10 px excluded.

| Image | sRGB: bilinear | sRGB: Malvar | linear: bilinear | linear: Malvar |
|---|---|---|---|---|
| kodim01.png | 26.34 | 32.10 | 27.97 | 33.18 |
| kodim02.png | 33.38 | 38.17 | 34.64 | 36.43 |
| kodim03.png | 34.58 | 39.65 | 34.51 | 37.36 |
| kodim04.png | 33.75 | 39.17 | 34.24 | 36.38 |
| kodim05.png | 26.70 | 33.42 | 27.69 | 33.25 |
| kodim06.png | 27.84 | 33.33 | 28.00 | 32.65 |
| kodim07.png | 33.52 | 39.48 | 35.21 | 39.25 |
| kodim08.png | 23.62 | 29.20 | 22.21 | 27.59 |
| kodim09.png | 32.42 | 38.07 | 32.20 | 37.18 |
| kodim10.png | 32.49 | 38.70 | 31.54 | 37.39 |
| kodim11.png | 29.20 | 34.73 | 30.50 | 34.81 |
| kodim12.png | 33.46 | 38.93 | 31.57 | 36.36 |
| kodim13.png | 23.89 | 29.56 | 23.33 | 28.46 |
| kodim14.png | 29.33 | 34.44 | 30.26 | 33.59 |
| kodim15.png | 33.16 | 38.22 | 32.81 | 36.91 |
| kodim16.png | 31.32 | 36.50 | 34.54 | 39.08 |
| kodim17.png | 32.11 | 37.85 | 32.92 | 37.25 |
| kodim18.png | 28.18 | 33.92 | 28.44 | 32.85 |
| kodim19.png | 28.08 | 33.68 | 27.19 | 31.64 |
| kodim20.png | 31.67 | 37.18 | 31.30 | 35.44 |
| kodim21.png | 28.55 | 34.14 | 29.02 | 33.59 |
| kodim22.png | 30.46 | 35.41 | 30.23 | 33.88 |
| kodim23.png | 35.03 | 41.04 | 33.72 | 38.84 |
| kodim24.png | 26.81 | 32.20 | 25.10 | 29.68 |
| **Mean (24 images)** | **30.25** | **35.79** | **30.38** | **34.71** |
