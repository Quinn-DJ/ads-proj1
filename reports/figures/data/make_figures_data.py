#!/usr/bin/env python3
"""make_figures_data.py -- CSV -> pgfplots .dat (+ meta macros) for reports/figures.

Reads THIS round's freshly measured CSVs and writes the data files the figure
.tex files consume. Pure standard library; deterministic (same CSV -> same
bytes), no timestamps, no personal data.

Inputs (defaults, relative to this script):
    ../../data/csv/tasks/wordcount-default.csv      (mse count, default pipeline)
    ../../data/csv/tasks/threshold_matrix.csv       (tools/threshold_matrix.py)
    ../../data/csv/verify/bptree-bench.csv          (mse bptree-bench, 4 KiB pages)

Outputs (in this directory):
    zipf.dat              rank tf ref            (log-spaced sample + ranks 1-10)
    zipf-meta.tex         \\ZipfTerms \\ZipfHapax \\ZipfHapaxPct
    entropy-{function,content,name}.dat  term rate
    entropy-threshold.dat term threshold        (flat 0.93 line over the x range)
    threshold-{max-df,min-df,min-match,top-x}.dat
                          level love sword kr_phrase kq_bool ssm_bool
    bptree-size.dat       keys file_bytes        (measured points)
    bptree-size-fit.dat   keys fit_bytes         (linear fit, to 4e8 keys)
    bptree-reads.dat      keys page_reads height_label
    bptree-meta.tex       \\BptSlope \\BptExtrapM \\BptExtrapGB \\BptExtrapH
                          \\BptExtrapPages \\BptExtrapKiB

Usage:
    python3 figures/data/make_figures_data.py                 # reports/ default
    python3 figures/data/make_figures_data.py --csv-root /path/to/reports/data/csv
    python3 figures/data/make_figures_data.py --out /tmp/dat  # scratch run
"""

import argparse
import csv
import math
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# configuration (figure spec, not measurements)
# ---------------------------------------------------------------------------
# Entropy lollipop: 8 function words, 5 content words, 2 proper nouns that are
# names of single-document characters (df == 1 -> entropy_rate_norm == 0).
ENTROPY_GROUPS = [
    ("function", ["the", "and", "i", "to", "of", "is", "it", "that"]),
    ("content", ["love", "king", "heart", "sweet", "man"]),
    ("name", ["osric", "banquo"]),
]
# Stop-word rule threshold the report draws as a horizontal line.
ENTROPY_THRESHOLD = 0.93

# Threshold sweep: (CSV query text, dat column name, plot legend label).
THRESHOLD_QUERIES = [
    ("love", "love", "love (word)"),
    ("sword", "sword", "sword (word)"),
    ("king richard", "kr_phrase", "king richard (phrase)"),
    ("king queen", "kq_bool", "king queen (bool)"),
    ("sweet sword moon", "ssm_bool", "sweet sword moon (bool)"),
]
# Panel -> levels, in display (left to right) order.  "off" is the CSV's
# no-threshold baseline row (param=off, value=-): for max-df it is the loosest
# setting (no cap) so it goes last; for min-df it is the loosest (no floor) so
# it goes first.  min-match/top-x baselines are not plotted because "off" has a
# query-dependent meaning there.
THRESHOLD_PANELS = {
    "max-df": ["0.1", "0.3", "0.5", "0.7", "0.9", "off"],
    "min-df": ["off", "2", "5", "20", "50"],
    "min-match": ["1", "2", "3"],
    "top-x": ["5", "10", "20"],
}
THRESHOLD_PARAMS = ["max-df", "min-df", "min-match", "top-x"]

# Zipf: how many log-spaced points to keep from the tail (plus ranks 1-10 all).
ZIPF_SAMPLES = 300

# B+Tree: the bench uses 4 KiB pages (mse bptree-bench default) and the synthetic
# keys are base-26 counters.  Both are fixed properties of the experiment, not
# measurements; the extrapolation is derived from them + the measured points.
PAGE_SIZE = 4096                 # bytes per B+Tree page
AVG_KEY_LEN = 6.0                # synthetic base-26 keys reach 26^6 ~ 3.1e8
BPT_EXTRAP_KEYS = 400_000_000    # the "400M distinct words" bonus target


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def read_csv(path):
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_lines(path, lines):
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def fmt(value, digits=6):
    """Fixed-point number, trailing zeros stripped (pgfplots friendly)."""
    text = f"{value:.{digits}f}".rstrip("0").rstrip(".")
    return text if text not in ("", "-0") else "0"


def fail(message):
    sys.exit(f"make_figures_data: {message}")


