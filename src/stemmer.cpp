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
// preceded by a consonant.
//
// In Porter's definition y is a vowel when the preceding character is a
// consonant, and a consonant at word start or after a vowel. Thus the final y
// in "happy" (after p) and "sky" (after k) is a vowel, while the y in "toy"
// (after o) is a consonant. The recursive form handles runs of y's correctly
// and follows the paper's literal definition.
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
// optional leading C and before an optional trailing V.
//
// Examples from the paper: m=0 for TR, EE, TREE; m=1 for TROUBLE; m=2 for
// TROUBLES. Most Porter conditions are expressed as m > 0, m > 1 or m == 1,
// so this function is the heart of the stemmer. Complexity: O(end).
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

// *v* -- the stem word[0..end) contains a vowel.
//
// Used by step 1b (ED/ING removal) and step 1c (Y -> I) to avoid stripping a
// suffix from a stem that would be left without a vowel. Complexity: O(end).
bool stemContainsVowel(const std::string& word, std::size_t end) {
  for (std::size_t i = 0; i < end; ++i) {
    if (!isConsonant(word, i)) {
      return true;
    }
  }
  return false;
}

// *d -- the word ends with a double consonant (e.g. -TT, -SS).
//
// Used by the step 1b cleanup (HOPPING -> HOP) and step 5b (CONTROLL ->
// CONTROL); note that the latter additionally requires a final L.
bool endsWithDoubleConsonant(const std::string& word) {
  const std::size_t n = word.size();
  return n >= 2 && word[n - 1] == word[n - 2] && isConsonant(word, n - 1);
}

// *o -- the word ends cvc, where the final c is not w, x or y.
//
// The w/x/y exclusion prevents adding an E to words like "snow" or "box".
// Together with m == 1 this decides whether step 1b restores a final E
// (FILING -> FILE) and whether step 5a keeps one (RATE -> RATE).
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

// True when `word` ends with the NUL-terminated `suffix`. The length check
// comes first so compare() is never called with an out-of-range position.
bool endsWith(const std::string& word, const char* suffix) {
  const std::size_t len = std::strlen(suffix);
  return word.size() >= len &&
         word.compare(word.size() - len, len, suffix) == 0;
}

// ---------------------------------------------------------------------------
// rule helpers
// ---------------------------------------------------------------------------

// One suffix-rewrite rule: replace `suffix` with `replacement` when the
// measure condition holds. An empty replacement deletes the suffix.
struct Rule {
  const char* suffix;
  const char* replacement;  // "" deletes the suffix
};

// Try the rules in order and stop at the first suffix that matches at all.
//
// The tables are ordered with the longest suffixes first (ATIONAL must win
// over TIONAL, EMENT over MENT over ENT). The measure condition is a gate on
// that single rule: if it fails, no shorter suffix is tried -- exactly what
// the canonical C implementation does ("agreement" matches EMENT, whose
// condition fails on "agre", so the word is left alone). The `minMeasure`
// argument is the threshold from the paper's table (0 for steps 2/3, 1 for
// step 4); the condition is `measure(stem) > minMeasure`.
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

// Step 1a: plural / third-person endings.
//
//   SSES -> SS, IES -> I, S -> (delete, but not after another S)
//
// The final S deletion has no measure condition in the paper; this is why
// "was" -> "wa" and "this" -> "thi" (documented quirks, verified against
// NLTK's ORIGINAL_ALGORITHM).
void step1a(std::string& word) {
  if (endsWith(word, "sses")) {
    word.resize(word.size() - 2);  // SSES -> SS
  } else if (endsWith(word, "ies")) {
    word.resize(word.size() - 2);  // IES  -> I
  } else if (!endsWith(word, "ss") && endsWith(word, "s")) {
    word.resize(word.size() - 1);  // S    -> (paper: no condition)
  }
}

// The follow-up applied when the ED or ING rule of step 1b has fired.
//
// Three mutually exclusive repairs, in the paper's order:
//   AT/BL/IZ -> add E (CONFLAT -> CONFLATE, TROUBL -> TROUBLE);
//   double consonant (not L/S/Z) -> drop one (HOPPING -> HOP);
//   m == 1 and *o -> add E (FILING -> FILE).
// The L/S/Z exclusion is why "falling" stays "fall" and "hissing" stays
// "hiss" rather than losing a letter.
void step1bCleanup(std::string& word) {
  if (endsWith(word, "at") || endsWith(word, "bl") || endsWith(word, "iz")) {
    word.push_back('e');  // AT -> ATE, BL -> BLE, IZ -> IZE
  } else if (endsWithDoubleConsonant(word) && word.back() != 'l' &&
             word.back() != 's' && word.back() != 'z') {
    word.pop_back();  // *d (not L/S/Z) -> single letter
  } else if (measure(word, word.size()) == 1 && endsCvc(word, word.size())) {
    word.push_back('e');  // (m=1 and *o) -> E
  }
}

// Step 1b: past tense / gerund endings.
//
//   (m > 0) EED -> EE
//   (*v*) ED  -> delete, then cleanup
//   (*v*) ING -> delete, then cleanup
//
// The vowel test applies to the stem *before* the suffix; "bled"/"sing" keep
// their endings because the remaining stem has no vowel. EED is handled
// separately and never triggers the cleanup (the paper says so explicitly).
void step1b(std::string& word) {
  if (endsWith(word, "eed")) {
    if (measure(word, word.size() - 3) > 0) {
      word.resize(word.size() - 1);  // (m>0) EED -> EE
    }
    return;  // cleanup only follows ED / ING
  }
  if (endsWith(word, "ed")) {
    if (stemContainsVowel(word, word.size() - 2)) {
      word.resize(word.size() - 2);  // (*v*) ED ->
      step1bCleanup(word);
    }
  } else if (endsWith(word, "ing")) {
    if (stemContainsVowel(word, word.size() - 3)) {
      word.resize(word.size() - 3);  // (*v*) ING ->
      step1bCleanup(word);
    }
  }
}

