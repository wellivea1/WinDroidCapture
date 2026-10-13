# Native-output test report

Date: 2026-10-12. Development build 0.2.0. Windows 11 Pro x64 10.0.26200, build 26200; RTX 5070 Ti. Authorized USB Pixel 10 Pro XL, Android 17. Raw device identifiers and phone media are excluded from the published package.

**Release state:** app-owned camera registration and real phone video in Discord's local Test Video were verified. The custom microphone driver builds and passes static package checks, but is unsigned and not loaded. The webcam-plus-microphone requirement is therefore incomplete. Discord smoothness remains below the user's expectation. No live call, remote transmission or media recording was performed.

## Automated and component checks

scripts/test.ps1, evidence/native-final-automated-tests.log:

| Category | Result | Scope |
| --- | --- | --- |
| Core/protocol/config/ring checks | PASS, 50 checks, 0 failures | ADB states and transitions, distinct same-model devices/transports, explicit selection, capabilities, command conflicts, headless camera/mic flags, spaces/Unicode, wireless validation, cancellation generations, bounded packet/frame data, stopped hub, missing audio endpoint, DPAPI persistence, both scaler values, rejected malformed settings, v2 migration, simulated microphone-ring underrun/stale/overflow/reset/generation. |
| Simulated process lifecycle | PASS, 28 checks, 0 failures | Clearly labeled fake ADB/scrcpy/media host in an isolated Unicode path. Start/Stop race, explicit selected phone, process exit, selected transport becoming offline while another phone is authorized, owned-process/frame cleanup. These are not hardware-delivery evidence. |
| Real D3D11 execution with synthetic images | PASS | All three selectable filters; flat-field preservation and region orientation. Actual GPU, synthetic frames. |
| Actual COM camera-source component | PASS | Activation/event/sample delivery, release of activation before live source use, RGB full-range metadata, stopped image, Stop/Shutdown and unload. This does not establish Windows registration or phone delivery by itself. |
| Driver build / API / INF / catalog | PASS, package unsigned | C WDM/PortCls/WaveRT .sys built; WDK Universal API check, InfVerif /w, Inf2Cat 10_GE_X64. No INF/catalog errors or warnings. |
| Unsigned-package installer refusal | PASS | Catalog trust error 0x800B0100, before creating a root device. No security settings changed. Signed update/reuse path has not been exercised. |

Synthetic 1920×1080 → 1280×720, 12 warm frames, render plus readback: EWA Lanczos sharp **1.035 ms**, separable Lanczos 3 **0.796 ms**, bilinear **0.661 ms**. Cold GPU initialization plus first EWA render/readback was **122.341 ms**, even with build-time shader bytecode; driver/resource initialization can still cost time. These values exclude capture/transport/decode/receiving presentation/audio. They are not end-to-end latency or a proof of subjective image superiority.

## Physical evidence

1. The upstream-based no-window client delivered real phone camera packets and raw microphone PCM to the host. Initial 12-second probe: 340 video frames, 568,320 audio frames, 28.33 average video FPS including startup, peak 0.0431. The own microphone driver flag was 0; this was explicitly a PCM-only test. It did not create a Windows microphone.
2. The installed own camera appeared as **Android Webcam Camera (Windows Virtual Camera)**. Its source DLL's installed hash matched the RGB-range-corrected build at installation.
3. Initial native receiver: 901 frames / 806 changing samples over 30 seconds, 1920×1080, camera HRESULT success. User reported flicker and crushed/distorted colors. That visual result failed.
4. First corrected 180-second run: 5,402 received frames, 5,014 changing samples, 1920×1080, camera HRESULT success. User confirmed flicker was gone; colors remained a bit crushed and the main preview hitched once. This preceded the final RGB metadata/D3D11 changes.
5. Discord listed the own camera, it was explicitly selected, and local Test Video displayed the selected Pixel's real image. The integrated preview and Discord showed the same scene. The user subsequently said “The Discord preview could be smoother.” Final color equivalence was not explicitly confirmed; smoothness is unresolved.
6. Discord video continued while the manager preview was hidden. Stop removed the owned phone client/host and Discord displayed our explicit stopped banner. Existing microphone selection was not changed or tested as if it were phone audio.
7. UI checks exercised both real scaling lists and changed them independently. Both were restored to EWA Lanczos sharp. A stale integrated GPU image on Hide/Stop was found and fixed by recreating only the owned preview child when leaving live rendering; the final Hide check visibly showed the “Preview hidden / Windows output continues independently” placeholder.
8. A second RGB receiver failed with 0xC00D3704 (MF_E_HW_MFT_FAILED_START_STREAMING) while Discord held its local camera test open. Native NV12 also failed during that concurrent use. After explicitly closing Discord's Test Video/settings, the NV12 receiver passed: **446 received frames, 382 changing samples in 15 seconds, 1280×720, camera HRESULT 0**, displayed with D3D11/EWA. The host observed 1920×1080 source frames; the receiver negotiated 1280×720 and resampling was real. This is not labeled native 4K. Evidence: evidence/native-final-nv12-alone.log. The helper still returns a failure exit code because the required own microphone is absent.
9. Live host diagnostics during that camera-only session reported 1280×720 NV12 requests with cumulative processing means decreasing from 2.511 ms (300 requests) to 2.019 ms (2,400 requests); maximum 143.550 ms included first scaler initialization. Build-time HLSL compilation was subsequently implemented and all component/GPU tests passed. This last rendering-binary refinement has not been subjectively approved in Discord; no sustained-smoothness success is inferred from it.

