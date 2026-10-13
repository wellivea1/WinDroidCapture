# Android Webcam Manager — native output development build

A C Windows x64 controller for an explicitly selected Android phone. The default application uses its own Windows software camera, an integrated Direct3D 11 preview, and an own PortCls/WaveRT microphone driver implementation. OBS and VB-CABLE are not used or detected by the normal application.

**Completion state:** camera registration and a real Pixel feed have worked in the independent Windows receiver and Discord's local Test Video. Discord smoothness remains below the user's expectation. The microphone driver builds and validates, but is unsigned and not installed. This build cannot yet provide webcam **and** phone microphone to Discord. Treat it as a development camera build, not a production driver release.

## Run in this workspace

Open build-native/AndroidWebcamManager.exe. Its matching runtime DLLs and Android server are beside it. It does not capture on launch.

1. Choose the authorized phone. The app probes that exact transport and lists its cameras.
2. Select an enumerated camera. Saved profiles are migrated; matching model names are never merged.
3. Start with Standard: requested 1920×1080, camera FPS 30, H.264, 16 Mbps. Detail requests 3840×2160/30, H.265, 40 Mbps. Motion requests 1920×1080/60, H.264, 24 Mbps. These are starting profiles, not guarantees.
4. For the current unsigned-driver build, explicitly turn **Phone microphone off** to start camera only. Full microphone mode refuses to start if our input is unavailable.
5. Press Start. The preview reads full decoded frames; hiding it does not stop Windows output.
6. In Discord, open Settings → Voice & Video and choose **Android Webcam Camera (Windows Virtual Camera)** as Camera. The intended Input Device is **Android Webcam Microphone**, using its actual enumerated friendly name. That microphone is currently unavailable. There is no PC microphone, cable or speaker fallback.
7. **Test inputs** opens the separate local receiver. Close Discord's Test Video or other camera consumers first: concurrent receiving failed on this configuration. The receiver selects only our named inputs, with no network transmission or recording. A missing input stays visibly unavailable. Its display also uses D3D11/EWA, and a missing microphone makes the overall test incomplete even if video succeeds.

Setup / Advanced contains executable selectors and versions, camera-source installation/registration, modern wireless pairing, editable dimensions/FPS/bitrate/codec/encoder, bounded opt-in reconnection, and two scaling lists. **Preview scaling** applies immediately. **Camera output scaling** applies on the next Start. Choices: EWA Lanczos sharp (default), separable Lanczos 3, and bilinear (explicit fast choice).

Rotation is clockwise, followed by optional horizontal mirroring of the rotated image. Both affect the Windows image and preview. Discord may mirror its own self-preview separately. Scaling cannot recreate missing detail or make a lower-resolution feed genuine 4K.

## Dependencies

Verified desktop: Windows 11 Pro x64, build 26200. The software-camera API requires Windows build 22000 or later. COM-source installation requires elevation; current-user camera registration and normal operation do not.

The local export client is based on **upstream scrcpy 5.0.1**, with a small recorder-sink adapter. The selected upstream executable supplies actual version/help/camera/encoder probes. Adapter, runtime DLLs and Android server must match. Future scrcpy upgrades require updating the adapter and pinned build inputs; a mismatched version is rejected.

Official sources:

