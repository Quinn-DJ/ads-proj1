# Catch2 v3 — vendored amalgamated build

Single-translation-unit distribution of Catch2, used by the unit tests in
`tests/`. The `mse` binary never links it: the graded programs stay
third-party-free (plan.md §16).

- Upstream: <https://github.com/catchorg/Catch2>
- Version: **v3.16.1**
- Files: `catch_amalgamated.hpp` / `catch_amalgamated.cpp` taken verbatim from
  the release assets of tag v3.16.1; `LICENSE.txt` from the same tag.
- Downloaded: 2026-10-15.
- License: Boost Software License 1.0 (see `LICENSE.txt`); listed in the
  report References.

Why vendored instead of FetchContent or a system package: the course
submission is an **offline anonymous zip** that must configure and build on
the grader's machine without network access. The amalgamated distribution is
exactly two files, so the zip stays lean and the build stays reproducible.

When `tools/comment_ratio.py` lands, it must exclude this directory:
third-party code is neither ours to comment nor ours to count.
