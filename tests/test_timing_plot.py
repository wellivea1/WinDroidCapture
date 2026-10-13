"""Fake timing fixtures test attribution, not physical capture or latency."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("plot_timing", Path(__file__).parents[1] / "scripts" / "plot-timing.py")
plot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plot)


def capture(session, frame, packet=1000):
    return dict(session=session, frame=str(frame), packet_ms=packet, decode_start_ms=packet+1,
                decode_end_ms=packet+4, publish_ms=packet+5, source_gap_ms=33.3)


def preview(session, frame, packet=1000):
    return dict(session=session, frame=str(frame), copy_start_ms=packet+6, copy_end_ms=packet+7,
                present_start_ms=packet+7, present_end_ms=packet+8, ok=1)


class AttributionTests(unittest.TestCase):
    def test_exact_session_and_frame_only(self):
        _, _, report = plot.analyze([capture("a", 1)], [preview("b", 1), preview("a", 1)], warmup=0)
        self.assertEqual(report["joined_frames"], 1)
        self.assertEqual(report["preview_frames_without_capture"], 1)
        self.assertEqual(report["metrics"]["pc_latency"]["p50_ms"], 8)

    def test_duplicate_redraw_not_a_new_frame(self):
        _, series, report = plot.analyze([capture("a", 1)], [preview("a", 1)] * 2, warmup=0)
        self.assertEqual(report["joined_frames"], 1)
        self.assertEqual(report["duplicate_preview_redraws"], 1)
        self.assertEqual(series["preview_interval"], [])

    def test_missing_opaque_stamp_does_not_invent_latency(self):
        with self.assertRaises(ValueError):
            plot.analyze([capture("a", 1, float("nan"))], [preview("a", 1)], warmup=0)

    def test_warmup_and_bad_timeline_are_excluded(self):
        rows = [capture("a", 1), capture("a", 2, 4000), capture("a", 3, 4100)]
        previews = [preview("a", 1), preview("a", 2, 4000), preview("a", 3, 4090)]
        _, _, report = plot.analyze(rows, previews, warmup=2)
        self.assertEqual(report["joined_frames"], 1)
        self.assertEqual(report["invalid_joins"], 1)

    def test_duplicate_capture_identity_rejected(self):
        with self.assertRaises(ValueError):
            plot.analyze([capture("a", 1)] * 2, [preview("a", 1)], warmup=0)

    def test_receiver_is_cadence_only(self):
        _, series, report = plot.analyze([capture("a", 1)], [preview("a", 1)],
            [dict(frame=1, callback_ms=1010), dict(frame=2, callback_ms=1043)], warmup=0)
        self.assertEqual(series["receiver_callback_interval"], [(0.043, 33)])
        self.assertNotIn("receiver_latency", report["metrics"])

    def test_smoothing_wait_is_not_conversion_cost(self):
        source=capture("a",1);source.update(convert_end_ms=1005,publish_ms=1035,scheduled_ms=1034,queue_dropped=0,queue_late=0)
        received=preview("a",1,1030)
        _, _, report=plot.analyze([source],[received],warmup=0)
        self.assertEqual(report["metrics"]["convert"]["p50_ms"],1)
        self.assertEqual(report["metrics"]["queue_wait"]["p50_ms"],30)
        self.assertEqual(report["metrics"]["pc_latency"]["p50_ms"],38)


if __name__ == "__main__":
    unittest.main()
