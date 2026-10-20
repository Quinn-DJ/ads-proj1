// dict.hpp -- term dictionaries for the inverted-index build.
//
// Both implementations map a stem (std::string) to a TermEntry (whose
// PostingList accumulates the occurrences). The index builder programs
// against the Dictionary interface, so either structure can drive the same
// build pipeline -- which gives us two things at once (plan.md 6.1/6.5):
//
//   * SplayDictionary -- the primary structure (class01 material).
//     Self-adjusting splay tree: the term stream is Zipf-distributed, so hot
//     terms stay near the root; we only insert and look up (never delete);
//     and an in-order traversal yields the sorted term table for free.
//   * HashDictionary -- the baseline (chaining, FNV-1a 64), same semantics.
//     Used for the Chapter 4 comparison experiment and as an independent
//     cross-check: both dictionaries must reproduce byte-identical indexes.
//
// Entry references returned by upsert()/find() stay valid for the lifetime
// of the dictionary (nodes are never moved or reallocated).

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "posting.hpp"

namespace mse {

struct TermEntry {
  std::string term;
  PostingList postings;
};

// Instrumentation for the Chapter 4 comparison experiment: string
// comparisons are a timing-independent cost metric (splay vs hash).
struct DictStats {
  std::uint64_t stringComparisons = 0;
  std::uint64_t rotations = 0;  // splay tree only
  std::uint64_t rehashes = 0;   // hash table only
};

class Dictionary {
 public:
  virtual ~Dictionary() = default;
  Dictionary(const Dictionary&) = delete;
  Dictionary& operator=(const Dictionary&) = delete;

  // Find the entry for `term`, inserting an empty one when absent.
  virtual TermEntry& upsert(const std::string& term) = 0;

  // Lookup without insertion; nullptr when the term is absent.
  virtual TermEntry* find(const std::string& term) = 0;

  // Entries sorted by term (splay: in-order traversal; hash: collect+sort).
  virtual std::vector<TermEntry*> sortedEntries() = 0;

  std::size_t size() const { return size_; }
  DictStats stats() const { return stats_; }

 protected:
  Dictionary() = default;
  std::size_t size_ = 0;
  DictStats stats_;
};

// ---------------------------------------------------------------------------
// splay tree dictionary (primary)
// ---------------------------------------------------------------------------

class SplayDictionary : public Dictionary {
 public:
  SplayDictionary() = default;
  ~SplayDictionary() override;

  TermEntry& upsert(const std::string& term) override;
  TermEntry* find(const std::string& term) override;
  std::vector<TermEntry*> sortedEntries() override;

  // Test/diagnostics hook: the term currently at the root ("" when empty).
  std::string rootTermForTesting() const;

 private:
  struct Node {
    TermEntry entry;
    Node* parent = nullptr;
    Node* left = nullptr;
    Node* right = nullptr;
    explicit Node(std::string term) : entry{std::move(term), {}} {}
  };

  Node* root_ = nullptr;

  int compare(const std::string& a, const std::string& b);
  void rotate(Node* x);
  void splay(Node* x);
  static void destroy(Node* node);
};

// ---------------------------------------------------------------------------
// hash table dictionary (baseline / cross-check)
// ---------------------------------------------------------------------------

class HashDictionary : public Dictionary {
 public:
  HashDictionary() = default;
  ~HashDictionary() override;

  TermEntry& upsert(const std::string& term) override;
  TermEntry* find(const std::string& term) override;
  std::vector<TermEntry*> sortedEntries() override;

 private:
  struct Node {
    TermEntry entry;
    Node* next = nullptr;
    explicit Node(std::string term) : entry{std::move(term), {}} {}
  };

  std::vector<Node*> buckets_;

  static std::uint64_t hashOf(const std::string& term);  // FNV-1a 64
  void rehash(std::size_t newBucketCount);
  static void destroyChain(Node* node);
};

// ---------------------------------------------------------------------------
// std::map dictionary (standard-library baseline, typically a red-black
// tree; the third column of the plan.md 6.5 comparison experiment)
// ---------------------------------------------------------------------------

class MapDictionary : public Dictionary {
 public:
  // The comparator must see this object's stats_ from the first insert;
  // the base subobject is constructed before map_, so &stats_ is stable.
  MapDictionary() : map_(CountingCompare{&stats_}) {}

  TermEntry& upsert(const std::string& term) override;
  TermEntry* find(const std::string& term) override;
  std::vector<TermEntry*> sortedEntries() override;

 private:
  // Instrumented comparator: std::map's internal comparisons are not
  // observable, so the tree runs on our counting strict-weak-order --
  // every key comparison the map performs lands in DictStats, making
  // the three-way comparison metric machine-independent again.
  struct CountingCompare {
    DictStats* stats = nullptr;  // -> Dictionary::stats_ of the owner
    bool operator()(const std::string& a, const std::string& b) const;
  };

  // map nodes are stable: references handed out by upsert()/find() stay
  // valid for the dictionary's lifetime (same contract as the others).
  // The comparator points at this object's stats_, so the map must never
  // be copied out of the dictionary (Dictionary is non-copyable anyway).
  std::map<std::string, TermEntry, CountingCompare> map_;
};

}  // namespace mse
