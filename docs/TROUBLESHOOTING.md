# Troubleshooting by the layer that failed

[← README](../README.md) · [Quickstart](QUICKSTART.md) · [Test report](../TEST_REPORT.md)

| Symptom | Check next |
| --- | --- |
| No device / Unauthorized | Selected ADB path, USB cable/debugging, phone unlock and RSA prompt; don't bypass authorization |
| Offline / unknown state | Refresh the specific transport; unknown is not treated as healthy |
| Shared ADB version conflict | Read the mismatch explanation and use the explicit repair action only when intended; the app does not kill/restart a shared server automatically |
| Wireless pairing works but no device appears | Use the separate connection address/port; pairing port is not assumed to equal connection port |
| Screen capture starts but has no frames | Keep the selected display awake; ADB authorization does not mean the display is on |
| Camera-in-use or unsupported mode | Close the phone app holding the lens, inspect reported capabilities, then start with Standard; read the actual startup error |
| Direct Pixel lens absent | Verify exact model recognition and matching `scrcpy-server-awm`/manifest siblings; discovery remains firmware dependent |
| Preview works but Discord has no camera | Install/register our Windows source and explicitly select its exact device name; a preview is not registration |
| Separate receiver fails while Discord Test Video is open | Close the other camera consumer first; concurrent use failed with 0xC00D3704 on the tested desktop |
| Required microphone refuses startup | Own endpoint is absent: use Audio Off for video tests; unsigned source/PCM arrival is not a Windows microphone |
| DLL/version failure | Use the matching complete scrcpy 5.0.1 runtime, not independently mixed FFmpeg/SDL DLLs |
| Stop shows stale phone content | Verify owned host/client teardown and stopped-image status; export redacted diagnostics if the output retains a live image |

## Uneven motion

1. Check observed source dimensions/FPS and frame health in **Diagnostics**. Source variation can survive every downstream stage.
2. Keep **Smooth priority** for uneven arrivals, or choose **Minimum latency** when its latency tradeoff matters more. The change applies at next Start.
3. Compare the integrated preview with the explicit receiver at known dimensions. Discord's self-view and negotiation are independent.
4. Use the [numeric tracing procedure](PERFORMANCE.md) to distinguish packet arrival, decode/conversion, queue wait, preview submissions and receiver cadence. A timing graph does not include sensor or monitor scanout latency.

GPU decode/direct-surface transport and full A/V clock validation remain work. Micro-stutters have been reduced in some traces, but final Discord smoothness has not been approved.

## Export useful diagnostics

Use **Diagnostics → Export** for redacted local text. Verify the exported file before sharing it. Keep raw ADB/Camera2 dumps, selected serials, pairing details, DPAPI files, captures and local evidence private. There is no telemetry upload or automatic recording.

## Microphone production readiness

The own driver source exists and static build/package checks pass. It is **unsigned and never kernel loaded in the retained tests**. A trusted package is required before installation; the project does not change Secure Boot, signature enforcement, HVCI, Windows privacy settings or the default playback device.

After an accepted signed package exists, test endpoint presence, actual received samples, phone-specific speak/tap and a visible clap separately. Full local A/V and sustained drift checks remain **NOT RUN**. An OBS/VB-CABLE historical result does not validate this driver's input.
