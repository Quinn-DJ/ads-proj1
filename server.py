#!/usr/bin/env python3

import json
import os
import subprocess
import csv
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse


ROOT = os.path.dirname(os.path.abspath(__file__))
MSE = os.path.join(ROOT, "build", "mse")
INDEXES = {
    "shakespeare": os.path.join(ROOT, "build", "index.bin"),
    "rfc": os.path.join(ROOT, "build", "rfc-index.bin"),
}
MANIFESTS = {
    "shakespeare": os.path.join(ROOT, "data", "manifest.tsv"),
    "rfc": os.path.join(ROOT, "data2", "data", "manifest.tsv"),
}


def run_mse(command):
    result = subprocess.run(
        command,
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=20,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "mse query failed")
    return result.stdout


def load_manifest(corpus):
    documents = {}
    with open(MANIFESTS[corpus], encoding="utf-8") as file:
        for row in csv.DictReader(file, delimiter="\t"):
            documents[int(row["docid"])] = row
    return documents


def excerpt_lines(output):
    return [
        line[8:].strip()
        for line in output.splitlines()
        if line.startswith("      > ")
    ]


def word_excerpts(term, corpus):
    command = [
        MSE,
        "query",
        "--index",
        INDEXES[corpus],
        "--manifest",
        MANIFESTS[corpus],
        "--word",
        term,
        "--top-x",
        "0",
    ]
    if corpus == "shakespeare":
        command += [
            "--stop-words",
            os.path.join(ROOT, "data", "stopwords.txt"),
        ]

    hits = json.loads(run_mse(command + ["--json"]))["hits"]
    excerpts = excerpt_lines(run_mse(command + ["--pretty"]))
    return {
        hit["doc"]: excerpts[index]
        for index, hit in enumerate(hits)
        if index < len(excerpts)
    }


def query_mse(query, mode, corpus):
    command = [
        MSE,
        "query",
        "--index",
        INDEXES[corpus],
        "--manifest",
        MANIFESTS[corpus],
        "--top-x",
        "20",
    ]
    if mode == "phrase":
        command += ["--phrase", query]
    elif mode == "words-and":
        command += ["--words", query]
    elif mode == "words-or":
        command += ["--words", query, "--or"]
    else:
        command += ["--word", query]

    if corpus == "shakespeare":
        command += [
            "--stop-words",
            os.path.join(ROOT, "data", "stopwords.txt"),
        ]

    data = json.loads(run_mse(command + ["--json"]))
    pretty = run_mse(command + ["--pretty"])
    excerpts = excerpt_lines(pretty)
    documents = load_manifest(corpus)
    terms = data["terms"]
    evidence = {}

    if mode in ("words-and", "words-or") and len(terms) > 1:
        for term in terms:
            evidence[term] = word_excerpts(term, corpus)

    for index, hit in enumerate(data["hits"]):
        document = documents.get(hit["doc"], {})
        hit["title"] = document.get("title", "")
        hit["kind"] = document.get("kind", "")
        hit["category"] = document.get("category", "")
        hit["source"] = document.get("source", "")
        hit["url"] = document.get("url", "")
        hit["excerpt"] = excerpts[index] if index < len(excerpts) else ""
        hit["excerpts"] = [
            {"term": term, "text": evidence[term][hit["doc"]]}
            for term in terms
            if term in evidence and hit["doc"] in evidence[term]
        ]

    data["corpus"] = corpus
    return data


class Handler(BaseHTTPRequestHandler):
    def send_body(self, status, body, content_type):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_body(status, body, "application/json; charset=utf-8")

    def do_GET(self):
        url = urlparse(self.path)

        if url.path == "/":
            with open(os.path.join(ROOT, "index.html"), "rb") as file:
                self.send_body(200, file.read(), "text/html; charset=utf-8")
            return

        if url.path != "/search":
            self.send_json({"error": "not found"}, 404)
            return

        params = parse_qs(url.query)
        query = params.get("q", [""])[0].strip()
        mode = params.get("mode", ["word"])[0]
        corpus = params.get("corpus", ["shakespeare"])[0]

        if not query:
            self.send_json({"error": "query is required"}, 400)
            return
        if mode not in ("word", "phrase", "words-and", "words-or"):
            self.send_json({"error": "unknown mode"}, 400)
            return
        if corpus not in INDEXES:
            self.send_json({"error": "unknown corpus"}, 400)
            return
        if not os.path.exists(INDEXES[corpus]):
            self.send_json({"error": "index not found, run ./run.sh first"}, 500)
            return

        try:
            self.send_json(query_mse(query, mode, corpus))
        except (OSError, RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
            self.send_json({"error": str(error)}, 500)


if __name__ == "__main__":
    server = HTTPServer(("127.0.0.1", 8000), Handler)
    print("Open http://127.0.0.1:8000")
    server.serve_forever()
