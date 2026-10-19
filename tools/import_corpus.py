#!/usr/bin/env python3
"""import_corpus.py -- generic corpus importer: plain text -> manifest.

The engine (mse stats/count/build/query) is corpus-agnostic: it reads a
manifest plus the text files it references. `tools/extract.py` is the
Shakespeare-specific producer; this script is the generic one -- point it
at any directory of plain-text files and it produces a self-contained
corpus in the layout the engine expects:

    <root>/data/manifest.tsv      docid  kind  category  title  text  source  url  format
    <root>/data/text/<name>.txt   a copy of every input file

Documents are numbered in sorted order (deterministic); `title` is the
file name without extension; `source` records where the copy came from so
the corpus stays reproducible.

    python3 tools/import_corpus.py --input some/dir --root data2
    ./build/mse stats --manifest data2/data/manifest.tsv

Text files are used as-is (format=plain): the engine tokenizes ASCII
letter runs, skips '#' comment lines, and indexes every other line.
--kind/--category/--format override the manifest vocabulary; --sort-by
keeps|name picks the docID order.
"""

import argparse
import shutil
import sys
from pathlib import Path

MANIFEST_HEADER = ("docid", "kind", "category", "title", "text",
                   "source", "url", "format")


def main():
    parser = argparse.ArgumentParser(description="generic corpus importer")
    parser.add_argument("--input", required=True,
                        help="directory of .txt files to import")
    parser.add_argument("--root", required=True,
                        help="output corpus root: writes <root>/data/{manifest.tsv,text/}")
    parser.add_argument("--kind", default="text",
                        help="manifest `kind` vocabulary (default: text)")
    parser.add_argument("--category", default="General",
                        help="manifest `category` vocabulary (default: General)")
    parser.add_argument("--format", choices=("plain", "marked"), default="plain",
                        help="text-layer marker convention (default: plain)")
    parser.add_argument("--source-name", default=None,
                        help="record this provenance string instead of the input path")
    args = parser.parse_args()

    source_dir = Path(args.input)
    if not source_dir.is_dir():
        sys.exit(f"input directory not found: {source_dir}")
    files = sorted(p for p in source_dir.iterdir()
                   if p.is_file() and p.suffix.lower() == ".txt")
    if not files:
        sys.exit(f"no .txt files in {source_dir}")

    root = Path(args.root)
    text_dir = root / "data" / "text"
    text_dir.mkdir(parents=True, exist_ok=True)

    manifest = root / "data" / "manifest.tsv"
    with manifest.open("w", encoding="utf-8", newline="\n") as out:
        out.write("\t".join(MANIFEST_HEADER) + "\n")
        for docid, path in enumerate(files, start=1):
            title = path.stem.replace("_", " ")
            target = text_dir / f"{docid:04d}_{path.stem}{path.suffix}"
            shutil.copyfile(path, target)
            source = args.source_name or str(path)
            # url "-" = no canonical URL (local or offline material)
            out.write("\t".join((str(docid), args.kind, args.category, title,
                                 f"data/text/{target.name}", source, "-",
                                 args.format)) + "\n")

    print(f"imported : {len(files)} documents from {source_dir}")
    print(f"manifest : {manifest}")
    print(f"text     : {text_dir} ({args.format})")
    print(f"next     : ./build/mse stats --manifest {manifest}")


if __name__ == "__main__":
    main()
