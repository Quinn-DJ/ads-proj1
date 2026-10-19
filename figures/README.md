# figures/ — report figures

All figures are generated from committed data; nothing here is hand-edited.

| File | Generator | Source data |
|------|-----------|-------------|
| `zipf.svg` | `tools/plot_zipf.py` | `build/wordcount.csv` (`mse count`) |
| `threshold_matrix.svg` | `tools/threshold_matrix.py` | `build/index.bin` + 15 fixed queries |
| `bptree_scale.svg` | `tools/plot_bptree_scale.py` | `mse bptree-bench` runs (100k / 1M / 10M / 30M / 50M keys) |
| `*.png` | 2x rasterization of the SVGs (Edge headless) | included by `report.tex` |

The PNGs are only needed because XeLaTeX cannot include SVG directly. Regenerate
them with a 2x device scale factor (see the wrapper HTML in `build/fig-*.html`);
the SVGs remain the source of truth for the HTML preview.

Regenerate:

```bash
./build/mse count --csv build/wordcount.csv --top 0
./build/mse build --stop-words data/stopwords.txt --stem --index build/index.bin
python3 tools/threshold_matrix.py
python3 tools/plot_zipf.py --csv build/wordcount.csv --out figures/zipf.svg
python3 tools/plot_bptree_scale.py --out figures/bptree_scale.svg
```

The report references these files by relative path (`figures/...`), so keep the
directory next to `report.tex` when compiling or packaging the submission.
