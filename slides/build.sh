#!/usr/bin/env bash
# Build the slides: ctexbeamer + XeLaTeX.
#
# Usage: bash slides/build.sh
# Output: slides/mse_slides.pdf (intermediates in slides/build/)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
mkdir -p build

latexmk -xelatex -interaction=nonstopmode -halt-on-error \
  -file-line-error -outdir=build mse_slides.tex

cp build/mse_slides.pdf mse_slides.pdf
echo "PDF: $(pwd)/mse_slides.pdf"
