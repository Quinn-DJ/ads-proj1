#!/usr/bin/env bash
# Build the project report: article + ctex, compiled with XeLaTeX.
#
# Usage: bash reports/build.sh
# Output: reports/report.pdf (intermediates in reports/build/)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
mkdir -p build

latexmk -xelatex -interaction=nonstopmode -halt-on-error \
  -file-line-error -outdir=build report.tex

cp build/report.pdf report.pdf
echo "PDF: $(pwd)/report.pdf"
