// main.cpp -- command line entry point of the mini search engine (`mse`).
//
// Current commands:
//   mse stats      load the corpus and print storage statistics
//   mse tokenize   print the token stream of one document (or all documents)
//   mse count      word frequency / document frequency table -> CSV (task 1)
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
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Print the command/option reference to stderr.
//
// The help text is a single stream rather than a table structure: it is
// static, checked by eye, and keeping it literal makes the documented option
// names easy to grep against the parsers below. Commands that are planned but
// not implemented are listed explicitly so users do not mistake them for bugs.
void printUsage(const char* program) {
    std::cerr
        << "usage: " << program << " <command> [options]\n"
        << "\n"
        // Command list: one line per implemented command; planned commands
        // are named at the bottom so they are not mistaken for missing bugs.
        << "commands:\n"
        << "  stats                load manifest + text files and print statistics\n"
        << "  tokenize [options]   print the token stream of one document\n"
        << "  count [options]      word count table (tf/df) -> CSV\n"
        << "  build [options]      build the inverted index -> <index file>\n"
        << "  dump [options]       print one term's postings from an index file\n"
        << "  query [options]      run a query against the index (task 3)\n"
        << "\n"
        // Options shared by every command.
        << "options:\n"
        << "  --manifest <path>    manifest file (default: data/manifest.tsv)\n"
        << "\n"
        // Per-command option groups, in the same order as the commands.
        << "tokenize options:\n"
        << "  --doc <id>           document id (default: 1)\n"
        << "  --all                print \"docid<TAB>tokens\" for every document\n"
        << "  --limit <n>          how many sample tokens to print (default: 20)\n"
        << "  --keep-headings      include [ACT I] / [SCENE ...] lines\n"
        << "  --keep-speakers      include @BERNARDO lines\n"
        << "  --keep-stage         include (Exit Ghost) lines\n"
        << "\n"
        << "count options:\n"
        // The count command's CSV is the artifact cross-checked with Python;
        // --top only affects the terminal summary.
        << "  --csv <path>         output CSV (default: build/wordcount.csv)\n"
        << "  --top <n>            print the top n rows (default: 50, 0 = quiet)\n"
        << "  --stop-words <path>  drop words from this list before counting\n"
        << "  --stem               Porter-stem the tokens before counting\n"
        << "\n"
        << "build options:\n"
        // --dict selects the dictionary implementation; both must produce
        // byte-identical output (the cross-check property).
        << "  --index <path>       output index file (default: build/index.bin)\n"
        << "  --dict <name>        build dictionary: splay | hash (default: splay)\n"
        << "  --stop-words <path>  drop these tokens (recorded in the index flags)\n"
        << "  --stem               Porter-stem the tokens (recorded in the flags)\n"
        << "\n"
        << "dump options:\n"
        // --term takes the indexed form (lowercase, stop-filtered, stemmed);
        // the command prints a hint when a term is missing.
        << "  --index <path>       index file (default: build/index.bin)\n"
        << "  --term <word>        term to dump (the indexed form)\n"
        << "  --limit <n>          max postings printed (default: 20)\n"
        << "\n"
        << "query options:\n"
        // Exactly one of --word/--phrase/--words is required. The threshold
        // options default to "off" so a bare query is the baseline case in
        // the task 4 experiments.
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
//
// The scan starts at argv[2] (argv[1] is the command) and takes the first
// match; the parser does not support repeated options. Values are returned as
// strings and converted by intOption/doubleOption, so a malformed number gets
// a uniform error message instead of silently becoming 0.
std::string optionValue(int argc, char** argv, const std::string& name,
                        const std::string& fallback) {
    for (int i = 2; i + 1 < argc; ++i) {
        if (name == argv[i]) {
            return argv[i + 1];
        }
    }
    return fallback;
}

// Like optionValue(), but parse the value as an integer.
//
// std::stoi accepts a leading prefix ("10abc" -> 10), which is acceptable for
// a course CLI; a value that does not start with a number is reported with the
// option name. Range clamping (e.g. negative --min-df) happens in the command
// that owns the option, where the valid domain is known.
int intOption(int argc, char** argv, const std::string& name, const std::string& fallback) {
    const std::string value = optionValue(argc, argv, name, fallback);
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        throw std::runtime_error("option " + name + " expects a number, got '" + value + "'");
    }
}

// Format a boolean option as "on"/"off" for the tokenize header.
const char* onOff(bool value) {
    return value ? "on" : "off";
}

