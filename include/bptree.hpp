// bptree.hpp -- B+Tree keyed by term strings, page-oriented by design.
//
// This is the disk-dictionary data structure of the scaling plan (plan.md
// 11 / B-series): the term table of a huge index (bonus: 500k files, 400M
// distinct words) cannot live in memory, so lookups must touch only
// O(height) pages. The tree therefore thinks in *pages* from day one:
//
//   * every node is a page with a byte budget (pageSize - header); a node
//     splits when one more (key, slot) pair would not fit -- keys are
//     variable-length terms, so fan-out follows from the budget, not from
//     a fixed slot count,
//   * leaves carry (key -> value) pairs and are chained right for range /
//     prefix scans (the sorted-array dictionary of index v1 cannot do
//     those without loading everything),
//   * B1 (this file): pages live in an in-memory store keyed by pageId --
//     the tree logic, split policy and invariants are final,
//   * B2 swaps the store for a file-backed pager with an LRU cache; the
//     tree code is untouched because it only ever talks to loadPage() /
//     storePage().
//
// Invariants (checked by validate(), the course-facing spec):
//   I1  keys within every node are strictly ascending,
//   I2  inner routing: child[i] owns keys in [key[i-1], key[i]) -- the
//       left edge is inclusive because separators are copies of real keys,
//   I3  every non-root node holds at least pageSize/2 - maxEntry bytes:
//       the provable floor for variable-length keys (a split lands within
//       one entry of the byte midpoint; term-sized keys sit far above it),
//   I4  all leaves sit at the same depth; the leaf chain visits every key
//       in ascending order exactly once,
//   I5  upsert semantics: one value per key.
//
// Complexity: find/insert O(height) page accesses + O(log fanout) key
// compares per page. With 4 KiB pages and ~20 B per entry the fan-out is
// ~170, so 400M terms need height 4-5 (~20 KiB per lookup) -- the number
// the bonus analysis quotes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mse {

// Tuning + instrumentation. The byte accounting mirrors the serialized
// layout (bptree.cpp), so "fits in the budget" means the same thing in
// memory (B1) and on disk (B2).
struct BTreeStats {
  std::uint32_t height = 0;   // number of levels (single leaf = 1)
  std::size_t pageCount = 0;  // nodes currently allocated
  std::size_t leafCount = 0;
  std::size_t keyCount = 0;            // entries stored in the leaves
  std::uint32_t pageSize = 0;          // per-node byte budget (as configured)
  std::uint64_t minFillBytes = ~0ull;  // smallest non-root node payload
  std::uint64_t maxFillBytes = 0;      // largest node payload
  std::uint64_t minFanout = ~0ull;     // smallest inner child count
  std::uint64_t maxFanout = 0;         // largest inner child count
};

class BTree {
 public:
  // pageSize is the per-node byte budget (default 4096, the disk page).
  // Must be >= 4 * (maxKeyLen + 9) so both halves of a split node can
  // always accept one more maximum-size entry.
  explicit BTree(std::uint32_t pageSize = 4096);

  // Build from a strictly ascending, duplicate-free key/value range in
  // one bottom-up pass. This is the index-build path: the sorted term
  // table already exists when the dictionary is written out.
  static BTree bulkLoad(
      const std::vector<std::pair<std::string, std::uint64_t>>& sorted,
      std::uint32_t pageSize = 4096);

  // Point operations. find() returns nullptr when the key is absent;
  // insert() overwrites an existing key's value and returns whether a
  // *new* key was added.
  const std::uint64_t* find(const std::string& key) const;
  bool insert(const std::string& key, std::uint64_t value);

  // All (key, value) pairs with lo <= key < hi in key order: descend
  // once, then walk the leaf chain (the prefix query of the README).
  std::vector<std::pair<std::string, std::uint64_t>> rangeScan(
      const std::string& lo, const std::string& hi) const;

  // Aggregate shape; the fill/fan-out extremes are refreshed by the
  // latest validate() call (cheap enough to call before stats()).
  BTreeStats stats() const;

  // Full invariant check (I1-I5); throws std::runtime_error naming the
  // violated rule and page. Also refreshes the stats() extremes.
  void validate() const;

  // Test hook: pages touched by the last find() -- the I/O count the
  // disk-backed variant would pay for that lookup.
  std::size_t lastFindPageReads() const { return lastFindPageReads_; }

  // Serialize the whole tree into a freshly created page file (B2; the
  // layout is documented next to BTreeFile). Throws on any I/O error.
  void writeFile(const std::string& path) const;

 private:
  static constexpr std::uint32_t kMaxKeyLen = 255;

  struct Node {
    bool leaf = true;
    std::vector<std::string> keys;        // strictly ascending
    std::vector<std::uint32_t> children;  // inner: keys.size()+1 page ids
    std::vector<std::uint64_t> values;    // leaf: keys.size() values
    std::uint32_t nextLeaf = 0;  // 0 = chain end (id 0 is never a leaf target)
  };

