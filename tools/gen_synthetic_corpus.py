#!/usr/bin/env python3
"""gen_synthetic_corpus.py -- generate a deterministic plain-format corpus.

This is the build-side scale experiment helper for the Bonus question. It
writes a self-contained corpus (manifest + one text file per document) whose
size and vocabulary are controlled by flags, so the block builder can be
exercised at 500,000-document scale without downloading anything.

The tokens are synthetic identifiers (`w` + 8 hex digits). A global LCG makes
the stream deterministic; `--vocab` bounds the number of distinct identifiers,
which controls the dictionary size (the real 400M-term case is extrapolated,
not generated: it would need hundreds of GB).

Usage:
    python3 tools/gen_synthetic_corpus.py --root build/scale-corpus \
        --docs 500000 --tokens-per-doc 40 --vocab 5000000

The root layout matches the engine's contract:
    <root>/data/manifest.tsv
    <root>/data/text/000001.txt  (one plain line per document)
"""

import argparse
import time
from pathlib import Path

HEADER = "docid\tkind\tcategory\ttitle\ttext\tsource\turl\tformat"


def token_name(value):
    """Stable letter-only identifier for a vocabulary id.

    The engine's tokenizer treats digits as separators, so synthetic tokens
    must be letters only. `w` + seven base-26 letters covers 26^7 = 8.03B
    identifiers, enough for the full 400M-term target and for the smaller
    generated batches used in this repository.
    """
    letters = []
    for _ in range(7):
        letters.append(chr(ord("a") + value % 26))
        value //= 26
    return "w" + "".join(reversed(letters))


def main():
    parser = argparse.ArgumentParser(description="generate a synthetic plain corpus")
    parser.add_argument("--root", default="build/scale-corpus")
    parser.add_argument("--docs", type=int, default=500_000)
    parser.add_argument("--tokens-per-doc", type=int, default=40)
    parser.add_argument("--vocab", type=int, default=5_000_000)
    parser.add_argument("--force", action="store_true",
                        help="overwrite an existing root")
    args = parser.parse_args()

    root = Path(args.root)
    data = root / "data"
    text_dir = data / "text"
    if root.exists() and any(root.iterdir()) and not args.force:
        raise SystemExit(f"{root} is not empty; pass --force to overwrite")
    text_dir.mkdir(parents=True, exist_ok=True)

    started = time.perf_counter()
    state = 0x9E3779B97F4A7C15
    rows = [HEADER]
    total_tokens = 0
    total_bytes = 0

    for doc_id in range(1, args.docs + 1):
        tokens = []
        for _ in range(args.tokens_per_doc):
            # 64-bit LCG; high bits are the usable pseudo-random part.
            state = (state * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
            tokens.append(token_name((state >> 33) % args.vocab))
        line = " ".join(tokens)
        rel = f"data/text/{doc_id:06d}.txt"
        (root / rel).write_text(line + "\n", encoding="utf-8")
        total_bytes += len(line) + 1
        total_tokens += len(tokens)
        rows.append(f"{doc_id}\tplain\tsynthetic\tDoc {doc_id}\t{rel}\tsynthetic\t-\tplain")

    (data / "manifest.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    elapsed = time.perf_counter() - started
    print(f"root      : {root}")
    print(f"documents : {args.docs:,}")
    print(f"tokens    : {total_tokens:,}")
    print(f"vocab cap : {args.vocab:,}")
    print(f"text bytes: {total_bytes:,}")
    print(f"generate  : {elapsed:.2f} s")


if __name__ == "__main__":
    main()
