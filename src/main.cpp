// main.cpp -- command line entry point of the mini search engine (`mse`).
//
// Current commands:
//   mse stats      load the corpus and print storage statistics
//   mse tokenize   print the token stream of one document (or all documents)
//   mse count      word frequency / document frequency table -> CSV (task 1)
//   mse stopwords  derive a stop word list from corpus statistics
//   mse build      build the inverted index -> index.bin (task 2)
//   mse dump       print one term's postings from an index file
//   mse query      run a word / phrase / boolean query (task 3)
// Planned commands (plan.md section 2):
//   shell / selftest

#include "corpus.hpp"
#include "count.hpp"
#include "index.hpp"
#include "query.hpp"
#include "stopwords.hpp"
#include "tokenizer.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void printUsage(const char* program) {
    std::cerr
        << "usage: " << program << " <command> [options]\n"
        << "\n"
        << "commands:\n"
        << "  stats                load manifest + text files and print statistics\n"
        << "  tokenize [options]   print the token stream of one document\n"
        << "  count [options]      word count table (tf/df) -> CSV\n"
        << "  stopwords --derive   derive a stop word list from corpus statistics\n"
        << "  build [options]      build the inverted index -> <index file>\n"
        << "  dump [options]       print one term's postings from an index file\n"
        << "  query [options]      run a query against the index (task 3)\n"
        << "\n"
        << "options:\n"
        << "  --manifest <path>    manifest file (default: data/manifest.tsv)\n"
        << "\n"
        << "tokenize options:\n"
        << "  --doc <id>           document id (default: 1)\n"
        << "  --all                print \"docid<TAB>tokens\" for every document\n"
        << "  --limit <n>          how many sample tokens to print (default: 20)\n"
        << "  --keep-headings      include [ACT I] / [SCENE ...] lines\n"
        << "  --keep-speakers      include @BERNARDO lines\n"
        << "  --keep-stage         include (Exit Ghost) lines\n"
        << "\n"
        << "count options:\n"
        << "  --csv <path>         output CSV (default: build/wordcount.csv)\n"
        << "  --top <n>            print the top n rows (default: 50, 0 = quiet)\n"
        << "  --stop-words <path>  drop words from this list before counting\n"
        << "  --stem               Porter-stem the tokens before counting\n"
        << "\n"
        << "stopwords options:\n"
        << "  --derive             run the statistical rule (required for now)\n"
        << "  --out <path>         output list (default: build/stopwords-derived.txt)\n"
        << "  --min-df-ratio <r>   rule threshold: df ratio at least <r> (default 0.75)\n"
        << "  --min-tf-share <s>   rule threshold: tf share at least <s> (default 0.001)\n"
        << "  --min-entropy-rate <h>  rule threshold: entropy_rate_norm at least <h>\n"
        << "                       (default 0.93; the length-normalized entropy)\n"
        << "  --compare <path>     diff the result against an existing list\n"
        << "  --stem               derive over stemmed terms (matches count --stem)\n"
        << "\n"
        << "build options:\n"
        << "  --index <path>       output index file (default: build/index.bin)\n"
        << "  --dict <name>        build dictionary: splay | hash (default: splay)\n"
        << "  --stop-words <path>  drop these tokens (recorded in the index flags)\n"
        << "  --stem               Porter-stem the tokens (recorded in the flags)\n"
        << "\n"
        << "dump options:\n"
        << "  --index <path>       index file (default: build/index.bin)\n"
        << "  --term <word>        term to dump (the indexed form)\n"
        << "  --limit <n>          max postings printed (default: 20)\n"
        << "\n"
        << "query options:\n"
        << "  --word <w>           single-word query\n"
        << "  --phrase \"<text>\"    phrase query (adjacent positions required)\n"
        << "  --words \"<a b c>\"    multi-word query (see --min-match)\n"
        << "  --or                 shorthand for --min-match 1 (any term matches)\n"
        << "  --max-df <ratio>     drop terms with df ratio above <ratio> (default 1.0 = off)\n"
        << "  --min-df <n>         drop terms with df below <n> (default 1 = off)\n"
        << "  --min-match <k>      keep docs matching at least k terms (0 = all, default)\n"
        << "  --top-x <n>          keep at most n result docs (0 = all, default)\n"
        << "  --stop-words <path>  stop word list used by the build (explains skips;\n"
        << "                       needed for correct --pretty positions)\n"
        << "  --pretty             print the source line of each match\n"
        << "  --json               machine-readable output (for scripts/tests)\n"
        << "\n"
        << "planned: shell, selftest\n";
}

