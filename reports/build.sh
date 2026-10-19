#!/usr/bin/env bash
# Build all standalone test reports and copy the PDFs into pdf/.
#
# Requirements: XeLaTeX + latexmk with ctex/Fandol, DejaVu fonts, and the
# packages listed in style.tex (booktabs, tabularx, longtable, listings,
# fancyhdr, hyperref).
#
# Usage: bash reports/build.sh
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
mkdir -p build pdf

for name in 01-unit-tests 02-correctness 03-thresholds 04-scale; do
  echo "==> ${name}"
  latexmk -xelatex -interaction=nonstopmode -halt-on-error \
    -outdir=build "${name}.tex"
  cp "build/${name}.pdf" "pdf/${name}.pdf"
done

echo "PDFs written to reports/pdf/"
