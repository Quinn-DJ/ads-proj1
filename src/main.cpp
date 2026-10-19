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
#include "stemmer.hpp"
#include "stopwords.hpp"
#include "tokenizer.hpp"

#include "blockbuild.hpp"

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
        << "  build-blocks [opts]  block-wise (SPIMI-style) external build demo\n"
        << "  bptree-bench [opts]  synthetic-scale B+Tree experiment (bonus)\n"
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
        << "  --dict <name>        build dictionary: splay | hash | map (default: splay)\n"
        << "  --stop-words <path>  drop these tokens (recorded in the index flags)\n"
        << "  --stem               Porter-stem the tokens (recorded in the flags)\n"
        << "  --dict-file <path>   also write the term->postings B+Tree sidecar\n"
        << "                       (disk dictionary; query with --dict-file)\n"
        << "  --compress           v2 postings encoding: docID/position gaps in\n"
        << "                       byte-aligned varints (plan.md 6.4)\n"
        << "\n"
        << "build-blocks options (same pipeline flags as build, plus):\n"
        << "  --block-terms <n>    in-memory vocabulary budget per block (default 4096)\n"
        << "\n"
        << "bptree-bench options:\n"
        << "  --keys <n>           synthetic key count (default 1000000; keys are\n"
        << "                       the ascending base-26 counter, 26^6 = 309M max)\n"
        << "  --lookups <n>        verified random lookups (default 1000)\n"
        << "  --page-size <n>      B+Tree page budget (default 4096)\n"
        << "  --out <path>         output page file (default build/bptree-bench.bpt)\n"
        << "\n"
        << "dump options:\n"
        << "  --index <path>       index file (default: build/index.bin)\n"
        << "  --term <word>        term to dump (the indexed form)\n"
        << "  --limit <n>          max postings printed (default: 20)\n"
        << "  --dict-file <path>   resolve the term via the B+Tree sidecar\n"
        << "                       (loads no term table; needs --index too)\n"
        << "  --verify-all         with --dict-file: re-parse every term's\n"
        << "                       postings and compare against a full load\n"
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
        << "  --dict-file <path>   resolve the term via the B+Tree sidecar\n"
        << "                       (word queries only; no term table loaded)\n"
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
    const std::string dictFilePath = optionValue(argc, argv, "--dict-file", "");
    const bool compress = hasFlag(argc, argv, "--compress");

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
    } else if (dictName == "map") {
        dictionary = std::make_unique<mse::MapDictionary>();
    } else {
        throw std::runtime_error("unknown --dict '" + dictName +
                                 "' (expected splay|hash|map)");
    }

    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const auto started = std::chrono::steady_clock::now();
    const mse::Index index = mse::buildIndex(corpus, options, *dictionary);
    mse::validateIndex(index);
    if (compress) {
        mse::writeIndexCompressed(indexPath, index);
    } else {
        mse::writeIndex(indexPath, index);
    }
    if (!dictFilePath.empty()) {
        // Disk dictionary sidecar: term -> posting byte range (plan.md 11).
        mse::writeDictFile(index, dictFilePath, 4096, compress);
    }
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
              << std::filesystem::file_size(indexPath) << " bytes"
              << (compress ? ", v2 gap+varint" : "") << ")\n";
    if (!dictFilePath.empty()) {
        std::cout << "dict-file  : " << dictFilePath << " ("
                  << std::filesystem::file_size(dictFilePath)
                  << " bytes, b+tree)\n";
    }
    std::cout << "build time : " << std::fixed << std::setprecision(2)
              << seconds << " s\n";
    std::cout.unsetf(std::ios::fixed);
    std::cout << "invariants : OK\n";
    return 0;
}

int commandBuildBlocks(int argc, char** argv) {
    // Same pipeline options as `build`, plus the memory budget that drives
    // the block spills -- the bonus demo: O(blockTerms) dictionary memory
    // regardless of corpus vocabulary size.
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const std::string indexPath = optionValue(argc, argv, "--index", "build/index.bin");
    const std::string stopPath = optionValue(argc, argv, "--stop-words", "");
    int blockTerms = intOption(argc, argv, "--block-terms", "4096");
    const bool compress = hasFlag(argc, argv, "--compress");
    if (blockTerms < 1) {
        throw std::runtime_error("--block-terms must be at least 1");
    }

    mse::StopWords stopWords;
    mse::BlockBuildOptions options;
    options.build.tokenizer = tokenizerOptionsFromArgs(argc, argv);
    if (!stopPath.empty()) {
        stopWords = mse::StopWords::load(stopPath);
        options.build.stopWords = &stopWords;
    }
    options.build.stem = hasFlag(argc, argv, "--stem");
    options.blockTerms = static_cast<std::size_t>(blockTerms);

    const mse::Corpus corpus = mse::Corpus::load(manifest);
    mse::BlockBuildStats stats;
    const mse::Index index = mse::buildIndexBlocked(corpus, options, stats);
    mse::validateIndex(index);
    if (compress) {
        mse::writeIndexCompressed(indexPath, index);
    } else {
        mse::writeIndex(indexPath, index);
    }

    std::cout << "blocks    : " << stats.blocks
              << " (budget " << options.blockTerms << " terms, peak "
              << stats.maxLiveTerms << " live)\n"
              << "documents : " << index.docs.size() << "\n"
              << "tokens    : " << index.tokenCount << "\n"
              << "terms     : " << index.terms.size() << "\n"
              << "run bytes : " << stats.blockBytes << " (temporary, removed)\n"
              << "scan time : " << std::fixed << std::setprecision(2)
              << stats.scanSeconds << " s\n"
              << "merge time: " << stats.mergeSeconds << " s\n"
              << "index     : " << indexPath << " ("
              << std::filesystem::file_size(indexPath) << " bytes)\n";
    std::cout.unsetf(std::ios::fixed);
    std::cout << "invariants: OK\n";
    return 0;
}

