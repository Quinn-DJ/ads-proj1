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
stdout. Flags mirror the C++ side's tokenizer options, --stop-words and
--stem. When --stem is given, the stemming itself is done by the *reference*
NLTK implementation (reference/nltk-porter-3.9.1.py, MARTIN_EXTENSIONS
mode = the author's canonical C behaviour) after the same contraction
normalization our C++ stemmer applies, so the comparison validates opposite
sides: our C++ implementation vs NLTK.
"""

import argparse
import csv
import importlib.util
import os
import sys
import types
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


# --------------------------------------------------------------------------
# stemming (only used with --stem): reference NLTK Porter + our contraction
# normalization, duplicated here as an independent implementation of the
# same specification.
# --------------------------------------------------------------------------

CONTRACTION_SUFFIXES = ("n't", "'ll", "'re", "'ve", "'s", "'d", "'t", "'m")


def normalize_contractions(token):
    for suffix in CONTRACTION_SUFFIXES:
        if len(token) > len(suffix) and token.endswith(suffix):
            token = token[: -len(suffix)]
            break
    return token.replace("'", "")


def load_reference_porter():
    """Import reference/nltk-porter-3.9.1.py standalone (tiny shim for
    nltk.stem.api.StemmerI, the only import it needs)."""
    if "nltk" not in sys.modules:
        nltk_mod = types.ModuleType("nltk")
        nltk_mod.__path__ = []
        stem_mod = types.ModuleType("nltk.stem")
        stem_mod.__path__ = []
        api_mod = types.ModuleType("nltk.stem.api")

        class StemmerI:  # noqa: N801 - mirrors the NLTK name
            def stem(self, token):
                raise NotImplementedError

        api_mod.StemmerI = StemmerI
        sys.modules["nltk"] = nltk_mod
        sys.modules["nltk.stem"] = stem_mod
        sys.modules["nltk.stem.api"] = api_mod
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, os.pardir, "reference", "nltk-porter-3.9.1.py")
    spec = importlib.util.spec_from_file_location("reference_porter", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.PorterStemmer(mode=module.PorterStemmer.MARTIN_EXTENSIONS)


def main():
    parser = argparse.ArgumentParser(description="independent word-count check")
    parser.add_argument("--manifest", default="data/manifest.tsv")
    parser.add_argument("--stop-words", default=None,
                        help="drop these words before counting (same file as "
                             "`mse count --stop-words`)")
    parser.add_argument("--stem", action="store_true",
                        help="Porter-stem the tokens (reference NLTK "
                             "implementation; same flags as `mse count --stem`)")
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

    stop = set()
    if args.stop_words:
        for line in open(args.stop_words, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#"):
                stop.add(line)

    stem_token = None
    if args.stem:
        porter = load_reference_porter()
        stem_cache = {}

        def stem_token(word):  # memoized: each distinct form stems once
            if word not in stem_cache:
                stem_cache[word] = porter.stem(normalize_contractions(word))
            return stem_cache[word]

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
                    if token in stop:
                        continue
                    if stem_token is not None:
                        token = stem_token(token)
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
