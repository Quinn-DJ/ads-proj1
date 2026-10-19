// posting.hpp -- one term's posting list: docID-ordered records + positions.
//
// Layout (plan.md 6.2): for every document that contains the term we keep one
// Posting{docID, posStart, tf}; the actual positions live in a separate arena
// vector. An occurrence's position is its offset in the document's *filtered*
// token stream (stop words removed, plan.md 8), which is what phrase queries
// need. Positions of one document are appended consecutively, so a posting
// owns the slice positions[posStart .. posStart + tf).

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mse {

struct Posting {
    std::uint32_t docID = 0;     // 1-based, strictly increasing
    std::uint32_t posStart = 0;  // start of this document's slice in `positions`
    std::uint32_t tf = 0;        // occurrences inside this document
};

// Field-wise equality (tests compare posting tables directly; posStart is
// the slice offset into the owning arena and must match too).
inline bool operator==(const Posting& a, const Posting& b) {
    return a.docID == b.docID && a.posStart == b.posStart && a.tf == b.tf;
}

class PostingList {
public:
    // Append one occurrence of the term: `position` is the token's offset in
    // the filtered stream of document `docID`. Documents must be appended in
    // increasing docID order (the index builder walks documents in order).
    void append(std::uint32_t docID, std::uint32_t position);

    const std::vector<Posting>& postings() const { return postings_; }
    const std::vector<std::uint32_t>& positions() const { return positions_; }

    std::size_t df() const { return postings_.size(); }  // documents containing the term
    std::uint64_t tf() const { return tf_; }             // total occurrences

private:
    std::vector<Posting> postings_;
    std::vector<std::uint32_t> positions_;
    std::uint64_t tf_ = 0;
};

}  // namespace mse