// Return the value of "--name value" from argv, or `fallback` if absent.
std::string optionValue(int argc, char** argv, const std::string& name,
                        const std::string& fallback) {
    for (int i = 2; i + 1 < argc; ++i) {
        if (name == argv[i]) {
            return argv[i + 1];
        }
    }
    return fallback;
}

// Like optionValue(), but requires the value to be an integer.
int intOption(int argc, char** argv, const std::string& name, const std::string& fallback) {
    const std::string value = optionValue(argc, argv, name, fallback);
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        throw std::runtime_error("option " + name + " expects a number, got '" + value + "'");
    }
}

const char* onOff(bool value) {
    return value ? "on" : "off";
}

// " stopwords stemmed" / " (raw)" -- shared by dump and query.
std::string indexFlagsToString(std::uint32_t flags) {
    const std::uint32_t known =
        flags & (mse::kIndexFlagStopWords | mse::kIndexFlagStemmed);
    std::string text;
    if ((known & mse::kIndexFlagStopWords) != 0) text += " stopwords";
    if ((known & mse::kIndexFlagStemmed) != 0) text += " stemmed";
    return text.empty() ? " (raw)" : text;
}

// Like optionValue(), but requires the value to be a floating point number.
double doubleOption(int argc, char** argv, const std::string& name, double fallback) {
    const std::string value = optionValue(argc, argv, name, "");
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        throw std::runtime_error("option " + name + " expects a number, got '" + value + "'");
    }
}

// Minimal JSON string escaping (only what terms / query text need).
std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) out += c;
        }
    }
    return out;
}

// True when a bare flag like "--all" appears in argv.
bool hasFlag(int argc, char** argv, const std::string& name) {
    for (int i = 2; i < argc; ++i) {
        if (name == argv[i]) {
            return true;
        }
    }
    return false;
}

// Collect the shared tokenizer options (--keep-headings/--keep-speakers/
// --keep-stage) from the command line.
mse::TokenizerOptions tokenizerOptionsFromArgs(int argc, char** argv) {
    mse::TokenizerOptions options;
    options.keepHeadings = hasFlag(argc, argv, "--keep-headings");
    options.keepSpeakers = hasFlag(argc, argv, "--keep-speakers");
    options.keepStage = hasFlag(argc, argv, "--keep-stage");
    return options;
}

int commandTokenize(int argc, char** argv) {
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const mse::TokenizerOptions options = tokenizerOptionsFromArgs(argc, argv);
    const bool all = hasFlag(argc, argv, "--all");

    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const mse::Tokenizer tokenizer(options);
    std::vector<mse::Token> tokens;

    // --all: one "docid<TAB>token count" line per document (for scripting)
    if (all) {
        for (std::size_t i = 0; i < corpus.size(); ++i) {
            tokens.clear();
            tokenizer.tokenize(corpus[i], tokens);
            std::cout << corpus[i].id << '\t' << tokens.size() << '\n';
        }
        return 0;
    }

    const int docID = intOption(argc, argv, "--doc", "1");
    const int limit = intOption(argc, argv, "--limit", "20");
    const mse::Document& doc = corpus.byID(docID);
    tokenizer.tokenize(doc, tokens);

    std::cout << "doc#" << doc.id << " \"" << doc.title << "\" ("
              << doc.category << " / " << doc.kind << ")\n";
    std::cout << "options: headings=" << onOff(options.keepHeadings)
              << " speakers=" << onOff(options.keepSpeakers)
              << " stage=" << onOff(options.keepStage) << "\n";
    std::cout << "tokens : " << tokens.size() << "\n";
    std::cout << "sample :\n";
    for (int i = 0; i < limit && static_cast<std::size_t>(i) < tokens.size(); ++i) {
        std::cout << "  line " << tokens[static_cast<std::size_t>(i)].line
                  << "\t" << tokens[static_cast<std::size_t>(i)].text << "\n";
    }
    return 0;
}

