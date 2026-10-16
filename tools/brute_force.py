#!/usr/bin/env python3
"""brute_force.py -- independent cross-check of a built index (v1 format).

This is our "second implementation" for the whole indexing path:

  * it rebuilds the expected posting lists from data/text with the same
    *specified* pipeline (tokenize -> stop words -> Porter stemming via the
    reference NLTK implementation), and
  * it parses build/index.bin directly with a second implementation of the
    v1 binary format (see plan.md 7),

then compares every term on df, tf and each posting's (docID, tf, positions).

    ./build/mse build --stop-words data/stopwords.txt --stem
    python3 tools/brute_force.py --stop-words data/stopwords.txt --stem

Exit code 0 = the whole term table matches; mismatches print the first few
problems and exit with 1.
"""

import argparse
import csv
import struct
import sys
from collections import defaultdict

from check_wordcount import (line_kind, load_reference_porter,
                             normalize_contractions, tokenize)


class Cursor:
    """Little-endian reader with bounds checks (v1 format, plan.md 7)."""

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def require(self, n):
        if self.pos + n > len(self.data):
            raise ValueError("index file is truncated or corrupt")

    def u32(self):
        self.require(4)
        value = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def u64(self):
        self.require(8)
        value = struct.unpack_from("<Q", self.data, self.pos)[0]
        self.pos += 8
        return value

    def string(self):
        length = self.u32()
        self.require(length)
        text = self.data[self.pos:self.pos + length].decode("utf-8")
        self.pos += length
        return text

    def seek(self, offset):
        if offset > len(self.data):
            raise ValueError("index file is truncated or corrupt")
        self.pos = offset


def parse_index(path):
    data = open(path, "rb").read()
    if data[:4] != b"MSE1":
        sys.exit(f"{path}: bad magic (not an index file)")
    cursor = Cursor(data)
    cursor.pos = 4
    version = cursor.u32()
    if version != 1:
        sys.exit(f"{path}: unsupported index version {version}")
    flags = cursor.u32()
    doc_count = cursor.u32()
    term_count = cursor.u32()
    token_count = cursor.u64()

    docs = {}
    for _ in range(doc_count):
        doc_id = cursor.u32()
        kept = cursor.u32()
        title = cursor.string()
        docs[doc_id] = (title, kept)

    raw_terms = []
    for _ in range(term_count):
        term = cursor.string()
        df = cursor.u32()
        tf = cursor.u64()
        offset = cursor.u64()
        cursor.u32()  # byte size of the postings block, unused when seeking
        raw_terms.append((term, df, tf, offset))

    terms = {}
    for term, df, tf, offset in raw_terms:
        cursor.seek(offset)
        postings = []
        total = 0
        for _ in range(df):
            doc_id = cursor.u32()
            posting_tf = cursor.u32()
            positions = tuple(cursor.u32() for _ in range(posting_tf))
            postings.append((doc_id, posting_tf, positions))
            total += posting_tf
        if total != tf:
            sys.exit(f"{path}: corrupt: tf mismatch for term {term!r}")
        terms[term] = postings
    return {"flags": flags, "docs": docs, "tokens": token_count, "terms": terms}


def expected_terms(manifest_path, stop_path, stem_flag):
    """Rebuild the posting lists from the text layer (independent scan)."""
    stop = set()
    if stop_path:
        for line in open(stop_path, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#"):
                stop.add(line)

    porter = load_reference_porter() if stem_flag else None
    cache = {}

    def normalize(token):
        if porter is None:
            return token
        if token not in cache:
            cache[token] = porter.stem(normalize_contractions(token))
        return cache[token]

    rows = list(csv.reader(open(manifest_path, encoding="utf-8"), delimiter="\t"))
    terms = defaultdict(list)  # term -> [(docID, positions...)]
    for row in rows[1:]:
        doc_id = int(row[0])
        per_doc = defaultdict(list)  # term -> positions in the filtered stream
        position = 0
        with open(row[4], encoding="utf-8") as handle:
            for line in handle.read().splitlines():
                if not line or line.startswith("#"):
                    continue
                if line_kind(line) != "body":
                    continue
                for token in tokenize(line):
                    if token in stop:
                        continue
                    term = normalize(token)
                    per_doc[term].append(position)
                    position += 1
        for term, positions in per_doc.items():
            terms[term].append((doc_id, tuple(positions)))
    return terms


def main():
    parser = argparse.ArgumentParser(description="independent index check")
    parser.add_argument("--manifest", default="data/manifest.tsv")
    parser.add_argument("--index", default="build/index.bin")
    parser.add_argument("--stop-words", default=None)
    parser.add_argument("--stem", action="store_true")
    args = parser.parse_args()

    index = parse_index(args.index)
    expected = expected_terms(args.manifest, args.stop_words, args.stem)

    want_flags = (1 if args.stop_words else 0) | (2 if args.stem else 0)
    problems = []
    if index["flags"] != want_flags:
        problems.append(
            f"flags: file={index['flags']} expected={want_flags}")

    all_terms = sorted(set(index["terms"]) | set(expected))
    for term in all_terms:
        if term not in index["terms"]:
            problems.append(f"{term!r}: missing from the index")
            continue
        if term not in expected:
            problems.append(f"{term!r}: unexpectedly present in the index")
            continue
        got = sorted(index["terms"][term])
        want = sorted((doc_id, len(positions), positions)
                      for doc_id, positions in expected[term])
        if got != want:
            problems.append(f"{term!r}: posting mismatch")

    print(f"index    : {args.index}")
    print(f"documents: {len(index['docs'])}   terms: {len(index['terms'])}   "
          f"tokens: {index['tokens']}")
    print(f"compared : {len(all_terms)} terms "
          f"(df, tf, docIDs and positions for every posting)")
    if problems:
        print(f"\nMISMATCHES ({len(problems)}):")
        for problem in problems[:10]:
            print("  " + problem)
        return 1
    print("result   : OK -- independent scan and index file agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
