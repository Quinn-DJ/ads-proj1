#!/usr/bin/env python3
"""threshold_matrix.py -- task 4: how query thresholds change the results.

Runs a fixed query set (15 queries in six categories) through `mse query
--json`, varying one threshold at a time, and records for every (query,
setting): the result count, the Jaccard overlap with the no-threshold
baseline, how many terms were dropped (notes) and the wall-clock time.

Outputs (default):
    build/threshold_matrix.csv    every measurement (report Chapter 3)
    build/threshold_matrix.svg    result-count curves, one panel per threshold

Usage:
    ./build/mse build --stop-words data/stopwords.txt --stem
    python3 tools/threshold_matrix.py
"""

import argparse
import csv
import json
import subprocess
import sys
import time
from pathlib import Path

# (category, kind, query text) -- df ratios (from the stemmed/stop-word-free
# wordcount table) are noted so the sweep covers a real spread:
QUERIES = [
    ("high",   "word",   "love"),               # df 138/195 = 0.71
    ("high",   "word",   "sweet"),              # df  90/195 = 0.46
    ("mid",    "word",   "good"),               # df  57/195 = 0.29
    ("mid",    "word",   "night"),              # df  56/195 = 0.29
    ("low",    "word",   "sword"),              # df  37/195 = 0.19
    ("low",    "word",   "moon"),               # df  35/195 = 0.18
    ("name",   "word",   "banquo"),             # df   1/195
    ("name",   "word",   "osric"),              # df   1/195
    ("phrase", "phrase", "king richard"),       # both terms indexed
    ("phrase", "phrase", "good night"),         # both terms indexed
    ("phrase-stop", "phrase", "my lord"),       # "my" is a stop word (degenerates)
    ("bool",   "words",  "king queen"),         # 2 terms, AND by default
    ("bool",   "words",  "sweet sword moon"),   # 3 terms, spread df
    ("stopword", "word", "the"),                # dropped: not indexed
    ("stopword", "word", "of"),                 # dropped: not indexed
]

GRID = {
    "max-df": ["0.9", "0.7", "0.5", "0.3", "0.1"],
    "min-df": ["2", "5", "20", "50"],
    "min-match": ["1", "2", "3"],
    "top-x": ["5", "10", "20"],
}

MODE_FLAG = {"word": "--word", "phrase": "--phrase", "words": "--words"}

CATEGORY_COLORS = {
    "high": "#d62728",
    "mid": "#1f77b4",
    "low": "#2ca02c",
    "name": "#9467bd",
    "phrase": "#ff7f0e",
    "phrase-stop": "#8c564b",
    "bool": "#17becf",
    "stopword": "#7f7f7f",
}


def run_query(binary, index, stop_path, kind, text, extra):
    cmd = [binary, "query", "--index", index, "--json"]
    if stop_path:
        cmd += ["--stop-words", stop_path]
    cmd += [MODE_FLAG[kind], text]
    cmd += extra
    started = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    if proc.returncode != 0:
        sys.exit(f"query failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stderr}")
    return json.loads(proc.stdout), elapsed_ms


def doc_set(payload):
    return {hit["doc"] for hit in payload["hits"]}


def jaccard(a, b):
    if not a and not b:
        return 1.0
    return len(a & b) / len(a | b)


