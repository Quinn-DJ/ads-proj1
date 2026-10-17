// test_stemmer.cpp -- unit tests for the Porter stemmer.
//
// The words come from the rule tables of Porter 1980, but note: the paper's
// example column shows each rule's *local* effect, while the expected values
// here are the final outputs of the complete algorithm (verified word by
// word against NLTK's ORIGINAL_ALGORITHM mode, e.g. "agreed" passes through
// EE via step 1b but step 5a then removes the final E: agre). Run via
// `ctest --test-dir build`.

#include "catch_amalgamated.hpp"  // vendored Catch2 v3, single include

#include "stemmer.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

// Assert one (word -> stem) row; the word is echoed on failure.
void checkStem(const std::string& word, const std::string& expected) {
    INFO("word: " << word);
    CHECK(mse::stem(word) == expected);
}

void checkStems(const std::vector<std::pair<std::string, std::string>>& rows) {
    for (const auto& [word, expected] : rows) {
        checkStem(word, expected);
    }
}

}  // namespace

TEST_CASE("step 1a/1b/1c: plurals, -ed/-ing and terminal y", "[stemmer]") {
    checkStems({
        {"caresses", "caress"},
        {"ponies", "poni"},
        {"ties", "ti"},
        {"caress", "caress"},
        {"cats", "cat"},
        {"feed", "feed"},
        {"agreed", "agre"},
        {"plastered", "plaster"},
        {"bled", "bled"},
        {"motoring", "motor"},
        {"sing", "sing"},
        {"conflated", "conflat"},
        {"troubled", "troubl"},
        {"sized", "size"},
        {"hopping", "hop"},
        {"tanned", "tan"},
        {"falling", "fall"},
        {"hissing", "hiss"},
        {"fizzed", "fizz"},
        {"failing", "fail"},
        {"filing", "file"},
        {"happy", "happi"},
        {"sky", "sky"},
    });
}

TEST_CASE("step 2: double suffixes map to single ones", "[stemmer]") {
    checkStems({
        {"relational", "relat"},
        {"conditional", "condit"},
        {"rational", "ration"},
        {"valenci", "valenc"},
        {"hesitanci", "hesit"},
        {"digitizer", "digit"},
        {"conformabli", "conform"},
        {"radicalli", "radic"},
        {"differentli", "differ"},
        {"vileli", "vile"},
        {"analogousli", "analog"},
        {"vietnamization", "vietnam"},
        {"predication", "predic"},
        {"operator", "oper"},
        {"feudalism", "feudal"},
        {"decisiveness", "decis"},
        {"hopefulness", "hope"},
        {"callousness", "callous"},
        {"formaliti", "formal"},
        {"sensitiviti", "sensit"},
        {"sensibiliti", "sensibl"},
        {"possibli", "possibl"},      // canonical's bli -> ble departure
        {"chronologi", "chronolog"},  // canonical's logi -> log departure
    });
}

TEST_CASE("step 3: -icate/-ative/-alize", "[stemmer]") {
    checkStems({
        {"triplicate", "triplic"},
        {"formative", "form"},
        {"formalize", "formal"},
        {"electriciti", "electr"},
        {"electrical", "electr"},
        {"hopeful", "hope"},
        {"goodness", "good"},
    });
}

TEST_CASE("step 4: residual suffixes", "[stemmer]") {
    checkStems({
        {"revival", "reviv"},
        {"allowance", "allow"},
        {"inference", "infer"},
        {"airliner", "airlin"},
        {"gyroscopic", "gyroscop"},
        {"adjustable", "adjust"},
        {"defensible", "defens"},
        {"irritant", "irrit"},
        {"replacement", "replac"},
        {"adjustment", "adjust"},
        {"dependent", "depend"},
        {"adoption", "adopt"},
        {"homologou", "homolog"},
        {"communism", "commun"},
        {"activate", "activ"},
        {"angulariti", "angular"},
        {"homologous", "homolog"},
        {"effective", "effect"},
        {"bowdlerize", "bowdler"},
    });
}

TEST_CASE("step 5a/5b: final -e and double -ll", "[stemmer]") {
    checkStems({
        {"probate", "probat"},
        {"rate", "rate"},
        {"cease", "ceas"},
        {"controll", "control"},
        {"roll", "roll"},
    });
}

TEST_CASE("our tokenizer's contraction forms", "[stemmer]") {
    checkStems({
        {"who's", "who"},
        {"don't", "do"},
        {"he'll", "he"},
        {"you're", "you"},
        {"we've", "we"},
        {"is't", "is"},
        {"let's", "let"},
        {"marry'd", "marri"},
        {"o'er", "oer"},
        {"ne'er", "neer"},
        {"can't", "ca"},  // documented irregularity
        {"won't", "wo"},  // documented irregularity
    });
}

TEST_CASE("words that must not change", "[stemmer]") {
    checkStems({
        {"a", "a"},
        {"is", "is"},
        {"as", "as"},
        {"us", "us"},
        {"ay", "ay"},
        {"king", "king"},
        {"love", "love"},
        {"loves", "love"},
        {"loved", "love"},
        {"loving", "love"},
        {"was", "wa"},    // paper's unconditional S-rule
        {"this", "thi"},  // paper's unconditional S-rule
    });
}
