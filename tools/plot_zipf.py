#!/usr/bin/env python3
"""plot_zipf.py -- dependency-free SVG plot of the rank/frequency distribution.

Reads the word-count CSV produced by `mse count` and draws tf versus rank on
log-log axes, plus a 1/rank reference line. The Shakespeare corpus is a
textbook Zipf distribution: a few very frequent words and a long hapax tail.
No third-party plotting library is needed; the SVG is written directly.

Usage:
    ./build/mse count --csv build/wordcount.csv --top 0
    python3 tools/plot_zipf.py --csv build/wordcount.csv --out figures/zipf.svg
"""

import argparse
import csv
import math


def nice_ticks(max_value):
    """Return 1, 10, 100, ... up to max_value (inclusive)."""
    ticks = []
    value = 1
    while value <= max_value:
        ticks.append(value)
        value *= 10
    return ticks


def main():
    parser = argparse.ArgumentParser(description="plot a Zipf rank/frequency curve")
    parser.add_argument("--csv", default="build/wordcount.csv")
    parser.add_argument("--out", default="figures/zipf.svg")
    parser.add_argument("--title", default="Word frequency vs. rank (Shakespeare corpus)")
    args = parser.parse_args()

    rows = list(csv.DictReader(open(args.csv, encoding="utf-8")))
    points = [(int(r["rank"]), int(r["tf"]), r["term"]) for r in rows]
    if not points:
        raise SystemExit("empty word-count table")

    n = len(points)
    max_tf = points[0][1]
    log_n = math.log10(n)
    log_tf = math.log10(max_tf)

    width, height = 880, 540
    left, right, top, bottom = 86, 34, 62, 66
    plot_w = width - left - right
    plot_h = height - top - bottom

    def x_of(rank):
        # rank starts at 1; log10(1) == 0
        return left + plot_w * math.log10(rank) / log_n

    def y_of(tf):
        return top + plot_h * (1.0 - math.log10(tf) / log_tf)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'font-family="Helvetica,Arial,sans-serif">',
        f'<rect width="{width}" height="{height}" fill="white"/>',
        f'<text x="{width/2}" y="30" text-anchor="middle" font-size="17" '
        f'font-weight="bold">{args.title}</text>',
        f'<text x="{width/2}" y="50" text-anchor="middle" font-size="12" '
        f'fill="#555">N = {n:,} terms; log-log axes; dashed line = 1/rank reference</text>',
    ]

    # grid + ticks
    for tick in nice_ticks(n):
        if tick > n:
            continue
        x = x_of(tick)
        parts.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{top+plot_h}" '
                     f'stroke="#e5e7eb" stroke-width="1"/>')
        parts.append(f'<text x="{x:.1f}" y="{top+plot_h+22}" text-anchor="middle" '
                     f'font-size="11">{tick:,}</text>')
    for tick in nice_ticks(max_tf):
        if tick > max_tf:
            continue
        y = y_of(tick)
        parts.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left+plot_w}" y2="{y:.1f}" '
                     f'stroke="#e5e7eb" stroke-width="1"/>')
        parts.append(f'<text x="{left-10}" y="{y+4:.1f}" text-anchor="end" '
                     f'font-size="11">{tick:,}</text>')

    # axes
    parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top+plot_h}" stroke="#333"/>')
    parts.append(f'<line x1="{left}" y1="{top+plot_h}" x2="{left+plot_w}" y2="{top+plot_h}" stroke="#333"/>')
    parts.append(f'<text x="{width/2}" y="{height-18}" text-anchor="middle" font-size="13">'
                 f'rank (log scale)</text>')
    parts.append(f'<text x="22" y="{height/2}" text-anchor="middle" font-size="13" '
                 f'transform="rotate(-90 22 {height/2})">term frequency tf (log scale)</text>')

    # 1/rank reference line, anchored at the most frequent term
    ref = []
    steps = 400
    for i in range(steps + 1):
        rank = 1 + (n - 1) * i / steps
        tf = max_tf / rank
        if tf < 1:
            break
        ref.append(f"{x_of(rank):.1f},{y_of(tf):.1f}")
    parts.append(f'<polyline points="{" ".join(ref)}" fill="none" stroke="#9ca3af" '
                 f'stroke-width="1.4" stroke-dasharray="6 5"/>')

    # measured curve
    curve = " ".join(f"{x_of(rank):.1f},{y_of(tf):.1f}" for rank, tf, _ in points)
    parts.append(f'<polyline points="{curve}" fill="none" stroke="#1f77b4" stroke-width="1.8"/>')

    # annotate the five most frequent terms
    for rank, tf, term in points[:5]:
        x, y = x_of(rank), y_of(tf)
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3" fill="#d62728"/>')
        parts.append(f'<text x="{x+6:.1f}" y="{y-6:.1f}" font-size="11" fill="#333">{term}</text>')

    # legend
    parts.append(f'<line x1="{left+14}" y1="{top+18}" x2="{left+44}" y2="{top+18}" '
                 f'stroke="#1f77b4" stroke-width="2"/>')
    parts.append(f'<text x="{left+50}" y="{top+22}" font-size="12">measured tf</text>')
    parts.append(f'<line x1="{left+150}" y1="{top+18}" x2="{left+180}" y2="{top+18}" '
                 f'stroke="#9ca3af" stroke-width="1.5" stroke-dasharray="6 5"/>')
    parts.append(f'<text x="{left+186}" y="{top+22}" font-size="12">1/rank</text>')

    parts.append("</svg>")
    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(parts))
    print(f"svg: {args.out}  ({n} points)")


if __name__ == "__main__":
    main()