def main():
    parser = argparse.ArgumentParser(description="task 4 threshold sweep")
    parser.add_argument("--binary", default="./build/mse")
    parser.add_argument("--index", default="build/index.bin")
    parser.add_argument("--stop-words", default="data/stopwords.txt")
    parser.add_argument("--csv", default="build/threshold_matrix.csv")
    parser.add_argument("--svg", default="build/threshold_matrix.svg")
    args = parser.parse_args()

    rows = []
    for category, kind, text in QUERIES:
        baseline, ms = run_query(args.binary, args.index, args.stop_words, kind, text, [])
        base_set = doc_set(baseline)
        rows.append(dict(query=text, category=category, mode=kind, param="off",
                         value="-", results=len(base_set), jaccard=1.0,
                         notes=len(baseline["notes"]), ms=round(ms, 1)))
        for param, values in GRID.items():
            if param == "min-match" and kind != "words":
                continue  # only multi-word boolean queries have a k-of-n score
            for value in values:
                payload, ms = run_query(args.binary, args.index, args.stop_words,
                                        kind, text, [f"--{param}", value])
                got = doc_set(payload)
                rows.append(dict(query=text, category=category, mode=kind,
                                 param=param, value=value, results=len(got),
                                 jaccard=round(jaccard(base_set, got), 4),
                                 notes=len(payload["notes"]), ms=round(ms, 1)))

    with open(args.csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=[
            "query", "category", "mode", "param", "value",
            "results", "jaccard", "notes", "ms"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"csv : {args.csv}  ({len(rows)} measurements)")

    write_svg(args.svg, rows)
    print(f"svg : {args.svg}")

    # Terminal pivot tables: one per threshold, results(jaccard) per query.
    for param in GRID:
        print(f"\n== {param} ==")
        header = f"{'query':16s} {'base':>10s}" + "".join(
            f" {value:>10s}" for value in GRID[param])
        print(header)
        for category, kind, text in QUERIES:
            base = next(r for r in rows
                        if r["query"] == text and r["param"] == "off")
            cells = [f"{base['results']:>10d}"]
            for value in GRID[param]:
                match = next((r for r in rows if r["query"] == text and
                              r["param"] == param and r["value"] == value), None)
                cells.append("      n/a " if match is None
                             else f"{match['results']:>4d}({match['jaccard']:.2f})")
            print(f"{text:16s}" + "".join(cells))


def write_svg(path, rows):
    """Minimal dependency-free SVG: one panel per threshold, one polyline of
    result counts per query (color = category, label at the last point)."""
    params = list(GRID.keys())
    panel_w, panel_h = 560, 340
    width, height = panel_w * 2, panel_h * 2
    category_of = {text: category for category, _, text in QUERIES}

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" font-family="Helvetica,Arial,sans-serif" '
        f'font-size="10">',
        f'<rect width="{width}" height="{height}" fill="white"/>',
    ]
    for index, param in enumerate(params):
        ox, oy = (index % 2) * panel_w, (index // 2) * panel_h
        values = GRID[param]
        series = {}
        for row in rows:
            if row["param"] == param:
                series.setdefault(row["query"], {})[row["value"]] = row["results"]
        max_y = max((max(v.values()) for v in series.values() if v), default=1)
        max_y = max(max_y, 1)
        left, top, plot_w, plot_h = 46, 30, 300, 260

        parts.append(f'<g transform="translate({ox},{oy})">')
        parts.append(f'<text x="{left}" y="18" font-size="13" '
                     f'font-weight="bold">{param}</text>')
        parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" '
                     f'y2="{top + plot_h}" stroke="#333"/>')
        parts.append(f'<line x1="{left}" y1="{top + plot_h}" '
                     f'x2="{left + plot_w}" y2="{top + plot_h}" stroke="#333"/>')
        parts.append(f'<text x="{left - 4}" y="{top + 4}" '
                     f'text-anchor="end">{max_y}</text>')
        parts.append(f'<text x="{left - 4}" y="{top + plot_h}" '
                     f'text-anchor="end">0</text>')
        count = len(values)
        for i, value in enumerate(values):
            x = left + (plot_w * i / (count - 1) if count > 1 else plot_w / 2)
            parts.append(f'<text x="{x}" y="{top + plot_h + 14}" '
                         f'text-anchor="middle">{value}</text>')
        for query, by_value in series.items():
            color = CATEGORY_COLORS[category_of[query]]
            points = []
            for i, value in enumerate(values):
                if value not in by_value:
                    continue
                x = left + (plot_w * i / (count - 1) if count > 1 else plot_w / 2)
                y = top + plot_h * (1 - by_value[value] / max_y)
                points.append((x, y))
            if not points:
                continue
            polyline = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
            parts.append(f'<polyline points="{polyline}" fill="none" '
                         f'stroke="{color}" stroke-width="1.5"/>')
            last_x, last_y = points[-1]
            parts.append(f'<text x="{last_x + 4}" y="{last_y + 3}" '
                         f'fill="{color}">{query}</text>')
        parts.append('</g>')
    parts.append('</svg>')
    Path(path).write_text("\n".join(parts), encoding="utf-8")


if __name__ == "__main__":
    main()