int commandCount(int argc, char** argv) {
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const std::string csvPath = optionValue(argc, argv, "--csv", "build/wordcount.csv");
    const std::string stopPath = optionValue(argc, argv, "--stop-words", "");
    int top = intOption(argc, argv, "--top", "50");
    if (top < 0) {
        top = 0;
    }

    mse::StopWords stopWords;
    const mse::StopWords* stopPtr = nullptr;
    if (!stopPath.empty()) {
        stopWords = mse::StopWords::load(stopPath);
        stopPtr = &stopWords;
    }

    mse::CountOptions countOptions;
    countOptions.tokenizer = tokenizerOptionsFromArgs(argc, argv);
    countOptions.stopWords = stopPtr;
    countOptions.stem = hasFlag(argc, argv, "--stem");

    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const mse::WordCount result = mse::countWords(corpus, countOptions);
    mse::writeWordCountCsv(csvPath, result);

    std::cout << "manifest : " << manifest << "\n"
              << "documents: " << result.documents << "\n"
              << "tokens   : " << result.tokens << "\n"
              << "distinct : " << result.distinctTerms << "\n"
              << "hapax    : " << result.hapax << " (tf = 1)\n";
    if (stopPtr != nullptr) {
        const std::uint64_t allTokens = result.tokens + result.stopwordTokens;
        const double removedPercent =
            allTokens == 0 ? 0.0
                           : 100.0 * static_cast<double>(result.stopwordTokens) /
                                 static_cast<double>(allTokens);
        std::cout << "stopwords: " << stopPath << " (" << stopPtr->size() << " words)\n"
                  << "removed  : " << result.stopwordTokens << " tokens ("
                  << std::fixed << std::setprecision(2) << removedPercent << "%)\n";
        std::cout.unsetf(std::ios::fixed);
    }
    if (countOptions.stem) {
        std::cout << "stemming : on (Porter 1980)\n";
    }
    std::cout << "csv      : " << csvPath << "\n";

    // stop-word candidates for the task (1) analysis (plan.md 4.2)
    std::size_t candidates = 0;
    std::string list;
    for (const mse::TermRow& row : result.rows) {
        if (!row.stopCandidate) {
            continue;
        }
        if (candidates < 40) {
            if (!list.empty()) list += ' ';
            list += row.term;
        }
        ++candidates;
    }
    std::cout << "stop-candidates (df_ratio>=0.75 && tf_share>=0.1%): " << candidates << "\n";
    if (!list.empty()) {
        std::cout << "  " << list << (candidates > 40 ? " ..." : "") << "\n";
    }

    if (top > 0) {
        std::cout << "\ntop " << top << " by term frequency:\n"
                  << "  rank\tterm\ttf\tdf\tdf%\tshare%\n";
        for (int i = 0; i < top && static_cast<std::size_t>(i) < result.rows.size(); ++i) {
            const mse::TermRow& row = result.rows[static_cast<std::size_t>(i)];
            std::cout << "  " << (i + 1) << '\t' << row.term << '\t' << row.tf
                      << '\t' << row.df << '\t'
                      << std::fixed << std::setprecision(1) << (100.0 * row.dfRatio)
                      << '\t' << (100.0 * row.tfShare) << '\n';
        }
        std::cout.unsetf(std::ios::fixed);
    }
    return 0;
}