// " stopwords stemmed" / " (raw)" -- shared by dump and query.
//
// Only the two known flag bits are interpreted; unknown bits are ignored so a
// future v2 file with extra flags still prints something sensible instead of
// garbage or an exception.
std::string indexFlagsToString(std::uint32_t flags) {
    const std::uint32_t known =
        flags & (mse::kIndexFlagStopWords | mse::kIndexFlagStemmed);
    std::string text;
    if ((known & mse::kIndexFlagStopWords) != 0) text += " stopwords";
    if ((known & mse::kIndexFlagStemmed) != 0) text += " stemmed";
    return text.empty() ? " (raw)" : text;
}

// Like optionValue(), but parse the value as a double.
//
// An absent option (empty string) returns the fallback, which is how the
// threshold defaults are expressed. A non-numeric value throws with the option
// name; NaN/inf are not rejected here because the threshold comparisons simply
// treat them as extremes.
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
//
// Terms are ASCII, but query text can contain quotes, backslashes or tabs;
// escaping them keeps --json parseable by the threshold experiment script.
// Control characters below 0x20 other than the named ones are dropped, which
// is acceptable for display/analysis output.
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

// True when a bare flag like "--all" appears anywhere in argv[2..].
//
// Flags take no value, so unlike optionValue() there is no lookahead; the
// whole tail is scanned. Unknown flags are ignored, which keeps command
// parsing permissive and lets --help-style additions happen without touching
// every command.
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
//
// count, build and tokenize all use this helper so the three stages agree on
// which lines contribute text. The defaults (all off) define the formal
// index; the flags exist for the Task 1 experiments and sensitivity checks.
mse::TokenizerOptions tokenizerOptionsFromArgs(int argc, char** argv) {
    mse::TokenizerOptions options;
    options.keepHeadings = hasFlag(argc, argv, "--keep-headings");
    options.keepSpeakers = hasFlag(argc, argv, "--keep-speakers");
    options.keepStage = hasFlag(argc, argv, "--keep-stage");
    return options;
}

// `tokenize` -- inspect the token stream of one document (or all documents).
//
// Two modes: with --all it prints "docid<TAB>count" per document for scripts
// and cross-checks; otherwise it prints the first --limit tokens of --doc with
// their source line numbers. This is a debugging/experiment command, not part
// of the graded pipeline, but it is how the tokenizer was verified against the
// independent Python implementation.
int commandTokenize(int argc, char** argv) {
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const mse::TokenizerOptions options = tokenizerOptionsFromArgs(argc, argv);
    const bool all = hasFlag(argc, argv, "--all");

    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const mse::Tokenizer tokenizer(options);
    std::vector<mse::Token> tokens;

    // --all: one "docid<TAB>token count" line per document. The output is
    // intentionally machine-friendly (no header, no prose) so it can be
    // summed/diffed from the shell; this is the form used in the cross-check.
    if (all) {
        // One document at a time; the token vector is reused so the batch
        // mode does not allocate per document. Output order is docID order.
        for (std::size_t i = 0; i < corpus.size(); ++i) {
            tokens.clear();
            tokenizer.tokenize(corpus[i], tokens);
            std::cout << corpus[i].id << '\t' << tokens.size() << '\n';
        }
        return 0;
    }

    // Single-document mode. byID() validates the range and throws with the
    // offending ID, so a typo produces a clear error rather than an empty
    // sample that looks like a tokenizer bug.
    const int docID = intOption(argc, argv, "--doc", "1");
    const int limit = intOption(argc, argv, "--limit", "20");
    const mse::Document& doc = corpus.byID(docID);
    tokenizer.tokenize(doc, tokens);

    // Header first (document identity + which line kinds were included), then
    // the token sample with source line numbers.
    std::cout << "doc#" << doc.id << " \"" << doc.title << "\" ("
              << doc.category << " / " << doc.kind << ")\n";
    std::cout << "options: headings=" << onOff(options.keepHeadings)
              << " speakers=" << onOff(options.keepSpeakers)
              << " stage=" << onOff(options.keepStage) << "\n";
    // Token total plus the first `limit` tokens. Each sample line shows the
    // source line index and the normalized token, which is exactly the pair
    // the --pretty mapping later relies on.
    std::cout << "tokens : " << tokens.size() << "\n";
    std::cout << "sample :\n";
    for (int i = 0; i < limit && static_cast<std::size_t>(i) < tokens.size(); ++i) {
        std::cout << "  line " << tokens[static_cast<std::size_t>(i)].line
                  << "\t" << tokens[static_cast<std::size_t>(i)].text << "\n";
    }
    return 0;
}

