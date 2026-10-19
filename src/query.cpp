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
// True when `word` was excluded from the index as a stop word.
//
// Both conditions are required: the index must have been built with the stop
// word filter (flag), and the caller must supply the same list. Without the
// list we cannot know which words were removed; --pretty warns about this
// because position renumbering would otherwise be wrong.
bool isIndexedStopWord(const Index& index, const StopWords* stopWords,
                       const std::string& word) {
    return (index.flags & kIndexFlagStopWords) != 0 && stopWords != nullptr &&
           stopWords->contains(word);
}

// Map a query token to the form stored in the index.
//
// This must mirror the build pipeline exactly: stem only when the index was
// built with stemming. Applying the wrong transform is the classic cause of
// "term not in the index" for words that are visibly present in the corpus.
std::string toIndexForm(const Index& index, const std::string& word) {
    return (index.flags & kIndexFlagStemmed) != 0 ? stem(word) : word;
}

// Find one document's posting inside a term's posting list.
//
// Lists are short in this corpus (average df ~8), so a linear scan is simpler
// and cache-friendly; a binary search would only pay off for very long lists
// (a possible optimization for the Bonus scale). Returns nullptr when the
// document does not contain the term.
const Posting* postingFor(const IndexTerm* entry, std::uint32_t docID) {
    for (const Posting& posting : entry->postings) {
        if (posting.docID == docID) {
            return &posting;
        }
    }
    return nullptr;
}

// True when `searchable` occurs adjacently (in order) inside `docID`.
//
// All phrase terms must be present in the document. For each occurrence of the
// first term, the function checks whether the next term has position start+1,
// then start+2, and so on. Positions within a posting slice are sorted
// ascending, so each check is a binary search rather than a scan. On success
// `windowStart` receives the position of the first term.
//
// Complexity: O(tf_first * k * log tf_i) per candidate document, where k is
// the phrase length.
bool phraseMatches(const std::vector<const IndexTerm*>& searchable,
                   std::uint32_t docID, std::uint32_t* windowStart) {
    // Resolve each phrase term's posting for this document once; the inner
    // loop then works on positions only.
    std::vector<const Posting*> slices(searchable.size(), nullptr);
    for (std::size_t i = 0; i < searchable.size(); ++i) {
        slices[i] = postingFor(searchable[i], docID);
        if (slices[i] == nullptr) {
            return false;  // every phrase term must be present
        }
    }
    // Try every occurrence of the rarest term (chosen by the caller) as the
    // phrase start. This keeps the number of candidate windows small.
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

// Run a query against a loaded index.
//
// The four stages are:
//   1. normalize the query exactly like the corpus (tokenize -> stop words ->
//      stem, driven by the index flags);
//   2. dictionary lookup plus the term-side thresholds (--max-df/--min-df);
//   3. match: accumulate per-document scores, or verify positional adjacency
//      for a phrase;
//   4. apply --min-match/--top-x and return results in deterministic score
//      order (matched desc, tfSum desc, docID asc).
//
// Terms that cannot participate are reported in `notes` with a reason, so the
// CLI can explain surprising empty result sets instead of silently returning
// nothing. Complexity: O(Σ df) for word/boolean, O(df_rarest * k log tf) for
// phrases, plus O(#docs log #docs) for the final sort.
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

    // Build the list of searchable terms. Stop words are dropped with a note
    // (they were never indexed). Word/boolean queries deduplicate repeated
    // terms; a phrase keeps duplicates because "very very" has a different
    // positional signature from "very".
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
    // --word is deliberately strict: "hello world" is an error, not an
    // implicit AND. The CLI offers --words/--phrase for the other semantics,
    // so guessing here would hide a user mistake.
    if (options.mode == QueryMode::Word && wanted.size() > 1) {
        result.notes.push_back(
            {queryText, "expected a single word (use --words for multi-word queries)"});
        return result;
    }

    // 2. Dictionary lookup; term-side thresholds (--max-df / --min-df).
    //    Phrases ignore the term thresholds: every phrase token is required.
    // 2. Dictionary lookup and term-side thresholds. Missing terms are noted
    //    rather than treated as an error: a boolean query may still be
    //    answerable with its remaining terms.
    std::vector<const IndexTerm*> searchable;
    for (const std::string& term : wanted) {
        const IndexTerm* entry = findIndexTerm(index, term);
        if (entry == nullptr) {
            result.notes.push_back({term, "not in the index"});
            continue;
        }
        // Phrase terms bypass both thresholds: dropping any term would change
        // the phrase's meaning, so a phrase either has all its terms or fails.
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
    // Record the indexed forms actually used (post-stemming), which is what
    // the CLI prints and what the JSON consumer sees.
    for (const IndexTerm* entry : searchable) {
        result.terms.push_back(entry->term);
    }

    // 3. Match: accumulate per-document scores (word / boolean), or verify
    //    positional adjacency (phrase).
    //
    // Hits are accumulated in an array indexed by docID. docIDs are dense
    // 1..N (validated at load), so this avoids a hash map and an extra sort
    // by key; slot 0 is unused.
    const std::size_t docCount = index.docs.size();
    std::vector<QueryHit> hits(docCount + 1);
    if (options.mode == QueryMode::Phrase) {
        // Start from the rarest term's posting list: it yields the fewest
        // candidate documents, and phraseMatches() verifies each candidate.
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
            // All phrase terms matched adjacently. tfSum is the sum of the
            // individual term frequencies (used for ordering among phrase
            // hits), and firstPosition is the start of the first window.
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
        // Word/boolean scoring: each postings entry contributes one matched
        // term and its tf. firstPosition is the earliest occurrence among the
        // matched terms, which --pretty later maps back to a source line.
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
    //
    // Collect the used slots (slot 0 and documents with no match stay
    // docID == 0), then sort by matched terms, total tf, and docID. The docID
    // tie-break makes the output fully deterministic for a given index.
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

    // A phrase always requires every term (matched == termCount). For other
    // modes, minMatch == 0 means AND (all terms); otherwise at least k, with k
    // clamped to the number of usable terms so k > n cannot return an empty
    // set for a wording mistake.
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
        // topX is a result-side cap: results are already in score order, so
        // stopping at n keeps the n best documents.
        if (options.topX > 0 && result.hits.size() >= options.topX) {
            break;
        }
    }
    return result;
}

// Map a filtered-stream position back to the source line that produced it.
//
// The function re-runs the same tokenizer/stop-word pipeline over the
// document's Body lines, counting only tokens that occupy a position (stop
// words do not), until the counter reaches `position`. This is O(document)
// per hit and is only used by --pretty; storing line numbers in every posting
// would bloat the index for a display-only feature.
//
// Returns an empty string when the position cannot be located (for example
// when the caller passed the wrong stop word list, so the renumbered stream
// does not match the index).
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
