// dict.cpp -- splay-tree and hash dictionaries (see dict.hpp).

#include "dict.hpp"

#include <algorithm>
#include <utility>

namespace mse {

// ---------------------------------------------------------------------------
// SplayDictionary
// ---------------------------------------------------------------------------

// Destruction is delegated to destroy(), which walks the tree iteratively.
// A recursive post-order delete could overflow the call stack on a degenerate
// (linked-list-shaped) tree; the iterative version is safe for any shape.
SplayDictionary::~SplayDictionary() { destroy(root_); }

// Compare two terms and count the comparison.
//
// Every ordering decision in the splay dictionary goes through here, so
// DictStats::stringComparisons is an accurate, timing-independent measure of
// the work done by the structure (used in the Chapter 4 dictionary comparison).
int SplayDictionary::compare(const std::string& a, const std::string& b) {
  ++stats_.stringComparisons;
  return a.compare(b);
}

// Single rotation around x (x must have a parent).
//
// The rotation must repair four links: the parent's child pointer, x's inner
// child's parent pointer (if any), x's parent pointer, and the grandparent's
// child pointer (or root_ when x becomes the root). Getting any one of these
// wrong corrupts the tree; the splay tests exercise all four zig/zig-zig/
// zig-zag cases.
void SplayDictionary::rotate(Node* x) {
  Node* parent = x->parent;
  Node* grand = parent->parent;
  ++stats_.rotations;
  if (parent->left == x) {
    parent->left = x->right;
    if (x->right != nullptr) {
      x->right->parent = parent;
    }
    x->right = parent;
  } else {
    parent->right = x->left;
    if (x->left != nullptr) {
      x->left->parent = parent;
    }
    x->left = parent;
  }
  parent->parent = x;
  x->parent = grand;
  if (grand == nullptr) {
    root_ = x;
  } else if (grand->left == parent) {
    grand->left = x;
  } else {
    grand->right = x;
  }
}

// Move x to the root with the three splay steps.
//
//   zig      : x's parent is the root -> one rotation.
//   zig-zig  : x and its parent are on the same side -> rotate parent, then x.
//   zig-zag  : x and its parent are on opposite sides -> rotate x twice.
//
// Each iteration moves x two levels up (one level for zig), so a splay costs
// O(depth). The amortized cost over a sequence of operations is O(log n);
// regardless of the current shape, the accessed node ends at the root, which
// is what gives hot terms their short future paths under a Zipf workload.
void SplayDictionary::splay(Node* x) {
  while (x->parent != nullptr) {
    Node* parent = x->parent;
    Node* grand = parent->parent;
    if (grand == nullptr) {
      rotate(x);  // zig
    } else if ((grand->left == parent) == (parent->left == x)) {
      rotate(parent);  // zig-zig
      rotate(x);
    } else {
      rotate(x);  // zig-zag
      rotate(x);
    }
  }
}

// Find `term`, inserting an empty entry when it is absent.
//
// The search descends like a normal BST. On a hit the node is splayed so the
// term stays at the root (the next occurrence is then a single comparison);
// on a miss a new node is linked as a leaf and splayed. The returned reference
// points into the node and stays valid until destruction: rotations change
// links, never node addresses.
//
// Complexity: amortized O(log n); O(1) extra space.
TermEntry& SplayDictionary::upsert(const std::string& term) {
  if (root_ == nullptr) {
    root_ = new Node(term);
    ++size_;
    return root_->entry;
  }
  Node* current = root_;
  while (true) {
    const int cmp = compare(term, current->entry.term);
    if (cmp == 0) {
      splay(current);  // hot term: keep it at the root
      return current->entry;
    }
    Node*& slot = (cmp < 0) ? current->left : current->right;
    if (slot == nullptr) {
      // Copy the node pointer before splaying: splay() rewires the
      // child slots, so a reference into the tree must not be used
      // after the rotations.
      Node* inserted = new Node(term);
      inserted->parent = current;
      slot = inserted;
      ++size_;
      splay(inserted);
      return inserted->entry;
    }
    current = slot;
  }
}

// Look up `term` without inserting. A hit is splayed to the root.
//
// A miss also splays the last node visited -- the classic splay-tree behavior.
// It warms the tree for nearby keys (useful because query terms often share
// prefixes) and keeps the amortized analysis uniform; the caller only sees
// nullptr. Complexity: amortized O(log n).
TermEntry* SplayDictionary::find(const std::string& term) {
  Node* current = root_;
  Node* last = nullptr;
  while (current != nullptr) {
    last = current;
    const int cmp = compare(term, current->entry.term);
    if (cmp == 0) {
      splay(current);
      return &current->entry;
    }
    current = (cmp < 0) ? current->left : current->right;
  }
  if (last != nullptr) {
    splay(last);  // classic splay: a miss still splays the last node visited
  }
  return nullptr;
}

// Return all entries in lexicographic order via an iterative in-order walk.
//
// This is the "freeze" step of index construction: a BST's in-order sequence
// is exactly the sorted term table, so no separate sort is needed (unlike the
// hash dictionary). The explicit stack avoids recursion-depth limits; memory
// is O(height) for the stack plus O(n) for the result. The returned pointers
// are non-owning and remain valid as long as the dictionary lives.
std::vector<TermEntry*> SplayDictionary::sortedEntries() {
  std::vector<TermEntry*> out;
  out.reserve(size_);
  std::vector<Node*> stack;
  Node* current = root_;
  while (current != nullptr || !stack.empty()) {
    while (current != nullptr) {
      stack.push_back(current);
      current = current->left;
    }
    current = stack.back();
    stack.pop_back();
    out.push_back(&current->entry);
    current = current->right;
  }
  return out;
}

// Test/diagnostic hook: the term currently at the root ("" when empty).
// Tests use it to prove that an access actually splayed the node, without
// exposing the internal Node type.
std::string SplayDictionary::rootTermForTesting() const {
  return root_ == nullptr ? std::string() : root_->entry.term;
}

// Free every node in the subtree rooted at `node`.
//
// We collect nodes with an iterative pre-order walk and delete them in a
// second pass. Deleting in any order is safe because each node is independent
// once the links are not followed; the point is simply to avoid recursion on
// a potentially very deep tree. Complexity: O(n) time, O(n) temporary space.
void SplayDictionary::destroy(Node* node) {
  if (node == nullptr) {
    return;
  }
  // Iterative post-order cleanup (deleting in any order is fine, but the
  // explicit stack keeps deep trees from overflowing the call stack).
  std::vector<Node*> stack{node};
  std::vector<Node*> order;
  while (!stack.empty()) {
    Node* current = stack.back();
    stack.pop_back();
    order.push_back(current);
    if (current->left != nullptr) {
      stack.push_back(current->left);
    }
    if (current->right != nullptr) {
      stack.push_back(current->right);
    }
  }
  for (Node* current : order) {
    delete current;
  }
}

// ---------------------------------------------------------------------------
// HashDictionary
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kInitialBuckets = 16;

}  // namespace

