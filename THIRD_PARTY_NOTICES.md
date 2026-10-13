# Dependencies, versions and notices

[← README](README.md) · [Build inputs](docs/BUILDING.md) · [Public source boundary](docs/PUBLISHING.md)

Verified during the 2026-10-12–13 development runs. A Windows development package includes our C programs, a modified upstream scrcpy client and the separate owned **scrcpy-server-awm** Android server extension. It does not bundle third-party runtime DLLs, ADB or the stock upstream Android server. Obtain the matching official scrcpy distribution separately and retain its notices. OBS and VB-CABLE are not normal runtime dependencies.

> [!NOTE]
> Version and hash records describe the tested inputs, rather than a promise of compatibility with future upstream releases. The own driver and application artifacts are unsigned development builds. No project-wide license has been selected for the remaining original project material; listed components retain their own terms.

| Component | Version / role | Official source and terms |
| --- | --- | --- |
| scrcpy | 5.0.1; upstream Android camera/display/microphone transport, modified recorder sink and Windows compatibility adapter | [Project / release](https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1), [Apache-2.0 license](https://github.com/Genymobile/scrcpy/blob/v5.0.1/LICENSE). Apache license included in licenses/scrcpy-Apache-2.0.txt. Our modifications and checked build patches are in native/scrcpy and scripts. The upstream wire protocol is preserved; the original distribution is unchanged. The separate Android server extension is listed below. |
| ADB | 37.0.1-15733141, protocol 1.0.41; selected executable used for discovery and capture | [Android platform tools](https://developer.android.com/tools/releases/platform-tools), [AOSP source](https://android.googlesource.com/platform/packages/modules/adb/). Distributed separately with the official scrcpy runtime; retain all included component notices. |
| FFmpeg | 9.0.2 headers; avcodec 63.1.102, avformat 63.1.102, avutil 61.1.102, swresample 7 | [Source](https://ffmpeg.org/), [licensing](https://ffmpeg.org/legal.html). Dynamically linked through the matching separately installed DLLs. The actual avcodec DLL reports “LGPL version 2.1 or later”; LGPL text included. No runtime DLLs are redistributed in this package. |
| SDL | 3.4.18; upstream client runtime | [Release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18), zlib license included in licenses/SDL-zlib.txt. Runtime DLL remains external. |
| libusb | 1.0.30 in the official scrcpy distribution | [Project](https://github.com/libusb/libusb), LGPL-2.1. Not linked by our manager or included in the package. Retain publisher notices in the separate distribution. |
| mpv reference | Commit 88eb9f2e73945d8bc9c133a2f67babaa9af34680, inspected only | [Repository](https://github.com/mpv-player/mpv/tree/88eb9f2e73945d8bc9c133a2f67babaa9af34680). Mathematical Jinc parameters, event/queue behavior and D3D11 upload/presentation informed independently written C/HLSL. No mpv/libmpv/libplacebo renderer code or binaries are included or linked. See docs/RENDERING.md for exact references and licensing bounds. |
| Optional graph developer tools | matplotlib 3.11.2, NumPy 2.3.5 in the measured run | [Matplotlib](https://matplotlib.org/stable/project/license.html), [NumPy](https://numpy.org/doc/stable/license.html). Required only to run scripts/plot-timing.py; no libraries or binaries are redistributed in the Windows package or required for capture. |
| Microsoft reference samples | Windows-Camera and Windows-driver-samples, consulted for API/ABI/INF structure | [Windows camera samples](https://github.com/microsoft/Windows-Camera), [driver samples](https://github.com/microsoft/Windows-driver-samples). MIT-licensed checkouts retain original notices locally; their implementations are not bundled in this package. |
| Windows development tools | VS Community 2026, MSVC 14.51.36231, LLVM 22.1.8, SDK/WDK 10.0.28000.0 | Installed development-tool licenses apply. Our release programs link the C runtime statically and use Windows system libraries. Driver and application executables are unsigned development artifacts. |
| Owned Android server extension | scrcpy 5.0.1 / AWM camera API 1 | Apache-2.0 upstream server with physical Camera2 output/enumeration changes in CameraCapture.java and LogUtils.java plus AwmPhysicalCameras.java. The separate scrcpy-server-awm DEX asset is included; awm-camera-server.json identifies original source hashes and built asset SHA256. Exact extension and build recipe are under native/scrcpy/server and scripts. The upstream source remains available from the tagged official source link above. No replacement camera app or wire protocol is supplied. AOSP AIDL interfaces from the upstream source retain their Apache-2.0 notices. |
| Android server build tools | Microsoft OpenJDK 21.0.12.1; Google Android platform 36 r02 / build tools 36.0.0 | [Microsoft OpenJDK](https://learn.microsoft.com/en-us/java/openjdk/download), [Android SDK](https://developer.android.com/studio). Developer tools only, not redistributed and not needed by end users. Publisher checksums verified as listed below. |

The actual FFmpeg configuration was inspected through avcodec_configuration(). It enables shared libraries, scrcpy's limited decoder/muxer set and D3D11VA capabilities, disables swscale/avfilter/network, and does not enable GPL/nonfree options. This does not make our media host hardware-decoded: its current decode path is software. Preserve corresponding source and build configuration if you later redistribute those DLLs.

Verified official runtime archive SHA256:

~~~text
scrcpy-win64-v5.0.1.zip
b12a2c4ee8be317422451fc7dcf8ee20a71b5ea7ef9ad73ddd825a25316227a5
~~~

Verified FFmpeg source archive SHA256:

~~~text
ffmpeg-9.0.2.tar.xz
8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
~~~

Runtime file hashes are recorded in native-dependencies.json. Archive verification is against the publisher's hash, not a claim that each executable is Authenticode-signed.

Android build archive verification (2026-10-13):

| Official archive | Publisher checksum verified | Locally computed SHA256 |
| --- | --- | --- |
| microsoft-jdk-21.0.12.1-windows-x64.zip | Microsoft SHA256: 192441a9d27da813bada974bb88b4cf64d37a9589ed37f204374d411ca5ce07f | Same value. |
| platform-36_r02.zip | Google repository SHA1: 2c1a80dd4d9f7d0e6dd336ec603d9b5c55a6f576 | 37607369a28c5b640b3a7998868d45898ebcb777565a0e85f9acf36f29631d2e |
| build-tools_r36_windows.zip | Google repository SHA1: f16ccffd34de8790dede813a6c7d8e2c11a27b50 | aa1095cb14d83e483818a748a2c06faaeb8e601561b06a356a119a1b2ca280d3 |

Google package checksums came from its official https://dl.google.com/android/repository/repository2-3.xml metadata. The computed SHA256 values are local integrity records, not a claim that Google published SHA256 for those archives. The normal runtime launcher uses the selected official ADB and matching runtime DLLs, but the managed client's own server path is selected explicitly in its child environment.

The original OBS/VB-CABLE bridge source is preserved for history behind an explicit legacy build option. Those components are not normal runtime dependencies, are not linked or detected by the native manager, and are not included in the new package. Previously installed copies have not been uninstalled or modified.

Behavior references: installed scrcpy --help and versioned camera/audio/video/connection documentation; [MFCreateVirtualCamera](https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera); [kernel signing policy](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-); [image stride](https://learn.microsoft.com/en-us/windows/win32/medfound/image-stride). The camera requires Windows build 22000 or later.