# ---------------------------------------------------------------------------
# fig:zipf -- rank/frequency distribution on log-log axes
# ---------------------------------------------------------------------------
def write_zipf(rows, out):
    points = sorted((int(r["rank"]), int(r["tf"])) for r in rows)
    if not points:
        fail("word-count CSV is empty")
    terms = len(points)
    tf_max = points[0][1]

    # Keep every rank up to 10 (the head is what the eye reads) and then a
    # log-spaced subset of the tail: log-log axes lose nothing visually.
    selected = {rank for rank, _ in points if rank <= 10}
    if terms > 10:
        lo, hi = math.log10(11.0), math.log10(float(terms))
        for i in range(ZIPF_SAMPLES):
            rank = int(round(10.0 ** (lo + (hi - lo) * i / (ZIPF_SAMPLES - 1))))
            selected.add(min(max(rank, 1), terms))
    tf_by_rank = dict(points)
    selected = sorted(r for r in selected if r in tf_by_rank)

    lines = ["rank tf ref"]
    for rank in selected:
        # 1/rank reference anchored at the most frequent term ("scale aligned").
        lines.append(f"{rank} {tf_by_rank[rank]} {fmt(tf_max / rank)}")
    write_lines(out / "zipf.dat", lines)

    hapax = sum(1 for _, tf in points if tf == 1)
    meta = [
        "% zipf-meta.tex -- generated by figures/data/make_figures_data.py",
        f"\\newcommand{{\\ZipfTerms}}{{{terms:,}}}".replace(",", "\\,"),
        f"\\newcommand{{\\ZipfHapax}}{{{hapax:,}}}".replace(",", "\\,"),
        f"\\newcommand{{\\ZipfHapaxPct}}{{{100.0 * hapax / terms:.1f}}}",
    ]
    write_lines(out / "zipf-meta.tex", meta)
    print(f"zipf: {terms} terms, {len(selected)} plotted points, "
          f"{hapax} hapax ({100.0 * hapax / terms:.1f}%)")


# ---------------------------------------------------------------------------
# fig:entropy -- entropy_rate_norm lollipop chart
# ---------------------------------------------------------------------------
def write_entropy(rows, out):
    by_term = {r["term"]: r for r in rows}
    plotted = []
    for group, words in ENTROPY_GROUPS:
        lines = ["term rate"]
        for word in words:
            row = by_term.get(word)
            if row is None:
                print(f"entropy: warning: term '{word}' not in CSV, skipped")
                continue
            rate = float(row["entropy_rate_norm"])
            lines.append(f"{word} {fmt(rate)}")
            plotted.append(word)
        if len(lines) == 1:
            fail(f"no term of group '{group}' found in the word-count CSV")
        write_lines(out / f"entropy-{group}.dat", lines)
    # The threshold line is drawn through every plotted term so that it spans
    # the whole (symbolic) x axis.
    write_lines(out / "entropy-threshold.dat",
                ["term threshold"] + [f"{w} {fmt(ENTROPY_THRESHOLD)}" for w in plotted])
    print(f"entropy: {len(plotted)} terms plotted, threshold {ENTROPY_THRESHOLD}")


# ---------------------------------------------------------------------------
# fig:threshold -- four-panel threshold sweep
# ---------------------------------------------------------------------------
def write_threshold(rows, out):
    # (query, param, value) -> results
    results = {}
    for row in rows:
        results[(row["query"], row["param"], row["value"])] = int(row["results"])

    # ``idx`` is the x coordinate (0..n-1) so that the axis never has to parse
    # levels like "0.1" as numbers; ``level`` is kept for human readers and the
    # xticklabels are taken from THRESHOLD_PANELS.
    header = ("idx level " +
              " ".join(column for _, column, _ in THRESHOLD_QUERIES))
    for param in THRESHOLD_PARAMS:
        lines = [header]
        for idx, level in enumerate(THRESHOLD_PANELS[param]):
            cells = []
            for query, _, _ in THRESHOLD_QUERIES:
                if level == "off":
                    value = results.get((query, "off", "-"))
                else:
                    value = results.get((query, param, level))
                cells.append("nan" if value is None else str(value))
            lines.append(f"{idx} {level} " + " ".join(cells))
        write_lines(out / f"threshold-{param}.dat", lines)
    print(f"threshold: {len(rows)} measurements, {len(THRESHOLD_QUERIES)} queries, "
          f"{len(THRESHOLD_PARAMS)} panels")


