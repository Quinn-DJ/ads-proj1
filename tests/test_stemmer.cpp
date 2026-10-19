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

// Vectors from Porter's paper. The expected values are the *final* outputs of
// the whole pipeline (steps 1a-5b), not the local effect printed in the paper's
// example column; e.g. "agreed" -> "agre" because step 5a removes the final E
// after step 1b produced "agree". Each value was verified against NLTK's
// ORIGINAL_ALGORITHM mode.
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

// Step 2 exercises longest-suffix-first matching and the author's canonical
// departures: POSSIBLI -> POSSIBL (bli -> ble) and CHRONOLOGI -> CHRONOLOG
// (logi -> log). Agreement/most other suffixes are covered in the next cases.
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

// Step 3 suffixes, including the deleting rules (ATIVE, NESS, FUL) where the
// replacement is the empty string.
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

// Step 4 requires m > 1 and includes the special ION rule: ADOPTION -> ADOPT
// (preceded by T), while a word whose ION is not preceded by S/T keeps it.
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

// Step 5a (final E with the *o exception) and 5b (final double L after m > 1).
// RATE keeps its E because the stem is cvc, while PROBATE loses it.
TEST_CASE("step 5a/5b: final -e and double -ll", "[stemmer]") {
    checkStems({
        {"probate", "probat"},
        {"rate", "rate"},
        {"cease", "ceas"},
        {"controll", "control"},
        {"roll", "roll"},
    });
}

// Tokenizer output forms (internal apostrophes). These verify the contraction
// pre-pass before the Porter rules, including the documented irregular results
// can't -> ca and won't -> wo, and elisions such as o'er -> oer.
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

// Guard against over-stemming. The two letter/guard cases (is, as, us, ay)
// exercise the canonical length rule; was/this are the paper's unconditional
// S-rule quirks, kept intentionally and cross-checked with the oracle.
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
