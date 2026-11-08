#!/usr/bin/env python3
"""Plot actual StrokeFilter fixtures exported by the stabilizer Qt tests.

Development-only dependency: matplotlib. No substitute filter is implemented here.
Usage: python scripts/plot_stabilizer_review.py fixtures.json --output build/stabilizer-review
"""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle


def points(samples):
    return [s[0] for s in samples], [s[1] for s in samples]


def plot_trace(ax, fixture, title):
    ax.plot(*points(fixture["raw"]), color="#a0a6ad", linewidth=1.4, label="Raw input")
    ax.plot(*points(fixture["live"]), color="#1766ac", linewidth=1.8, label="Live stabilized path")
    if fixture["tail"]:
        tip = fixture["live"][-1:]
        ax.plot(*points(tip + fixture["tail"]), color="#b44d12", linewidth=2, label="Finish tail")
    ax.set_title(title, loc="left", fontweight="bold")
    ax.set_xlabel("Logical view pixels"); ax.set_ylabel("Logical view pixels")
    ax.grid(alpha=.2); ax.legend(fontsize=8, frameon=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixtures", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.fixtures.read_text())
    if data.get("source") != "actual StrokeFilter engine output":
        raise ValueError("Expected actual-engine fixtures")
    fixtures = {item["name"]: item for item in data["fixtures"]}
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10, "axes.spines.top": False,
                         "axes.spines.right": False, "savefig.facecolor": "white"})
    fig, axes = plt.subplots(2, 2, figsize=(12, 8.4), layout="constrained")
    smooth, strong = fixtures["noisy-horizontal-smooth"], fixtures["noisy-horizontal-stabilizer"]
    ax = axes[0, 0]
    ax.plot(*points(smooth["raw"]), color="#a0a6ad", linewidth=.8, alpha=.8, label="Raw tremor")
    for fixture, color, label in ((smooth, "#1766ac", "Smooth 100; dead zone 0"),
                                   (strong, "#b44d12", "Stabilizer 100; dead zone 1.2")):
        ax.plot(*points(fixture["live"]), color=color, linewidth=1.7, label=label)
        ax.plot(*points(fixture["live"][-1:] + fixture["tail"]), color=color, linewidth=1, linestyle="--")
    ax.set_xlim(250, 700); ax.set_ylim(-4, 4)
    ax.set_title("A  Tremor attenuation after warm-up", loc="left", fontweight="bold")
    ax.set_xlabel("Logical view pixels"); ax.set_ylabel("Deviation from straight line (px)")
    ax.grid(alpha=.2); ax.legend(fontsize=8, loc="upper right", frameon=False)
    raw_rms = smooth["metrics"]["rawRmsYAfter250"]
    ax.text(.02, .03, f"Live RMS: raw {raw_rms:.2f} px; Smooth {smooth['metrics']['liveRmsYAfter250']:.3f} px; "
            f"Stabilizer {strong['metrics']['liveRmsYAfter250']:.3f} px\nDashed portions are generated finish tails.",
            transform=ax.transAxes, fontsize=8, va="bottom", bbox={"facecolor": "white", "alpha": .85, "edgecolor": "none"})

    plot_trace(axes[0, 1], fixtures["corner-pending-tail"], "B  Finishing a delayed corner in path order")
    axes[0, 1].set_aspect("equal", adjustable="datalim")
    plot_trace(axes[1, 0], fixtures["intentional-loop"], "C  Loop: Stabilizer 80; dead zone 1.2")
    axes[1, 0].set_aspect("equal", adjustable="box")
    axes[1, 0].text(.02, .02, "The live path retains lag and an inward bias.",
                    transform=axes[1, 0].transAxes, fontsize=8, va="bottom")

    rest, ax = fixtures["stationary-tremor"], axes[1, 1]
    ax.plot(*points(rest["raw"]), color="#a0a6ad", linewidth=.6, alpha=.65, label="Raw stationary tremor")
    ax.scatter(*points(rest["live"]), color="#1766ac", s=30, zorder=3, label="Stable tip")
    ax.add_patch(Circle((0, 0), rest["settings"]["deadZone"], fill=False, color="#b44d12", linestyle="--", label="2 px dead zone"))
    ax.set_aspect("equal", adjustable="box"); ax.set_xlim(-2.5, 2.5); ax.set_ylim(-2.5, 2.5)
    ax.set_title("D  Stationary tremor; 2 px dead zone", loc="left", fontweight="bold")
    ax.set_xlabel("Logical view pixels"); ax.set_ylabel("Logical view pixels"); ax.grid(alpha=.2)
    ax.legend(fontsize=8, frameon=False, loc="upper right")
    fig.suptitle("Toon Pro: actual stabilizer engine output", fontsize=16, fontweight="bold")
    fig.supxlabel("Deterministic synthetic input • pixel scale 1 • no physical-tablet latency or CSP comparison measured", fontsize=9)
    args.output.mkdir(parents=True, exist_ok=True)
    for suffix in ("png", "svg"):
        fig.savefig(args.output / f"stabilizer-comparison.{suffix}", dpi=160)
    plt.close(fig)
    summary = {}
    for name in ("noisy-horizontal-smooth", "noisy-horizontal-stabilizer"):
        fixture = fixtures[name]; metrics = fixture["metrics"]
        summary[name] = {"settings": fixture["settings"], **metrics,
                         "liveRmsReductionPercent": 100 * (1 - metrics["liveRmsYAfter250"] / metrics["rawRmsYAfter250"])}
    (args.output / "stabilizer-metrics.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
