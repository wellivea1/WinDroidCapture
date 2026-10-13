# Phone screen capture and UI refactor plan

Date: 12 October 2026. Status: **implemented in development build 0.3.0; physical acceptance is incomplete**. The requirements below record the plan that started from build 0.2.0. See [TEST_REPORT.md](../TEST_REPORT.md) for actual checks and remaining gates. The older REFACTOR_PLAN.md records the preceding OBS/VB-CABLE replacement project and is retained as history.

## Product decision

Add **Phone camera / Phone screen** as a source selector feeding the same app-owned Windows virtual camera. Keep one selected device, one video source and one capture session. The receiving application keeps selecting **Android Webcam Camera (Windows Virtual Camera)** when the user changes modes.

Normal use becomes: select an authorized device, choose the video source and a profile, press Start, then select the camera in Discord. The integrated preview shows the output framing. It remains an optional consumer; hiding it, covering the manager or minimizing the manager must not affect Windows output.

The first screen release captures one enumerated existing phone display, without phone control. Source changes use **Stop → select source → Start**. It uses the native camera path already in the repository and introduces no OBS or audio-cable dependency. Screen capture does not require a new Windows device, camera CLSID or camera registration.

## Verified baseline and evidence boundary

Repository inspection found no AGENTS.md in the workspace or checked ancestors, and no Git repository. Existing source, builds, evidence and the baseline archive are preserved. The implementation is C17/Win32 with CMake, clang-cl, asynchronous ADB/scrcpy processes, a private scrcpy packet exporter, FFmpeg decoding, D3D11 rendering and a Media Foundation camera source.

Read-only checks for this plan:

| Item | Observed result |
| --- | --- |
| Windows | Windows 11 Pro x64, 10.0.26200, build 26200 |
| Compiler | clang-cl 22.1.8; Visual Studio Community 2026 available |
| ADB | 37.0.1-15733141, protocol 1.0.41 |
| scrcpy | Local 5.0.1; official latest release also 5.0.1 when checked |
| Selected device | Authorized USB Pixel 10 Pro XL, Android 17 |
| Selected-device display probe | Display ID 0, 1344 × 2992 |
| Screen capture through own camera | **Implemented and physically exercised:** real Pixel display frames in the explicit Windows receiver; see current test report for static-screen observations and tests not yet run |

The probe explicitly addressed the selected device with --list-displays, --no-audio and --no-control. Discovery is not capture evidence. Generated phone screens and dimensions in the design images are illustrations, not observed streams.

The existing own camera has delivered real phone-camera video in Discord's local Test Video. Discord smoothness remains unresolved; final color equivalence and negotiated Discord format were not established. A second receiver previously failed while Discord held its camera test open, then worked after that test was closed. These issues remain test obligations.

The custom microphone driver builds and passes static package checks, but is **unsigned, not installed and not kernel tested**. No own Windows microphone is currently available. Screen-video implementation can proceed independently; complete native audio/video delivery cannot be claimed until the driver is safely tested, signed, installed and verified. See the existing [TEST_REPORT.md](../TEST_REPORT.md) for the evidence.

## Target paths

~~~mermaid
flowchart LR
    P[Selected phone] --> S[Upstream scrcpy: camera OR display]
    S --> E[AWM encoded video / optional microphone export]
    E --> H[C media host: decode, session health, source frames]
    H --> C[D3D11 composition at camera request size]
    C --> V[Existing app-owned Windows virtual camera]
    V --> R[Discord or an explicit local receiver]
    H --> U[Independent integrated D3D11 preview]
    H --> M[Own microphone driver, when installed and validated]
    M --> R
~~~

Composition rules and filters are shared by preview and camera output. Each renders directly from the full source frame at its own target size. There is no preview recapture and no small intermediate preview canvas later upscaled to a claimed high-resolution camera feed.

The preview follows the aspect ratio of the preferred output, or the known active camera request when available. The preferred format also supplies the initial format request for our explicit local receiver test. A receiver can negotiate a different supported format; the app must show this distinction. Selecting a preferred format cannot force Discord's selection.

## Source model, discovery and settings

Introduce a native CaptureKind enum and typed CameraSettings and ScreenSettings. Keep common encoding, audio and output-composition settings separate. Avoid extending the legacy camera Profile with unrelated fields and continuing to validate everything as a camera.

Camera retains an enumerated camera ID, exact requested dimensions, camera FPS, codec/bitrate and optional encoder. Screen has an enumerated display ID, maximum long-edge size or Native, FPS limit, orientation policy, codec/bitrate and optional encoder. Common output transforms are applied once in the host. Camera IDs and display IDs remain different types and validators.

