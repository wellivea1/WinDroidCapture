"""Plot real, opt-in AWM numeric traces. No media, network or device fallback.

Requires matplotlib and numpy only when rendering. Frame correlation uses both
session and frame number. PC QPC timestamps must never be compared to Android's
absolute presentation timestamps. Source PTS intervals are comparable durations.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def load(path):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def number(row, key):
    try:
        value = float(row[key])
        return value if math.isfinite(value) else math.nan
    except (ValueError, KeyError):
        return math.nan


def percentile(values, fraction):
    values = sorted(values)
    index = (len(values) - 1) * fraction
    left = int(index)
    return values[left] + (values[min(left + 1, len(values) - 1)] - values[left]) * (index - left)


def stats(values):
    values = [v for v in values if math.isfinite(v) and v >= 0]
    if not values:
        return {"n": 0}
    return {"n": len(values), "mean_ms": statistics.mean(values),
            "p50_ms": percentile(values, .5), "p95_ms": percentile(values, .95),
            "p99_ms": percentile(values, .99), "max_ms": max(values)}


def frame_key(row):
    return row["session"], int(row["frame"])


def interval_series(rows, field, origin, cutoff, session=True):
    result, previous = [], {}
    for row in rows:
        clock = number(row, field)
        tag = row.get("session", "receiver") if session else "receiver"
        last = previous.get(tag)
        if math.isfinite(clock):
            if last is not None and clock >= cutoff and last >= cutoff and clock >= last:
                result.append(((clock - origin) / 1000, clock - last))
            previous[tag] = clock
    return result


def analyze(capture, preview, receiver=(), warmup=2, packets=None):
    if warmup < 0:
        raise ValueError("Warmup must be nonnegative")
    finite = [number(r, "packet_ms") for r in capture if math.isfinite(number(r, "packet_ms"))]
    if not finite:
        raise ValueError("No correlated packet timestamps; latency cannot be measured")
    origin = min(finite)
    cutoff = origin + warmup * 1000
    captured = {}
    for row in capture:
        key = frame_key(row)
        if key in captured:
            raise ValueError("Duplicate capture frame identity; use a fresh timing directory")
        captured[key] = row
    successful = [r for r in preview if number(r, "ok") == 1]
    # Resizing or changing a filter can present one frame again. Only the first
    # successful submission measures delivery of a new source update.
    unique, seen = [], set()
    for row in successful:
        key = frame_key(row)
        if key not in seen:
            seen.add(key)
            unique.append(row)
    joined, rejected = [], 0
    for row in unique:
        source = captured.get(frame_key(row))
        if source is None:
            continue
        packet, decode_start, decoded, published = [number(source, k) for k in
            ("packet_ms", "decode_start_ms", "decode_end_ms", "publish_ms")]
        converted = number(source, "convert_end_ms") if "convert_end_ms" in source else published
        copy_start, copy_end, present_start, submitted = [number(row, k) for k in
            ("copy_start_ms", "copy_end_ms", "present_start_ms", "present_end_ms")]
        if packet < cutoff:
            continue
        if not all(math.isfinite(v) for v in (packet, decode_start, decoded, published,
                                              copy_start, copy_end, present_start, submitted)):
            rejected += 1
            continue
        if not (packet <= decode_start <= decoded <= converted <= published <= submitted and
                packet <= copy_start <= copy_end <= present_start <= submitted):
            rejected += 1
            continue
        joined.append({"t": (submitted - origin) / 1000, "pc_latency": submitted - packet,
                       "decode": decoded - decode_start, "convert": converted - decoded,
                       "queue_wait": published - converted,
                       "preview_wait": copy_start - published, "copy": copy_end - copy_start,
                       "render": submitted - present_start,
                       "publish_to_submit": submitted - published})
    if not joined:
        raise ValueError("No valid frame joins after warmup; no latency result will be invented")
    packet_rows = packets if packets is not None else capture
    packet_gaps = interval_series(packet_rows, "packet_ms", origin, cutoff)
    source_gaps = [((number(r, "packet_ms") - origin) / 1000, number(r, "source_gap_ms"))
                   for r in packet_rows if number(r, "packet_ms") >= cutoff and
                   math.isfinite(number(r, "source_gap_ms"))]
    preview_gaps = interval_series(unique, "present_end_ms", origin, cutoff)
    # Receiver callbacks have no source-frame identity in the MF interface.
    # This is a cadence series, never a source-to-receiver latency estimate.
    receiver_origin = number(receiver[0], "callback_ms") if receiver else origin
    receiver_gaps = interval_series(receiver, "callback_ms", origin,
                                   receiver_origin + warmup * 1000, session=False)
    series = {"source_pts_interval": source_gaps, "packet_interval": packet_gaps,
              "preview_interval": preview_gaps, "receiver_callback_interval": receiver_gaps}
    metrics = {name: stats([v for _, v in points]) for name, points in series.items()}
    for name in ("pc_latency", "decode", "convert", "queue_wait", "publish_to_submit", "copy", "render"):
        metrics[name] = stats([r[name] for r in joined])
    summary = {"measurement": "PC packet arrival to preview Present return; not phone-to-display latency",
               "packet_series": "all received compressed video packets" if packets is not None else "packets associated with published frames (legacy trace)",
               "warmup_seconds": warmup, "joined_frames": len(joined), "invalid_joins": rejected,
               "capture_frames": len(capture), "successful_preview_submissions": len(successful),
               "unique_preview_frames": len(unique), "duplicate_preview_redraws": len(successful) - len(unique),
               "preview_frames_without_capture": sum(frame_key(r) not in captured for r in unique),
               "metrics": metrics}
    if any("queue_dropped" in r for r in capture):
        summary["queue_dropped_total"] = max(int(r["queue_dropped"]) for r in capture)
        summary["queue_late_total"] = max(int(r["queue_late"]) for r in capture)
        summary["queue_dropped_change_in_selected_window"] = summary["queue_dropped_total"] - min(int(r["queue_dropped"]) for r in capture)
        summary["queue_late_change_in_selected_window"] = summary["queue_late_total"] - min(int(r["queue_late"]) for r in capture)
    return joined, series, summary


def render(joined, series, summary, out, fps, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "axes.spines.top": False, "axes.spines.right": False})
    fig, axes = plt.subplots(3, 1, figsize=(14, 10), constrained_layout=True)
    fig.suptitle(title, fontsize=16, fontweight="bold")
    colors = {"source_pts_interval": "#a77b08", "packet_interval": "#006da8",
              "preview_interval": "#059874", "receiver_callback_interval": "#a055bb"}
    labels = {"source_pts_interval": "Phone PTS intervals", "packet_interval": "PC packet arrivals",
              "preview_interval": "Preview submissions", "receiver_callback_interval": "Virtual camera callbacks"}
    for name, points in series.items():
        if points:
            x, y = zip(*points)
            axes[0].plot(x, y, color=colors[name], lw=.8, alpha=.8, label=labels[name])
    axes[0].axhline(1000 / fps, color="#50545b", linestyle="--", lw=1, label=f"{fps} FPS reference ({1000/fps:.2f} ms)")
    axes[0].set(ylabel="Frame interval (ms)", xlabel="Seconds since first PC packet")
    axes[0].set_title("Frame cadence / jitter — long static-screen gaps remain visible", loc="left")
    axes[0].legend(ncol=3, fontsize=9)
    times = [r["t"] for r in joined]
    axes[1].plot(times, [r["pc_latency"] for r in joined], color="#006da8", lw=1, label="Packet → preview submission")
    axes[1].plot(times, [r["publish_to_submit"] for r in joined], color="#059874", lw=.8, label="Published frame → preview submission")
    latency = summary["metrics"]["pc_latency"]
    for key, style in (("p50_ms", "--"), ("p95_ms", ":")):
        axes[1].axhline(latency[key], color="#70757e", linestyle=style, lw=1,
                        label=f"{key.split('_')[0]} {latency[key]:.2f} ms")
    axes[1].set(ylabel="Measured PC delay (ms)", xlabel="Seconds since first PC packet")
    axes[1].set_title(f"Correlated PC pipeline — {len(joined):,} frames; excludes phone encoding and display scanout", loc="left")
    axes[1].legend(ncol=2, fontsize=9)
    for name, label, color in (("decode", "Decoder", "#006da8"), ("convert", "GPU conversion", "#a77b08"),
                               ("queue_wait", "Smoothing queue wait", "#a055bb"),
                               ("render", "Preview render + Present", "#059874")):
        axes[2].plot(times, [r[name] for r in joined], lw=.8, color=color, label=label)
    axes[2].set(ylabel="Stage duration (ms)", xlabel="Seconds since first PC packet")
    axes[2].set_title("Processing costs — buffers flushed every five seconds; no media recording", loc="left")
    axes[2].legend(ncol=3, fontsize=9)
    for ax in axes:
        ax.grid(True, alpha=.2)
        ax.set_ylim(bottom=0)
    fig.savefig(out / "timing-over-time.png", dpi=160)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(13, 4), constrained_layout=True)
    values = np.asarray([r["pc_latency"] for r in joined])
    axes[0].hist(values, bins=min(60, max(10, int(math.sqrt(len(values))))), color="#006da8", alpha=.85)
    axes[0].axvline(latency["p95_ms"], color="#e08820", linestyle="--", label=f"p95 {latency['p95_ms']:.2f} ms")
    axes[0].set(title="PC packet → preview submission", xlabel="Delay (ms)", ylabel="Frames")
    axes[0].legend()
    for name in ("packet_interval", "preview_interval", "receiver_callback_interval"):
        points = series[name]
        if points:
            values = np.sort([v for _, v in points])
            axes[1].plot(values, np.arange(1, len(values)+1)/len(values)*100, color=colors[name], label=labels[name])
    axes[1].axvline(1000/fps, color="#70757e", linestyle="--")
    axes[1].set(title="Cadence distribution — full measured tail", xlabel="Frame interval (ms)", ylabel="Cumulative percentage")
    axes[1].legend(fontsize=9)
    for ax in axes:
        ax.grid(True, alpha=.2)
    fig.savefig(out / "timing-distributions.png", dpi=160)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(12, 5), constrained_layout=True)
    fig.suptitle("Measured cadence and PC processing", fontsize=16, fontweight="bold")
    cadence = [name for name in ("source_pts_interval", "packet_interval",
                                 "preview_interval", "receiver_callback_interval")
               if summary["metrics"][name]["n"]]
    positions = np.arange(len(cadence))
    p95 = [summary["metrics"][name]["p95_ms"] for name in cadence]
    p50 = [summary["metrics"][name]["p50_ms"] for name in cadence]
    axes[0].barh(positions, p95, color=[colors[name] for name in cadence], alpha=.85,
                 height=.55, label="95th percentile")
    axes[0].scatter(p50, positions, color="#202b36", marker="|", s=180,
                    linewidths=2, label="Median")
    for y, value in zip(positions, p95):
        axes[0].text(value + .6, y, f"{value:.2f}", va="center", fontsize=10)
    axes[0].set_yticks(positions, [labels[name] for name in cadence])
    axes[0].invert_yaxis()
    axes[0].axvline(1000/fps, color="#50545b", ls="--", lw=1)
    axes[0].set(title=f"Cadence: bars = p95; ticks = median\n{fps:g} FPS reference = {1000/fps:.2f} ms",
                xlabel="Frame interval (ms)", xlim=(0, max(p95)*1.22))
    stages = [("decode", "Decode", "#006da8"),
              ("convert", "GPU conversion", "#a77b08"),
              ("queue_wait", "Smoothing queue wait", "#a055bb"),
              ("publish_to_submit", "Published → preview", "#059874")]
    means = [summary["metrics"][name]["mean_ms"] for name, _, _ in stages]
    axes[1].barh(range(len(stages)), means, color=[color for _, _, color in stages],
                 height=.55, alpha=.85)
    for y, value in enumerate(means):
        axes[1].text(value + .4, y, f"{value:.2f}", va="center", fontsize=10)
    axes[1].set_yticks(range(len(stages)), [label for _, label, _ in stages])
    axes[1].invert_yaxis()
    axes[1].set(title="PC stage costs (mean)", xlabel="Duration (ms)",
                xlim=(0, max(1, max(means))*1.25))
    for ax in axes:
        ax.grid(True, axis="x", alpha=.2)
        ax.set_axisbelow(True)
    fig.supxlabel(f"{len(joined):,} correlated frames · packet → preview median {latency['p50_ms']:.2f} ms, "
                  f"p95 {latency['p95_ms']:.2f} ms\n"
                  "Excludes phone encoding and display scanout. Receiver callbacks are cadence only.",
                  fontsize=10)
    fig.savefig(out / "timing-overview.png", dpi=160)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", required=True, type=Path)
    parser.add_argument("--preview", required=True, type=Path)
    parser.add_argument("--receiver", type=Path)
    parser.add_argument("--packets", type=Path, help="All-packet trace; defaults to capture.csv.packets.csv when present")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--warmup", type=float, default=2)
    parser.add_argument("--fps", type=float, default=30)
    parser.add_argument("--title", default="WinDroid Capture — measured latency and jitter")
    parser.add_argument("--motion-window", type=Path, help="Optional JSON with explicit PC start_ms/end_ms of the motion test")
    args = parser.parse_args()
    if args.fps <= 0:
        parser.error("--fps must be positive")
    capture, preview = load(args.capture), load(args.preview)
    receiver = load(args.receiver) if args.receiver else []
    packet_path=args.packets or Path(str(args.capture)+".packets.csv")
    packets=load(packet_path) if packet_path.exists() else None
    if args.motion_window:
        window = json.loads(args.motion_window.read_text(encoding="utf-8-sig"))
        start, end = float(window["start_ms"]), float(window["end_ms"])
        if not math.isfinite(start) or not math.isfinite(end) or start >= end:
            parser.error("Invalid motion window")
        capture = [r for r in capture if start <= number(r, "packet_ms") <= end]
        preview = [r for r in preview if start <= number(r, "present_end_ms") <= end]
        receiver = [r for r in receiver if start <= number(r, "callback_ms") <= end]
        if packets is not None: packets=[r for r in packets if start <= number(r,"packet_ms") <= end]
    joined, series, summary = analyze(capture, preview, receiver, args.warmup, packets)
    summary["selection"] = "explicit motion-test window" if args.motion_window else "whole run, including static-screen gaps"
    args.out.mkdir(parents=True, exist_ok=True)
    summary["fps_reference"] = args.fps
    (args.out / "timing-summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False), encoding="utf-8")
    # Relative times and processing durations only; omit the raw QPC/session
    # identifiers from this shareable derived table.
    with (args.out / "timing-derived.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(joined[0]))
        writer.writeheader()
        writer.writerows(joined)
    render(joined, series, summary, args.out, args.fps, args.title)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