int commandStopwords(int argc, char** argv) {
    if (!hasFlag(argc, argv, "--derive")) {
        throw std::runtime_error("stopwords needs --derive (statistical rule; "
                                 "see `mse` usage)");
    }
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const std::string outPath = optionValue(argc, argv, "--out", "build/stopwords-derived.txt");
    const std::string comparePath = optionValue(argc, argv, "--compare", "");

    // Rule thresholds default to StopWordRule's; each can be overridden for
    // the sensitivity sweep (the point of the entropy experiment).
    mse::StopWordRule rule;
    rule.minDfRatio = doubleOption(argc, argv, "--min-df-ratio", rule.minDfRatio);
    rule.minTfShare = doubleOption(argc, argv, "--min-tf-share", rule.minTfShare);
    rule.minEntropyRateNorm =
        doubleOption(argc, argv, "--min-entropy-rate", rule.minEntropyRateNorm);

    // Derivation always counts the *raw* stream (no stop-word filter): the
    // rule must see the full distribution to judge every candidate. The
    // tokenizer line-kind options and --stem stay available so the derived
    // list matches whichever pipeline configuration the index will use.
    mse::CountOptions countOptions;
    countOptions.tokenizer = tokenizerOptionsFromArgs(argc, argv);
    countOptions.stem = hasFlag(argc, argv, "--stem");

    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const mse::WordCount result = mse::countWords(corpus, countOptions);
    const std::vector<std::string> words = mse::deriveStopWords(result, rule);

    // The list file carries its own provenance: the rule, the thresholds and
    // the corpus it was derived from (deliberately no timestamp, so two runs
    // over the same corpus produce byte-identical files).
    std::ostringstream header;
    header << "# Derived stop word list -- generated by `mse stopwords --derive`.\n"
           << "# Rule (entropy-based extension of plan.md 4.2, see count.hpp):\n"
           << "#   df_ratio          >= " << rule.minDfRatio << "\n"
           << "#   tf_share          >= " << rule.minTfShare << "\n"
           << "#   entropy_rate_norm >= " << rule.minEntropyRateNorm << "\n"
           << "# Corpus: " << manifest << " (" << result.documents
           << " documents, " << result.tokens << " tokens"
           << (countOptions.stem ? ", stemmed" : "") << ")\n"
           << "# Matched " << words.size() << " terms (tf descending).\n";
    {
        const std::filesystem::path target(outPath);
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path());
        }
        std::ofstream out(outPath);
        if (!out) {
            throw std::runtime_error("cannot write stop word list: " + outPath);
        }
        out << header.str();
        for (const std::string& word : words) {
            out << word << '\n';
        }
    }

    std::cout << header.str();  // same provenance on the terminal
    std::cout << "list     : ";
    for (const std::string& word : words) {
        std::cout << word << ' ';
    }
    std::cout << "(" << words.size() << " words)\n"
              << "file     : " << outPath << "\n";

    // Optional diff against a hand-reviewed list: the overlap and both
    // difference sets are the interesting output of the experiment.
    if (!comparePath.empty()) {
        const mse::StopWords other = mse::StopWords::load(comparePath);
        // The static list is probed against the whole counted vocabulary,
        // which is a superset of anything either list can contain.
        std::set<std::string> derived(words.begin(), words.end());
        std::set<std::string> statics;
        for (const mse::TermRow& row : result.rows) {
            if (other.contains(row.term)) {
                statics.insert(row.term);
            }
        }
        std::set<std::string> overlap;
        std::set_intersection(derived.begin(), derived.end(), statics.begin(),
                              statics.end(), std::inserter(overlap, overlap.begin()));
        std::set<std::string> onlyStatic;
        std::set_difference(statics.begin(), statics.end(), derived.begin(),
                            derived.end(),
                            std::inserter(onlyStatic, onlyStatic.begin()));
        std::set<std::string> onlyDerived;
        std::set_difference(derived.begin(), derived.end(), statics.begin(),
                            statics.end(),
                            std::inserter(onlyDerived, onlyDerived.begin()));
        std::cout << "compare  : " << comparePath << " (" << statics.size()
                  << " words in vocabulary)\n"
                  << "overlap  : " << overlap.size() << "\n"
                  << "only in list   : ";
        for (const std::string& word : onlyStatic) std::cout << word << ' ';
        std::cout << "(" << onlyStatic.size() << ")\n"
                  << "only in derived: ";
        for (const std::string& word : onlyDerived) std::cout << word << ' ';
        std::cout << "(" << onlyDerived.size() << ")\n";
    }
    return 0;
}

int commandBuild(int argc, char** argv) {
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const std::string indexPath = optionValue(argc, argv, "--index", "build/index.bin");
    const std::string stopPath = optionValue(argc, argv, "--stop-words", "");
    const std::string dictName = optionValue(argc, argv, "--dict", "splay");

    mse::StopWords stopWords;
    mse::BuildOptions options;
    options.tokenizer = tokenizerOptionsFromArgs(argc, argv);
    if (!stopPath.empty()) {
        stopWords = mse::StopWords::load(stopPath);
        options.stopWords = &stopWords;
    }
    options.stem = hasFlag(argc, argv, "--stem");

    std::unique_ptr<mse::Dictionary> dictionary;
    if (dictName == "splay") {
        dictionary = std::make_unique<mse::SplayDictionary>();
    } else if (dictName == "hash") {
        dictionary = std::make_unique<mse::HashDictionary>();
    } else {
        throw std::runtime_error("unknown --dict '" + dictName +
                                 "' (expected splay|hash)");
    }

    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const auto started = std::chrono::steady_clock::now();
    const mse::Index index = mse::buildIndex(corpus, options, *dictionary);
    mse::validateIndex(index);
    mse::writeIndex(indexPath, index);
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();

    std::uint64_t postings = 0;
    std::uint64_t positions = 0;
    for (const mse::IndexTerm& term : index.terms) {
        postings += term.postings.size();
        positions += term.positions.size();
    }
    const mse::DictStats stats = dictionary->stats();
    std::cout << "dictionary : " << dictName << "  (comparisons="
              << stats.stringComparisons << ", rotations=" << stats.rotations
              << ", rehashes=" << stats.rehashes << ")\n"
              << "documents  : " << index.docs.size() << "\n"
              << "tokens     : " << index.tokenCount << "\n"
              << "terms      : " << index.terms.size() << "\n"
              << "postings   : " << postings << "\n"
              << "positions  : " << positions << "\n"
              << "index      : " << indexPath << " ("
              << std::filesystem::file_size(indexPath) << " bytes)\n"
              << "build time : " << std::fixed << std::setprecision(2)
              << seconds << " s\n";
    std::cout.unsetf(std::ios::fixed);
    std::cout << "invariants : OK\n";
    return 0;
}