- [scrcpy and bundled ADB](https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1)
- [ADB documentation](https://developer.android.com/tools/adb)
- [Visual Studio C desktop tools](https://visualstudio.microsoft.com/downloads/)
- [LLVM clang-cl](https://github.com/llvm/llvm-project/releases)
- [Windows SDK / WDK](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
- [FFmpeg 9.0.2](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz)
- [SDL development package](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18)

scripts/setup-scrcpy.ps1 defaults to the matching 5.0.1 official distribution and verifies its published SHA256, preserving existing folders and user-managed paths. An explicit -Version latest selects a different upstream release, which may require updating the adapter before capture. The same selected ADB path is supplied to the capture subprocess. Shared ADB protocol conflicts are explained before discovery, with no automatic server restart.

USB: enable Developer options and USB debugging, unlock the phone and accept authorization. Camera mode requires an authorized physical Android 12+ device. Emulators, unauthorized, offline, disconnected, unsupported and unknown states are distinct.

Wireless: use the pairing address/port and six-digit code from Wireless debugging → Pair device. Then connect using its separate connection address/port. Codes go only to ADB stdin, are erased after use, and are not saved or logged. mDNS discovery does not scan arbitrary networks. Legacy TCP/IP mode is not enabled automatically.

## Build, test, publish

PowerShell 7:

~~~powershell
.\scripts\build.ps1
.\scripts\test.ps1 -SkipBuild
.\scripts\build-driver.ps1
.\scripts\publish.ps1 -SkipBuild -SkipTests
~~~

Build inputs: VS/MSVC libraries, clang-cl for upstream GNU C extensions, CMake/Ninja, Python, and matching scrcpy/SDL/FFmpeg sources under tools. Extract official scrcpy v5.0.1 source into tools/scrcpy-5.0.1, FFmpeg source into tools/ffmpeg-9.0.2, SDL's VC development archive into tools/SDL3-3.4.18, and the matching runtime into tools/scrcpy-win64-v5.0.1. The links above identify the publishers; scrcpy's [tagged source archive](https://github.com/Genymobile/scrcpy/archive/refs/tags/v5.0.1.zip) supplies the source tree. scripts/prepare-native.py generates import libraries for the official runtime DLLs. HLSL shaders compile during the build and are embedded in our programs; no runtime shader compilation is needed. The driver uses WDK 10.0.28000.0. Exact runtime hashes are in build-native/deps/native-dependencies.json.

Published development packages include our executables/source and unsigned driver material. Third-party runtime DLLs remain in the separately installed official distribution. Launch an extracted package with:

~~~powershell
.\scripts\run.ps1 -ScrcpyFolder 'C:\path\scrcpy-win64-v5.0.1'
~~~

Only the launcher's child search path changes. There is no system default audio or persistent PATH change. This is not fully portable: Windows output registration and a signed kernel driver require installation.

Install the camera from Setup or:

~~~powershell
.\scripts\install-outputs.ps1 -Camera
~~~

The installer stores versioned source DLLs in Program Files and registers only our CLSID/current-user camera. Old files are retained while Windows might have them loaded.

## Microphone release gate

drivers/microphone contains actual C WDM PortCls/WaveRT source, INF and IOCTL contract. build-driver contains the .sys, INF and generated catalog. They are **unsigned development artifacts**. The installer refuses an untrusted catalog before creating a device.

Required before production: kernel audit; isolated driver-development testing with Driver Verifier; PnP/power, multiple-client, malformed-IOCTL, timing and HVCI checks; applicable HLK tests; Microsoft signing through the Windows Hardware Developer program. A signing account/certificate/submission cannot be manufactured by this local build. No Secure Boot, HVCI or signature-enforcement settings were changed.

When a signed package exists:

~~~powershell
.\scripts\install-outputs.ps1 -SignedMicrophonePackage 'C:\path\signed-package'
~~~

Then enable Phone microphone and test our Windows endpoint. Verify a phone-specific speak/tap response and visible clap before claiming delivery or usable synchronization. A moving meter alone is insufficient.

## Validation and limits

Read TEST_REPORT.md for automated, simulated, GPU and physical results. Earlier OBS/VB-CABLE results remain historical evidence; they do not validate the new outputs.

Current decoding/YUV conversion runs on the CPU, with D3D11 rendering/resampling and a GPU readback for scaled camera output. This is not fully GPU-resident decoding. HDR/10-bit tone mapping, ICC calibration, native 4K/60 negotiation, signed-driver delivery and sustained A/V clock validation remain release work. A higher nominal profile does not itself promise better image quality.

Capture remains local. Stop/loss closes owned processes, flushes the microphone producer and replaces camera output with a stopped image. No recording, call, cloud account or telemetry is used. DPAPI settings/per-device profiles live under LocalAppData; earlier settings are preserved. The original project is saved in evidence/baseline-v0.1-before-native-refactor.zip. Archived OBS code/tests are excluded by default.