Technical logs remain local under evidence. The package includes source, notices and this report, not captured phone pictures/audio, credentials or raw device logs. Historical OBS/VB-CABLE results do not validate these native outputs.

## Not run and remaining blockers

| Check | State and exact reason |
| --- | --- |
| Own Windows microphone delivery / Discord Input Device / phone-specific speak/tap | **NOT RUN:** kernel package is unsigned; own endpoint is absent. No PC/cable/default microphone substitution. |
| Full native A/V clap sync and sustained drift | **NOT RUN:** no loaded own microphone driver. Source timestamps and independent Windows clocks still require validation and correction where needed. |
| Kernel functional safety / Verifier / PnP/power / HVCI / multiple-client timing / malformed IOCTL / HLK | **NOT RUN:** driver has never been loaded in an isolated driver development environment. Static compile/package checks cannot prove kernel behavior. |
| Microsoft signing and production installation | **BLOCKED externally:** requires the publisher's Hardware Developer account/signing submission, certificate and applicable validation. No local fake signature or weakened signature enforcement was used. |
| Final explicit color-equivalence and subjective smoothness approval | **NOT PASSED:** user asked for smoother Discord video; no final calibrated comparison or frame-time measurement of Discord has established smooth output. |
| Receiving app negotiated FPS/dimensions | **NOT MEASURED in Discord:** camera presence and visible feed were checked. A nominal camera media type or manager observed source size is not Discord's negotiated output. |
| Native 4K / 60 FPS, output rotation/mirroring with real receiver | **NOT RUN for final native chain:** only Standard was physically exercised. Command construction and synthetic orientation checks pass. Historical scrcpy/OBS mode checks are not native-output validation. |
| Controlled unplug/replug and wireless pairing/loss | **NOT RUN physically:** USB phone remained connected; fake selected-device-loss cleanup passed. Wireless UI and argument validation implemented, not a wireless hardware test. |
| Long thermal/latency/quality measurements | **NOT RUN:** no objective camera comparison, synchronized clocks or long thermal run. |
| Signed microphone repeated install/update and uninstall | **NOT RUN:** unsigned installer refusal verified; trusted-package path cannot be exercised without an accepted signed package. |
| Browser / Zoom / other receiving applications | **NOT RUN:** native receiver and Discord local Test Video only. No call or transmission authorized. |
| Concurrent receiving applications | **FAILED on this configuration:** a standalone receiver could not start while Discord held Test Video open, for RGB and native NV12 (0xC00D3704). It passed once Discord's camera test was closed. Close other camera consumers before using Test inputs until sharing/negotiation is corrected. |

Normal capture runs without elevation. Only output installation requires elevation. OBS, VB-CABLE and the shared ADB server were not terminated or repurposed by the refactor.

Remaining implementation limits: CPU decode/YUV conversion, GPU readback for resized camera frames, SDR/8-bit formats, bounded scaling footprints, unverified receiver negotiation beyond tested modes, incomplete shared A/V clock work, and kernel release validation/signing. A selected quality filter does not establish genuine 4K or better sensor image quality.