Probe --list-displays asynchronously for the explicitly selected transport. Parse multiple IDs, unknown sizes, no displays and unexpected output without inventing a healthy capability. Cache probes by device identity, transport, selected tool version and source type. Reprobe on a transport/tool change or failed startup. Display enumeration does not prove capture support.

Separate ADB authorization/connection state from source support. The current manager's global Android 12+ restriction and emulator rejection are camera-specific and must not hide potentially usable screen devices. Older devices and emulators remain unverified until tested; the first physical acceptance target is the existing Pixel.

Persist independent Camera and Screen profiles per device, the last selected mode, global preferred camera output format and both existing scaler selections. Migrate DPAPI settings v3 to v4, preserving the camera profile and dependency paths verbatim. Default the new screen profile to Display 0 only if enumeration confirms it, Balanced, Follow phone, Fit, audio Off and mirror Off. Never start capture on launch or on selecting a source. Preserve the prior settings file before migration; malformed/unknown versions must not be silently overwritten.

## Commands and honest profile labels

Use the existing safe argument array and the same selected ADB for discovery and the managed client. Both modes retain explicit selection, no-control, no-window, no-playback, no-downsize-on-error and the app's owned export target. Validate the actual patched-client capability/version; a stock client cannot perform the private export merely because it supports display capture.

| Mode | Mode-specific command options | Forbidden cross-mode options |
| --- | --- | --- |
| Phone camera | --video-source=camera, --camera-id, --camera-size, --camera-fps | --display-id, display capture orientation and screen-only sizing policy |
| Phone screen | --video-source=display, --display-id, optional --max-size, --max-fps, optional --capture-orientation=@ | --camera-id, --camera-facing, --camera-size, --camera-fps, camera zoom |