// `count` -- Task 1 word counting (tf/df) with optional stop words and stemming.
//
// Produces the full CSV (the machine-readable artifact compared byte-for-byte
// with tools/check_wordcount.py) and a terminal summary: token/type/hapax
// counts, the coarse stop-candidate list, and the top-n table. The four
// variants used in the report are all this command with different flags.
int commandCount(int argc, char** argv) {
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const std::string csvPath = optionValue(argc, argv, "--csv", "build/wordcount.csv");
    const std::string stopPath = optionValue(argc, argv, "--stop-words", "");
    int top = intOption(argc, argv, "--top", "50");
    if (top < 0) {
        top = 0;
    }

    // The stop list is optional. When absent, stopPtr stays null and the
    // counter runs the raw pipeline; when present, `stopWords` is a local that
    // outlives countWords(), so the raw pointer in CountOptions is safe.
    mse::StopWords stopWords;
    const mse::StopWords* stopPtr = nullptr;
    if (!stopPath.empty()) {
        stopWords = mse::StopWords::load(stopPath);
        stopPtr = &stopWords;
    }

    // The three stages are independent flags: tokenizer line kinds, stop
    // filter, and stemming. This function never changes the order (filter
    // before stem), matching build and query.
    mse::CountOptions countOptions;
    countOptions.tokenizer = tokenizerOptionsFromArgs(argc, argv);
    countOptions.stopWords = stopPtr;
    countOptions.stem = hasFlag(argc, argv, "--stem");

    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const mse::WordCount result = mse::countWords(corpus, countOptions);
    mse::writeWordCountCsv(csvPath, result);

    // Summary fields used in the Task 1 analysis: `tokens` counts only tokens
    // that survived the stop filter, `distinct` is the vocabulary size, and
    // `hapax` is the number of terms with tf == 1. The ratio hapax/distinct is
    // the Zipf long-tail observation discussed in Chapter 4.
    std::cout << "manifest : " << manifest << "\n"
              << "documents: " << result.documents << "\n"
              << "tokens   : " << result.tokens << "\n"
              << "distinct : " << result.distinctTerms << "\n"
              << "hapax    : " << result.hapax << " (tf = 1)\n";
    if (stopPtr != nullptr) {
        // Denominator is the pre-filter token total (kept + removed). This is
        // the 35.74% removal figure quoted in the report; using only kept
        // tokens here would overstate the percentage.
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

    // Coarse stop-word candidates for the Task 1 analysis (plan.md 4.2).
    // The printed list is capped at 40 terms to keep the terminal readable;
    // `candidates` still reports the true total, and the full set is in the
    // CSV's stop_candidate column.
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

    // Human-readable top table (rank, tf, df, percentages). --top 0 suppresses
    // it for scripted runs that only need the CSV.
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

// `build` -- Task 2: construct and serialize the inverted index.
//
// The command selects a dictionary implementation (splay is the primary
// structure, hash is the baseline/cross-check), builds the index through the
// shared pipeline, validates the in-memory structure, and writes the v1 file.
// It prints the statistics used in the report: dictionary instrumentation,
// document/term/posting/position counts, file size, and build time.
int commandBuild(int argc, char** argv) {
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const std::string indexPath = optionValue(argc, argv, "--index", "build/index.bin");
    const std::string stopPath = optionValue(argc, argv, "--stop-words", "");
    const std::string dictName = optionValue(argc, argv, "--dict", "splay");

    // Same lifetime rule as commandCount: the local stop list must outlive the
    // build, and BuildOptions stores only a pointer to it.
    mse::StopWords stopWords;
    mse::BuildOptions options;
    options.tokenizer = tokenizerOptionsFromArgs(argc, argv);
    if (!stopPath.empty()) {
        stopWords = mse::StopWords::load(stopPath);
        options.stopWords = &stopWords;
    }
    options.stem = hasFlag(argc, argv, "--stem");

    // The dictionary is chosen at runtime through the shared interface; the
    // rest of the build is identical, which is what makes the byte-identical
    // output comparison meaningful.
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
    // Time only the build+validate+write section, excluding corpus loading,
    // so the dictionary comparison is not diluted by I/O from the text layer.
    const auto started = std::chrono::steady_clock::now();
    const mse::Index index = mse::buildIndex(corpus, options, *dictionary);
    // Validate before writing: a structural bug should fail here rather than
    // produce a file that only fails when a later query loads it.
    mse::validateIndex(index);
    mse::writeIndex(indexPath, index);
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();

    // Aggregate counts for the report. postings is the number of distinct
    // (term, document) pairs; positions is the total number of tokens stored
    // in the index (equal to index.tokenCount, checked by validateIndex).
    std::uint64_t postings = 0;
    std::uint64_t positions = 0;
    for (const mse::IndexTerm& term : index.terms) {
        postings += term.postings.size();
        positions += term.positions.size();
    }
    // Instrumentation collected by the dictionary during the build. The
    // comparison/rotation/rehash counts are deterministic and hardware-
    // independent, unlike the elapsed time printed below.
    const mse::DictStats stats = dictionary->stats();
    // Build report. `dictionary` prints the hardware-independent counters;
    // terms/postings/positions describe the frozen index, and the byte size +
    // elapsed time are the performance numbers used in the comparison table.
    // postings == distinct (term, document) pairs; positions == index.tokenCount.
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
    // Reaching this line means validateIndex() accepted the structure; the
    // line is printed last so a validation failure never claims success.
    std::cout << "invariants : OK\n";
    return 0;
}

// `dump` -- print one term's postings from an index file.
//
// The index is loaded and validated first, so dumping also exercises the
// deserialization path. The term is taken verbatim (the *indexed* form:
// lowercase, stop words removed, stemmed if the index was built with --stem);
// a miss is reported as a normal outcome, not an error.
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

    // loadIndex() validates all structural invariants; a corrupt file throws
    // before any output is produced. The header line repeats the index flags
    // because a term dump is meaningless without knowing whether the index
    // was stop-filtered/stemmed.
    const mse::Index index = mse::loadIndex(indexPath);
    std::cout << "index      : " << indexPath << "  (" << index.docs.size()
              << " docs, " << index.terms.size() << " terms, "
              << index.tokenCount << " tokens)\n"
              << "flags      :" << indexFlagsToString(index.flags) << "\n";

    // A miss is usually a stem/stop-word mismatch, so the message explains
    // which form the index expects instead of just printing "not found".
    if (!mse::dumpTerm(index, term, static_cast<std::size_t>(limit), std::cout)) {
        std::cout << "term not found: " << term
                  << "\n  (dump takes the indexed form: lowercase, stop words removed\n"
                  << "   and stemmed when the index was built with --stem)\n";
    }
    return 0;
}

// `query` -- Task 3/4: run a word, phrase or boolean query.
//
// Exactly one of --word/--phrase/--words must be given. The four threshold
// options are parsed with the same defaults as QueryOptions (all off), and
// negative values are clamped to the documented minimum. Output is either the
// human-readable listing (with optional --pretty source lines) or --json for
// scripts (tools/threshold_matrix.py consumes this form).
int commandQuery(int argc, char** argv) {
    const std::string indexPath = optionValue(argc, argv, "--index", "build/index.bin");
    const std::string stopPath = optionValue(argc, argv, "--stop-words", "");
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const bool pretty = hasFlag(argc, argv, "--pretty");

    // Enforce a single query mode. Accepting two would require precedence
    // rules that are easy to forget; an explicit error is clearer.
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
    // Thresholds. maxDfRatio is a ratio in [0,1] (1.0 disables it); the
    // integer thresholds are clamped at 0, so a negative value means "off"
    // rather than wrapping around in the unsigned conversion.
    options.maxDfRatio = doubleOption(argc, argv, "--max-df", 1.0);
    options.minDf = static_cast<std::uint32_t>(
        std::max(0, intOption(argc, argv, "--min-df", "1")));
    options.minMatch = static_cast<std::uint32_t>(std::max(
        0, intOption(argc, argv, "--min-match",
                     hasFlag(argc, argv, "--or") ? "1" : "0")));
    options.topX =
        static_cast<std::size_t>(std::max(0, intOption(argc, argv, "--top-x", "0")));

    // The stop list is needed for two things: explaining why a query term was
    // skipped, and re-running the position pipeline for --pretty. It must be
    // the same list used at build time; otherwise positions can diverge.
    mse::StopWords stopWords;
    const mse::StopWords* stopPtr = nullptr;
    if (!stopPath.empty()) {
        stopWords = mse::StopWords::load(stopPath);
        stopPtr = &stopWords;
    }

    const mse::Index index = mse::loadIndex(indexPath);
    const mse::QueryResult result = mse::runQuery(index, queryText, options, stopPtr);

    // Human/JSON label for the mode. "words" (plural) is the CLI spelling for
    // the boolean mode; runQuery calls it QueryMode::Boolean.
    const char* modeName = options.mode == mse::QueryMode::Word     ? "word"
                           : options.mode == mse::QueryMode::Phrase ? "phrase"
                                                                    : "words";

    // --json: one line, stable key order, no prose. scripts parse this; the
    // threshold experiment relies on notes and hits being present even when
    // the result set is empty (that is the interesting case).
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
        // notes is an array of [term, reason] pairs. It is emitted even when
        // empty so consumers can rely on the key always being present.
        std::cout << "],\"notes\":[";
        for (std::size_t i = 0; i < result.notes.size(); ++i) {
            if (i > 0) std::cout << ',';
            std::cout << "[\"" << jsonEscape(result.notes[i].term) << "\",\""
                      << jsonEscape(result.notes[i].reason) << "\"]";
        }
        std::cout << "]}\n";
        return 0;
    }

    // Human-readable header. `terms` is the list actually used after
    // normalization/thresholds, which may be shorter than the query text;
    // notes below explain each dropped term and why.
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
    // One note per unusable query term, with the reason (stop word, missing
    // from the index, dropped by a threshold, ...). These lines explain empty
    // or unexpectedly large result sets.
    for (const mse::TermNote& note : result.notes) {
        std::cout << "note       : " << note.term << " -- " << note.reason << "\n";
    }
    std::cout << "results    : " << result.hits.size() << " documents";
    if (options.topX > 0) {
        std::cout << " (--top-x " << options.topX << ")";
    }
    std::cout << "\n";

    // One line per hit: "docID<TAB>title". Multi-term results also show the
    // matched/total term count and the summed tf, which are the two score
    // components used for ordering. docID indexes index.docs directly because
    // documents are dense 1..N.
    for (const mse::QueryHit& hit : result.hits) {
        const mse::IndexDoc& doc = index.docs[hit.docID - 1];
        std::cout << "  " << hit.docID << '\t' << doc.title;
        if (options.mode != mse::QueryMode::Word || result.terms.size() > 1) {
            std::cout << "  [matched " << hit.matched << '/' << result.terms.size()
                      << ", tf " << hit.tfSum << ']';
        }
        std::cout << "\n";
    }

    // --pretty: re-run the corpus pipeline to turn the stored filtered-stream
    // position into the original source line. This requires the stop list the
    // index was built with; without it, positions are interpreted on an
    // unfiltered stream and would point at the wrong words, so warn loudly.
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

// `stats` -- load the corpus and print storage statistics.
//
// These are the numbers used to cross-check the Python preprocessing:
// document/line/word/byte totals plus a breakdown by line kind. It is also a
// fast smoke test that the manifest and every referenced text file load.
int commandStats(int argc, char** argv) {
    const std::string manifest = optionValue(argc, argv, "--manifest", "data/manifest.tsv");
    const mse::Corpus corpus = mse::Corpus::load(manifest);
    const mse::Corpus::Stats stats = corpus.stats();

    // Corpus totals first, then the line-kind breakdown (the categories are
    // the four text-layer markers) and the whitespace-word/byte counts.
    std::cout << "manifest : " << manifest << "\n";
    std::cout << "documents: " << stats.documents << "\n";
    std::cout << "lines    : " << stats.lines << "\n";
    std::cout << "  body   : " << stats.linesByKind[static_cast<std::size_t>(mse::LineKind::Body)] << "\n";
    std::cout << "  heading: " << stats.linesByKind[static_cast<std::size_t>(mse::LineKind::Heading)] << "\n";
    std::cout << "  stage  : " << stats.linesByKind[static_cast<std::size_t>(mse::LineKind::Stage)] << "\n";
    std::cout << "  speaker: " << stats.linesByKind[static_cast<std::size_t>(mse::LineKind::Speaker)] << "\n";
    std::cout << "words    : " << stats.words << "\n";
    std::cout << "bytes    : " << stats.bytes << "\n";

    // Category counts (Comedy/History/Tragedy/Poetry). std::map is used only
    // here, for its sorted iteration; the graded index never depends on it.
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

// CLI entry point: dispatch on argv[1] and translate exceptions into a
// diagnostic plus exit code 1. Unknown/planned commands print the usage and
// return 2, so scripts can distinguish "bad invocation" from "runtime error".
int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 2;
    }

    // Dispatch is a simple if-chain: there are only six commands and no
    // plugin mechanism, so a map would add indirection without benefit. Each
    // command returns its own exit code; all exceptions are translated here
    // once, which keeps the command bodies free of error-printing boilerplate.
    const std::string command = argv[1];
    try {
        // Commands are independent: each loads exactly what it needs, so a
        // failure in one cannot leave shared state behind for the next.
        if (command == "stats") {
            return commandStats(argc, argv);
        }
        if (command == "tokenize") {
            return commandTokenize(argc, argv);
        }
        if (command == "count") {
            return commandCount(argc, argv);
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
        // All runtime failures (I/O, malformed manifest/index, bad option
        // value) are reported here; exit code 1 distinguishes them from the
        // usage error (2) returned for an unknown command.
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
