#!/usr/bin/env python3
"""check_phrases.py -- independent brute-force cross-check of phrase queries.

`brute_force.py` validates the index structure (postings/positions). This
script validates the *query semantics* on top of it: for randomly sampled
phrases it scans the normalized token stream itself and compares the document
set with `mse query --phrase`.

Normalization deliberately mirrors the documented pipeline:
  tokenize -> drop stop words -> Porter stem (when the index was stemmed)
A phrase that contains stop words therefore degenerates exactly like the CLI
(and the note lines explain the dropped tokens). The phrase must occur as a
contiguous run in the filtered stream; order matters.

Usage:
    ./build/mse build --stop-words data/stopwords.txt --stem --index build/index.bin
    python3 tools/check_phrases.py --index build/index.bin \
        --stop-words data/stopwords.txt --stem --queries 300

Exit code 0 = every sampled phrase matched; 1 = at least one mismatch.
"""

import argparse
import csv
import json
import random
import subprocess
import sys
from collections import defaultdict

from check_wordcount import (line_kind, load_reference_porter,
                             normalize_contractions, tokenize)


def load_corpus(manifest_path, stop_path, stem_flag):
    """Return {docID: [(surface, normalized), ...]} for every body token."""
    stop = set()
    if stop_path:
        for line in open(stop_path, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#"):
                stop.add(line)

    porter = load_reference_porter() if stem_flag else None
    stem_cache = {}

    def normalize(token):
        if porter is None:
            return token
        if token not in stem_cache:
            stem_cache[token] = porter.stem(normalize_contractions(token))
        return stem_cache[token]

    docs = {}
    rows = list(csv.reader(open(manifest_path, encoding="utf-8"), delimiter="\t"))
    for row in rows[1:]:
        doc_id = int(row[0])
        pairs = []
        with open(row[4], encoding="utf-8") as handle:
            for line in handle.read().splitlines():
                if not line or line.startswith("#"):
                    continue
                if line_kind(line) != "body":
                    continue
                for token in tokenize(line):
                    if token in stop:
                        continue
                    pairs.append((token, normalize(token)))
        docs[doc_id] = pairs
    return docs


def expected_docs(docs, normalized_terms):
    """Documents containing the normalized phrase as a contiguous run."""
    hits = set()
    k = len(normalized_terms)
    if k == 0:
        return hits
    for doc_id, pairs in docs.items():
        terms = [term for _, term in pairs]
        for start in range(0, len(terms) - k + 1):
            if terms[start:start + k] == normalized_terms:
                hits.add(doc_id)
                break
    return hits


def run_query(binary, index, stop_path, phrase):
    cmd = [binary, "query", "--index", index, "--json", "--phrase", phrase]
    if stop_path:
        cmd += ["--stop-words", stop_path]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"query failed: {' '.join(cmd)}\n{proc.stderr}")
    payload = json.loads(proc.stdout)
    return {hit["doc"] for hit in payload["hits"]}, payload.get("notes", [])


def sample_phrases(docs, count, rng):
    """Random consecutive surface tokens (2-4), plus a few fixed phrases."""
    fixed = [
        "king richard", "good night", "my lord",
        "to be or not to be", "sweet sword moon",
    ]
    phrases = list(fixed)
    doc_ids = list(docs)
    while len(phrases) < count + len(fixed):
        doc_id = rng.choice(doc_ids)
        pairs = docs[doc_id]
        if len(pairs) < 2:
            continue
        length = rng.randint(2, min(4, len(pairs)))
        start = rng.randint(0, len(pairs) - length)
        surface = [surface for surface, _ in pairs[start:start + length]]
        phrases.append(" ".join(surface))
    return phrases


def normalize_phrase(phrase, stop_path, stem_flag):
    """Apply the same tokenize -> stop -> stem pipeline to a phrase."""
    stop = set()
    if stop_path:
        for line in open(stop_path, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#"):
                stop.add(line)
    porter = load_reference_porter() if stem_flag else None

    terms = []
    for token in tokenize(phrase):
        if token in stop:
            continue
        if porter is not None:
            token = porter.stem(normalize_contractions(token))
        terms.append(token)
    return terms


def main():
    parser = argparse.ArgumentParser(description="independent phrase-query check")
    parser.add_argument("--binary", default="./build/mse")
    parser.add_argument("--manifest", default="data/manifest.tsv")
    parser.add_argument("--index", default="build/index.bin")
    parser.add_argument("--stop-words", default=None)
    parser.add_argument("--stem", action="store_true")
    parser.add_argument("--queries", type=int, default=300)
    parser.add_argument("--seed", type=int, default=20261019)
    args = parser.parse_args()

    docs = load_corpus(args.manifest, args.stop_words, args.stem)
    rng = random.Random(args.seed)
    phrases = sample_phrases(docs, args.queries, rng)

    checked = 0
    mismatches = []
    skipped = 0
    for phrase in phrases:
        terms = normalize_phrase(phrase, args.stop_words, args.stem)
        if len(terms) < 2:
            skipped += 1  # degenerates to a word query / all stop words
            continue
        want = expected_docs(docs, terms)
        got, notes = run_query(args.binary, args.index, args.stop_words, phrase)
        checked += 1
        if got != want:
            mismatches.append((phrase, terms, sorted(want), sorted(got), notes))

    print(f"corpus   : {args.manifest} ({len(docs)} documents)")
    print(f"index    : {args.index}")
    print(f"phrases  : {len(phrases)} sampled, {checked} checked, {skipped} skipped")
    if mismatches:
        print(f"\nMISMATCHES ({len(mismatches)}):")
        for phrase, terms, want, got, notes in mismatches[:10]:
            print(f"  {phrase!r} -> {terms}")
            print(f"    expected docs: {want}")
            print(f"    actual docs  : {got}")
            if notes:
                print(f"    notes        : {notes}")
        return 1
    print("result   : OK -- every checked phrase matches the brute-force scan")
    return 0


if __name__ == "__main__":
    sys.exit(main())
