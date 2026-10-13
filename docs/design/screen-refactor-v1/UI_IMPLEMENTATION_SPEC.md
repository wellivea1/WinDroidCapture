# Compact Win32 UI implementation specification

Date: 12 October 2026. Status: implemented as the Win32/C development UI in build 0.3.0, with user-requested appearance, layout and rendering corrections. These raster references guide the implementation; their healthy-state illustrations are not hardware evidence.

## References and selected direction

- [Four-page dark concept](compact-dark-workspace-v1.png): Capture with phone screen, Devices, Settings, Diagnostics.
- [Light Capture alternative](compact-light-camera-v1.png): the same shell with camera-specific controls.
- [Exact image-generation prompts](imagegen-prompts.json): built-in image_gen, ui-mockup use case, complete prompts and correction notes.
- [Backend and implementation plan](../../SCREEN_CAPTURE_UI_PLAN.md).

The implementation uses the four-page information architecture and one shell/control implementation for both source modes. Settings offers System, Light and Dark appearance. Source Settings is boxed, with an aligned fixed Advanced header and independently scrolling content. The settings box uses the full right column, including the area beside Windows inputs. Hide preview sits in the preview header. Status and operation messages occupy the top right beside the four navigation tabs; the bottom band contains the capture action. Preview/output explanations are kept in documentation and Diagnostics instead of occupying capture space. Neither pictured preview nor any healthy state is actual capture evidence.

## Visual rules and dimensions

Use native Windows caption/system buttons, standard common controls, Segoe UI at approximately 10 pt, section labels at 10–11 pt semibold, and a monospaced log font. Body text and control labels remain ordinary size. Keep client surfaces flat, with straight one-DIP dividers, square or native small-radius controls, and visible dropdown arrows. No pill switches, rounded cards, large empty onboarding surfaces, gradients, blur or custom titlebar.

Initial light tokens, overridden by system high-contrast colors:

| Token | Initial value / role |
| --- | --- |
| Window background | #F3F3F3 |
| Form/control surface | #FFFFFF |
| Text | #202020 |
| Secondary text | #555555 |
| Dividers | #C4C4C4 |
| Action/selection | System accent, restrained blue fallback #0067C0 |
| Unavailable/warning | #8A5700 plus descriptive text |
| Error | #B42318 plus descriptive text |
| Preview background | #000000 |

