# Controls, profiles and output behavior

[← README](../README.md) · [Quickstart](QUICKSTART.md) · [Rendering](RENDERING.md)

## The four pages

| Page | Main job |
| --- | --- |
| **Capture** | Device/source selection, integrated preview, source settings and Start/Stop |
| **Devices** | Inspect distinct transports, USB instructions, wireless pair/connect/disconnect |
| **Settings** | Output format, scaling, pacing, appearance, dependencies and installation |
| **Diagnostics** | Requested/observed configurations, frame health, output requests and redacted export |

The status strip separates **ADB**, **Capture**, **Camera** and **Mic**. Green means connected/live/available; blue means an operation is in progress; amber covers waiting/retrying/unknown; red indicates failure/unavailable input; gray means unselected/stopped/off. Text remains alongside color. Camera Available establishes enumeration, not received video. Mic Forwarding requires both phone PCM and the own driver.

## Starting profiles are editable requests

### Phone camera

| Profile | Dimensions | Camera FPS | Codec | Approximate bitrate |
| --- | --- | ---: | --- | ---: |
| Standard | 1920×1080 | 30 | H.264 | 16 Mbps |
| Detail | 3840×2160 | 30 | H.265 | 40 Mbps |
| Motion | 1920×1080 | 60 | H.264 | 24 Mbps |

Camera mode uses the actual camera-frame-rate option. IDs, sizes and encoders come from the selected device's enumeration. An explicit camera ID is not combined with facing selection or conflicting automatic size controls. Reported capabilities are declarations; startup and observed stream dimensions still decide whether a mode works.

### Phone screen

| Profile | Long-edge bound | FPS cap | Codec | Approximate bitrate |
| --- | --- | ---: | --- | ---: |
| Balanced | 1920 pixels | 30 | H.264 | 16 Mbps |
| Native | No size limit | 30 | H.265 | 24 Mbps |
| Motion | 1920 pixels | 60 | H.264 | 24 Mbps |

Screen capture is content-driven. A healthy static screen may reuse its last frame without being declared disconnected; heartbeat and selected-session health remain distinct from content changes. **Native** means a capture size request, not a guarantee about receiver output or image quality.

## Geometry

| Setting | Defined output behavior |
| --- | --- |
| Output rotation | Clockwise, applied to preview and Windows output |
| Mirror output | Horizontal reflection after rotation |
| Fit — preserve all content | Keeps aspect ratio and adds black bars as necessary |
| Crop to fill — edges removed | Keeps aspect ratio and intentionally removes edges |
| Screen Follow phone | Permits source orientation/dimension changes |
| Screen Lock current orientation | Uses the upstream capture-orientation lock |

Screen orientation capture and final output rotation are separate stages. Discord can mirror its own self-preview. There is no desktop capture, window embedding, phone-control injection or speculative focus/HDR/exposure slider.

## Settings → Output

| Selectable list | Choices | When applied |
| --- | --- | --- |
| Preferred output | Available camera formats | Next session / receiving-client negotiation; not forced onto Discord |
| Preview scaling | EWA Lanczos sharp, Lanczos 3, bilinear | Immediately |
| Pipeline scaling | EWA Lanczos sharp, Lanczos 3, bilinear | Next Start |
| Presentation pacing | **Smooth priority** (default), Minimum latency | Next Start; shared by camera and screen |
| Appearance | System, Light, Dark | Immediately; high-contrast colors respected |

Smooth priority intentionally buffers about one requested interval with a bounded PTS-aware queue: roughly 33 ms at 30 FPS or 17 ms at 60 FPS. Minimum latency publishes as packets decode. Both count late/dropped frames and flush on Stop/reconfiguration. The queue can absorb arrival variability, but cannot generate missing phone frames. [Measured tradeoffs →](PERFORMANCE.md)

Exact-size output copies pixels; actual resizing uses the selected GPU filter on full decoded frames. Preview scaling is an independent consumer. Enlarging a low-resolution image does not produce genuine 4K detail.

## Audio and persistence

**Audio: Off** disables phone microphone capture. Required microphone mode is explicit and refuses startup if the own input is unavailable. There is no speaker-monitor path, cable, system-wide default-device change or desktop-audio fallback.

Camera and screen preferences are separate per-device profiles. Dependency paths, appearance, output preferences and pacing use Windows DPAPI-protected local storage. Supported older versions are backed up during migration; malformed/future files are refused without replacement. Reconnect is opt-in, bounded to three attempts for the same selected device and canceled by Stop. Capture never starts on application launch.