# ---------------------------------------------------------------------------
# fig:bptree -- file size / page reads vs key count
# ---------------------------------------------------------------------------
def write_bptree(rows, out):
    measured = sorted(
        (int(r["keys"]), int(r["file_bytes"]), int(r["height"]),
         float(r["page_reads"]), float(r["bytes_per_key"]))
        for r in rows
    )
    if len(measured) < 2:
        fail("bptree bench CSV needs at least two measured points")

    # Linear least squares fit file_bytes = slope*keys + intercept.
    n = len(measured)
    xs = [m[0] for m in measured]
    ys = [m[1] for m in measured]
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    sxx = sum((x - mean_x) ** 2 for x in xs)
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    slope = sxy / sxx
    intercept = mean_y - slope * mean_x
    size_ext = slope * BPT_EXTRAP_KEYS + intercept

    write_lines(out / "bptree-size.dat",
                ["keys file_bytes"] + [f"{k} {b}" for k, b, _, _, _ in measured])
    write_lines(out / "bptree-size-fit.dat",
                ["keys fit_bytes",
                 f"{measured[0][0]} {fmt(slope * measured[0][0] + intercept, 1)}",
                 f"{BPT_EXTRAP_KEYS} {fmt(size_ext, 1)}"])
    write_lines(out / "bptree-reads.dat",
                ["keys page_reads height_label"] +
                [f"{k} {fmt(p, 2)} h={h}" for k, _, h, p, _ in measured])

    # Height model: leaves hold ~PAGE_SIZE/bytes_per_key keys; inner pages fan
    # out by ~PAGE_SIZE/(key + 1-byte length + 4-byte child).  Checked against
    # every measured point, then used for the 4e8 extrapolation.
    leaf_keys = PAGE_SIZE / (sum(m[4] for m in measured) / n)
    fanout = PAGE_SIZE / (AVG_KEY_LEN + 1 + 4)

    def height_of(keys):
        return 1 + math.ceil(math.log(keys / leaf_keys) / math.log(fanout))

    expectations = [height_of(k) for k, _, _, _, _ in measured]
    if expectations != [m[2] for m in measured]:
        print("bptree: warning: height model does not reproduce measured heights: "
              f"model={expectations} measured={[m[2] for m in measured]}")
    extrap_height = height_of(BPT_EXTRAP_KEYS)
    extrap_pages = extrap_height  # page reads per lookup == tree height
    # Prefer the measured page_reads/height relation when it is not 1:1.
    ratios = [m[3] / m[2] for m in measured if m[2]]
    if ratios and all(abs(r - ratios[0]) < 1e-9 for r in ratios):
        extrap_pages = int(round(ratios[0] * extrap_height))

    # Extrapolated marker and the dashed extension lines: one row each, so the
    # .tex files never hard-code a measured/extrapolated number.
    write_lines(out / "bptree-extrap.dat",
                ["keys fit_bytes page_reads",
                 f"{BPT_EXTRAP_KEYS} {fmt(size_ext, 1)} {extrap_pages}"])
    write_lines(out / "bptree-reads-fit.dat",
                ["keys page_reads",
                 f"{measured[-1][0]} {fmt(measured[-1][3], 2)}",
                 f"{BPT_EXTRAP_KEYS} {extrap_pages}"])

    meta = [
        "% bptree-meta.tex -- generated by figures/data/make_figures_data.py",
        f"\\newcommand{{\\BptSlope}}{{{fmt(slope, 2)}}}",
        f"\\newcommand{{\\BptExtrapM}}{{{BPT_EXTRAP_KEYS // 1_000_000}}}",
        f"\\newcommand{{\\BptExtrapGB}}{{{fmt(size_ext / 1e9, 2)}}}",
        f"\\newcommand{{\\BptExtrapH}}{{{extrap_height}}}",
        f"\\newcommand{{\\BptExtrapPages}}{{{extrap_pages}}}",
        f"\\newcommand{{\\BptExtrapKiB}}{{{extrap_pages * PAGE_SIZE // 1024}}}",
        f"\\newcommand{{\\BptLeafKeys}}{{{leaf_keys:.0f}}}",
        f"\\newcommand{{\\BptFanout}}{{{fanout:.0f}}}",
    ]
    write_lines(out / "bptree-meta.tex", meta)
    print(f"bptree: fit {fmt(slope, 2)} B/key, "
          f"{BPT_EXTRAP_KEYS // 1_000_000}M keys -> {fmt(size_ext / 1e9, 1)} GB, "
          f"height {extrap_height} ({extrap_pages} page reads = "
          f"{extrap_pages * PAGE_SIZE // 1024} KiB)")


# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="generate figure data files")
    here = Path(__file__).resolve().parent
    parser.add_argument("--csv-root", type=Path,
                        default=here.parents[1] / "data" / "csv",
                        help="directory holding tasks/ and verify/ CSVs")
    parser.add_argument("--out", type=Path, default=here,
                        help="output directory (default: figures/data/)")
    args = parser.parse_args()

    wordcount = args.csv_root / "tasks" / "wordcount-default.csv"
    threshold = args.csv_root / "tasks" / "threshold_matrix.csv"
    bptree = args.csv_root / "verify" / "bptree-bench.csv"
    for path in (wordcount, threshold, bptree):
        if not path.is_file():
            fail(f"missing input CSV: {path}")
    args.out.mkdir(parents=True, exist_ok=True)

    write_zipf(read_csv(wordcount), args.out)
    write_entropy(read_csv(wordcount), args.out)
    write_threshold(read_csv(threshold), args.out)
    write_bptree(read_csv(bptree), args.out)
    print(f"output: {args.out}")


if __name__ == "__main__":
    main()
