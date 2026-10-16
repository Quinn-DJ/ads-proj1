// stemmer.cpp -- implementation of the Porter stemmer (see stemmer.hpp).

#include "stemmer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace mse {

namespace {

// ---------------------------------------------------------------------------
// conditions used by the rules (see the paper's section 2 definitions)
// ---------------------------------------------------------------------------

// A consonant is a letter other than a, e, i, o, u -- and other than y
// preceded by a consonant (i.e. y is a vowel exactly when a consonant
// precedes it; word-initial y is a consonant).
bool isConsonant(const std::string& word, std::size_t i) {
    switch (word[i]) {
        case 'a':
        case 'e':
        case 'i':
        case 'o':
        case 'u':
            return false;
        case 'y':
            return i == 0 ? true : !isConsonant(word, i - 1);
        default:
            return true;
    }
}

// The measure m of word[0..end): the number of VC sequences, after an
// optional leading C and before an optional trailing V (m=0 for TR, EE,
// TREE; m=1 for TROUBLE; m=2 for TROUBLES).
int measure(const std::string& word, std::size_t end) {
    std::size_t i = 0;
    while (i < end && isConsonant(word, i)) {
        ++i;  // leading C
    }
    int m = 0;
    while (i < end) {
        while (i < end && !isConsonant(word, i)) {
            ++i;  // V
        }
        if (i >= end) {
            break;  // trailing V
        }
        ++m;  // one VC pair completed
        while (i < end && isConsonant(word, i)) {
            ++i;  // C
        }
    }
    return m;
}

// *v* -- the stem contains a vowel.
bool stemContainsVowel(const std::string& word, std::size_t end) {
    for (std::size_t i = 0; i < end; ++i) {
        if (!isConsonant(word, i)) {
            return true;
        }
    }
    return false;
}

// *d -- the word ends with a double consonant (e.g. -TT, -SS).
bool endsWithDoubleConsonant(const std::string& word) {
    const std::size_t n = word.size();
    return n >= 2 && word[n - 1] == word[n - 2] && isConsonant(word, n - 1);
}

// *o -- the word ends cvc, where the final c is not w, x or y.
bool endsCvc(const std::string& word, std::size_t end) {
    if (end < 3) {
        return false;
    }
    if (!isConsonant(word, end - 3) || isConsonant(word, end - 2) ||
        !isConsonant(word, end - 1)) {
        return false;
    }
    const char last = word[end - 1];
    return last != 'w' && last != 'x' && last != 'y';
}

bool endsWith(const std::string& word, const char* suffix) {
    const std::size_t len = std::strlen(suffix);
    return word.size() >= len &&
           word.compare(word.size() - len, len, suffix) == 0;
}

// ---------------------------------------------------------------------------
// rule helpers
// ---------------------------------------------------------------------------

struct Rule {
    const char* suffix;
    const char* replacement;  // "" deletes the suffix
};

// Try the rules in order and stop at the first suffix that matches at all.
// The tables are ordered with the longest suffixes first (ATIONAL must win
// over TIONAL, EMENT over MENT over ENT). The measure condition is a gate on
// that single rule: if it fails, no shorter suffix is tried -- exactly what
// the canonical C implementation does ("agreement" matches EMENT, whose
// condition fails on "agre", so the word is left alone).
void applyTable(std::string& word, const Rule* rules, std::size_t count,
                int minMeasure) {
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t len = std::strlen(rules[i].suffix);
        if (word.size() < len ||
            word.compare(word.size() - len, len, rules[i].suffix) != 0) {
            continue;  // this suffix does not match at all
        }
        if (measure(word, word.size() - len) > minMeasure) {
            word.replace(word.size() - len, len, rules[i].replacement);
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// the five Porter steps
// ---------------------------------------------------------------------------

void step1a(std::string& word) {
    if (endsWith(word, "sses")) {
        word.resize(word.size() - 2);       // SSES -> SS
    } else if (endsWith(word, "ies")) {
        word.resize(word.size() - 2);       // IES  -> I
    } else if (!endsWith(word, "ss") && endsWith(word, "s")) {
        word.resize(word.size() - 1);       // S    -> (paper: no condition)
    }
}

// The follow-up applied when the ED or ING rule of step 1b has fired.
void step1bCleanup(std::string& word) {
    if (endsWith(word, "at") || endsWith(word, "bl") || endsWith(word, "iz")) {
        word.push_back('e');                // AT -> ATE, BL -> BLE, IZ -> IZE
    } else if (endsWithDoubleConsonant(word) && word.back() != 'l' &&
               word.back() != 's' && word.back() != 'z') {
        word.pop_back();                    // *d (not L/S/Z) -> single letter
    } else if (measure(word, word.size()) == 1 &&
               endsCvc(word, word.size())) {
        word.push_back('e');                // (m=1 and *o) -> E
    }
}

void step1b(std::string& word) {
    if (endsWith(word, "eed")) {
        if (measure(word, word.size() - 3) > 0) {
            word.resize(word.size() - 1);   // (m>0) EED -> EE
        }
        return;                             // cleanup only follows ED / ING
    }
    if (endsWith(word, "ed")) {
        if (stemContainsVowel(word, word.size() - 2)) {
            word.resize(word.size() - 2);   // (*v*) ED -> 
            step1bCleanup(word);
        }
    } else if (endsWith(word, "ing")) {
        if (stemContainsVowel(word, word.size() - 3)) {
            word.resize(word.size() - 3);   // (*v*) ING -> 
            step1bCleanup(word);
        }
    }
}

void step1c(std::string& word) {
    if (word.size() >= 2 && word.back() == 'y' &&
        stemContainsVowel(word, word.size() - 1)) {
        word.back() = 'i';                  // (*v*) Y -> I
    }
}

void step2(std::string& word) {
    static const Rule rules[] = {
        {"ational", "ate"}, {"ization", "ize"}, {"iveness", "ive"},
        {"fulness", "ful"}, {"ousness", "ous"}, {"tional", "tion"},
        {"biliti", "ble"},  {"iviti", "ive"},   {"aliti", "al"},
        {"ousli", "ous"},   {"entli", "ent"},   {"alism", "al"},
        {"ation", "ate"},   {"izer", "ize"},    {"logi", "log"},
        {"alli", "al"},     {"ator", "ate"},    {"enci", "ence"},
        {"anci", "ance"},   {"eli", "e"},       {"bli", "ble"},
    };
    applyTable(word, rules, sizeof(rules) / sizeof(rules[0]), 0);
}

void step3(std::string& word) {
    static const Rule rules[] = {
        {"icate", "ic"}, {"ative", ""}, {"alize", "al"}, {"iciti", "ic"},
        {"ical", "ic"},  {"ness", ""},  {"ful", ""},
    };
    applyTable(word, rules, sizeof(rules) / sizeof(rules[0]), 0);
}

void step4(std::string& word) {
    // (m>1 and (*S or *T)) ION -> 
    if (endsWith(word, "ion") && word.size() >= 4 &&
        measure(word, word.size() - 3) > 1 &&
        (word[word.size() - 4] == 's' || word[word.size() - 4] == 't')) {
        word.resize(word.size() - 3);
        return;
    }
    static const Rule rules[] = {
        {"ement", ""}, {"ance", ""}, {"ence", ""}, {"able", ""},
        {"ible", ""},  {"ment", ""}, {"ant", ""},  {"ate", ""},
        {"ent", ""},   {"iti", ""},  {"ous", ""},  {"ive", ""},
        {"ize", ""},   {"ism", ""},  {"al", ""},   {"er", ""},
        {"ic", ""},    {"ou", ""},
    };
    applyTable(word, rules, sizeof(rules) / sizeof(rules[0]), 1);
}

void step5a(std::string& word) {
    if (word.empty() || word.back() != 'e') {
        return;
    }
    const int m = measure(word, word.size() - 1);
    if (m > 1 || (m == 1 && !endsCvc(word, word.size() - 1))) {
        word.pop_back();                    // (m>1) E -> / (m=1 and not *o) E ->
    }
}

void step5b(std::string& word) {
    if (word.size() >= 2 && word.back() == 'l' &&
        endsWithDoubleConsonant(word) && measure(word, word.size()) > 1) {
        word.pop_back();                    // (m>1 and *d and *L) -> single letter
    }
}

}  // namespace

std::string normalizeContractions(const std::string& token) {
    // Contraction endings, longest first ("n't" before "'t" so that "don't"
    // becomes "do" rather than "don"). Only one ending is stripped.
    static const char* const kSuffixes[] = {"n't", "'ll", "'re", "'ve",
                                            "'s",  "'d",  "'t",  "'m"};
    std::string word = token;
    for (const char* suffix : kSuffixes) {
        const std::size_t len = std::strlen(suffix);
        if (word.size() > len &&
            word.compare(word.size() - len, len, suffix) == 0) {
            word.resize(word.size() - len);
            break;
        }
    }
    // Any remaining apostrophe marks an elision (o'er, ne'er, o'clock):
    // drop it so the letters join up for the Porter rules.
    word.erase(std::remove(word.begin(), word.end(), '\''), word.end());
    return word;
}

std::string stem(const std::string& token) {
    std::string word = normalizeContractions(token);
    if (word.size() <= 2) {
        return word;                        // the paper: very short words
    }
    step1a(word);
    step1b(word);
    step1c(word);
    step2(word);
    step3(word);
    step4(word);
    step5a(word);
    step5b(word);
    return word;
}

}  // namespace mse