// 26^e for the bench's base-26 key generator (e <= 5).
int pow26(int exponent) {
    int value = 1;
    for (int i = 0; i < exponent; ++i) {
        value *= 26;
    }
    return value;
}

int commandBptreeBench(int argc, char** argv) {
    // Scale experiment for the bonus answer (plan.md 11): build a B+Tree
    // over N synthetic keys and measure what actually matters at 400M
    // terms -- height, file size and page reads per lookup. Keys are the
    // base-26 counter in ascending order (deterministic, unique, sorted:
    // exactly the shape bulkLoad expects from the term table).
    const int keys = intOption(argc, argv, "--keys", "1000000");
    const int lookups = intOption(argc, argv, "--lookups", "1000");
    const int pageSize = intOption(argc, argv, "--page-size", "4096");
    const std::string outPath = optionValue(argc, argv, "--out", "build/bptree-bench.bpt");
    if (keys < 2 || lookups < 1 || pageSize < 1056) {
        throw std::runtime_error("bptree-bench: need --keys >= 2, --lookups >= 1, "
                                 "--page-size >= 1056");
    }

    const auto started = std::chrono::steady_clock::now();
    std::vector<std::pair<std::string, std::uint64_t>> pairs;
    pairs.reserve(static_cast<std::size_t>(keys));
    for (int c = 0; c < keys; ++c) {
        std::string key(6, 'a');  // 26^6 = 309M keys; 6 chars, SSO-friendly
        for (int digit = 0; digit < 6; ++digit) {
            // most significant digit first: lexicographic == numeric order,
            // so the generated sequence is already sorted for bulkLoad
            key[static_cast<std::size_t>(5 - digit)] =
                static_cast<char>('a' + (c / pow26(digit)) % 26);
        }
        pairs.emplace_back(key, static_cast<std::uint64_t>(c));
    }
    const mse::BTree tree = mse::BTree::bulkLoad(pairs, static_cast<std::uint32_t>(pageSize));
    tree.validate();
    tree.writeFile(outPath);
    const double buildSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    const mse::BTreeStats stats = tree.stats();
    const std::uintmax_t fileSize = std::filesystem::file_size(outPath);

    // Cold-ish lookup path: cache of one page, so every descent re-reads
    // the inner pages -- the I/O a query pays when the dictionary is huge.
    const mse::BTreeFile file(outPath, 1);
    std::uint32_t state = 987654321u;
    std::uint64_t readsBefore = file.pageReads();
    for (int i = 0; i < lookups; ++i) {
        state = state * 1664525u + 1013904223u;
        const std::uint64_t wanted = state % static_cast<std::uint32_t>(keys);
        const auto found = file.findValue(pairs[static_cast<std::size_t>(wanted)].first);
        if (!found.has_value() || *found != wanted) {
            throw std::runtime_error("bptree-bench: wrong value for key " +
                                     std::to_string(wanted));
        }
    }
    const double readsPerLookup =
        static_cast<double>(file.pageReads() - readsBefore) / lookups;

    std::cout << "keys      : " << keys << "\n"
              << "page size : " << pageSize << "\n"
              << "height    : " << stats.height << "\n"
              << "pages     : " << stats.pageCount
              << " (" << stats.leafCount << " leaves)\n"
              << "fill      : min " << stats.minFillBytes << " / max "
              << stats.maxFillBytes << " bytes (budget " << pageSize << ")\n"
              << "file      : " << outPath << " (" << fileSize
              << " bytes, " << static_cast<double>(fileSize) / keys
              << " bytes/key)\n"
              << "lookups   : " << lookups << " verified, "
              << std::fixed << std::setprecision(2) << readsPerLookup
              << " page reads each (cache = 1 page)\n"
              << "build     : " << buildSeconds << " s (generate+load+write)\n";
    std::cout.unsetf(std::ios::fixed);
    return 0;
}

