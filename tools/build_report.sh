#!/usr/bin/env bash
# build_report.sh -- convert report.md to LaTeX and compile report.pdf.
#
# Requirements (user-level installs are fine):
#   pandoc >= 3
#   TinyTeX / TeX Live with xelatex, latexmk, ctex, fandol, listings
#
# The Markdown report remains the source of truth. This script creates a
# LaTeX-specific copy (PNG figures, YAML title block) in build/, runs Pandoc to
# produce report.tex, and compiles it with XeLaTeX via latexmk. The generated
# report.pdf is copied to the project root.
#
# Usage: bash tools/build_report.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
MD="$BUILD/report-latex.md"
TEX="$ROOT/report.tex"
HEADER="$ROOT/tools/latex-header.tex"

for tool in pandoc xelatex latexmk; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "missing tool: $tool" >&2
    echo "install Pandoc and TinyTeX/TeX Live (xelatex + latexmk), then retry." >&2
    exit 1
  fi
done
mkdir -p "$BUILD"

# 1. LaTeX-specific Markdown:
#    - drop the first H1/H2 (they become the YAML title/subtitle),
#    - point images at the PNG renders (XeLaTeX cannot include SVG directly),
#    - replace status emoji that the Fandol/Latin fonts do not provide.
python3 - "$ROOT/report.md" "$MD" <<'PY'
import re
import sys
from pathlib import Path

src, dst = Path(sys.argv[1]), Path(sys.argv[2])
text = src.read_text(encoding="utf-8")

# Title block: the Markdown starts with "# title\n\n## subtitle\n\n".
text = re.sub(r"\A#\s+.*?\n+\#\#\s+.*?\n+", "", text, count=1)
text = (
    "---\n"
    'title: "迷你搜索引擎项目报告"\n'
    'subtitle: "基于莎士比亚全集的带词干化倒排索引"\n'
    'date: "2026-10-19"\n'
    "---\n\n" + text
)

# PDF figures are rasterized from the committed SVG sources.
text = text.replace("](figures/zipf.svg)", "](figures/zipf.png)")
text = text.replace("](figures/threshold_matrix.svg)", "](figures/threshold_matrix.png)")
text = text.replace("](figures/bptree_scale.svg)", "](figures/bptree_scale.png)")

# Fandol/Latin Modern do not carry emoji; use plain status words in the PDF.
text = text.replace("✅", "完成").replace("🚧", "进行中")

dst.write_text(text, encoding="utf-8")
PY

# 2. Pandoc: Markdown -> standalone LaTeX (ctexart for Chinese).
pandoc "$MD" \
  --from gfm --to latex --standalone \
  --output "$TEX" \
  --pdf-engine=xelatex \
  -V documentclass=ctexart \
  -V fontsize=11pt \
  -V geometry:margin=2.2cm \
  --toc --toc-depth=2 \
  --columns=100 \
  -H "$HEADER" \
  --resource-path="$ROOT"

# Pandoc emits pipe tables as natural-width longtables (l/r/c columns), so
# wide comparison tables run off the page. Replace every such column spec with
# equal p{} columns: wrapped text is readable, and no table can exceed the
# text width. (Tables with explicit widths are left untouched.)
python3 - "$TEX" <<'PY'
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")

def repl(match):
    spec = match.group(1)
    n = len(spec)
    width = f"{0.90 / n:.4f}\\linewidth"
    cols = " ".join([rf">{{\raggedright\arraybackslash}}p{{{width}}}"] * n)
    return rf"\begin{{longtable}}{{@{{}}{cols}@{{}}}}"

text, count = re.subn(r"\\begin\{longtable\}\[\]\{@\{\}([lrc]+)@\{\}\}",
                      repl, text)
path.write_text(text, encoding="utf-8")
print(f"latex: wrapped columns for {count} longtables")
PY

# 3. Compile (latexmk runs XeLaTeX as many times as the TOC needs).
latexmk -xelatex -interaction=nonstopmode -halt-on-error \
  -outdir="$BUILD" "$TEX"

cp "$BUILD/report.pdf" "$ROOT/report.pdf"
echo "written: $TEX"
echo "written: $ROOT/report.pdf"
