/* AWM extension to scrcpy 5.0.1. Apache-2.0, like the upstream server.
 * Enumerates Camera2 members and binds an encoder surface to one member.
 * No replacement camera app, protocol implementation or Android installation. */
package com.genymobile.scrcpy.video;

import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.wrappers.ServiceManager;
import android.graphics.Rect;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.MediaCodec;
import android.os.Build;
import android.util.Range;
import android.util.SizeF;
import java.util.Locale;
import java.util.TreeSet;

public final class AwmPhysicalCameras {
    public static final String PREFIX = "awm-physical:";
    private AwmPhysicalCameras() { }

    public static String[] resolve(String requested) {
        if (requested == null || !requested.startsWith(PREFIX)) {
            return new String[] { requested, null };
        }
        String[] parts = requested.substring(PREFIX.length()).split(":", -1);
        if (parts.length != 2 || parts[0].isEmpty() || parts[1].isEmpty()) {
            throw new IllegalArgumentException("Invalid AWM physical camera identity");
        }
        return parts;
    }

    public static void validate(String logical, String physical, boolean highSpeed, float zoom) throws CameraAccessException {
        if (physical == null) {
            return;
        }
        if (Build.VERSION.SDK_INT < 29 || highSpeed || zoom != 1) {
            throw new IllegalArgumentException("AWM physical capture requires Android 10+, regular capture and zoom 1");
        }
        CameraCharacteristics parent = ServiceManager.getCameraManager().getCameraCharacteristics(logical);
        if (!parent.getPhysicalCameraIds().contains(physical)) {
            throw new IllegalArgumentException("Selected physical camera is not a member of the selected logical camera");
        }
    }

    public static void appendMetadata(StringBuilder out, CameraCharacteristics characteristics) {
        float[] focal = characteristics.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
        SizeF sensor = characteristics.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE);
        if (focal != null && focal.length > 0) {
            out.append(String.format(Locale.US, ", focal-mm=%.7g", focal[0]));
        }
        if (sensor != null) {
            out.append(String.format(Locale.US, ", sensor-width-mm=%.7g", sensor.getWidth()));
        }
    }

    public static void append(StringBuilder out, String logical, boolean includeSizes) {
        if (Build.VERSION.SDK_INT < 29 || logical.indexOf(':') >= 0) {
            return;
        }
        CameraManager manager = ServiceManager.getCameraManager();
        try {
            CameraCharacteristics parent = manager.getCameraCharacteristics(logical);
            for (String physical : new TreeSet<>(parent.getPhysicalCameraIds())) {
                if (physical.indexOf(':') >= 0) {
                    continue;
                }
                try {
                    CameraCharacteristics c = manager.getCameraCharacteristics(physical);
                    int[] capabilities = c.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES);
                    boolean color = false;
                    if (capabilities != null) {
                        for (int capability : capabilities) {
                            color |= capability == CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE;
                        }
                    }
                    StreamConfigurationMap map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
                    android.util.Size[] sizes = map == null ? null : map.getOutputSizes(MediaCodec.class);
                    Rect active = c.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);
                    if (!color || active == null || sizes == null || sizes.length == 0) {
                        continue; // Depth/unknown members are not advertised as color video.
                    }
                    Integer facing = c.get(CameraCharacteristics.LENS_FACING);
                    String direction = facing == null ? "unknown" : facing == CameraCharacteristics.LENS_FACING_BACK ? "back"
                            : facing == CameraCharacteristics.LENS_FACING_FRONT ? "front" : "external";
                    TreeSet<Integer> fps = new TreeSet<>();
                    Range<Integer>[] ranges = c.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
                    if (ranges != null) {
                        for (Range<Integer> range : ranges) {
                            fps.add(range.getLower());
                            fps.add(range.getUpper());
                        }
                    }
                    out.append("\n    --camera-id=").append(PREFIX).append(logical).append(':').append(physical);
                    out.append("    (").append(direction).append(", ").append(active.width()).append('x').append(active.height());
                    out.append(", fps={");
                    String separator = "";
                    for (int value : fps) {
                        out.append(separator).append(value);
                        separator = ", ";
                    }
                    out.append("}, zoom-range=[1, 1]");
                    appendMetadata(out, c);
                    out.append(')');
                    if (includeSizes) {
                        for (android.util.Size size : sizes) {
                            out.append("\n        - ").append(size.getWidth()).append('x').append(size.getHeight());
                        }
                    }
                } catch (CameraAccessException | IllegalArgumentException e) {
                    Ln.w("AWM physical member metadata unavailable: " + logical + "/" + physical);
                }
            }
        } catch (CameraAccessException | IllegalArgumentException e) {
            Ln.w("AWM physical camera enumeration unavailable for " + logical);
        }
    }
}
