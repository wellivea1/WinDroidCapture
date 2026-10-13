# Rendering and mpv reference

Inspected mpv commit **88eb9f2e73945d8bc9c133a2f67babaa9af34680**, fetched from its official repository on 2026-10-12. evidence/mpv-reference.json records the revision.

[Direct3D context](https://github.com/mpv-player/mpv/blob/88eb9f2e73945d8bc9c133a2f67babaa9af34680/video/out/d3d11/context.c), [polar shader sampling](https://github.com/mpv-player/mpv/blob/88eb9f2e73945d8bc9c133a2f67babaa9af34680/video/out/gpu/video_shaders.c), [filter kernels](https://github.com/mpv-player/mpv/blob/88eb9f2e73945d8bc9c133a2f67babaa9af34680/video/out/filter_kernels.c), [color representation](https://github.com/mpv-player/mpv/blob/88eb9f2e73945d8bc9c133a2f67babaa9af34680/video/csputils.c).

The reference separates video range from output color space, uses flip-model D3D11, widens downscale kernels and uses radial lookup weights for EWA. The second review also inspected [vo.c](https://github.com/mpv-player/mpv/blob/88eb9f2e73945d8bc9c133a2f67babaa9af34680/video/out/vo.c), [D3D11 uploads](https://github.com/mpv-player/mpv/blob/88eb9f2e73945d8bc9c133a2f67babaa9af34680/video/out/d3d11/ra_d3d11.c), and [swap-chain helpers](https://github.com/mpv-player/mpv/blob/88eb9f2e73945d8bc9c133a2f67babaa9af34680/video/out/gpu/d3d11_helpers.c). Event-driven handoff, bounded presentation queues, synchronized presentation and discard uploads informed the changes below. AWM implements those principles in its own C/HLSL. mpv/libmpv/libplacebo are not runtime dependencies.

## Filters

Both settings lists control real stages:

- **EWA Lanczos sharp** (default): Jinc-windowed Jinc matching mpv's ewa_lanczossharp parameters: radius 3.2383154841662362, blur 0.9812505837223707, window radius 1.2196698912665045. A 2048-entry float LUT avoids GPU Bessel evaluation. The footprint widens along downscaled axes. A central-neighborhood ringing limiter blends 70% toward clamped output; this is our choice, not a byte-for-byte port.
- **Lanczos 3**: separable passes, float16 intermediate, widened downscale support.
- **Bilinear**: an explicit hardware-sampler fast choice.

EWA suits diagonals and rotational symmetry; separable scaling costs less. No filter is claimed universally superior. Tests establish execution, normalization and processing time, not subjective superiority.

Exact-size camera output bypasses scaling. Other resizing uses the selected GPU path. GPU failures are reported without a silent nearest-neighbor fallback. Footprint support is bounded.

## Range, stride and presentation

Supported YUV is expanded to full BGRA with decoded range/matrix metadata. The software camera advertises full RGB32 and limited BT.709 NV12. The receiver uses actual IMF2DBuffer stride, otherwise the negotiated default stride.

The first physical test found flicker and crushed/distorted receiver colors. Corrections include retaining valid frames during contention, proper stride and RGB range metadata. The user confirmed flicker was gone after the first fixes. Discord subsequently showed the real phone image after the range correction, but the user requested smoother video; final color equivalence was not explicitly confirmed. The local receiver now consumes native limited BT.709 NV12 and presents via D3D11/EWA rather than GDI resizing.

Preview uses a flip-discard swap chain, explicit full-range SDR output space where supported, SyncInterval 1 and a waitable maximum-frame-latency-one queue. The worker waits for capacity before copying, so it takes the freshest complete source frame. A per-session frame-ready event replaces the fixed 16 ms render poll; its 100 ms timeout checks health only. Copy contention preserves the prior healthy preview instead of falsely declaring disconnection. Stable metadata and pixels come from one seqlock snapshot. Hide/Stop destroys preview GPU resources and hides the canvas while the separate placeholder owns its rectangle; a fresh update is requested when it returns. Offscreen camera scaling uses the same filter implementation independently of the UI. Duplicate live frame requests reuse the last correctly sized/formatted output; stop/stale state invalidates it.

YUV420P/YUVJ420P/NV12 plane uploads and integer-equivalent SDR range/matrix expansion now execute on D3D11. The output is read back for the existing shared-memory transport; software decoding and readback remain. Context1 UpdateSubresource1 uses D3D11_COPY_DISCARD, with base UpdateSubresource on contexts without Context1. Eight range/matrix/layout combinations and padded strides are checked against the previous conversion math. Chroma reconstruction and the three selectable scalers are unchanged; this is a processing-cost improvement, not a new image-quality claim.

The chosen waitable swap-chain implementation follows [Microsoft's API requirements](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject). It is our addition; the reviewed mpv legacy D3D11 context does not itself use this waitable-object path. The virtual camera separately uses QPC deadlines and a high-resolution timer available on the verified Windows 11 host, rather than rounded millisecond waits.

Settings → Output adds a simple Presentation pacing list. Smooth priority (default) maps relative phone PTS to QPC publication deadlines with a bounded one-frame buffer; Minimum latency publishes on arrival. This stage precedes both the integrated preview and Windows-camera source, applies to camera and screen, and changes on next Start. Overflow is counted, static pauses/PTS discontinuities re-anchor, Stop discards pending frames, and reconfiguration flushes the old epoch. A gentle minimum-arrival correction removes accidental startup offset without introducing another buffer. Decode/publication request MMCSS Capture and preview requests Playback; unavailable tasks use normal scheduling. These per-thread scheduling requests follow mpv's Windows task choice and do not add a buffer or change global timer/power settings.

## Measurement

The latest controlled synthetic pacing checks and observational real-phone graphs are in [PERFORMANCE.md](PERFORMANCE.md). A synthetic 60 FPS producer previously retained 53.33% of warm source updates; early revised runs retained 100% at both 30 and 60 FPS. The October 12 complete suite retained 135/135 at 30 FPS and 264/270 (97.78%) at 60 FPS under current desktop load. These are actual D3D11 submissions with synthetic frames, not measured display refresh or physical-device support. On real Pixel screen frames at 862×1920, conversion plus publication decreased from about 7.8 ms to about 1.5 ms before buffering was introduced. Smooth priority subsequently reduced preview arrival variation while adding about one frame of queue wait. Residual source/receiver interval variation remains visible; no Discord subjective-smoothness pass is inferred. Phone unavailability interrupted the exact screen clock-offset retest. Camera delivery was retested on October 13 with direct members and the current Smooth-priority pipeline, but callback jitter remained and the shared-video-queue regression had a timing failure. [Current evidence →](../TEST_REPORT.md)

Actual RTX 5070 Ti, synthetic flat image, 1920×1080 → 1280×720, 12 warm frames, render plus readback:

| Filter | Mean |
| --- | --- |
| EWA Lanczos sharp | 1.035 ms |
| Lanczos 3 | 0.796 ms |
| Bilinear | 0.661 ms |

Historical evidence: evidence/native-final-automated-tests.log. A prior run measured 1.025 / 0.730 / 0.712 ms. The latest complete run, evidence/shared-pacing-complete-final-tests.log, measured 1.381 / 1.235 / 0.909 ms respectively and 1.212 ms for GPU NV12 → BGRA conversion/readback. These exclude phone capture/encoding, transport, decoding, receiving presentation and audio, and are not end-to-end latency.

Live diagnostics observed a 143.550 ms maximum while opening the scaler, with subsequent cumulative means near 2 ms for 1280×720 NV12 requests. Runtime HLSL compilation was then removed: scripts/compile-shaders.c generates embedded optimized bytecode during the build. This removes HLSL compilation from the UI/camera request path; it is not proof that every Discord hitch is fixed. An earlier synthetic test measured 122.341 ms and the latest run measured **202.251 ms** for cold GPU initialization plus first render/readback. Driver initialization/resource creation and driver shader compilation remain potential startup costs.

HDR/10-bit, ICC calibration, GPU decode/direct surfaces and sustained A/V clock validation remain work.

## Licensing

mpv's C renderer and shader-generation code were inspected, not copied. Jinc equations/parameters are reproduced mathematically. filter_kernels.c offers New BSD or applicable alternative LGPL terms; other reference files retain LGPL notices. The reference checkout retains all original licenses. Future direct reuse/linking must retain its applicable license/source obligations.
