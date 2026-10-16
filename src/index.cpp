// index.cpp -- index build, v1 serialization, loading, validation, dump.

#include "index.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <unordered_map>

#include "stemmer.hpp"
#include "stopwords.hpp"

namespace mse {

namespace {

// Fixed part of the header: magic + version + flags + docCount + termCount +
// tokenCount (see plan.md 7 for the full layout).
constexpr std::size_t kHeaderBytes = 4 + 4 + 4 + 4 + 4 + 8;

// Dump: how many positions of a single posting are printed before "…".
constexpr std::uint32_t kDumpPositionsPerPosting = 12;

void writeU32(std::ostream& out, std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value & 0xFFu),
                           static_cast<char>((value >> 8) & 0xFFu),
                           static_cast<char>((value >> 16) & 0xFFu),
                           static_cast<char>((value >> 24) & 0xFFu)};
    out.write(bytes, sizeof(bytes));
}

void writeU64(std::ostream& out, std::uint64_t value) {
    writeU32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    writeU32(out, static_cast<std::uint32_t>(value >> 32));
}

void writeString(std::ostream& out, const std::string& text) {
    writeU32(out, static_cast<std::uint32_t>(text.size()));
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::uint64_t postingsBytesOf(const IndexTerm& term) {
    return static_cast<std::uint64_t>(term.postings.size()) * 8 +
           static_cast<std::uint64_t>(term.positions.size()) * 4;
}

// ---------------------------------------------------------------------------
// little-endian reader with bounds checks (the load side of the spec)
// ---------------------------------------------------------------------------

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& data) : data_(data) {}

    void expectMagic() {
        require(4);
        if (std::memcmp(data_.data() + pos_, "MSE1", 4) != 0) {
            throw std::runtime_error("not an index file (bad magic)");
        }
        pos_ += 4;
    }

    std::uint32_t u32() {
        require(4);
        const std::uint32_t value =
            static_cast<std::uint32_t>(data_[pos_]) |
            (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
            (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
            (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return value;
    }

    std::uint64_t u64() {
        const std::uint64_t low = u32();
        const std::uint64_t high = u32();
        return low | (high << 32);
    }

    std::string string() {
        const std::uint32_t length = u32();
        require(length);
        std::string text(reinterpret_cast<const char*>(data_.data() + pos_), length);
        pos_ += length;
        return text;
    }

    void seek(std::uint64_t offset) {
        if (offset > data_.size()) {
            throw std::runtime_error("index file is truncated or corrupt");
        }
        pos_ = static_cast<std::size_t>(offset);
    }

private:
    void require(std::size_t bytes) {
        if (pos_ + bytes > data_.size()) {
            throw std::runtime_error("index file is truncated or corrupt");
        }
    }

    const std::vector<std::uint8_t>& data_;
    std::size_t pos_ = 0;
};

std::vector<std::uint8_t> readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open index file: " + path);
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    if (in.bad()) {
        throw std::runtime_error("error while reading index file: " + path);
    }
    return data;
}

}  // namespace

Index buildIndex(const Corpus& corpus, const BuildOptions& options,
                 Dictionary& dictionary) {
    const Tokenizer tokenizer(options.tokenizer);
    std::vector<Token> tokens;
    // Stemming is deterministic: each distinct form only needs one lookup.
    std::unordered_map<std::string, std::string> stemCache;
    const auto stemmed = [&stemCache](const std::string& word) -> const std::string& {
        auto it = stemCache.find(word);
        if (it == stemCache.end()) {
            it = stemCache.emplace(word, stem(word)).first;
        }
        return it->second;
    };

    Index index;
    index.flags = (options.stopWords != nullptr ? kIndexFlagStopWords : 0u) |
                  (options.stem ? kIndexFlagStemmed : 0u);
    index.docs.reserve(corpus.size());

    for (std::size_t i = 0; i < corpus.size(); ++i) {
        const Document& doc = corpus[i];
        tokens.clear();
        tokenizer.tokenize(doc, tokens);

        std::uint32_t position = 0;  // offset in the document's filtered stream
        for (const Token& token : tokens) {
            if (options.stopWords != nullptr &&
                options.stopWords->contains(token.text)) {
                continue;  // stop words do not occupy positions
            }
            const std::string& term =
                options.stem ? stemmed(token.text) : token.text;
            dictionary.upsert(term).postings.append(
                static_cast<std::uint32_t>(doc.id), position);
            ++position;
        }

        index.docs.push_back(IndexDoc{static_cast<std::uint32_t>(doc.id),
                                      doc.title, position});
        index.tokenCount += position;
    }

    // Freeze: BST in-order (or collect+sort for the hash table) gives the
    // lexicographic term table that the file format and queries rely on.
    const std::vector<TermEntry*> sorted = dictionary.sortedEntries();
    index.terms.reserve(sorted.size());
    for (const TermEntry* entry : sorted) {
        IndexTerm term;
        term.term = entry->term;
        term.postings = entry->postings.postings();
        term.positions = entry->postings.positions();
        index.terms.push_back(std::move(term));
    }
    return index;
}

