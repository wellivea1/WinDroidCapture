"""Prepare a bounded, documented scrcpy 5.0.1 physical-camera extension.

Original tools/scrcpy-5.0.1 sources remain untouched. The output is a derived
server source tree; there is no new wire protocol or replacement camera app.
"""
from pathlib import Path
import hashlib
import json
import shutil
import sys

root = Path(__file__).resolve().parents[1]
source = root / "tools/scrcpy-5.0.1/server/src/main"
out = root / "build-native/android-server"
target = out / "sources"
target.mkdir(parents=True, exist_ok=True)
for path in source.rglob("*"):
    if path.is_file():
        destination = target / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit("Upstream camera patch anchor changed; no server is published")
    return text.replace(old, new, 1)


camera = target / "java/com/genymobile/scrcpy/video/CameraCapture.java"
text = camera.read_text(encoding="utf-8")
text = replace_once(text, "    private String cameraId;", "    private String cameraId;\n    private String physicalCameraId;\n    private boolean physicalResultReported;")
text = replace_once(text, "            cameraId = selectCamera(explicitCameraId, cameraFacing);",
    "            String[] identity = AwmPhysicalCameras.resolve(explicitCameraId);\n"
    "            cameraId = selectCamera(identity[0], cameraFacing);\n"
    "            physicalCameraId = identity[1];\n"
    "            AwmPhysicalCameras.validate(cameraId, physicalCameraId, highSpeed, zoom);")
text = replace_once(text, "            Ln.i(\"Using camera '\" + cameraId + \"'\");",
    "            Ln.i(\"Using camera '\" + cameraId + \"'\");\n"
    "            if (physicalCameraId != null) {\n"
    "                Ln.i(\"AWM physical output bound: logical=\" + cameraId + \" physical=\" + physicalCameraId);\n"
    "            }")
text = replace_once(text, "captureSize = selectSize(cameraId, explicitSize, maxSize, aspectRatio, highSpeed);",
    "captureSize = selectSize(physicalCameraId == null ? cameraId : physicalCameraId, explicitSize, maxSize, aspectRatio, highSpeed);")
text = replace_once(text, "        OutputConfiguration outputConfig = new OutputConfiguration(captureSurface);",
    "        OutputConfiguration outputConfig = new OutputConfiguration(captureSurface);\n"
    "        if (physicalCameraId != null) {\n"
    "            outputConfig.setPhysicalCameraId(physicalCameraId);\n"
    "        }")
text = replace_once(text, "requestBuilder = cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);",
    "requestBuilder = physicalCameraId == null ? cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_RECORD)\n"
    "                            : cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_RECORD, Collections.singleton(physicalCameraId));")
anchor = "        CameraCaptureSession.CaptureCallback callback = new CameraCaptureSession.CaptureCallback() {"
text = replace_once(text, anchor, anchor + "\n"
    "            @Override\n"
    "            public void onCaptureCompleted(CameraCaptureSession session, CaptureRequest request, android.hardware.camera2.TotalCaptureResult result) {\n"
    "                if (physicalCameraId != null && !physicalResultReported) {\n"
    "                    android.hardware.camera2.CaptureResult member = result.getPhysicalCameraResults().get(physicalCameraId);\n"
    "                    if (member != null) {\n"
    "                        physicalResultReported = true;\n"
    "                        Ln.i(\"AWM physical result confirmed: physical=\" + physicalCameraId\n"
    "                                + \" focal-mm=\" + member.get(android.hardware.camera2.CaptureResult.LENS_FOCAL_LENGTH));\n"
    "                    }\n"
    "                }\n"
    "            }")
camera.write_text(text, encoding="utf-8", newline="\n")

log = target / "java/com/genymobile/scrcpy/util/LogUtils.java"
text = log.read_text(encoding="utf-8")
start = text.index("    public static String buildCameraListMessage(")
end = text.index("    private static String getFormattedUniqueSet(", start)
fragment = text[start:end]
fragment = replace_once(fragment, 'new StringBuilder("List of cameras:")', 'new StringBuilder("List of cameras: [AWM camera API 1]")')
fragment = replace_once(fragment, "                    builder.append(')');",
    "                    com.genymobile.scrcpy.video.AwmPhysicalCameras.appendMetadata(builder, characteristics);\n"
    "                    builder.append(')');")
fragment = replace_once(fragment, "                    }\n                }\n            }\n        } catch (CameraAccessException e)",
    "                    }\n"
    "                    com.genymobile.scrcpy.video.AwmPhysicalCameras.append(builder, id, includeSizes);\n"
    "                }\n            }\n        } catch (CameraAccessException e)")
log.write_text(text[:start] + fragment + text[end:], encoding="utf-8", newline="\n")
helper = root / "native/scrcpy/server/AwmPhysicalCameras.java"
shutil.copy2(helper, target / "java/com/genymobile/scrcpy/video/AwmPhysicalCameras.java")
generated = out / "generated/com/genymobile/scrcpy"
generated.mkdir(parents=True, exist_ok=True)
(generated / "BuildConfig.java").write_text(
    'package com.genymobile.scrcpy; public final class BuildConfig { public static final boolean DEBUG=false; public static final String VERSION_NAME="5.0.1"; }\n', encoding="utf-8")
manifest = {"upstream": "scrcpy 5.0.1", "awm_camera_api": 1,
    "mechanism": "OutputConfiguration.setPhysicalCameraId; existing camera_id argument, no wire change",
    "modified_files": ["CameraCapture.java", "LogUtils.java"],
    "extension": "AwmPhysicalCameras.java", "upstream_source_hashes": {}}
for filename in ("java/com/genymobile/scrcpy/video/CameraCapture.java", "java/com/genymobile/scrcpy/util/LogUtils.java"):
    manifest["upstream_source_hashes"][filename] = hashlib.sha256((source / filename).read_bytes()).hexdigest()
(out / "server-source-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print("Prepared isolated upstream server extension: AWM camera API 1")
