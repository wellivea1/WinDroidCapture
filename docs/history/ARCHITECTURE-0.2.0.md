# Native output architecture

## Video path

Selected ADB transport → upstream scrcpy Android camera server → managed private upstream-based C client → bounded local packet export → user-mode FFmpeg decoder → full-size BGRA frame hub.

Two independent consumers:

- The C manager uploads to its integrated Direct3D 11 preview.
- A Media Foundation source DLL pulls full frames through a secured local pipe and supplies NV12/RGB32 as Android Webcam Camera (Windows Virtual Camera).

MFCreateVirtualCamera uses a stable source CLSID, CurrentUser access and System lifetime. The camera remains enumerable between manager sessions; stopped receivers get an explicit stopped image. COM registration is machine-wide so Frame Server can load the DLL; camera registration belongs to the current user.

The UI is never captured. Covering/minimizing/hiding its preview does not alter output. There is no visible scrcpy window, OBS scene or desktop-capture fallback.

## Audio path

Phone microphone → scrcpy explicit required mic source/raw PCM → user-mode host downmix to 48 kHz mono PCM16 → own IOCTL producer → own WaveRT capture buffer → Windows audio engine → receiving application.

No playback endpoint, speakers, cable, default-device change or desktop-audio capture is involved.

The PortCls/WaveRT C driver validates begin/write/reset/query sizes and formats and uses a per-file-object/generation producer lease. Producer close, timeout and underrun emit silence. Receiving phone PCM does not prove a Windows microphone.

**Driver not installed:** the unsigned binary/catalog cannot establish the endpoint. Installation, endpoint presence, PCM arrival, receiver samples, phone-specific sound and synchronization are separate gates.

## Ownership

A worker owns discovery/capability subprocesses, session transactions and bounded polling. Windows argument arrays/quoting, selected ADB environment and owned jobs are used. UI snapshots have a short lock. Stop cancels operations and invalidates earlier queued Starts.

Only one producer is allowed. Frame readers use double buffers and sequence validation, retaining a previous valid frame through brief contention but never beyond a stale deadline or explicit Stop. Camera pipe ACLs allow owning user, SYSTEM and LocalService; remote clients are rejected. Consumers have bounded I/O and separate scaling resources.

Opt-in reconnect targets the same serial/transport with at most three retries and stops on Stop. Shared ADB is never automatically killed/restarted. Android authorization cannot be bypassed.

## Image and clock semantics

Requested/declarative modes, observed captured dimensions, software-camera dimensions/frame rate and receiver negotiation are distinct. Output resizing operates on full decoded frames through the selected D3D11 resampler; exact-size output copies pixels. Preview resizing is separate.

8-bit SDR formats have explicit RGB/full versus NV12/limited range. Unsupported decoded formats fail. No HDR guarantee is made.

Phone timestamps are transported, but kernel capture clock and camera pacing need sustained physical synchronization testing; there is no measured lip-sync guarantee.

## Source map

- src/native/manager.c, command.c, settings.c: device/session/configuration.
- native/scrcpy: upstream sink adapter and Windows C compatibility.
- src/native/media_host.c, frame_hub.c: decoding, transports, transforms, health.
- src/native/preview.c, scale_shader.h: D3D11 preview/output scaling.
- camera/source.c, control.c: C COM source and scoped installer.
- drivers/microphone: C kernel driver/package/installer.
- tests/native_receiver.c: independent explicit local receiver.

OBS/VB-CABLE code is archived and excluded from the normal build.
