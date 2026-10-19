#!/usr/bin/env python3
"""plot_bptree_scale.py -- dependency-free SVG of the B+Tree scale experiment.

The measured points come from `mse bptree-bench` with 4 KiB pages:

    ./build/mse bptree-bench --keys 100000   --lookups 1000
    ./build/mse bptree-bench --keys 1000000  --lookups 1000
    ./build/mse bptree-bench --keys 10000000 --lookups 1000

The figure has two panels: file size (log-log, with the linear 15.1 B/key
extrapolation to 400M keys) and page reads per lookup (the cost the disk
dictionary pays when the term table cannot stay resident).

Usage:
    python3 tools/plot_bptree_scale.py --out figures/bptree_scale.svg
"""

import argparse
import math

# (keys, file bytes, tree height, page reads per random lookup)
MEASURED = [
    (100_000, 1_515_520, 2, 2.0),
    (1_000_000, 15_110_144, 3, 3.0),
    (10_000_000, 151_003_136, 3, 3.0),
    (30_000_000, 452_993_024, 3, 3.0),
    (50_000_000, 754_987_008, 4, 4.0),
]
# Extrapolation: 15.1 bytes/key, height 4 (the measured fan-out predicts it).
EXTRAPOLATED = (400_000_000, 400_000_000 * 15.1, 4, 4.0)


