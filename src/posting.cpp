// posting.cpp -- implementation of PostingList (see posting.hpp).

#include "posting.hpp"

namespace mse {

void PostingList::append(std::uint32_t docID, std::uint32_t position) {
    if (postings_.empty() || postings_.back().docID != docID) {
        postings_.push_back(
            Posting{docID, static_cast<std::uint32_t>(positions_.size()), 0});
    }
    ++postings_.back().tf;
    positions_.push_back(position);
    ++tf_;
}

}  // namespace mse
