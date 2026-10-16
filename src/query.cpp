// query.cpp -- implementation of query processing (see query.hpp).

#include "query.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "stemmer.hpp"
#include "stopwords.hpp"
#include "tokenizer.hpp"

namespace mse {

namespace {

// The index header records whether stop words and stemming were applied;
// the query pipeline must match it or terms will not line up.
bool isIndexedStopWord(const Index& index, const StopWords* stopWords,
                       const std::string& word) {
    return (index.flags & kIndexFlagStopWords) != 0 && stopWords != nullptr &&
           stopWords->contains(word);
}

std::string toIndexForm(const Index& index, const std::string& word) {
    return (index.flags & kIndexFlagStemmed) != 0 ? stem(word) : word;
}

const Posting* postingFor(const IndexTerm* entry, std::uint32_t docID) {
    for (const Posting& posting : entry->postings) {
        if (posting.docID == docID) {
            return &posting;
        }
    }
    return nullptr;
}

// True when `searchable` occurs adjacently (in order) inside `docID`.
bool phraseMatches(const std::vector<const IndexTerm*>& searchable,
                   std::uint32_t docID, std::uint32_t* windowStart) {
    std::vector<const Posting*> slices(searchable.size(), nullptr);
    for (std::size_t i = 0; i < searchable.size(); ++i) {
        slices[i] = postingFor(searchable[i], docID);
        if (slices[i] == nullptr) {
            return false;  // every phrase term must be present
        }
    }
    const IndexTerm* first = searchable[0];
    const Posting* firstPosting = slices[0];
    for (std::uint32_t t = 0; t < firstPosting->tf; ++t) {
        const std::uint32_t start = first->positions[firstPosting->posStart + t];
        std::uint32_t next = start + 1;
        bool chain = true;
        for (std::size_t i = 1; i < searchable.size(); ++i) {
            const IndexTerm* entry = searchable[i];
            const Posting* posting = slices[i];
            const auto begin = entry->positions.begin() + posting->posStart;
            const auto end = begin + posting->tf;
            // positions are sorted ascending, and the next phrase token must
            // sit exactly one filtered-stream position later
            if (!std::binary_search(begin, end, next)) {
                chain = false;
                break;
            }
            ++next;
        }
        if (chain) {
            *windowStart = start;
            return true;
        }
    }
    return false;
}

}  // namespace

QueryResult runQuery(const Index& index, const std::string& queryText,
                     const QueryOptions& options, const StopWords* stopWords) {
    QueryResult result;

    // 1. Tokenize the query with the same rules as the corpus, then apply the
    //    same per-token pipeline the index used (stop words, stemming).
    Tokenizer tokenizer;
    std::vector<Token> tokens;
    tokenizer.tokenizeText(queryText, 0, tokens);
    if (tokens.empty()) {
        result.notes.push_back({queryText, "no searchable tokens"});
        return result;
    }

    std::vector<std::string> wanted;
    for (const Token& token : tokens) {
        if (isIndexedStopWord(index, stopWords, token.text)) {
            result.notes.push_back({token.text, "stop word (excluded from the index)"});
            continue;
        }
        std::string term = toIndexForm(index, token.text);
        if (options.mode == QueryMode::Phrase) {
            wanted.push_back(std::move(term));  // keep repeats: order matters
        } else if (std::find(wanted.begin(), wanted.end(), term) == wanted.end()) {
            wanted.push_back(std::move(term));
        }
    }
    if (wanted.empty()) {
        return result;  // everything was filtered out
    }
    if (options.mode == QueryMode::Word && wanted.size() > 1) {
        result.notes.push_back(
            {queryText, "expected a single word (use --words for multi-word queries)"});
        return result;
    }

    // 2. Dictionary lookup; term-side thresholds (--max-df / --min-df).
    //    Phrases ignore the term thresholds: every phrase token is required.
    std::vector<const IndexTerm*> searchable;
    for (const std::string& term : wanted) {
        const IndexTerm* entry = findIndexTerm(index, term);
        if (entry == nullptr) {
            result.notes.push_back({term, "not in the index"});
            continue;
        }
        if (options.mode != QueryMode::Phrase) {
            const double dfRatio = static_cast<double>(entry->postings.size()) /
                                   static_cast<double>(index.docs.size());
            if (options.maxDfRatio < 1.0 && dfRatio > options.maxDfRatio) {
                result.notes.push_back({term, "df ratio above --max-df"});
                continue;
            }
            if (entry->postings.size() < options.minDf) {
                result.notes.push_back({term, "df below --min-df"});
                continue;
            }
        }
        searchable.push_back(entry);
    }
    if (options.mode == QueryMode::Phrase && searchable.size() != wanted.size()) {
        return result;  // a phrase needs every one of its terms present
    }
    if (searchable.empty()) {
        return result;
    }
    for (const IndexTerm* entry : searchable) {
        result.terms.push_back(entry->term);
    }

    // 3. Match: accumulate per-document scores (word / boolean), or verify
    //    positional adjacency (phrase).
    const std::size_t docCount = index.docs.size();
    std::vector<QueryHit> hits(docCount + 1);  // indexed by docID (dense 1..N)
    if (options.mode == QueryMode::Phrase) {
        const IndexTerm* rarest = *std::min_element(
            searchable.begin(), searchable.end(),
            [](const IndexTerm* a, const IndexTerm* b) {
                return a->postings.size() < b->postings.size();
            });
        for (const Posting& candidate : rarest->postings) {
            std::uint32_t windowStart = 0;
            if (!phraseMatches(searchable, candidate.docID, &windowStart)) {
                continue;
            }
            QueryHit hit;
            hit.docID = candidate.docID;
            hit.matched = static_cast<std::uint32_t>(searchable.size());
            hit.firstPosition = windowStart;
            for (const IndexTerm* entry : searchable) {
                const Posting* posting = postingFor(entry, candidate.docID);
                if (posting != nullptr) {
                    hit.tfSum += posting->tf;
                }
            }
            hits[candidate.docID] = hit;
        }
    } else {
        for (const IndexTerm* entry : searchable) {
            for (const Posting& posting : entry->postings) {
                QueryHit& hit = hits[posting.docID];
                hit.docID = posting.docID;
                ++hit.matched;
                hit.tfSum += posting.tf;
                const std::uint32_t first = entry->positions[posting.posStart];
                if (hit.matched == 1 || first < hit.firstPosition) {
                    hit.firstPosition = first;
                }
            }
        }
    }

    // 4. Score order, --min-match and --top-x.
    std::vector<QueryHit> scored;
    for (QueryHit& hit : hits) {
        if (hit.docID != 0) {
            scored.push_back(hit);
        }
    }
    std::sort(scored.begin(), scored.end(),
              [](const QueryHit& a, const QueryHit& b) {
                  if (a.matched != b.matched) return a.matched > b.matched;
                  if (a.tfSum != b.tfSum) return a.tfSum > b.tfSum;
                  return a.docID < b.docID;
              });

    const std::uint32_t termCount = static_cast<std::uint32_t>(searchable.size());
    const std::uint32_t required =
        (options.mode == QueryMode::Phrase || options.minMatch == 0)
            ? termCount
            : std::min(options.minMatch, termCount);
    for (const QueryHit& hit : scored) {
        if (hit.matched < required) {
            continue;
        }
        result.hits.push_back(hit);
        if (options.topX > 0 && result.hits.size() >= options.topX) {
            break;
        }
    }
    return result;
}

std::string lineOfPosition(const Document& doc, const Index& index,
                           const StopWords* stopWords, std::uint32_t position) {
    Tokenizer tokenizer;
    std::vector<Token> tokens;
    std::uint32_t pos = 0;
    for (const TextLine& line : doc.lines) {
        if (line.kind != LineKind::Body) {
            continue;
        }
        tokens.clear();
        tokenizer.tokenizeText(line.text, 0, tokens);
        for (const Token& token : tokens) {
            if (isIndexedStopWord(index, stopWords, token.text)) {
                continue;  // stop words do not occupy positions
            }
            if (pos == position) {
                return line.text;
            }
            ++pos;
        }
    }
    return std::string();
}

}  // namespace mse