// Free every bucket chain. Each node belongs to exactly one chain, so this is
// a single O(n) pass over the table.
HashDictionary::~HashDictionary() {
  for (Node* head : buckets_) {
    destroyChain(head);
  }
}

// FNV-1a 64-bit hash of a term.
//
// We use a fixed, well-specified hash (not std::hash) so bucket assignment,
// rehash counts and build statistics are identical across standard libraries
// and platforms. FNV-1a is simple, fast for short strings, and adequate here:
// the term table is frozen and sorted afterwards, so the hash only affects
// build-time chain lengths, not the serialized index order.
std::uint64_t HashDictionary::hashOf(const std::string& term) {
  // FNV-1a, 64 bit: offset basis and prime from the FNV specification.
  std::uint64_t hash = 1469598103934665603ULL;
  for (const char c : term) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

// Look up `term` without inserting.
//
// The bucket index is the hash modulo the current table size; the chain is
// walked until an exact string match or the end. Comparisons are counted so
// the hash baseline reports the same kind of cost metric as the splay tree.
// Expected complexity: O(1 + load factor), i.e. O(1) with the 0.75 cap.
TermEntry* HashDictionary::find(const std::string& term) {
  if (buckets_.empty()) {
    return nullptr;
  }
  Node* node = buckets_[hashOf(term) % buckets_.size()];
  while (node != nullptr) {
    ++stats_.stringComparisons;
    if (node->entry.term == term) {
      return &node->entry;
    }
    node = node->next;
  }
  return nullptr;
}

// Find `term`, inserting an empty entry when it is absent.
//
// Existing entries are returned by reference without moving. New nodes are
// allocated once and linked at the head of their bucket, so references stay
// valid across later insertions and rehashes (rehash relinks nodes, never
// copies them). The table doubles whenever the load factor would exceed 0.75,
// which keeps the expected chain length constant.
//
// Expected complexity: O(1) amortized; rehash is O(n) but only O(log n) times.
TermEntry& HashDictionary::upsert(const std::string& term) {
  if (buckets_.empty()) {
    buckets_.assign(kInitialBuckets, nullptr);
  }
  const std::size_t index = hashOf(term) % buckets_.size();
  for (Node* node = buckets_[index]; node != nullptr; node = node->next) {
    ++stats_.stringComparisons;
    if (node->entry.term == term) {
      return node->entry;
    }
  }
  // Absent: prepend to the chain. Head insertion is O(1) and does not
  // invalidate any existing TermEntry reference.
  Node* node = new Node(term);
  node->next = buckets_[index];
  buckets_[index] = node;
  ++size_;
  if (size_ * 4 > buckets_.size() * 3) {  // load factor > 0.75
    rehash(buckets_.size() * 2);
  }
  return node->entry;
}

// Grow the table and redistribute every node into `newBucketCount` buckets.
//
// Nodes are relinked, not copied, so all TermEntry references handed out
// earlier remain valid. Each chain is traversed once and its nodes are pushed
// onto the new chains in reverse order (which is fine: chain order is not
// semantically meaningful). Complexity: O(n + newBucketCount).
void HashDictionary::rehash(std::size_t newBucketCount) {
  ++stats_.rehashes;
  std::vector<Node*> fresh(newBucketCount, nullptr);
  for (Node* head : buckets_) {
    while (head != nullptr) {
      Node* next = head->next;
      const std::size_t index = hashOf(head->entry.term) % newBucketCount;
      head->next = fresh[index];
      fresh[index] = head;
      head = next;
    }
  }
  buckets_.swap(fresh);  // entry references stay valid: nodes never move
}

// Return all entries sorted by term.
//
// Unlike the splay tree, a hash table has no traversal order, so entries are
// collected from every bucket and sorted explicitly. This is the price of the
// hash baseline: O(n log n) instead of the splay tree's free in-order walk.
// The sort is by term, which makes the serialized index deterministic.
std::vector<TermEntry*> HashDictionary::sortedEntries() {
  std::vector<TermEntry*> out;
  out.reserve(size_);
  for (Node* head : buckets_) {
    for (Node* node = head; node != nullptr; node = node->next) {
      out.push_back(&node->entry);
    }
  }
  std::sort(out.begin(), out.end(), [](const TermEntry* a, const TermEntry* b) {
    return a->term < b->term;
  });
  return out;
}

// Free one bucket chain iteratively. Saving `next` before deleting the current
// node is required: once the node is freed its `next` field is gone.
void HashDictionary::destroyChain(Node* node) {
  while (node != nullptr) {
    Node* next = node->next;
    delete node;
    node = next;
  }
}

// ---------------------------------------------------------------------------
// MapDictionary (std::map + counting comparator; plan.md 6.5 third column)
// ---------------------------------------------------------------------------

bool MapDictionary::CountingCompare::operator()(const std::string& a,
                                                const std::string& b) const {
  if (stats != nullptr) {
    ++stats->stringComparisons;  // the metric the other two report too
  }
  return a < b;  // strict weak ordering, identical semantics to the others
}

TermEntry& MapDictionary::upsert(const std::string& term) {
  // try_emplace reports insertion, so size_ stays exact; map nodes are
  // stable, hence the returned reference stays valid forever. The key is
  // a private copy, so the entry's own `term` mirror is filled here --
  // callers (sortedEntries consumers, the index freeze) read TermEntry.
  const auto [entry, inserted] = map_.try_emplace(term);
  if (inserted) {
    entry->second.term = term;
    ++size_;
  }
  return entry->second;
}

TermEntry* MapDictionary::find(const std::string& term) {
  const auto it = map_.find(term);
  return it == map_.end() ? nullptr : &it->second;
}

std::vector<TermEntry*> MapDictionary::sortedEntries() {
  std::vector<TermEntry*> out;
  out.reserve(size_);
  for (auto& entry : map_) {
    out.push_back(&entry.second);
  }
  return out;  // map iteration IS the sorted order -- no extra sort pass
}

}  // namespace mse
