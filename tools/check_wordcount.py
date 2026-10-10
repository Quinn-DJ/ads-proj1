#!/usr/bin/env python3
"""check_wordcount.py -- independent re-implementation of `mse count`.

The C++ counter (src/count.cpp + src/tokenizer.cpp) and this script implement
the same specification *independently*; their outputs must be byte-identical:

    cmake -S . -B build && cmake --build build
    ./build/mse count --csv build/wordcount.csv
    python3 tools/check_wordcount.py > build/wordcount_py.csv
    diff build/wordcount.csv build/wordcount_py.csv && echo identical

It reads data/manifest.tsv and the referenced data/text/*.txt files, then
prints the same CSV (rank,term,tf,df,df_ratio,tf_share,stop_candidate) to
stdout. Flags mirror the tokenizer options of the C++ side.
"""

import argparse
import csv
import sys
from collections import defaultdict


def is_letter(c):
    """Same rule as src/tokenizer.cpp: ASCII letters only."""
    return ("a" <= c <= "z") or ("A" <= c <= "Z")


def tokenize(line):
    """ASCII-letter tokens; apostrophe kept only between letters."""
    tokens = []
    i, n = 0, len(line)
    while i < n:
        if not is_letter(line[i]):
            i += 1
            continue
        parts, j = [], i
        while j < n:
            c = line[j]
            if is_letter(c):
                parts.append(c.lower())
                j += 1
            elif c == "'" and j + 1 < n and is_letter(line[j + 1]):
                parts.append("'")
                j += 1
            else:
                break
        tokens.append("".join(parts))
        i = j
    return tokens


def line_kind(line):
    """Same classification as src/corpus.cpp (first AND last char must match)."""
    if len(line) >= 2 and line[0] == "[" and line[-1] == "]":
        return "heading"
    if len(line) >= 2 and line[0] == "(" and line[-1] == ")":
        return "stage"
    if line.startswith("@"):
        return "speaker"
    return "body"


def main():
    parser = argparse.ArgumentParser(description="independent word-count check")
    parser.add_argument("--manifest", default="data/manifest.tsv")
    parser.add_argument("--keep-headings", action="store_true")
    parser.add_argument("--keep-speakers", action="store_true")
    parser.add_argument("--keep-stage", action="store_true")
    args = parser.parse_args()

    rows = list(csv.reader(open(args.manifest, encoding="utf-8"), delimiter="\t"))
    header, documents_rows = rows[0], rows[1:]
    if header != ["docid", "kind", "category", "title", "text", "source", "url"]:
        sys.exit(f"unexpected manifest header: {header}")

    def wanted(kind):
        return (kind == "body"
                or (kind == "heading" and args.keep_headings)
                or (kind == "speaker" and args.keep_speakers)
                or (kind == "stage" and args.keep_stage))

    tf = defaultdict(int)
    df = defaultdict(int)
    documents = 0
    total_tokens = 0

    for row in documents_rows:
        documents += 1
        seen = set()
        with open(row[4], encoding="utf-8") as handle:
            for line in handle.read().splitlines():
                if not line or line.startswith("#"):
                    continue
                kind = line_kind(line)
                if not wanted(kind):
                    continue
                if kind != "body":
                    line = line[1:-1] if kind != "speaker" else line[1:]
                for token in tokenize(line):
                    tf[token] += 1
                    total_tokens += 1
                    seen.add(token)
        for token in seen:
            df[token] += 1

    print("rank,term,tf,df,df_ratio,tf_share,stop_candidate")
    for i, term in enumerate(sorted(tf, key=lambda t: (-tf[t], t)), start=1):
        df_ratio = df[term] / documents
        tf_share = tf[term] / total_tokens
        candidate = 1 if (df_ratio >= 0.75 and tf_share >= 0.001) else 0
        print(f"{i},{term},{tf[term]},{df[term]},{df_ratio:.6f},{tf_share:.6f},{candidate}")


if __name__ == "__main__":
    main()
