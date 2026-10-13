# Set up WinDroid Capture

[← README](../README.md) · [Camera settings](CONTROLS.md) · [Get help](TROUBLESHOOTING.md)

You need **Windows 11 (64-bit)**, an Android phone and a USB cable for the first setup. Keep your phone unlocked while testing.

> [!IMPORTANT]
> Choose **Audio: Off**. This early release supports video; phone microphone support is not ready yet.

## 1. Download the app and connection tools

1. Open the [Windows download page](https://github.com/wellivea1/WinDroidCapture/releases/tag/v0.3.0-dev.1). Download the **WinDroidCapture…zip** file and extract it to a folder.
2. Open the [official scrcpy 5.0.1 download page](https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1). Download **scrcpy-win64-v5.0.1.zip** and extract it to a separate folder.
3. Copy the **`.dll` files** from the scrcpy folder into the folder containing **WinDroidCapture.exe**. These files let the app process video. Keep the original scrcpy folder for the next step.

scrcpy supplies the phone-connection tools that the app needs. Keep its files together. You do not need OBS or a virtual audio cable.

## 2. Open the app

Open **WinDroidCapture.exe** in the app folder. It opens the interface without starting your phone's camera or screen capture.

In **Settings → Dependencies**, use **Browse…** to choose these files from the extracted scrcpy folder:

| App field | File to choose |
| --- | --- |
| Selected ADB executable | **adb.exe** |
| Stock scrcpy executable | **scrcpy.exe** |

Press **Validate and Apply**. If video startup reports missing DLLs, check the copied files from step 1, or use the alternative launcher below.

<details>
<summary><strong>Alternative: launch without copying the video files</strong></summary>

Open PowerShell in the extracted app folder. Run the following, replacing the example path with your extracted scrcpy folder:

```powershell
.\scripts\run.ps1 -ScrcpyFolder 'C:\path\scrcpy-win64-v5.0.1'
```

The launcher makes the matching runtime files available to the app and its child processes. It leaves the system-wide PATH unchanged. End users need neither Java nor an Android SDK.

For a source checkout, the optional `.\scripts\setup-scrcpy.ps1` downloader verifies the publisher's SHA256 and refuses to replace an existing target. [Build instructions](BUILDING.md).

</details>

## 3. Add the Windows camera

In **Settings → Output**:

1. Press **Install camera source**. Windows asks for administrator approval to install the camera component; handle that prompt yourself.
2. Press **Register camera**.

The camera appears as **Android Webcam Camera (Windows Virtual Camera)**. Later, normal capture does not need administrator privileges. Installing it once does not start phone capture.

<details>
<summary>Alternative: install from PowerShell</summary>

Run from the extracted app folder, or a built source checkout:

```powershell
.\scripts\install-outputs.ps1 -Camera
```

This installs only the project's camera source and registers the current user's virtual camera. The software-camera API requires Windows build 22000 or later; the tested desktop is Windows 11 build 26200.

</details>

## 4. Connect your phone

On Android, turn on **Developer options → USB debugging**. Connect the USB cable, unlock the phone and accept the phone's **Allow USB debugging** prompt. Return to WinDroid Capture and choose your phone. Use **Refresh** if it has not appeared.

If the phone says **Unauthorized**, check for the authorization prompt on the phone. If it says **Offline**, reconnect and refresh. [Connection help](TROUBLESHOOTING.md).

### Wireless connection (optional)

<details>
<summary>Pair and connect without USB</summary>

On a phone with modern Android wireless debugging, open **Wireless debugging → Pair device with pairing code**. In the app's **Devices** page, enter that pairing address/port and code. Then connect using the phone's separate connection address/port.

The two ports can differ; port 5555 is not assumed. Use the offered discovery list or manual entry. Pairing codes are not saved or logged. Legacy USB-to-TCP/IP mode is not enabled automatically.

</details>

## 5. Start video

On **Capture**:

1. Choose **Phone camera** or **Phone screen**, then the camera/display you want.
2. Start with **Standard** for the camera, or **Balanced** for the screen.
3. Set **Audio: Off** and press **Start capture**.

Keep the phone awake for screen capture. It shows the selected phone display, including any notifications, so use a neutral page when testing. To change device or source, press **Stop** first.

Pixel 10 Pro XL owners can choose direct rear wide, ultrawide or telephoto cameras. [Camera choices](PIXEL_CAMERAS.md) · [All controls](CONTROLS.md).

## 6. Choose it in Discord

Go to **Discord → Settings → Voice & Video → Camera** and choose:

**Android Webcam Camera (Windows Virtual Camera)**

Use **Test Video** to check the picture. No live call is needed. Move an object in front of the phone camera, or scroll a neutral phone screen, and confirm the picture follows it.

Phone microphone support is unavailable in this release. A different microphone does not establish phone-audio delivery. Discord may mirror its own preview independently of the app's mirroring control.

## Useful everyday controls

- **Hide preview** saves screen space while Windows camera output continues.
- **Stop** ends capture and replaces the outgoing picture with a stopped image.
- **Test input** opens a separate local video receiver for 90 seconds. Close Discord's Test Video first; simultaneous camera use has failed on the tested desktop.
- Closing and reopening the app keeps your preferences, but never starts capture automatically.

<details>
<summary>What has been tested, and what still needs work?</summary>

Video has been observed in Discord's local camera test and in the independent receiver, with separate source/mode checks. An app preview, an installed camera and a receiving app showing video are different states. Final smoothness, wider resolution/frame-rate support and full phone microphone/synchronization checks remain incomplete. [Detailed test report](../TEST_REPORT.md).

</details>