int commandDump(int argc, char** argv) {
    const std::string indexPath = optionValue(argc, argv, "--index", "build/index.bin");
    const std::string term = optionValue(argc, argv, "--term", "");
    int limit = intOption(argc, argv, "--limit", "20");
    if (term.empty()) {
        throw std::runtime_error("dump needs --term <word>");
    }
    if (limit < 0) {
        limit = 0;
    }

    const mse::Index index = mse::loadIndex(indexPath);
    std::cout << "index      : " << indexPath << "  (" << index.docs.size()
              << " docs, " << index.terms.size() << " terms, "
              << index.tokenCount << " tokens)\n"
              << "flags      :" << indexFlagsToString(index.flags) << "\n";

    if (!mse::dumpTerm(index, term, static_cast<std::size_t>(limit), std::cout)) {
        std::cout << "term not found: " << term
                  << "\n  (dump takes the indexed form: lowercase, stop words removed\n"
                  << "   and stemmed when the index was built with --stem)\n";
    }
    return 0;
}

int commandQuery(int argc, char** argv) {
    const std::string indexPath = optionValue(argc, argv, "--index", "build/index.bin");
    const std::string stopPath = optionValue(argc, argv, "--stop-words", "");
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const bool pretty = hasFlag(argc, argv, "--pretty");

    const std::string wordArg = optionValue(argc, argv, "--word", "");
    const std::string phraseArg = optionValue(argc, argv, "--phrase", "");
    const std::string wordsArg = optionValue(argc, argv, "--words", "");
    const int modeCount = (wordArg.empty() ? 0 : 1) + (phraseArg.empty() ? 0 : 1) +
                          (wordsArg.empty() ? 0 : 1);
    if (modeCount != 1) {
        throw std::runtime_error(
            "query needs exactly one of --word / --phrase / --words");
    }

    mse::QueryOptions options;
    std::string queryText;
    if (!wordArg.empty()) {
        options.mode = mse::QueryMode::Word;
        queryText = wordArg;
    } else if (!phraseArg.empty()) {
        options.mode = mse::QueryMode::Phrase;
        queryText = phraseArg;
    } else {
        options.mode = mse::QueryMode::Boolean;
        queryText = wordsArg;
    }
    options.maxDfRatio = doubleOption(argc, argv, "--max-df", 1.0);
    options.minDf = static_cast<std::uint32_t>(
        std::max(0, intOption(argc, argv, "--min-df", "1")));
    options.minMatch = static_cast<std::uint32_t>(std::max(
        0, intOption(argc, argv, "--min-match",
                     hasFlag(argc, argv, "--or") ? "1" : "0")));
    options.topX =
        static_cast<std::size_t>(std::max(0, intOption(argc, argv, "--top-x", "0")));

    mse::StopWords stopWords;
    const mse::StopWords* stopPtr = nullptr;
    if (!stopPath.empty()) {
        stopWords = mse::StopWords::load(stopPath);
        stopPtr = &stopWords;
    }

    const mse::Index index = mse::loadIndex(indexPath);
    const mse::QueryResult result = mse::runQuery(index, queryText, options, stopPtr);

    const char* modeName = options.mode == mse::QueryMode::Word     ? "word"
                           : options.mode == mse::QueryMode::Phrase ? "phrase"
                                                                    : "words";

    if (hasFlag(argc, argv, "--json")) {
        std::cout << "{\"query\":\"" << jsonEscape(queryText) << "\",\"mode\":\""
                  << modeName << "\",\"terms\":[";
        for (std::size_t i = 0; i < result.terms.size(); ++i) {
            if (i > 0) std::cout << ',';
            std::cout << '"' << jsonEscape(result.terms[i]) << '"';
        }
        std::cout << "],\"hits\":[";
        for (std::size_t i = 0; i < result.hits.size(); ++i) {
            if (i > 0) std::cout << ',';
            const mse::QueryHit& hit = result.hits[i];
            std::cout << "{\"doc\":" << hit.docID << ",\"matched\":" << hit.matched
                      << ",\"tf\":" << hit.tfSum << '}';
        }
        std::cout << "],\"notes\":[";
        for (std::size_t i = 0; i < result.notes.size(); ++i) {
            if (i > 0) std::cout << ',';
            std::cout << "[\"" << jsonEscape(result.notes[i].term) << "\",\""
                      << jsonEscape(result.notes[i].reason) << "\"]";
        }
        std::cout << "]}\n";
        return 0;
    }

    std::cout << "index      : " << indexPath << "  (" << index.docs.size()
              << " docs, " << index.terms.size() << " terms)\n"
              << "flags      :" << indexFlagsToString(index.flags) << "\n"
              << "query      : " << modeName << " \"" << queryText << "\"\n"
              << "terms      :";
    if (result.terms.empty()) {
        std::cout << " (none)";
    } else {
        for (const std::string& term : result.terms) {
            std::cout << ' ' << term;
        }
    }
    std::cout << "\n";
    for (const mse::TermNote& note : result.notes) {
        std::cout << "note       : " << note.term << " -- " << note.reason << "\n";
    }
    std::cout << "results    : " << result.hits.size() << " documents";
    if (options.topX > 0) {
        std::cout << " (--top-x " << options.topX << ")";
    }
    std::cout << "\n";

    for (const mse::QueryHit& hit : result.hits) {
        const mse::IndexDoc& doc = index.docs[hit.docID - 1];
        std::cout << "  " << hit.docID << '\t' << doc.title;
        if (options.mode != mse::QueryMode::Word || result.terms.size() > 1) {
            std::cout << "  [matched " << hit.matched << '/' << result.terms.size()
                      << ", tf " << hit.tfSum << ']';
        }
        std::cout << "\n";
    }

    if (pretty && !result.hits.empty()) {
        if ((index.flags & mse::kIndexFlagStopWords) != 0 && stopPtr == nullptr) {
            std::cerr << "warning: --pretty positions need --stop-words matching the build\n";
        }
        const mse::Corpus corpus = mse::Corpus::load(manifest);
        for (const mse::QueryHit& hit : result.hits) {
            const std::string line =
                mse::lineOfPosition(corpus.byID(hit.docID), index, stopPtr,
                                    hit.firstPosition);
            if (!line.empty()) {
                std::cout << "      > " << line << "\n";
            }
        }
    }
    return 0;
}

