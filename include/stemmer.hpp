// stemmer.hpp -- Porter stemming with contraction handling.
//
// Implements M. F. Porter, "An algorithm for suffix stripping", Program
// 14(3), 1980 (the rule tables live in reference/porter-1980-paper.txt).
//
// Input: a normalized token from the tokenizer -- lowercase ASCII letters,
// possibly containing internal apostrophes ("who's", "o'er", "marry'd").
// Output: the stem ("who", "oer", "marri").
//
// Pipeline:
//   1. contraction stage -- strip a contraction ending ("n't", "'s", "'d",
//      "'ll", "'re", "'ve", "'t", "'m"), then remove any remaining
//      apostrophes (elisions like o'er -> oer);
//   2. Porter steps 1a, 1b, 1c, 2, 3, 4, 5a, 5b, exactly as specified in
//      the paper (words of two letters or fewer are returned unchanged).
//
// Which variant: this implementation follows the *author's own canonical C
// code* (reference/porter-canonical-c.txt) -- the 1980 paper plus the three
// changes Porter marks there as --DEPARTURE--:
//   * words of two letters or fewer are left alone (guard),
//   * bli -> ble  (step 2; generalises the paper's abli -> able),
//   * logi -> log (step 2).
// NLTK's MARTIN_EXTENSIONS mode ("only the modifications included in the
// implementations on Martin Porter's website") is our cross-check oracle.
//
// Documented quirks (English spelling, not implementation bugs; verified
// against the oracle):
//   can't -> ca, won't -> wo   ("n't" stripped from an irregular spelling)
//   was -> wa, this -> thi     (the paper's S-rule in step 1a is
//                               unconditional; later variants added guards)

#pragma once

#include <string>

namespace mse {

// Full pipeline: contraction normalization + Porter steps 1a-5b.
std::string stem(const std::string& token);

// Exposed for tests: the contraction/cleanup stage only (no Porter rules).
std::string normalizeContractions(const std::string& token);

}  // namespace mse