void writeIndex(const std::string& path, const Index& index) {
    // Pre-compute every offset so the file is written sequentially and
    // deterministically (no pointer fix-ups, same bytes for the same input).
    std::uint64_t offset = kHeaderBytes;
    for (const IndexDoc& doc : index.docs) {
        offset += 12 + doc.title.size();  // docID + keptTokens + length + title
    }
    for (const IndexTerm& term : index.terms) {
        offset += 28 + term.term.size();  // len + term + df + tf + offset + bytes
    }
    std::vector<std::uint64_t> termOffsets(index.terms.size());
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        termOffsets[i] = offset;
        offset += postingsBytesOf(index.terms[i]);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot write index file: " + path);
    }

    out.write("MSE1", 4);
    writeU32(out, kIndexVersion);
    writeU32(out, index.flags);
    writeU32(out, static_cast<std::uint32_t>(index.docs.size()));
    writeU32(out, static_cast<std::uint32_t>(index.terms.size()));
    writeU64(out, index.tokenCount);

    for (const IndexDoc& doc : index.docs) {
        writeU32(out, doc.docID);
        writeU32(out, doc.keptTokens);
        writeString(out, doc.title);
    }
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        const IndexTerm& term = index.terms[i];
        writeString(out, term.term);
        writeU32(out, static_cast<std::uint32_t>(term.postings.size()));
        writeU64(out, static_cast<std::uint64_t>(term.positions.size()));
        writeU64(out, termOffsets[i]);
        writeU32(out, static_cast<std::uint32_t>(postingsBytesOf(term)));
    }
    for (const IndexTerm& term : index.terms) {
        for (const Posting& posting : term.postings) {
            writeU32(out, posting.docID);
            writeU32(out, posting.tf);
            for (std::uint32_t k = 0; k < posting.tf; ++k) {
                writeU32(out, term.positions[posting.posStart + k]);
            }
        }
    }
    if (!out) {
        throw std::runtime_error("error while writing index file: " + path);
    }
}

Index loadIndex(const std::string& path) {
    // Keep the buffer alive for the whole parse: Reader holds a reference to
    // it (binding the temporary directly to Reader would dangle immediately).
    const std::vector<std::uint8_t> data = readWholeFile(path);
    Reader reader(data);
    reader.expectMagic();
    const std::uint32_t version = reader.u32();
    if (version != kIndexVersion) {
        throw std::runtime_error("unsupported index version " +
                                 std::to_string(version));
    }

    Index index;
    index.flags = reader.u32();
    const std::uint32_t docCount = reader.u32();
    const std::uint32_t termCount = reader.u32();
    index.tokenCount = reader.u64();

    index.docs.reserve(docCount);
    for (std::uint32_t i = 0; i < docCount; ++i) {
        IndexDoc doc;
        doc.docID = reader.u32();
        doc.keptTokens = reader.u32();
        doc.title = reader.string();
        index.docs.push_back(std::move(doc));
    }

    // Read the term table first, then jump to each term's postings block via
    // its recorded offset (this also exercises the offset fields).
    struct RawTerm {
        std::string term;
        std::uint32_t df = 0;
        std::uint64_t tf = 0;
        std::uint64_t offset = 0;
        std::uint32_t bytes = 0;
    };
    std::vector<RawTerm> raw;
    raw.reserve(termCount);
    for (std::uint32_t i = 0; i < termCount; ++i) {
        RawTerm term;
        term.term = reader.string();
        term.df = reader.u32();
        term.tf = reader.u64();
        term.offset = reader.u64();
        term.bytes = reader.u32();
        raw.push_back(std::move(term));
    }

    index.terms.reserve(termCount);
    for (RawTerm& source : raw) {
        reader.seek(source.offset);
        IndexTerm term;
        term.term = std::move(source.term);
        term.postings.reserve(source.df);
        std::uint64_t tfTotal = 0;
        for (std::uint32_t d = 0; d < source.df; ++d) {
            Posting posting;
            posting.docID = reader.u32();
            posting.tf = reader.u32();
            posting.posStart = static_cast<std::uint32_t>(term.positions.size());
            for (std::uint32_t k = 0; k < posting.tf; ++k) {
                term.positions.push_back(reader.u32());
            }
            tfTotal += posting.tf;
            term.postings.push_back(posting);
        }
        if (tfTotal != source.tf) {
            throw std::runtime_error("index file corrupt: tf mismatch for term '" +
                                     term.term + "'");
        }
        index.terms.push_back(std::move(term));
    }

    validateIndex(index);
    return index;
}