int commandStats(int argc, char** argv) {
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const mse::Corpus::Stats stats = corpus.stats();

    std::cout << "manifest : " << manifest << "\n";
    std::cout << "documents: " << stats.documents << "\n";
    std::cout << "lines    : " << stats.lines << "\n";
    std::cout << "  body   : " << stats.linesByKind[static_cast<std::size_t>(mse::LineKind::Body)] << "\n";
    std::cout << "  heading: " << stats.linesByKind[static_cast<std::size_t>(mse::LineKind::Heading)] << "\n";
    std::cout << "  stage  : " << stats.linesByKind[static_cast<std::size_t>(mse::LineKind::Stage)] << "\n";
    std::cout << "  speaker: " << stats.linesByKind[static_cast<std::size_t>(mse::LineKind::Speaker)] << "\n";
    std::cout << "words    : " << stats.words << "\n";
    std::cout << "bytes    : " << stats.bytes << "\n";

    std::map<std::string, std::size_t> byCategory;
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        ++byCategory[corpus[i].category];
    }
    std::cout << "documents by category:\n";
    for (const auto& entry : byCategory) {
        std::cout << "  " << entry.first << ": " << entry.second << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string command = argv[1];
    try {
        if (command == "stats") {
            return commandStats(argc, argv);
        }
        if (command == "tokenize") {
            return commandTokenize(argc, argv);
        }
        if (command == "count") {
            return commandCount(argc, argv);
        }
        if (command == "stopwords") {
            return commandStopwords(argc, argv);
        }
        if (command == "build") {
            return commandBuild(argc, argv);
        }
        if (command == "dump") {
            return commandDump(argc, argv);
        }
        if (command == "query") {
            return commandQuery(argc, argv);
        }
        std::cerr << "unknown or not-yet-implemented command: " << command << "\n\n";
        printUsage(argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
