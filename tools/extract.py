#!/usr/bin/env python3
"""extract.py -- build the text layer and document manifest for proj1.

Input : data/raw/           HTML mirror of shakespeare.mit.edu (see fetch_corpus.sh)
Output: data/text/*.txt     one file per document (line-based, format below)
        data/manifest.tsv   docID, kind, category, title, text, source, url

The document set is exactly what the site's own index page lists:
  37 plays  (each <work>/full.html)
  154 sonnets (each Poetry/sonnet.<roman>.html)
  4 poems   (LoversComplaint, RapeOfLucrece, VenusAndAdonis, elegy)
  = 195 documents, docID 1..195 in the order they appear on the index page
    (Sonnets expand in place: I, II, ... CLIV).

Text format (one line = one unit; header lines start with '#'):
  [ACT I]            heading  (ACT/SCENE titles, poem headings, from <h1>-<h3>)
  (Exit Ghost)       stage    (stage directions, from <i>...</i>)
  @BERNARDO          speaker  (speech labels, from <b>...</b>)
  Who's there?       body     (verse lines / prose, original punctuation kept)

In the Moby edition, inline stage directions are written in square brackets
inside the verse ("[Aside]  Heaven, that I had thy head!", "[Sings]").  The
extractor relocates a *leading* "[...]" fragment to its own stage line:
  (Aside)
  Heaven, that I had thy head!
so that "aside"/"sings"/addressee names never leak into the searchable text.
Editorial insertions *inside* a line ("[Censorinus,]", "[   ]") stay put.

HTML hygiene rules (verified against the corpus):
  * <head>, the top navigation table (td.play / td.nav) and <a href> links are dropped
  * entities are decoded; whitespace inside a line is collapsed to single spaces
  * in the one table-based text (elegy) the line-number cells (digits only) are dropped
"""

import argparse
import re
from html.parser import HTMLParser
from pathlib import Path

BASE_URL = "http://shakespeare.mit.edu/"
HEAD_TAGS = {"h1", "h2", "h3", "h4", "h5", "h6"}
BLOCK_OPEN = {"p", "blockquote", "table", "tr", "td", "th", "center",
              "address", "li", "dt", "dd", "div"} | HEAD_TAGS
BREAK_TAGS = {"br", "hr"}
IGNORED_TAGS = {"body", "html", "meta", "link", "title", "img", "font"}


# --------------------------------------------------------------------------
# page parser: HTML -> list of text lines
# --------------------------------------------------------------------------

class PageParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []        # [(tag, attrs)]
        self.buf = []          # text of the line being built
        self.lines = []        # finished output lines
        self.in_head = False
        self.bracket_fragments = 0   # leading [stage directions] relocated from verse
        self.skipped_artifacts = 0   # bogus headings found in the source (see _flush)

    # -- context helpers ---------------------------------------------------

    def _skipping(self):
        """True while inside regions that are not document text."""
        if self.in_head:
            return True
        for tag, attrs in self.stack:
            if tag == "a" and "href" in attrs:
                return True
            if tag == "td" and attrs.get("class") in ("play", "nav"):
                return True
        return False

    def _mode(self):
        """Innermost interesting context decides how the line is marked."""
        for tag, attrs in reversed(self.stack):
            if tag in HEAD_TAGS:
                return "heading"
            if tag == "i":
                return "stage"
            if tag == "b":
                return "speaker"
            if tag == "a" and "name" in attrs:
                return "verse"
        return "prose"

    def _flush(self):
        text = " ".join("".join(self.buf).split())
        self.buf.clear()
        if not text:
            return
        # elegy uses a table; its line-number cells are digits only -> drop
        if text.isdigit() and any(t == "td" for t, _ in self.stack):
            return
        mode = self._mode()
        if mode == "heading":
            # 2henryiv/full.html contains a bogus "<h3>None</h3>" where the
            # ACT I heading should be (Moby-conversion artifact). Drop it;
            # inferring "ACT I" would mean inventing data we cannot verify.
            if text == "None":
                self.skipped_artifacts += 1
                return
            self.lines.append(f"[{text}]")
        elif mode == "stage":
            self.lines.append(f"({text})")
        elif mode == "speaker":
            self.lines.append("@" + text.rstrip(":"))
        else:
            self._emit_body(text)

    def _emit_body(self, text):
        """Emit verse/prose text, relocating leading [stage fragments]."""
        while text.startswith("["):
            close = text.find("]")
            if close == -1:
                break                    # no closing bracket: keep as plain text
            fragment = text[1:close].strip()
            if fragment:
                self.lines.append(f"({fragment})")
                self.bracket_fragments += 1
            text = text[close + 1:].strip()
            if not text:
                return
        self.lines.append(text)

    # -- tag handling ------------------------------------------------------

    def _auto_close_before(self, tag):
        """Close elements whose end tag was omitted (HTML 4 style)."""
        if tag == "td":
            while self.stack and self.stack[-1][0] == "td":
                self._flush()
                self.stack.pop()
        elif tag == "tr":
            while self.stack and self.stack[-1][0] == "td":
                self._flush()
                self.stack.pop()
            while self.stack and self.stack[-1][0] == "tr":
                self._flush()
                self.stack.pop()
        elif tag in ("i", "b"):
            while self.stack and self.stack[-1][0] == tag:
                self._flush()
                self.stack.pop()
        elif tag in BLOCK_OPEN:
            while self.stack and self.stack[-1][0] == "p":
                self._flush()
                self.stack.pop()

    def handle_starttag(self, tag, attrs):
        attrs = {k.lower(): (v or "") for k, v in attrs}
        if tag == "head":
            self.in_head = True
            return
        if tag == "body":
            # elegy.html has a malformed "</TITLE" so "</head>" never fires;
            # <body> is then the reliable end-of-head signal
            self.in_head = False
            return
        if self.in_head:
            return
        if tag in IGNORED_TAGS:
            return                       # transparent or ignored (never pushed)
        if tag in BREAK_TAGS:
            self._flush()
            return
        self._auto_close_before(tag)
        self.stack.append((tag, attrs))

    def handle_startendtag(self, tag, attrs):
        if tag in BREAK_TAGS:
            self._flush()

    def handle_endtag(self, tag):
        if tag == "head":
            self.in_head = False
            return
        if self.in_head:
            return
        if tag in IGNORED_TAGS:
            if tag in ("body", "html"):
                self._flush()
            return
        self._flush()                    # flush while the closing tag is still on the stack
        for j in range(len(self.stack) - 1, -1, -1):
            if self.stack[j][0] == tag:
                del self.stack[j:]
                break

    def handle_data(self, data):
        if self._skipping():
            return
        self.buf.append(data)