Follow phone omits the orientation lock. Lock current orientation uses the supported @ behavior. This is separate from an optional clockwise output rotation in our composition path; do not apply the same transform twice or assume every device has a portrait natural orientation. The version-matched [scrcpy video documentation](https://github.com/Genymobile/scrcpy/blob/v5.0.1/doc/video.md) is the reference for these options.

Starting screen profiles are editable requests, not proven performance modes:

| Profile | Source size policy | Capture FPS limit | Codec / bitrate |
| --- | --- | --- | --- |
| Balanced | Maximum long edge 1920 | 30 | H.264 / 16 Mbps |
| Native | Device display dimensions, within host limits | 30 | H.265 / 24 Mbps |
| Motion | Maximum long edge 1920 | 60 | H.264 / 24 Mbps |

Show the actual decoded dimensions after startup. Do not call a portrait long-edge limit 1080p, promise that an FPS cap produces constant FPS, or label a source/output combination genuine 4K merely because a consumer requests 3840 × 2160. Encoder alignment can alter dimensions. Native is rejected with a clear alternative when reported/observed dimensions exceed the host's bounds: even dimensions, each axis at most 3840 and total pixels at most 3840 × 2160. No silent mode downgrade.

Audio choices remain **Off / Phone microphone**, with Off the new screen profile's default. Off emits --no-audio. Microphone emits explicit --audio-source=mic, --audio-codec=raw and --require-audio when supported, and requires a usable own microphone component for full output mode. Never inherit scrcpy's default phone-output audio or silently substitute another Windows microphone. The tagged [audio documentation](https://github.com/Genymobile/scrcpy/blob/v5.0.1/doc/audio.md) confirms the need to choose the microphone explicitly. Phone app audio is outside the first screen release.

## Required backend changes before UI polish

### Static screens and session liveness

scrcpy screen video is content-driven. The existing hub rejects a frame after one second and the host packet reader times out after three seconds. An unchanged screen with audio Off would therefore fail in the current path.

Split frame-content arrival from session health. Add versioned exporter heartbeat/control messages on the same ordered stream, serialized with packet writes and stopped immediately on exporter shutdown. A proposed initial heartbeat interval is 500 ms with a three-second expiry; these are implementation/test targets, not measured guarantees. Retain bounded startup and I/O cancellation.

Track exporter/process health, demux end/error state, selected-device ADB health, host heartbeat, source-frame age and capture generation independently. Reuse a valid unchanged screen frame only while the session remains healthy. A camera keeps its own freshness deadline. An exporter heartbeat does not count as a new Android frame or prove that the phone's displayed content has changed. A pathological stalled video connection can be indistinguishable from a legitimately static screen while other health checks still pass; expose last-content age rather than claiming fresh phone images from a heartbeat alone.

The camera source already supplies timed samples. It may repeat the valid unchanged display frame at the negotiated camera cadence, with fresh output sample timestamps. Report **capture updates/s**, **camera sample requests/s** and any receiver-observed cadence separately. Do not present repeated output samples as newly captured phone frames. Do not use stock scrcpy playback-buffer flags to pretend to smooth the private headless export.

Stop invalidates live output before asynchronous process teardown. Detected unplug/offline, process death, export EOS/error or heartbeat expiry invalidates video and microphone forwarding; Windows output becomes the owned disconnected/stopped image. Old generations cannot republish. Stop also cancels bounded reconnection immediately.

### Rotation and video reconfiguration

The current export adapter ignores its initial session metadata and omits the optional packet-sink push_session callback. Upstream 5.0.1 demux calls that callback for new video dimensions during display capture; it does not necessarily close and reopen the sink. The relevant local upstream files are app/src/trait/packet_sink.h and app/src/demuxer.c under tools/scrcpy-5.0.1.

Export initial and subsequent session metadata, including dimensions and client-resize information. Add an ordered video epoch/configuration event, validate the frame shape, invalidate old-epoch frames and reset/reconfigure the video decoder and scaler resources as required by the codec. Handle H.264/H.265 in-band configuration as well as distinct configuration packets. Resume after a valid decoded frame/keyframe; use a neutral reconfiguring output while no valid frame exists. Include epoch, decoded-frame sequence, target shape/format and composition/filter choice in the rendered-output cache key, rather than relying on PTS alone. Do not reset the audio clock or the whole capture generation for every phone rotation.

Keep the Windows camera's negotiated output dimensions stable across portrait/landscape changes. Initial output formats remain the existing landscape formats; portrait phone screens are fitted into them. A source-size change must not force an unnecessary Windows camera reinstall or change the device identity.

### Protocol and resource ownership

Split the single protocol-version constant into exporter, frame-hub and camera-RPC versions. Advance exporter/hub versions for the new metadata, without changing the already installed camera RPC v2 when its schema remains adequate. Rebuild the camera source only if a demonstrated provider/RPC change needs it; screen mode alone is not a registration change.

Replace the single fixed frame mapping with a per-session GUID mapping known to the host and preview. Retain a separate one-producer ownership guard. An old preview reader holding a mapping must not block the next Start or allow a previous session to appear after a source change. Keep restricted local IPC, bounded payloads, ordered sequences and cancelable writes/reads.

Model Idle, Starting, Running, Stopping, Reconnecting and Error explicitly in the manager. Starting has a visible Cancel action. Device/source changes are disabled during a session with a nearby explanation. Close performs an asynchronous shutdown handshake instead of waiting indefinitely on the UI thread.

## Composition and rendering

Default to **Fit — preserve all content** with black letter/pillarboxing. Add an explicit **Crop to fill — edges are removed** choice. Omit stretching. Keep whole-display capture in the first release; interactive source-region selection can follow after rotation/liveness is validated.

Use one tested composition transform for aspect ratio, framing, clockwise output rotation and final horizontal mirror. Preview and each camera output render from source pixels using this transform at their respective dimensions. Mirroring is Off by default for screen text. Hide or resize the preview without reallocating or changing camera output.

Retain the existing selectable **Preview scaling** and **Pipeline scaling** lists: EWA Lanczos sharp, Lanczos 3 and bilinear, with EWA the initial high-quality choice. Preview selection can update immediately; pipeline/capture changes apply on next Start initially and say so. Preserve the existing mpv-derived filter reference/notices. Validate thin text, sharp edges and ringing, not only photographs; selecting a filter does not prove subjective superiority.

Keep D3D11 and build-time shader bytecode. Move preview resource creation, upload and rendering off the UI thread onto a renderer worker that owns its context/swap chain. Coalesce frames, render on new content/resize/filter changes, and avoid copying full static frames every 16 ms. Warm resources before declaring the preview ready and measure cold and warm costs. Use an explicit worker-stop acknowledgment before destroying/recreating its owned preview HWND.

This milestone does not claim zero-copy hardware decoding: the current host still decodes and converts on the CPU and reads resized frames back from the GPU. Measure that path and bound queues first. Shared GPU textures/hardware decoding are later optimizations if measured performance justifies them; a scrcpy playback hardware-decoder option does not automatically accelerate our private host.

## Complete UI refactor

Use the four-page geometry from the dark concept sheet and the compact light Capture treatment for the first implementation. The dark sheet is a palette alternative, not a separate interface or framework. Flat fills, thin dividers, ordinary Win32 tabs/dropdowns/edits/buttons/checkboxes, Segoe UI and one D3D11 child make this reproducible in C. No oversized rounded cards, pill switches, glass effects or ornamental status controls.

| Page | Main tasks and streamlined behavior |
| --- | --- |
| Capture | Device and Video source selectors; large output preview; compact source-specific property column; one Start/Cancel/Stop action; actual output names with Copy; separate receiver-test status |
| Devices | Responsive report list with distinct transports/states; selected-device details; Refresh/Inspect; USB guidance; Wireless pairing and connection fields with separate ports and targeted actions |
| Settings | Output, Dependencies and Advanced subpages; preferred output format; two scaling lists; paths/versions; output-component installation state; opt-in reconnect and advanced encoding choices |
| Diagnostics | Separate stage states, effective command, actual stream/output statistics and bounded redacted logs; export diagnostics without media or secrets |

Delete the oversized fixed Setup dialog from the normal workflow by moving its existing functions into Settings and Devices. Probe capabilities automatically after an authorized selection, and show a small progress/error state instead of requiring a separate routine Probe click. Save validated per-source changes automatically with a visible Saved state; dependency edits use an explicit Validate and Apply action so a half-edited path cannot replace the working tool selection.

The Capture page changes only the appropriate controls: Camera + camera profile/FPS in camera mode; Display + long-edge/FPS-limit/orientation policy in screen mode. Common framing/audio/output rows stay in place. Advanced encoding/rotation/mirroring choices expand within the property pane rather than creating another giant dialog. The two scaling lists live only in Settings.

Target a 1040 × 700 DIP client area, with a 960 × 640 minimum and scrollable property/settings areas. Preserve the preview's aspect ratio and the bottom action position; implement WM_DPICHANGED and verify 100%, 125%, 150% and 200% scaling. Use ordinary keyboard focus, access keys, selection arrows, useful disabled-state explanations and system high-contrast colors.

Use text plus color for ADB, Source, Camera and Microphone states. Camera installed/enumerated, host publishing, a consumer requesting frames, and a receiving app confirmed are different states. Receiver stays Not checked until an explicit test. Test input selects the own Windows camera explicitly and reports a camera-busy failure; it never substitutes a PC camera. A required own microphone that is absent is reported as unavailable, not as an audio test success.

In screen mode display the short notice **Entire phone display is captured, including notifications.** Do not alter notification settings or phone security automatically. Android protected content can be blank; it is not a reason to bypass protection or claim encoder failure. See [Android's secure-content guidance](https://developer.android.com/security/fraud-prevention/activities).

Detailed geometry, control mapping, state copy and corrections to the generated images are in [UI_IMPLEMENTATION_SPEC.md](design/screen-refactor-v1/UI_IMPLEMENTATION_SPEC.md). Images are references, not runtime assets or acceptance screenshots.

## Implementation sequence and gates

| Milestone | Work | Required gate |
| --- | --- | --- |
| 1. Source contracts | Typed profiles, screen-specific capability probe/parser, argument validation, settings migration | Camera regressions pass; display commands contain no camera flags; selected-device/transport identity preserved |
| 2. Live display transport | Heartbeat/health split, push_session export, decoder reconfiguration, epochs and per-session mapping | Static audio-off fake runs, resize/config fixtures, Stop/start races and dead-process cleanup pass |
| 3. Smallest real screen path | Use minimal current UI/test runner to select actual display and feed existing own camera | Real Pixel screen reaches explicit local camera receiver, remains live when unchanged, rotates, stops and disconnects cleanly |
| 4. Composition/performance | Shared Fit/Fill geometry, existing filter lists, renderer worker, timing diagnostics | Preview/camera framing agree at different sizes; hidden preview independent; synthetic text/color and real scrolling checks pass |
| 5. Win32 pages | Replace monolithic UI/Setup with four pages, source-aware forms, keyboard/DPI behavior | All existing setup/device actions accessible; Cancel/Stop reliable; layout reviewed against generated references |
| 6. Acceptance/publish | Real receiver and Discord local tests, camera regressions, docs/build/publish package | Report separates automated, simulated and physical results; remaining microphone/concurrency/smoothness limits explicit |

Do not polish all pages before milestone 3. Do not mark screen capture complete after display enumeration, a preview-only success or fake tests.

Suggested file changes:

| Files | Responsibility |
| --- | --- |
| New src/native/capture_profile.h/.c | Native source types, display capabilities, mode-specific defaults and validation |
| src/core.h/.c, src/native/command.c | Add compatible tool-feature/probe parsing; build separate safe camera/display argument arrays; preserve legacy backend |
| src/native/settings.h/.c | DPAPI v4 migration and independent source profiles |
| src/native/manager.h/.c | Explicit phases, capability cache identity, selected-device health, cancellation and per-session ownership |
| native/scrcpy/recorder.c, src/native/protocol.h | Session callback, serialized heartbeat/control events and independently versioned internal contracts |
| src/native/media_host.c, frame_hub.h/.c | Static-display policy, video epochs/config changes, source frame metadata and composition at camera request dimensions |
| src/native/preview.h/.c, scale_shader.h | Composition geometry, render worker and dirty/coalesced redraws; keep compiled shaders |
| New src/native/ui/*.h/.c; src/native/main.c | Small shared shell plus Capture, Devices, Settings, Diagnostics pages; main retains WinMain and lifecycle wiring |
| camera/source.c, only if needed | Provider timing/negotiation corrections proven by tests; preserve camera identity/RPC where possible |
| tests/*.c, native/CMakeLists.txt, scripts/* | New source/liveness/reconfiguration tests, inclusion of new C modules and updated publish contents |

## Acceptance matrix

| Category | Required coverage |
| --- | --- |
| Command/capability unit tests | Multiple display IDs, unknown sizes/states, camera vs screen support, no cross-mode conflicts, explicit selection, spaces/Unicode paths, bounded dimensions, encoder failures, explicit audio behavior |
| Clearly labeled simulated integration | At least 60 seconds of unchanged screen with audio Off and healthy heartbeats; heartbeat loss/EOS/offline/Stop; video reconfiguration with in-band and separate codec config; old epochs never publish; audio clock preserved |
| Session/config regressions | Cancel during Start, Stop during reconnect, ten Camera/Screen cycles, lingering old mapping reader, settings v3 migration, malformed config and independent saved profiles/scalers |
| Real GPU with synthetic patterns | Portrait/landscape Fit and Fill geometry, rotation/mirror order, pixel aspect, thin text/checkerboards/color bars, all three scalers, preview resize without output-quality loss |
| Physical Pixel screen | Explicit display selection; static page ≥60 seconds; scrolling/moving test content; portrait/landscape rotation; Follow/Lock; Fit/Crop; preview hidden/minimized; unplug/reconnect; Stop replaces screen and stops audio |
| Explicit receivers | Own camera in independent local receiver, then Discord local Test Video; report actual available format/cadence observations; reproduce/investigate simultaneous-consumer failure separately |
| Smoothness | Startup time, capture updates, sample delivery intervals, p50/p95 gaps and dropped/repeated counts where measurable; user inspection of scrolling; no invented Discord negotiated FPS or latency |
| Camera regression | Existing Standard camera mode, source selection, output transform, colors, scaling and Stop remain usable; untested 4K/60 remain labeled untested |
| Own microphone / A/V | **NOT RUN until a signed, safely validated driver is available.** Then explicit phone speak/tap, visible clap and sustained synchronization/drift tests in each source mode |
| UX/accessibility | Small window, 100/125/150/200% DPI, keyboard-only use, high contrast, long/Unicode names, unavailable components and cancellation; no phone control or implicit recording |

All receiver tests are local. No live call, transmission to other people or media recording is part of acceptance without separate user action. The release report records exact reasons for any skipped physical tests. A moving meter, a camera name or a simulation cannot stand in for the selected phone's actual input.

## Planning deliverables and remaining decisions

This planning change adds this document, two generated UI reference images, the Win32 UI specification and the exact image-generation prompt set. It does not alter the running application's source, dependencies, installed camera or microphone, or start a capture session.

The selected baseline is: existing display capture, Fit by default, read-only phone access, one stable virtual camera, source changes while stopped, audio Off for the new screen profile, and compact native light styling. Seamless source swaps, interactive crop selection, new Android virtual displays, phone input control, app-audio forwarding, picture-in-picture, HDR and a GPU zero-copy redesign are deferred until the baseline is measured and working.

Before full webcam-plus-microphone claims, the existing driver signing/kernel-validation and shared A/V timing blockers must be resolved. Before smoothness or concurrent-consumer claims, the existing failures must be reproduced and corrected. These are independent of the planning images and remain visible in the implementation/test report.
