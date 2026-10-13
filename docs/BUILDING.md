# Build WinDroid Capture on Windows x64

[← README](../README.md) · [Third-party versions](../THIRD_PARTY_NOTICES.md) · [Architecture](../ARCHITECTURE.md)

## Toolchain and inputs

The verified development environment uses Windows 11 build 26200, Visual Studio Community 2026, MSVC 14.51.36231 libraries, LLVM 22.1.8 and Windows SDK/WDK 10.0.28000.0. The controller/output components are C17; a small extension to scrcpy's upstream Java server provides Camera2 physical-member support. Java tools are needed at build time only.

| Install | Official source / purpose |
| --- | --- |
| PowerShell 7 | [Microsoft PowerShell](https://github.com/PowerShell/PowerShell/releases) for the scripts |
| VS C++ desktop tools, SDK, CMake/Ninja | [Visual Studio](https://visualstudio.microsoft.com/downloads/); C libraries and Windows tools |
| LLVM with clang-cl | [LLVM releases](https://github.com/llvm/llvm-project/releases); upstream C extensions |
| Python 3 | [Python](https://www.python.org/downloads/windows/); dependency preparation and timing fixtures |
| Microsoft OpenJDK 21 | [OpenJDK](https://learn.microsoft.com/en-us/java/openjdk/download); pinned Android server compilation |
| Android platform 36 + build tools 36.0.0 | [Android SDK](https://developer.android.com/studio); android.jar, AIDL and D8 |
| WDK 10.0.28000.0, optional | [Windows driver kit](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk); kernel build/static package checks |

Clone the project:

```powershell
git clone https://github.com/wellivea1/WinDroidCapture.git
Set-Location WinDroidCapture
```

The repository intentionally excludes downloaded third-party trees and build products. Extract these official source/development packages into the named local directories:

```text
tools/
├─ scrcpy-5.0.1/                 # tagged upstream source
├─ scrcpy-win64-v5.0.1/          # official runtime distribution
├─ ffmpeg-9.0.2/                # headers matching runtime ABI
├─ SDL3-3.4.18/                 # VC development package
├─ openjdk21/<jdk-directory>/
└─ android-sdk-parts/
   ├─ platforms-android-36/android-36/
   └─ build-tools-36.0.0/android-16/
```

- [scrcpy tagged source](https://github.com/Genymobile/scrcpy/archive/refs/tags/v5.0.1.zip) and [runtime](https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1).
- [FFmpeg 9.0.2 source](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz).
- [SDL 3.4.18 development packages](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18).

Runtime downloads through `scripts/setup-scrcpy.ps1` require the publisher's SHA256 and refuse an existing target. Exact archives, hashes and component terms are in the [notices](../THIRD_PARTY_NOTICES.md). A newer upstream binary is not assumed compatible with this pinned adapter.

## Application build

```powershell
.\scripts\build.ps1
```

The script initializes the VS x64 tool environment, derives the owned Android server, prepares upstream headers/import libraries, then configures a Release CMake/Ninja build. It expects clang-cl in `C:\Program Files\LLVM\bin`. Outputs are in `build-native/`; launch `WinDroidCapture.exe` there, keeping its matching runtime DLLs/server siblings.

For a non-default Java/SDK layout, first build the server with explicit paths:

```powershell
.\scripts\build-camera-server.ps1 `
    -Jdk 'C:\path\jdk-21' `
    -Platform 'C:\path\android-36' `
    -BuildTools 'C:\path\build-tools\36.0.0'
```

The current top-level build script calls the server script with its default layout; supply that layout for the normal build, or adapt those defaults locally. The derived server is `scrcpy-server-awm`; `awm-camera-server.json` records source/asset hashes. Keep both beside `scrcpy-awm.exe`. Original upstream trees and user-managed runtime installations are preserved.

HLSL compiles during the build and is embedded. Runtime shader compilation and Node.js are not required. Normal builds consume the committed multi-size ICO; regenerating artwork is an optional Node/Sharp developer task:

```powershell
node .\scripts\build-branding.mjs
# Or: node .\scripts\build-branding.mjs --sharp 'C:\path\node_modules\sharp'
```

## Test the appropriate layer

```powershell
.\scripts\test.ps1 -SkipBuild
```

Stop any active phone session before shared-transport tests. The script runs core, screen/profile/persistence, known-device profiles, simulated lifecycle, GPU, preview pacing, shared publication queue, COM camera source and Python timing-attribution fixtures. A failed component stops the script. Actual GPU/COM execution still does not establish physical phone/Discord delivery.

The retained October 13 run has a shared-video-queue timing failure; don't present the historical source snapshot as a completely passing suite. [Results and exact scopes →](../TEST_REPORT.md)

## Driver development is a separate release gate

```powershell
.\scripts\build-driver.ps1
```

This builds the C PortCls/WaveRT source and runs Universal API, INF and catalog checks. It produces **unsigned development material**, not an installed microphone. The trusted installer refuses an untrusted catalog. Kernel audit, isolated Verifier/PnP/power/HVCI/malformed-IOCTL testing, applicable HLK work and Microsoft signing remain.

## Package a development snapshot

```powershell
.\scripts\publish.ps1 -SkipBuild -SkipTests
```

These flags explicitly reuse existing outputs/results; they do not assert tests just ran. Publishing requires Git and a reviewed tracked source tree. Each package has a new timestamp, source, documentation, notices, dependency/source hashes and SHA256SUMS. Untracked/local material and unsigned kernel binaries are omitted; driver source remains included. Runtime DLLs and the stock ADB/server distribution remain separate. Camera installation still requires elevation, and microphone release validation is still required: this is not a fully portable production release.

## Archived bridge

The original OBS controller is excluded by default. Its explicit CMake options are `AWM_NATIVE=OFF`, `AWM_LEGACY=ON`; never enable both controllers in one build. Its history does not validate the current native microphone or camera output.