void validateIndex(const Index& index) {
    std::uint64_t docTokens = 0;
    for (std::size_t i = 0; i < index.docs.size(); ++i) {
        if (index.docs[i].docID != i + 1) {
            throw std::runtime_error("index invariant: docIDs are not dense");
        }
        docTokens += index.docs[i].keptTokens;
    }
    if (docTokens != index.tokenCount) {
        throw std::runtime_error("index invariant: doc token sum != tokenCount");
    }

    std::uint64_t termTokens = 0;
    for (std::size_t i = 0; i < index.terms.size(); ++i) {
        const IndexTerm& term = index.terms[i];
        if (term.term.empty()) {
            throw std::runtime_error("index invariant: empty term");
        }
        if (i > 0 && !(index.terms[i - 1].term < term.term)) {
            throw std::runtime_error("index invariant: term table not strictly sorted");
        }
        if (term.postings.empty()) {
            throw std::runtime_error("index invariant: empty posting list for '" +
                                     term.term + "'");
        }
        std::uint64_t positionsSeen = 0;
        std::uint32_t lastDoc = 0;
        for (const Posting& posting : term.postings) {
            if (posting.docID <= lastDoc || posting.docID > index.docs.size()) {
                throw std::runtime_error("index invariant: bad docID in '" +
                                         term.term + "'");
            }
            if (posting.tf == 0) {
                throw std::runtime_error("index invariant: zero tf in '" +
                                         term.term + "'");
            }
            if (posting.posStart != positionsSeen) {
                throw std::runtime_error("index invariant: positions are not dense in '" +
                                         term.term + "'");
            }
            lastDoc = posting.docID;
            positionsSeen += posting.tf;
        }
        if (positionsSeen != term.positions.size()) {
            throw std::runtime_error("index invariant: tf != positions in '" +
                                     term.term + "'");
        }
        termTokens += positionsSeen;
    }
    if (termTokens != index.tokenCount) {
        throw std::runtime_error("index invariant: term tf sum != tokenCount");
    }
}

const IndexTerm* findIndexTerm(const Index& index, const std::string& term) {
    const auto it = std::lower_bound(
        index.terms.begin(), index.terms.end(), term,
        [](const IndexTerm& candidate, const std::string& value) {
            return candidate.term < value;
        });
    if (it == index.terms.end() || it->term != term) {
        return nullptr;
    }
    return &*it;
}

bool dumpTerm(const Index& index, const std::string& term, std::size_t maxPostings,
              std::ostream& out) {
    const IndexTerm* entry = findIndexTerm(index, term);
    if (entry == nullptr) {
        return false;
    }

    out << "term " << entry->term << ": df=" << entry->postings.size()
        << ", tf=" << entry->positions.size() << "\n";
    const std::size_t shown = std::min(maxPostings, entry->postings.size());
    for (std::size_t i = 0; i < shown; ++i) {
        const Posting& posting = entry->postings[i];
        const std::string& title = index.docs[posting.docID - 1].title;
        out << "  doc " << posting.docID << " \"" << title << "\"  tf="
            << posting.tf << "  positions:";
        const std::uint32_t printed =
            std::min(posting.tf, kDumpPositionsPerPosting);
        for (std::uint32_t k = 0; k < printed; ++k) {
            out << ' ' << entry->positions[posting.posStart + k];
        }
        if (posting.tf > printed) {
            out << " …(+" << (posting.tf - printed) << " more)";
        }
        out << "\n";
    }
    if (entry->postings.size() > shown) {
        out << "  …(+" << (entry->postings.size() - shown) << " more postings)\n";
    }
    return true;
}

}  // namespace mse
