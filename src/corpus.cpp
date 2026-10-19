// corpus.cpp -- implementation of the document storage layer (see corpus.hpp).

#include "corpus.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mse {

namespace {

// ---------------------------------------------------------------------------
// file / string helpers
// ---------------------------------------------------------------------------

// Read an entire file as bytes. Throws with the path on failure so callers
// can always tell *which* input is broken.
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open file: " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw std::runtime_error("error while reading file: " + path);
    }
    return buffer.str();
}

// Split on '\n', dropping an optional trailing '\r' (the generated files use
// '\n' only, but this keeps the loader robust on any platform).
std::vector<std::string> splitLines(const std::string& data) {
    std::vector<std::string> lines;
    std::string current;
    for (const char ch : data) {
        if (ch == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

// Split one manifest row on tabs. Titles may contain spaces but never a tab
// (the manifest producers guarantee this), so a plain split is safe.
std::vector<std::string> splitTab(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (const char ch : line) {
        if (ch == '\t') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string joinPaths(const std::string& dir, const std::string& rel) {
    if (dir.empty() || dir == ".") {
        return rel;
    }
    if (dir.back() == '/') {
        return dir + rel;
    }
    return dir + "/" + rel;
}

// Directory part of a path: "a/b/c" -> "a/b", "c" -> ".", "/c" -> "/".
// Both separators are accepted so Windows-style argv paths also resolve.
std::string dirName(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

// Classify one line of a data/text/*.txt file and strip its marker.
LineKind classifyLine(const std::string& line, std::string& content) {
    if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
        content = line.substr(1, line.size() - 2);
        return LineKind::Heading;
    }
    if (line.size() >= 2 && line.front() == '(' && line.back() == ')') {
        content = line.substr(1, line.size() - 2);
        return LineKind::Stage;
    }
    if (!line.empty() && line.front() == '@') {
        content = line.substr(1);
        return LineKind::Speaker;
    }
    content = line;
    return LineKind::Body;
}

}  // namespace

const char* toString(LineKind kind) {
    switch (kind) {
        case LineKind::Body:    return "body";
        case LineKind::Heading: return "heading";
        case LineKind::Stage:   return "stage";
        case LineKind::Speaker: return "speaker";
    }
    return "unknown";
}

const char* toString(TextFormat format) {
    switch (format) {
        case TextFormat::Marked: return "marked";
        case TextFormat::Plain:  return "plain";
    }
    return "unknown";
}

// Parse the optional 8th manifest column. Absent means "marked", so every
// existing manifest (including the golden4 fixture) keeps working unchanged.
TextFormat parseTextFormat(const std::string& value, const std::string& where) {
    if (value == "marked") {
        return TextFormat::Marked;
    }
    if (value == "plain") {
        return TextFormat::Plain;
    }
    throw std::runtime_error(where + ": unknown text format '" + value +
                             "' (expected marked|plain)");
}

Corpus Corpus::load(const std::string& manifestPath) {
    const std::string manifest = readFile(manifestPath);

    // The manifest lives at <root>/data/manifest.tsv; paths inside it are
    // relative to <root>, so two dirName() steps recover the project root.
    const std::string root = dirName(dirName(manifestPath));

    const std::vector<std::string> rows = splitLines(manifest);
    const std::string expectedHeader = "docid\tkind\tcategory\ttitle\ttext\tsource\turl";
    if (rows.empty() || rows.front().compare(0, expectedHeader.size(), expectedHeader) != 0) {
        throw std::runtime_error("unexpected manifest header in " + manifestPath);
    }
    // Optional 8th column "format" (marked|plain); anything else is rejected
    // so a typo cannot silently fall back to the default.
    const bool hasFormatColumn = rows.front() == expectedHeader + "\tformat";
    if (!hasFormatColumn && rows.front() != expectedHeader) {
        throw std::runtime_error("unexpected manifest header in " + manifestPath +
                                 " (optional 8th column must be 'format')");
    }

    Corpus corpus;
    corpus.docs_.reserve(rows.size() - 1);

    for (std::size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].empty()) {
            continue;
        }
        const std::string where = manifestPath + " row " + std::to_string(i + 1);

        const std::vector<std::string> fields = splitTab(rows[i]);
        const std::size_t expectedFields = hasFormatColumn ? 8 : 7;
        if (fields.size() != expectedFields) {
            throw std::runtime_error(where + ": expected " +
                                     std::to_string(expectedFields) +
                                     " tab-separated fields, got " +
                                     std::to_string(fields.size()));
        }

        Document doc;
        try {
            doc.id = std::stoi(fields[0]);
        } catch (const std::exception&) {
            throw std::runtime_error(where + ": docID is not a number: '" + fields[0] + "'");
        }
        doc.kind = fields[1];
        doc.category = fields[2];
        doc.title = fields[3];
        doc.textPath = fields[4];
        doc.sourcePath = fields[5];
        doc.url = fields[6];
        if (hasFormatColumn) {
            doc.format = parseTextFormat(fields[7], where);
        }

        // docIDs must be dense and ordered 1..N; byID() relies on this.
        if (doc.id != static_cast<int>(corpus.docs_.size()) + 1) {
            throw std::runtime_error(where + ": docID " + std::to_string(doc.id) +
                                     " is not in sequence (expected " +
                                     std::to_string(corpus.docs_.size() + 1) + ")");
        }

        // Load the line-based text; '#' lines are producer header comments.
        // Plain-format documents skip marker classification entirely: every
        // non-comment line is Body, so "(a remark)" or "[note]" lines in an
        // ordinary prose corpus stay searchable.
        const std::string textFile = joinPaths(root, doc.textPath);
        for (const std::string& line : splitLines(readFile(textFile))) {
            if (line.empty() || line.front() == '#') {
                continue;
            }
            if (doc.format == TextFormat::Plain) {
                doc.lines.push_back(TextLine{LineKind::Body, line});
                continue;
            }
            std::string content;
            const LineKind kind = classifyLine(line, content);
            doc.lines.push_back(TextLine{kind, std::move(content)});
        }

        corpus.docs_.push_back(std::move(doc));
    }
    return corpus;
}

const Document& Corpus::byID(int docID) const {
    if (docID < 1 || static_cast<std::size_t>(docID) > docs_.size()) {
        throw std::out_of_range("docID out of range: " + std::to_string(docID));
    }
    return docs_[static_cast<std::size_t>(docID) - 1];
}

Corpus::Stats Corpus::stats() const {
    Stats stats;
    stats.documents = docs_.size();
    for (const Document& doc : docs_) {
        for (const TextLine& line : doc.lines) {
            ++stats.lines;
            ++stats.linesByKind[static_cast<std::size_t>(line.kind)];
            stats.bytes += line.text.size();
            // count whitespace-separated fields, the same rule the Python
            // preprocessing report uses (tools/extract.py), so the numbers
            // can be compared directly
            bool inWord = false;
            for (const char ch : line.text) {
                const bool isSpace = ch == ' ' || ch == '\t' || ch == '\r' ||
                                     ch == '\n' || ch == '\f' || ch == '\v';
                if (!isSpace && !inWord) {
                    ++stats.words;
                    inWord = true;
                } else if (isSpace) {
                    inWord = false;
                }
            }
        }
    }
    return stats;
}

}  // namespace mse