def main():
    parser = argparse.ArgumentParser(description="plot the B+Tree scale results")
    parser.add_argument("--out", default="figures/bptree_scale.svg")
    args = parser.parse_args()

    width, height = 1040, 470
    panel_w, panel_h = 470, 330
    left_margin, top_margin = 76, 78
    gap = 60

    def log_x(keys, x0, w, kmax):
        return x0 + w * (math.log10(keys) - math.log10(100_000)) / (
            math.log10(kmax) - math.log10(100_000))

    def log_y(value, y0, h, vmax, vmin):
        return y0 + h * (1.0 - (math.log10(value) - math.log10(vmin)) /
                         (math.log10(vmax) - math.log10(vmin)))

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'font-family="Helvetica,Arial,sans-serif">',
        f'<rect width="{width}" height="{height}" fill="white"/>',
        f'<text x="{width/2}" y="30" text-anchor="middle" font-size="17" '
        f'font-weight="bold">B+Tree dictionary scale (4 KiB pages)</text>',
        f'<text x="{width/2}" y="50" text-anchor="middle" font-size="12" fill="#555">'
        f'measured 100k / 1M / 10M / 30M / 50M keys; dashed point = 400M-key extrapolation</text>',
    ]

    # ---------------- left panel: file size ----------------
    x0, y0 = left_margin, top_margin
    w, h = panel_w, panel_h
    kmax = EXTRAPOLATED[0]
    bmin, bmax = 1_000_000, 10_000_000_000

    parts.append(f'<text x="{x0}" y="{y0-14}" font-size="13" font-weight="bold">'
                 f'file size vs. keys</text>')
    for exp in range(6, 11):  # 1 MB .. 10 GB
        value = 10 ** exp
        y = log_y(value, y0, h, bmax, bmin)
        parts.append(f'<line x1="{x0}" y1="{y:.1f}" x2="{x0+w}" y2="{y:.1f}" '
                     f'stroke="#e5e7eb"/>')
        label = f"{value // 1_000_000} MB" if value < 1_000_000_000 else f"{value // 1_000_000_000} GB"
        parts.append(f'<text x="{x0-8}" y="{y+4:.1f}" text-anchor="end" font-size="10">'
                     f'{label}</text>')
    for keys in (100_000, 1_000_000, 10_000_000, 100_000_000, 400_000_000):
        x = log_x(keys, x0, w, kmax)
        parts.append(f'<line x1="{x:.1f}" y1="{y0}" x2="{x:.1f}" y2="{y0+h}" '
                     f'stroke="#f3f4f6"/>')
        label = f"{keys // 1_000_000}M" if keys >= 1_000_000 else f"{keys // 1000}k"
        parts.append(f'<text x="{x:.1f}" y="{y0+h+18}" text-anchor="middle" '
                     f'font-size="10">{label}</text>')
    parts.append(f'<line x1="{x0}" y1="{y0}" x2="{x0}" y2="{y0+h}" stroke="#333"/>')
    parts.append(f'<line x1="{x0}" y1="{y0+h}" x2="{x0+w}" y2="{y0+h}" stroke="#333"/>')
    parts.append(f'<text x="{x0+w/2}" y="{y0+h+40}" text-anchor="middle" font-size="12">'
                 f'number of keys (log scale)</text>')
    parts.append(f'<text x="22" y="{y0+h/2}" text-anchor="middle" font-size="12" '
                 f'transform="rotate(-90 22 {y0+h/2})">file size (log scale)</text>')

    measured_pts = [(log_x(k, x0, w, kmax), log_y(b, y0, h, bmax, bmin))
                    for k, b, _, _ in MEASURED]
    parts.append(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x,y in measured_pts)}" '
                 f'fill="none" stroke="#1f77b4" stroke-width="2"/>')
    for (k, b, height, _), (x, y) in zip(MEASURED, measured_pts):
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="#1f77b4"/>')
        parts.append(f'<text x="{x+7:.1f}" y="{y-7:.1f}" font-size="10" fill="#333">'
                     f'{b/1_000_000:.1f} MB (h={height})</text>')

    ex_x = log_x(EXTRAPOLATED[0], x0, w, kmax)
    ex_y = log_y(EXTRAPOLATED[1], y0, h, bmax, bmin)
    last_x, last_y = measured_pts[-1]
    parts.append(f'<line x1="{last_x:.1f}" y1="{last_y:.1f}" x2="{ex_x:.1f}" y2="{ex_y:.1f}" '
                 f'stroke="#d62728" stroke-width="1.5" stroke-dasharray="6 5"/>')
    parts.append(f'<circle cx="{ex_x:.1f}" cy="{ex_y:.1f}" r="4" fill="none" '
                 f'stroke="#d62728" stroke-width="2"/>')
    parts.append(f'<text x="{ex_x-8:.1f}" y="{ex_y-8:.1f}" text-anchor="end" font-size="10" '
                 f'fill="#d62728">400M: ~6.0 GB (h=4)</text>')
    parts.append(f'<text x="{x0+12}" y="{y0+h-12}" font-size="11" fill="#555">'
                 f'measured slope: ~15.1 bytes/key</text>')

    # ---------------- right panel: page reads ----------------
    x1 = x0 + w + gap
    parts.append(f'<text x="{x1}" y="{y0-14}" font-size="13" font-weight="bold">'
                 f'page reads per random lookup (cache = 1 page)</text>')
    for reads in range(0, 6):
        y = y0 + h * (1.0 - reads / 5.0)
        parts.append(f'<line x1="{x1}" y1="{y:.1f}" x2="{x1+w}" y2="{y:.1f}" '
                     f'stroke="#e5e7eb"/>')
        parts.append(f'<text x="{x1-8}" y="{y+4:.1f}" text-anchor="end" font-size="10">'
                     f'{reads}</text>')
    for keys in (100_000, 1_000_000, 10_000_000, 100_000_000, 400_000_000):
        x = log_x(keys, x1, w, kmax)
        parts.append(f'<line x1="{x:.1f}" y1="{y0}" x2="{x:.1f}" y2="{y0+h}" '
                     f'stroke="#f3f4f6"/>')
        label = f"{keys // 1_000_000}M" if keys >= 1_000_000 else f"{keys // 1000}k"
        parts.append(f'<text x="{x:.1f}" y="{y0+h+18}" text-anchor="middle" '
                     f'font-size="10">{label}</text>')
    parts.append(f'<line x1="{x1}" y1="{y0}" x2="{x1}" y2="{y0+h}" stroke="#333"/>')
    parts.append(f'<line x1="{x1}" y1="{y0+h}" x2="{x1+w}" y2="{y0+h}" stroke="#333"/>')
    parts.append(f'<text x="{x1+w/2}" y="{y0+h+40}" text-anchor="middle" font-size="12">'
                 f'number of keys (log scale)</text>')
    parts.append(f'<text x="{x1-52}" y="{y0+h/2}" text-anchor="middle" font-size="12" '
                 f'transform="rotate(-90 {x1-52} {y0+h/2})">page reads</text>')

    read_pts = [(log_x(k, x1, w, kmax), y0 + h * (1.0 - r / 5.0))
                for k, _, _, r in MEASURED]
    parts.append(f'<polyline points="{" ".join(f"{x:.1f},{y:.1f}" for x,y in read_pts)}" '
                 f'fill="none" stroke="#2ca02c" stroke-width="2"/>')
    for (k, _, height, reads), (x, y) in zip(MEASURED, read_pts):
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="#2ca02c"/>')
        parts.append(f'<text x="{x+7:.1f}" y="{y-7:.1f}" font-size="10" fill="#333">'
                     f'{reads:.0f} (h={height})</text>')
    ex_x2 = log_x(EXTRAPOLATED[0], x1, w, kmax)
    ex_y2 = y0 + h * (1.0 - EXTRAPOLATED[3] / 5.0)
    last_x2, last_y2 = read_pts[-1]
    parts.append(f'<line x1="{last_x2:.1f}" y1="{last_y2:.1f}" x2="{ex_x2:.1f}" '
                 f'y2="{ex_y2:.1f}" stroke="#d62728" stroke-width="1.5" stroke-dasharray="6 5"/>')
    parts.append(f'<circle cx="{ex_x2:.1f}" cy="{ex_y2:.1f}" r="4" fill="none" '
                 f'stroke="#d62728" stroke-width="2"/>')
    parts.append(f'<text x="{ex_x2-8:.1f}" y="{ex_y2-8:.1f}" text-anchor="end" font-size="10" '
                 f'fill="#d62728">400M: 4 pages</text>')

    parts.append("</svg>")
    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(parts))
    print(f"svg: {args.out}")


if __name__ == "__main__":
    main()