// Step 1c: terminal Y.
//
//   (*v*) Y -> I
//
// Only when the stem contains a vowel: "happy" -> "happi", but "sky" keeps its
// y (the stem "sk" has no vowel). The size check avoids reading before the
// start of a very short word.
void step1c(std::string& word) {
  if (word.size() >= 2 && word.back() == 'y' &&
      stemContainsVowel(word, word.size() - 1)) {
    word.back() = 'i';  // (*v*) Y -> I
  }
}

// Step 2: derivational suffixes with m > 0 (ATIONAL -> ATE, IVITI -> IVE, ...).
//
// The table below is the paper's list plus the author's two canonical
// departures: `bli -> ble` (generalizing ABLI -> ABLE, so POSSIBLI ->
// POSSIBL) and `logi -> log` (CHRONOLOGI -> CHRONOLOG). See stemmer.hpp for
// why we follow the canonical C variant (== NLTK MARTIN_EXTENSIONS).
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

// Step 3: further derivational suffixes with m > 0.
//
//   ICATE -> IC, ATIVE -> (delete), ALIZE -> AL, ICITI -> IC,
//   ICAL -> IC, NESS -> (delete), FUL -> (delete)
//
// "ATIVE" and "NESS"/"FUL" delete the suffix outright, which is why
// FORMATIVE -> FORM and GOODNESS -> GOOD.
void step3(std::string& word) {
  static const Rule rules[] = {
      {"icate", "ic"}, {"ative", ""}, {"alize", "al"}, {"iciti", "ic"},
      {"ical", "ic"},  {"ness", ""},  {"ful", ""},
  };
  applyTable(word, rules, sizeof(rules) / sizeof(rules[0]), 0);
}

// Step 4: residual suffixes, all requiring m > 1.
//
// The ION rule is special: it only applies when the preceding letter is S or
// T (ADOPTION -> ADOPT, but COMMUNION keeps its ION). It is checked before
// the table because ION is not representable as an unconditional entry.
void step4(std::string& word) {
  // (m>1 and (*S or *T)) ION ->
  if (endsWith(word, "ion") && word.size() >= 4 &&
      measure(word, word.size() - 3) > 1 &&
      (word[word.size() - 4] == 's' || word[word.size() - 4] == 't')) {
    word.resize(word.size() - 3);
    return;
  }
  static const Rule rules[] = {
      {"ement", ""}, {"ance", ""}, {"ence", ""}, {"able", ""}, {"ible", ""},
      {"ment", ""},  {"ant", ""},  {"ate", ""},  {"ent", ""},  {"iti", ""},
      {"ous", ""},   {"ive", ""},  {"ize", ""},  {"ism", ""},  {"al", ""},
      {"er", ""},    {"ic", ""},   {"ou", ""},
  };
  applyTable(word, rules, sizeof(rules) / sizeof(rules[0]), 1);
}

// Step 5a: final E.
//
//   (m > 1) E -> (delete)
//   (m == 1 and not *o) E -> (delete)
//
// The *o exception preserves the cvc pattern (RATE -> RATE, but PROBATE ->
// PROBAT). This step runs after all suffix rewrites, so it sees the final
// stem shape.
void step5a(std::string& word) {
  if (word.empty() || word.back() != 'e') {
    return;
  }
  const int m = measure(word, word.size() - 1);
  if (m > 1 || (m == 1 && !endsCvc(word, word.size() - 1))) {
    word.pop_back();  // (m>1) E -> / (m=1 and not *o) E ->
  }
}

// Step 5b: double L after m > 1.
//
//   (m > 1 and *d and *L) -> single L
//
// Only a final L is affected (CONTROLL -> CONTROL); "roll" has m == 1 and
// stays unchanged. This is the last rule of the pipeline.
void step5b(std::string& word) {
  if (word.size() >= 2 && word.back() == 'l' && endsWithDoubleConsonant(word) &&
      measure(word, word.size()) > 1) {
    word.pop_back();  // (m>1 and *d and *L) -> single letter
  }
}

}  // namespace

// Normalize a tokenizer token before the Porter rules run.
//
// 1. Strip one contraction ending, longest first, so "don't" loses "n't"
//    rather than "'t"; at most one suffix is removed.
// 2. Remove any remaining apostrophe, joining elisions ("o'er" -> "oer",
//    "ne'er" -> "neer", "marry'd" -> "marryd").
//
// The `word.size() > len` guard (strict, not >=) prevents stripping a suffix
// that would leave an empty word. Irregular English spellings produce the
// documented results can't -> ca and won't -> wo; these match the NLTK oracle.
// Complexity: O(token length).
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

// Full stemming pipeline: contraction normalization + Porter steps 1a-5b.
//
// The steps are applied strictly in paper order; each mutates `word` in place
// (the small copy made here decouples the caller's token from the work
// buffer). Words of two letters or fewer are returned unchanged -- the
// canonical implementation's length guard, which also avoids all the
// off-by-one cases in the suffix rules.
//
// The function is pure and deterministic: the same input always yields the
// same stem, which is what allows count/index/query to share it and lets the
// whole vocabulary be compared byte-for-byte with NLTK. Complexity: O(n^2) in
// the worst case for a single word, but n is a word length (small in practice).
std::string stem(const std::string& token) {
  std::string word = normalizeContractions(token);
  if (word.size() <= 2) {
    return word;  // the paper: very short words
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