  // Bytes this node occupies in the serialized layout (bptree.cpp).
  std::size_t nodeBytes(const Node& node) const;
  bool nodeHasRoom(const Node& node, const std::string& key) const;

  Node& page(std::uint32_t id) { return pages_[id]; }
  const Node& page(std::uint32_t id) const { return pages_[id]; }
  // NOTE: references into pages_ are invalidated by newPage() (the
  // vector may reallocate) -- never hold one across a call that can
  // allocate (the dangling-reference lesson of notes.md 4.2, designed
  // out by re-fetching through the id after every allocation).

  std::uint32_t newPage(Node node);

  // First key stored anywhere in the subtree rooted at `id`.
  const std::string* firstKeyOfSubtree(std::uint32_t id) const;

  // Routing: index of the child slot whose subtree owns `key`.
  std::size_t slotFor(const Node& node, const std::string& key) const;

  // Recursive insert. Returns the split (separator key, new right page)
  // when this level split, so the caller can absorb it.
  std::optional<std::pair<std::string, std::uint32_t>> insertRec(
      std::uint32_t id, const std::string& key, std::uint64_t value,
      bool& inserted);

  std::uint32_t pageSize_;
  std::uint32_t root_ = 0;
  std::vector<Node> pages_;  // B1 store: page id -> node (B2: pager)
  std::size_t keyCount_ = 0;
  std::size_t leafCount_ = 0;
  mutable BTreeStats cached_;  // extremes refreshed by validate()
  mutable bool validated_ = false;
  mutable std::size_t lastFindPageReads_ = 0;
};

// ---------------------------------------------------------------------------
// B2: the on-disk form -- a page file plus a caching reader.
//
// File layout (little-endian; the same sizes nodeBytes() budgets against):
//   page 0 (header): magic "BPT1" | version u32 | pageSize u32 |
//                    pageCount u32 (= tree pages) | rootId u32 |
//                    height u32 | leafCount u64 | keyCount u64
//   page i+1: tree page i, nodeBytes() payload padded with zeros:
//     leaf u8 | keyCount u16 | nextLeaf u32 |
//     leaf  : values u64[count]            | blobs[count]
//     inner : children u32[count+1]        | blobs[count]
//     blob  : length u8 | bytes
//
// BTree::writeFile() turns a built tree into that file; BTreeFile answers
// queries from it, loading only O(height) pages per lookup through an LRU
// cache -- the resident-memory dictionary is no longer needed at query
// time, which is the whole point of the bonus scenario (400M terms).
// ---------------------------------------------------------------------------

// Read-side dictionary over a page file. Values are returned by copy: a
// cached page can be evicted at any load, so no references escape.
class BTreeFile {
 public:
  // Opens the file and reads the header page only. `cachePages` bounds
  // the LRU cache (>= 1); the sweep experiment varies it.
  explicit BTreeFile(const std::string& path, std::size_t cachePages = 8);

  // Point lookup: nullopt when the key is absent.
  std::optional<std::uint64_t> findValue(const std::string& key) const;

  // All (key, value) with lo <= key < hi, walking the leaf chain.
  std::vector<std::pair<std::string, std::uint64_t>> rangeScan(
      const std::string& lo, const std::string& hi) const;

  // Shape + I/O counters (pageReads counts actual file reads; hits are
  // answered from the cache).
  std::uint32_t pageCount() const { return header_.pageCount; }
  std::uint32_t pageSize() const { return header_.pageSize; }
  std::uint32_t height() const { return header_.height; }
  std::uint64_t keyCount() const { return header_.keyCount; }
  std::uint64_t pageReads() const { return pageReads_; }
  std::uint64_t cacheHits() const { return cacheHits_; }

 private:
  struct NodeView {
    bool leaf = true;
    std::uint32_t nextLeaf = 0;
    std::vector<std::string> keys;
    std::vector<std::uint32_t> children;
    std::vector<std::uint64_t> values;
  };

  struct Header {
    std::uint32_t pageSize = 0;
    std::uint32_t pageCount = 0;
    std::uint32_t rootId = 0;
    std::uint32_t height = 0;
    std::uint64_t leafCount = 0;
    std::uint64_t keyCount = 0;
  };

  // Load one tree page through the LRU cache.
  const NodeView& load(std::uint32_t id) const;

  Header header_;
  std::string path_;
  std::size_t cachePages_;
  mutable std::ifstream file_;
  mutable std::uint64_t pageReads_ = 0;
  mutable std::uint64_t cacheHits_ = 0;
  // LRU: front = most recently used; map for O(1) lookup.
  mutable std::list<std::pair<std::uint32_t, NodeView>> lru_;
  mutable std::unordered_map<
      std::uint32_t, std::list<std::pair<std::uint32_t, NodeView>>::iterator>
      lruIndex_;
};

}  // namespace mse