# --------------------------------------------------------------------------
# link parsers: ordered (category, links) from the site homepage;
#               plain link list for the sonnet index page
# --------------------------------------------------------------------------

class LinkParser(HTMLParser):
    """Collect (href, text) of every <a href> in document order."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.cur = None

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            d = {k.lower(): (v or "") for k, v in attrs}
            if "href" in d:
                self.cur = [d["href"], ""]

    def handle_data(self, data):
        if self.cur is not None:
            self.cur[1] += data

    def handle_endtag(self, tag):
        if tag == "a" and self.cur is not None:
            self.links.append(self.cur)
            self.cur = None

class IndexParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.row = -1
        self.col = -1
        self.cells = []          # dicts: row, col, h2, links
        self.cur = None
        self.in_h2 = False
        self.link = None

    def _close_cell(self):
        if self.cur is not None:
            self.cells.append(self.cur)
            self.cur = None

    def handle_starttag(self, tag, attrs):
        d = {k.lower(): (v or "") for k, v in attrs}
        if tag == "tr":
            self._close_cell()
            self.row += 1
            self.col = -1
        elif tag in ("td", "th"):
            self._close_cell()
            self.col += 1
            self.cur = {"row": self.row, "col": self.col, "h2": "", "links": []}
        elif tag == "h2":
            self.in_h2 = True
        elif tag == "a" and "href" in d and self.cur is not None:
            self.link = [d["href"], ""]

    def handle_data(self, data):
        if self.in_h2 and self.cur is not None:
            self.cur["h2"] += data
        if self.link is not None:
            self.link[1] += data

    def handle_endtag(self, tag):
        if tag == "h2":
            self.in_h2 = False
        elif tag == "a" and self.link is not None:
            if self.cur is not None:
                self.cur["links"].append(self.link)
            self.link = None
        elif tag in ("td", "th", "tr"):
            self._close_cell()


# --------------------------------------------------------------------------
# document list
# --------------------------------------------------------------------------

def roman_to_int(s):
    values = {"I": 1, "V": 5, "X": 10, "L": 50, "C": 100, "D": 500, "M": 1000}
    total = 0
    for i, ch in enumerate(s):
        v = values[ch]
        if i + 1 < len(s) and values[s[i + 1]] > v:
            total -= v
        else:
            total += v
    return total


def clean_text(s):
    return " ".join(s.split())


def slugify(title):
    s = re.sub(r"[^a-z0-9]+", "_", title.lower())
    return s.strip("_")[:48].rstrip("_") or "doc"


def build_doc_list(raw_dir):
    """Return [(kind, category, title, source_relpath), ...] in site order."""
    index_html = (raw_dir / "index.html").read_text(encoding="latin-1")
    parser = IndexParser()
    parser.feed(index_html)

    # find the row of category headers and the row of link lists right below it
    category_row = None
    for cell in parser.cells:
        if clean_text(cell["h2"]):
            category_row = cell["row"]
            break
    if category_row is None:
        raise SystemExit("could not find the category table in index.html")

    header_cat = {c["col"]: clean_text(c["h2"])
                  for c in parser.cells if c["row"] == category_row and clean_text(c["h2"])}
    link_cells = sorted([c for c in parser.cells if c["row"] == category_row + 1],
                        key=lambda c: c["col"])

    docs = []
    for cell in link_cells:
        category = header_cat.get(cell["col"], "Other")
        for href, text in cell["links"]:
            title = clean_text(text)
            if href.endswith("/index.html"):
                play_dir = href[: -len("/index.html")]
                docs.append(("play", category, title, f"{play_dir}/full.html"))
            elif category == "Poetry" and href.endswith("sonnets.html"):
                # expand the sonnet index page in roman-numeral order
                sonnet_html = (raw_dir / href).read_text(encoding="latin-1")
                sp = LinkParser()
                sp.feed(sonnet_html)
                sonnets = []
                for shref, _ in sp.links:
                    m = re.fullmatch(r"sonnet\.([IVXLCDM]+)\.html", shref)
                    if m:
                        sonnets.append(m.group(1))
                if len(sonnets) != 154:
                    raise SystemExit(f"expected 154 sonnets, found {len(sonnets)}")
                for roman in sorted(sonnets, key=roman_to_int):
                    docs.append(("sonnet", "Poetry", f"Sonnet {roman}",
                                 f"Poetry/sonnet.{roman}.html"))
            elif category == "Poetry" and href.endswith(".html"):
                docs.append(("poem", "Poetry", title, href))
            # anything else (external links) is not part of the document set
    return docs


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    here = Path(__file__).resolve().parent
    proj = here.parent

    ap = argparse.ArgumentParser(description="build data/text/ + data/manifest.tsv")
    ap.add_argument("--raw", type=Path, default=proj / "data/raw")
    ap.add_argument("--text", type=Path, default=proj / "data/text")
    ap.add_argument("--manifest", type=Path, default=proj / "data/manifest.tsv")
    args = ap.parse_args()

    docs = build_doc_list(args.raw)
    args.text.mkdir(parents=True, exist_ok=True)

    # refresh only our own generated files
    for old in args.text.glob("[0-9][0-9][0-9][0-9]_*.txt"):
        old.unlink()

    rows = []
    warnings = []
    bracket_total = 0
    artifact_total = 0
    for i, (kind, category, title, src_rel) in enumerate(docs, start=1):
        src = args.raw / src_rel
        parser = PageParser()
        parser.feed(src.read_text(encoding="latin-1"))
        parser.close()
        lines = parser.lines
        bracket_total += parser.bracket_fragments
        artifact_total += parser.skipped_artifacts
        if not lines:
            warnings.append(f"{src_rel}: no text extracted")
        for ln in lines:
            if "<" in ln or ">" in ln or re.search(r"&[a-zA-Z#0-9]+;", ln):
                warnings.append(f"{src_rel}: possible markup left: {ln[:60]!r}")

        out_name = f"{i:04d}_{slugify(title)}.txt"
        out_path = args.text / out_name
        header = [
            f"# docID: {i}",
            f"# title: {title}",
            f"# kind: {kind} | category: {category}",
            f"# source: {src_rel}",
            f"# url: {BASE_URL}{src_rel}",
            "",
        ]
        out_path.write_text("\n".join(header + lines) + "\n", encoding="utf-8")

        rows.append((i, kind, category, title,
                     str(out_path.relative_to(proj)),
                     str(src.relative_to(proj)),
                     BASE_URL + src_rel))

    with open(args.manifest, "w", encoding="utf-8") as fh:
        fh.write("docid\tkind\tcategory\ttitle\ttext\tsource\turl\n")
        for r in rows:
            fh.write("\t".join(str(x) for x in r) + "\n")

    (args.text / "README.md").write_text(
        "# data/text/ 格式说明（由 tools/extract.py 生成，勿手改）\n\n"
        "每个文档一个 `.txt`，一行一个单元，`#` 开头是头部注释：\n\n"
        "- `[ACT I]` / `[SCENE I. ...]`：标题行（H1–H3），索引时可用开关决定是否保留\n"
        "- `(Exit Ghost)`：舞台指示（<i>），以及从诗句中拆出的方括号提示（`[Aside]`、`[Sings]`），默认过滤，可开关\n"
        "- `@BERNARDO`：说话人标签（<b>），默认过滤，可开关\n"
        "- 其余行：台词/诗行/散文正文，保留原标点\n\n"
        "重新生成：`python3 tools/extract.py`（覆盖 `[0-9][0-9][0-9][0-9]_*.txt` 与 README 本身）。\n",
        encoding="utf-8")

    kinds = {}
    total_lines = 0
    total_words = 0
    for i, kind, category, title, tpath, spath, url in rows:
        text = (proj / tpath).read_text(encoding="utf-8")
        body = [ln for ln in text.splitlines() if ln and not ln.startswith("#")]
        kinds[kind] = kinds.get(kind, 0) + 1
        total_lines += len(body)
        total_words += sum(len(ln.split()) for ln in body)

    print(f"docs: {len(rows)}  ({', '.join(f'{k}={v}' for k, v in sorted(kinds.items()))})")
    print(f"lines: {total_lines}   words: {total_words}")
    print(f"bracket stage fragments relocated from verse: {bracket_total}")
    print(f"source-generator artifacts skipped: {artifact_total}")
    print(f"manifest: {args.manifest}")
    print(f"text: {args.text}")
    if warnings:
        print(f"\nWARNINGS ({len(warnings)}):")
        for w in warnings[:20]:
            print("  " + w)
    else:
        print("no extraction warnings")


if __name__ == "__main__":
    main()