int commandDump(int argc, char** argv) {
    const std::string indexPath = optionValue(argc, argv, "--index", "build/index.bin");
    const std::string term = optionValue(argc, argv, "--term", "");
    const std::string dictFilePath = optionValue(argc, argv, "--dict-file", "");
    const bool verifyAll = hasFlag(argc, argv, "--verify-all");
    int limit = intOption(argc, argv, "--limit", "20");
    if (limit < 0) {
        limit = 0;
    }

    // Disk-dictionary path: resolve the term through the B+Tree sidecar
    // and read only its posting bytes (the term table is never loaded).
    if (!dictFilePath.empty()) {
        const mse::LazyIndexReader lazy(indexPath, dictFilePath);
        std::cout << "index      : " << indexPath << "  (" << lazy.docs().size()
                  << " docs, " << lazy.termCount() << " terms, "
                  << lazy.tokenCount() << " tokens)\n";
        std::cout << "dict-file  : " << dictFilePath << "  (height "
                  << lazy.dictHeight() << ", reads " << lazy.dictPageReads()
                  << ")\n";
        std::cout << "flags      :" << indexFlagsToString(lazy.flags()) << "\n";

        if (verifyAll) {
            // Reference: a full load; the sweep must agree on every term.
            const mse::Index full = mse::loadIndex(indexPath);
            std::size_t checked = 0;
            std::size_t mismatches = 0;
            for (const auto& [name, packed] : lazy.allTerms()) {
                (void)packed;
                mse::IndexTerm lazyTerm;
                if (!lazy.readTerm(name, lazyTerm)) {
                    ++mismatches;
                    std::cout << "  missing via dict: " << name << "\n";
                    continue;
                }
                const mse::IndexTerm* ref = mse::findIndexTerm(full, name);
                bool same = ref != nullptr &&
                            lazyTerm.postings.size() == ref->postings.size() &&
                            lazyTerm.positions.size() == ref->positions.size();
                if (same) {
                    for (std::size_t i = 0; i < lazyTerm.postings.size() && same; ++i) {
                        same = lazyTerm.postings[i].docID == ref->postings[i].docID &&
                               lazyTerm.postings[i].tf == ref->postings[i].tf;
                    }
                    same = same && lazyTerm.positions == ref->positions;
                }
                ++checked;
                if (!same) {
                    ++mismatches;
                    if (mismatches <= 5) {
                        std::cout << "  mismatch: " << name << "\n";
                    }
                }
            }
            std::cout << "verified   : " << checked << " terms, "
                      << mismatches << " mismatches\n"
                      << "dict reads : " << lazy.dictPageReads()
                      << " page reads, " << lazy.dictCacheHits()
                      << " cache hits\n";
            return mismatches == 0 ? 0 : 1;
        }

        if (term.empty()) {
            throw std::runtime_error("dump needs --term <word> (or --verify-all)");
        }
        // One-term dump: reuse dumpTerm over a single-term view.
        mse::IndexTerm lazyTerm;
        if (!lazy.readTerm(term, lazyTerm)) {
            std::cout << "term not found: " << term << "\n";
            return 0;
        }
        mse::Index view;
        view.flags = lazy.flags();
        view.tokenCount = lazy.tokenCount();
        view.docs = lazy.docs();
        view.terms = {std::move(lazyTerm)};
        mse::dumpTerm(view, term, static_cast<std::size_t>(limit), std::cout);
        std::cout << "dict reads : " << lazy.dictPageReads()
                  << " page reads, " << lazy.dictCacheHits()
                  << " cache hits\n"
                  << "index read: " << lazy.bytesRead()
                  << " bytes (vs " << std::filesystem::file_size(indexPath)
                  << " for a full load)\n";
        return 0;
    }

    if (term.empty()) {
        throw std::runtime_error("dump needs --term <word>");
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
    const std::string dictFilePath = optionValue(argc, argv, "--dict-file", "");
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
    if (!dictFilePath.empty() && wordArg.empty()) {
        throw std::runtime_error("--dict-file supports --word queries only");
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

    // Normal path: load the whole index (term table included). Disk-
    // dictionary path: load only the doc table, resolve the (single) word
    // through the B+Tree sidecar and attach just its posting list -- the
    // runQuery() logic, thresholds and output are shared unchanged.
    mse::Index index;
    std::unique_ptr<mse::LazyIndexReader> lazy;
    if (!dictFilePath.empty()) {
        lazy = std::make_unique<mse::LazyIndexReader>(indexPath, dictFilePath);
        index.flags = lazy->flags();
        index.tokenCount = lazy->tokenCount();
        index.docs = lazy->docs();
        mse::Tokenizer tokenizer;  // default options, like the query side
        std::vector<mse::Token> queryTokens;
        tokenizer.tokenizeText(wordArg, 0, queryTokens);
        if (!queryTokens.empty()) {
            std::string normalized = queryTokens.front().text;
            const bool filtered =
                (index.flags & mse::kIndexFlagStopWords) != 0 && stopPtr != nullptr &&
                stopPtr->contains(normalized);
            if (!filtered) {
                if ((index.flags & mse::kIndexFlagStemmed) != 0) {
                    normalized = mse::stem(normalized);
                }
                mse::IndexTerm termRow;
                if (lazy->readTerm(normalized, termRow)) {
                    index.terms = {std::move(termRow)};
                }
            }
        }
    } else {
        index = mse::loadIndex(indexPath);
    }
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
        if (command == "build-blocks") {
            return commandBuildBlocks(argc, argv);
        }
        if (command == "bptree-bench") {
            return commandBptreeBench(argc, argv);
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
