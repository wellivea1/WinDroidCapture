# Get a phone into your Windows camera list

[← README](../README.md) · [Controls](CONTROLS.md) · [Troubleshooting](TROUBLESHOOTING.md)

> [!IMPORTANT]
> This release supports local video testing. Use **Audio: Off**: the own microphone driver is unsigned and no usable phone-microphone endpoint is supplied yet.

## 1. Prepare Windows and the runtime

| Requirement | Why it is needed |
| --- | --- |
| Windows 11 x64, build 22000 or later | Media Foundation software virtual-camera API; tested desktop is build 26200 |
| Matching official scrcpy **5.0.1** distribution | ADB, runtime DLLs, ordinary capability/version probes and upstream transport |
| USB debugging authorization | Explicit access to the selected Android phone |
| An awake phone for screen capture | Authorization alone does not produce display frames |

Download the Windows development ZIP from [Releases](https://github.com/wellivea1/WinDroidCapture/releases) and extract it. Obtain the [official scrcpy 5.0.1 Windows x64 ZIP](https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1) in a separate directory. Keep its bundled notices and files together. OBS and virtual audio cables are not required.

In a source checkout, the optional downloader uses official release metadata and requires a published SHA256:

```powershell
.\scripts\setup-scrcpy.ps1
```

It refuses to replace an existing destination. A newer scrcpy release may require adapter changes; this build is pinned to the verified distribution, rather than following upstream master.

## 2. Install the Windows camera

Run from the extracted application package, or the built source checkout:

```powershell
.\scripts\install-outputs.ps1 -Camera
```

Windows requests elevation to install the camera source DLL and its own COM registration. Handle that prompt yourself. The subsequent camera registration belongs to the current user, and normal capture runs without administrator privileges. Only this project's source/registration is managed.

Alternatively, use **Settings → Output → Install camera source**, then **Register camera** in the application. The device name is **Android Webcam Camera (Windows Virtual Camera)**. Registration and a working phone stream are separate states.

## 3. Launch with the matching runtime

```powershell
.\scripts\run.ps1 -ScrcpyFolder 'C:\path\scrcpy-win64-v5.0.1'
```

This sets the dependency search path for the app and children only. In **Settings → Dependencies**, select the actual `scrcpy.exe` and `adb.exe` paths, or the extracted distribution. Confirm the displayed versions. Discovery and capture use that same selected ADB.

No capture starts on launch. The installed camera can remain enumerable while its producer is stopped.

## 4. Authorize and choose the source

### USB

Enable Android **Developer options → USB debugging**, connect the phone, unlock it and accept its RSA authorization prompt. Refresh the device list if necessary. Select the authorized transport explicitly; multiple rows with the same model name are not merged.

### Wireless debugging

On modern Android, open **Wireless debugging → Pair device with pairing code**. In the app's **Devices** page, enter that pairing address/port and ephemeral code. Then connect using the phone's separate **connection** address/port. The two ports can differ; port 5555 is not assumed. Use the available mDNS discovery or explicit manual entry. Pairing codes are neither logged nor saved.

### Camera or screen

On **Capture**, choose **Phone camera** or **Phone screen**, then the enumerated camera/display. Keep the phone awake and unlocked for the initial screen test. Screen capture includes the selected phone display and any notifications on it; use a neutral Settings page while testing.

Choose **Standard** for a first camera test, or **Balanced** for a first screen test. These request modes and are validated at startup. Pixel 10 Pro XL owners can select a direct rear lens; [read the firmware-dependent binding notes](PIXEL_CAMERAS.md).

Select **Audio: Off**, then **Start capture**. Check ADB connection, live source status and Windows camera availability separately. Use **Stop → select → Start** to change device or source.

## 5. Select the input in Discord

Open **Discord → Settings → Voice & Video**:

| Setting | Select |
| --- | --- |
| Camera | **Android Webcam Camera (Windows Virtual Camera)** |
| Input Device | **Android Webcam Microphone**, only after a trusted driver exists and its exact name is enumerated |

The microphone entry is currently absent. Do not treat an existing PC microphone or a cable input as a phone-audio pass. Discord's self-view may mirror independently of the app's output transform.

Use Discord's **local Test Video**. No live call is needed. Move a lit object in front of the camera or scroll a neutral phone screen, then check that the selected content appears. Close Test Video before using the separate receiver: simultaneous consumers previously failed on the tested configuration.

## 6. Check output ownership and Stop

- Press **Test input** with Audio Off for an explicit video-only local receiver. It runs for 90 seconds and selects only our named camera; it does not transmit or record media.
- **Hide preview** or minimize the manager. The Windows camera continues independently.
- Press **Stop**. Both owned capture processes stop, pending images are discarded, and the camera emits a stopped image.
- Reopen the manager. Preferences remain, but capture stays stopped.

> [!NOTE]
> A preview, an enumerated camera and a receiving application displaying video are three different validation states. Resolution/FPS negotiation, sustained smoothness and audio/video synchronization require their own checks. See the [test report](../TEST_REPORT.md).
