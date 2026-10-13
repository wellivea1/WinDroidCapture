# Pixel 10 Pro XL camera discovery and direct capture

The 2026-10-13 investigation used the explicitly selected authorized USB Pixel 10 Pro XL, Android 17, upstream scrcpy 5.0.1 and ADB 37.0.1. The latest official scrcpy release was rechecked and still resolved to 5.0.1. Windows remains Windows 11 Pro x64 build 26200. No shared ADB server, unrelated capture process, security setting or official scrcpy installation was changed.

## What was missing

Stock scrcpy lists public Camera2 IDs. On this Pixel those are rear logical camera 0 and front logical camera 1. Camera2 deliberately omits physical members that can only be opened through their logical parent from [getCameraIdList](https://developer.android.com/reference/android/hardware/camera2/CameraManager#getCameraIdList()). Stock scrcpy's [versioned CameraCapture implementation](https://github.com/Genymobile/scrcpy/blob/v5.0.1/server/src/main/java/com/genymobile/scrcpy/video/CameraCapture.java) rejects an explicit ID outside that public list. This was not a C parsing omission: the original enumeration genuinely contained two IDs.

The rear logical camera reports six physical members, including three crop variants. Its zoom range is approximately 0.51–30×. A logical zoom request can select a different lens, but Android controls that selection. It is distinct from binding a surface to a physical member.

## Direct output extension

The supported [OutputConfiguration.setPhysicalCameraId](https://developer.android.com/reference/android/hardware/camera2/params/OutputConfiguration#setPhysicalCameraId(java.lang.String)) API binds the scrcpy encoder surface to a selected member while opening the logical parent. We extended the pinned upstream server for this purpose; the Android camera app and scrcpy wire protocol are unchanged. The Windows controller remains C.

The separate **scrcpy-server-awm** asset implements AWM camera API 1. A managed-client identity such as **awm-physical:0:3** means logical parent 0, physical member 3. This identity is an AWM extension carried in the existing camera-id argument; it is **not a stock scrcpy camera ID or upstream CLI feature**. The normal public IDs retain their upstream behavior.

Enumeration uses Camera2 getPhysicalCameraIds/getCameraCharacteristics and the member's reported codec sizes and FPS ranges. Color members without recognized MediaCodec output metadata are omitted. The parent/member relationship is checked again at startup. Direct mode requires regular capture and fixed zoom 1. Unsupported binding/configuration fails; it never silently returns to the automatic rear camera or labels a logical zoom view as a directly selected lens. The first returned physical capture result is logged with member ID and focal length. Listed modes remain declarations until startup.

## Pixel profile and selection

The C device profile matches Pixel_10_Pro_XL / Pixel 10 Pro XL / Google Pixel 10 Pro XL model forms, not serial numbers. Other Pixel models are not guessed to have identical hardware. Only this known model automatically uses the extended discovery path. Reported IDs remain authoritative; the profile names rear focal families from characteristics (approximately 2.02 mm ultrawide, 6.9 mm wide and 17.906 mm telephoto). Smaller sensor-size members of the same focal family are labeled crop variants. The unit tests deliberately use different IDs, proving that the labels do not assume IDs 2/3/4.

On Capture, select the phone and **Phone camera**, then choose the desired entry in **Camera**. Stop before changing entries. Direct physical choices reset zoom to 1 and hide the incompatible automatic-camera zoom editor. The existing resolution, camera FPS, codec, bitrate, orientation, mirroring and output-scaling controls continue to apply. Per-device settings v6 already store the selected camera identity, so no schema change is needed.

| Current Pixel selection | Parent / member | Reported focal length | Meaning |
| --- | --- | ---: | --- |
| Rear automatic | 0 | 6.9 mm base | Normal logical camera; Android may switch lenses with zoom/conditions. |
| Rear wide | 0 / 2 | 6.9 mm | Direct main rear member. |
| Rear ultrawide | 0 / 3 | 2.02 mm | Direct ultrawide member. |
| Rear telephoto | 0 / 4 | 17.906 mm | Direct telephoto member. |
| Rear wide crop | 0 / 5 | 6.9 mm | Separately reported member with smaller effective sensor size. |
| Rear telephoto crop | 0 / 6 | 17.906 mm | Separately reported member with smaller effective sensor size. |
| Rear ultrawide crop | 0 / 9 | 2.02 mm | Separately reported member with smaller effective sensor size. |

Front public/physical members are also discovered. All six rear physical members and both front physical members started real 1920×1080 / 30-requested capture during the October 13 checks, with the selected member confirmed in Camera2 result metadata. These were short startup/delivery checks, not validation of the full mode matrix, optical quality or sustained frame cadence. Physical IDs and metadata may vary with firmware; the app re-enumerates rather than applying the table as an ID map.

## Build and ownership

scripts/build-camera-server.ps1 copies the pinned upstream source into build-native/android-server, applies checked patch anchors, generates upstream AIDL interfaces and compiles a DEX server. Original tools/scrcpy-5.0.1 and scrcpy-win64-v5.0.1 files are preserved. The build uses Microsoft OpenJDK 21.0.12.1, Android SDK platform 36 r02 and build tools 36.0.0. The Android tool archives were verified against Google's published repository SHA1 values; locally computed SHA256 values are recorded in the notices. OpenJDK was verified against Microsoft's published SHA256. Java and SDK tools are build dependencies, not end-user capture dependencies.

When starting **scrcpy-awm.exe**, process creation sets SCRCPY_SERVER_PATH to the sibling owned asset in that child only, overriding an inherited official-server path. Other scrcpy executables retain their environment. The same selected ADB remains in use. Unicode/spaced paths and isolation are tested. The package includes the DEX server, server/source hash manifest, extension source and exact build recipe. Apache-2.0 notices cover the upstream server and AOSP AIDL.

The derived managed client also gives each phone-side JAR a session-specific /data/local/tmp/awm-scrcpy-<session>.jar path. Upstream cleanup uses that process's own classpath, preserving the stock /data/local/tmp/scrcpy-server.jar and other managed sessions. This source change is isolated in scripts/prepare-native.py; the upstream checkout is unchanged.

Tests and actual capture counts are in [TEST_REPORT.md](../TEST_REPORT.md). Native camera output still uses **Android Webcam Camera (Windows Virtual Camera)** in Discord. Microphone-driver signing is separate and unchanged by this work.