Stopped/Off are neutral, not red. Native hover, pressed, disabled and focus states take precedence over image styling. Use documented common-control painting/custom-draw mechanisms and simple GDI fills; no undocumented dark-mode hooks are required. Microsoft documents the [native common-control surface](https://learn.microsoft.com/en-us/windows/win32/controls/common-control-reference).

All dimensions below are starting DIP layout constraints, not literal raster coordinates copied from the generated images:

| Element | Layout target |
| --- | --- |
| Client area | 1040 × 700; resizable, minimum 960 × 640 |
| Outer padding / major gaps | 12 / 12 |
| Form control height / row gap | 26–28 / 6–8 |
| Top navigation | 30–32 high |
| Device and source rows | Two rows, approximately 64–70 total |
| Property pane | 280–300 wide; vertically scrollable |
| Preview pane | Remaining width; preserve output aspect within available rectangle |
| Output/receiver area | Approximately 88–108 high beneath the left preview, copyable names |
| Top right status area | Two compact lines beside navigation, ellipsis for long messages; full diagnostic log |
| Bottom action band | Approximately 42 high; capture action width 130 |

Compute rectangles in a small C layout function, scaled using the current window DPI. Use WM_SIZE and WM_DPICHANGED, the suggested DPI rectangle and fonts recreated for the current DPI. Preserve the bottom action; scroll the property/settings pane rather than clipping controls. Shrink the preview allocation at small heights without stretching its content. Long names use read-only edits or wrapping, not truncated values with no way to copy them.

## Native control map

| Feature | Win32 implementation |
| --- | --- |
| Main pages | WC_TABCONTROL plus persistent child page HWNDs |
| Labels/status text | STATIC; status-bar/common-control or simple GDI divider strip |
| Source/device/profile/display/camera choices | COMBOBOX with CBS_DROPDOWNLIST, WS_TABSTOP and stable item identity data |
| Start, Cancel, Stop, Copy, Refresh | BUTTON with standard focus/keyboard behavior |
| Mirror/reconnect | BUTTON with BS_AUTOCHECKBOX |
| Paths, addresses, output names | Unicode EDIT, read-only where appropriate; browse/validate buttons beside paths |
| Pairing code | EDIT with ES_PASSWORD; ephemeral value, cleared after use and never logged |
| Device and stage lists | WC_LISTVIEW report mode, full-row selection, column headers, explicit state text |
| Advanced source fields | Native controls within an expanded scrollable property child |
| Integrated output preview | Owned child HWND with independent D3D11 renderer worker |
| Diagnostics | Bounded read-only multiline EDIT/RichEdit plus copy/export actions |

Keep controls accessible through native names, normal focus rectangles and sensible Tab order. Custom painting must not erase keyboard focus, selection arrows or unavailable explanations. High contrast uses system colors. Device rows retain serial/transport identity internally even when the friendly names match.

## Shared shell and navigation

Four top tabs: **Capture, Devices, Settings, Diagnostics**. Capture is the initial page. Selecting a tab never starts/stops media, changes the source or invokes installation. Preserve live capture while browsing setup/diagnostics.

The bottom action remains at the same place on every page. Labels are Start capture, Cancel start, Stop capture, Cancel reconnect and Stopping… according to the explicit session phase. Never disable the only cancellation path during Starting. Pressing Close starts bounded asynchronous teardown while the UI stays responsive.

The top right presents concise **ADB**, **Capture**, **Camera**, **Mic** state text and operation messages. Each state has a small colored dot and matching value: green for connected/live/available, blue for checking/starting/stopping, amber for waiting/retrying/unknown states, red for failed/offline/unavailable, gray for unselected/stopped/off. Labels and complete accessible text convey meaning independently of color. Live capture requires source-frame health; Mic Forwarding requires actual phone PCM delivery to the own driver. Camera Available is enumeration, not receiver validation. Detailed stages live in Diagnostics. The Capture page has a separate receiver verification line. Both tab rows use a complete outer left edge; main tabs meet the top bar's shared bottom rule.

The source scrollbar's focus indication stays inside its thumb. Keep the original gray checker blink, alternating with blue while hovered, at the Windows blink cadence. Do not draw a dotted rectangle around the whole scrollbar.

## Capture page

Top rows:

1. Device dropdown, compact transport/ADB state and Refresh action. Show the selected transport clearly for duplicate devices. Capabilities probe automatically for an authorized selection.
2. Video source dropdown: **Phone camera / Phone screen**. While starting/running/reconnecting, disable Device and Source and show **Stop capture to change device or source**. Both modes reuse the same Windows camera.

Main area has the preview on the left and properties on the right. Label it **Output preview**. It shows exactly the selected framing/rotation/mirroring against the output aspect, but uses full source pixels. Preview controls are Show/Hide preview; changing pages also can suspend preview rendering without affecting output.

| Camera mode fields | Screen mode fields |
| --- | --- |
| Camera: enumerated friendly description and ID | Display: enumerated description/ID and reported dimensions |
| Profile: Standard / Detail / Motion / Custom | Profile: Balanced / Native / Motion / Custom |
| Requested camera dimensions and camera FPS, expanded when edited | Long-edge limit or Native, and FPS limit, expanded when edited |
| Supported camera controls only, in Advanced | Orientation: Follow phone / Lock current orientation |

Common fields: **Framing** (Fit — preserve all content / Crop to fill — edges are removed), **Audio** (Off / Phone microphone) and **Advanced** expansion. Advanced holds codec/bitrate/encoder and explicit output rotation/mirror. Mirroring is Off by default for screen mode. No nonfunctional focus/exposure/HDR controls.

Unavailable microphones remain an explained audio choice; selecting that choice while the component is absent opens/links to setup or shows validation, not a successful start. A video-only screen session is allowed with Off. Never silently replace the requested microphone mode with Off. A profile or settings change that needs restart says **Applies on next Start** and is stored as a draft, distinct from active settings.

Observed source dimensions, preferred output, client requests and detailed quality explanations are in Diagnostics and documentation. Their former capture rows were removed at the user's request to enlarge preview and source-settings space. Screen capture update cadence is variable and is not the camera sample rate. A fixed output format cannot make native portrait content wider or supply detail absent from the phone image.

Output rows use the actual enumerated camera name in a read-only edit with Copy name. Microphone shows its actual name only when enumerated; otherwise **Not installed — signed driver required** and Setup. Receiver shows **Not checked**, **Test running**, **Video verified** or a concrete failure. Camera/microphone evidence can be verified independently. Test input is explicitly local and never defaults to other Windows hardware. If another application holds the camera, report that failure and a Close other camera test instruction without stopping another application's process.

Documentation explains that the entire phone display, including notifications, is captured. Capture-space disclaimers were removed at the user's request. No click/touch input, clipboard bridge, live-call or recording action is added.

## Devices page

Report columns: Device, Connection, ADB state, Android and Active. Display authorized, unauthorized, offline, disconnected, unsupported-for-selected-source and unknown distinctly. Source support does not overwrite the actual ADB state. Do not combine transports merely because the model matches.

Show the selected serial/transport and source support in details, with identifiers available for local diagnosis but redacted on export. Buttons: Refresh, Inspect, targeted Disconnect/Reconnect and USB setup. Selecting a row while capture runs only inspects it; it must not change the active Capture selection.

Wireless section has separate **Pairing address:port**, masked **Pairing code** and **Connection address:port**. Pair and Connect remain separate actions; offer mDNS discovered services when supported and manual fields otherwise. Examples use numeric ports such as 192.168.1.20:37123, never :pair or an assumed 5555. Clear pairing codes after submission. Advanced legacy USB-to-TCP stays explicitly labeled, with no automatic network scan or ADB-server restart.

## Settings page

Ordinary sub-tabs: **Output, Dependencies, Advanced**.

- Output: preferred camera format drawn from implemented formats; **Preview scaling** and **Pipeline scaling** dropdowns only; actual output-component install/availability states and setup actions grouped inside a labeled Windows-input panel with separate camera/microphone sections. Preview scaling takes effect immediately; pipeline scaling takes effect on next Start in the first pass.
- Dependencies: existing selected ADB and stock probing scrcpy paths, patched capture-client path/version, camera component and driver/package information. Browse, Validate and Apply; exact versions and official setup links. Flag a stock capture client without private export support precisely.
- Advanced: boxed Session behavior and Microphone driver sections, selected-device reconnect opt-in, bounded retries, and signing information alongside its explanation. An explicit repair button for an ADB conflict may be offered; no silent shared-server restart.

Framing/rotation/audio are per-source Capture properties, not duplicated as conflicting Settings controls. Values changing active media remain visibly pending. Validated profile changes save automatically; dependency edits have explicit Apply. Nothing auto-starts on app launch.

## Diagnostics page

Use an observed-stage table and a bounded log. Stages: selected ADB transport, source capabilities/request, decoded dimensions/codec/configuration epoch, source content age, session health, camera component, camera client requests, microphone driver/PCM status and receiver verification.

Display requested settings, reported capabilities, observed stream, camera-request configuration and receiving-app results separately. Unknown data remains Unknown. Include measured capture updates, repeated camera samples, frame queue/drop counts, render/request timing and errors when instruments exist. Do not fill unused cells with fabricated latency/quality values.

Copy capture command gives an effective display-only string; execution still uses safe argument arrays. Export redacted diagnostics strips pairing codes, unnecessary serials, credentials and media. Do not log an indefinitely growing frame-per-line trace on the UI thread.

## State copy and interaction checks

| Condition | Main action and helpful text |
| --- | --- |
| No authorized selected device | Start disabled; Select an authorized device in Devices |
| Capability probe pending | Cancel probe available where needed; Checking selected display/camera |
| Ready and stopped | Start capture; Camera available / Microphone unavailable as applicable |
| Starting | Cancel start; Waiting for first valid source frame |
| Screen unchanged but healthy | Stop capture; Screen connected — no new content updates; show content age in Diagnostics |
| Rotation reconfiguration | Stop capture; Updating screen orientation; neutral output until valid new frame |
| Required microphone absent/fails | Start validation or session failure; Phone microphone output unavailable, with exact reason |
| Stop/disconnect | Stopping… / Stopped or Disconnected; stopped/blank output, microphone silence |
| Reconnecting, opt-in only | Cancel reconnect; attempt count and selected device/transport |
| Output available, no receiver test | Receiver: Not checked; offer explicit local Test input |

Validate keyboard-only navigation, Enter on the focused primary button, cancellation while starting/reconnecting, Unicode/long names, all DPI targets, high contrast, scroll position and stable tab focus after async updates. Pressing Escape must not ambiguously stop capture while editing; it cancels only a clearly active local edit/dialog. Preview hiding/minimizing cannot reset the source or camera output.

## Corrections to the generated reference images

The written spec is authoritative when a generated label/state is inconsistent:

1. Replace the dark image's invalid :pair address text with separate valid numeric pairing and connection ports.
2. Use neutral stopped/off indicators; amber for unavailable and red for actual failures.
3. Disable device/source selection while capture is active and keep Cancel available during Start.
4. Use **Preferred output**, not a claim to force a receiving application's resolution. Keep camera enumeration, host publication and receiver proof separate.
5. Remove duplicated Settings framing/rotation fields; these belong to the Capture profile. The two scaler lists stay in Settings.
6. Use flat colors/native focus states, and allow labels/controls to wrap or scroll at DPI changes. Do not copy faint gradients or tiny illegible text from the image.
7. Show our microphone as unavailable until actually installed/validated. Pictured devices, preview test cards and dimensions are illustrative.
8. Use the real live preview texture or an explicit stopped placeholder. The generated photo/screen is not an application asset or validation evidence.

Faithfulness means matching the geometry, information hierarchy, affordances and native control choices. It does not mean recreating erroneous text or baking the concept image into the app.

## Shared presentation pacing

Settings → Output adds a native selectable list: Smooth priority — buffer one frame (default), or Minimum latency — present on arrival. It applies on the next Start to the shared publication stage for camera and screen, including both preview and Windows output. DPAPI v6 migrates v5 with a backup and preserves appearance and per-device capture profiles. Pacing remains separate from the two quality/scaling lists. Numeric graphs separate intentional queue wait from processing costs.
