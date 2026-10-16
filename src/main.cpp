// main.cpp -- command line entry point of the mini search engine (`mse`).
//
// Current commands:
//   mse stats      load the corpus and print storage statistics
//   mse tokenize   print the token stream of one document (or all documents)
//   mse count      word frequency / document frequency table -> CSV (task 1)
//   mse build      build the inverted index -> index.bin (task 2)
//   mse dump       print one term's postings from an index file
// Planned commands (plan.md section 2):
//   query / shell / selftest

#include "corpus.hpp"
#include "count.hpp"
#include "index.hpp"
#include "stopwords.hpp"
#include "tokenizer.hpp"

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

void printUsage(const char* program) {
    std::cerr
        << "usage: " << program << " <command> [options]\n"
        << "\n"
        << "commands:\n"
        << "  stats                load manifest + text files and print statistics\n"
        << "  tokenize [options]   print the token stream of one document\n"
        << "  count [options]      word count table (tf/df) -> CSV\n"
        << "  build [options]      build the inverted index -> <index file>\n"
        << "  dump [options]       print one term's postings from an index file\n"
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
        << "planned: query, shell, selftest\n";
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
              << "flags      :";
    const std::uint32_t known =
        index.flags & (mse::kIndexFlagStopWords | mse::kIndexFlagStemmed);
    if ((known & mse::kIndexFlagStopWords) != 0) std::cout << " stopwords";
    if ((known & mse::kIndexFlagStemmed) != 0) std::cout << " stemmed";
    if (known == 0) std::cout << " (raw)";
    std::cout << "\n";

    if (!mse::dumpTerm(index, term, static_cast<std::size_t>(limit), std::cout)) {
        std::cout << "term not found: " << term
                  << "\n  (dump takes the indexed form: lowercase, stop words removed\n"
                  << "   and stemmed when the index was built with --stem)\n";
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
        if (command == "build") {
            return commandBuild(argc, argv);
        }
        if (command == "dump") {
            return commandDump(argc, argv);
        }
        std::cerr << "unknown or not-yet-implemented command: " << command << "\n\n";
        printUsage(argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
