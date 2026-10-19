// posting.cpp -- implementation of PostingList (see posting.hpp).

#include "posting.hpp"

namespace mse {

// Append one occurrence of the term.
//
// Documents are supplied in increasing docID order by the index builder, so a
// docID equal to the last posting's docID means "same document": update that
// posting (tf + 1) and append the position. A different docID opens a new
// posting whose posStart is the current end of the position arena. Positions
// for one document therefore occupy a contiguous slice
// positions[posStart .. posStart + tf), which is what phrase matching and
// serialization rely on. Complexity: amortized O(1).
void PostingList::append(std::uint32_t docID, std::uint32_t position) {
    // New document (or first occurrence): record where its position slice
    // starts. The tf field starts at zero and is incremented below, so the
    // "tf == number of positions" invariant holds after every append.
    if (postings_.empty() || postings_.back().docID != docID) {
        postings_.push_back(
            Posting{docID, static_cast<std::uint32_t>(positions_.size()), 0});
    }
    ++postings_.back().tf;
    positions_.push_back(position);
    ++tf_;
}

}  // namespace mse
