// bptree.cpp -- implementation of the page-oriented B+Tree (see bptree.hpp).
//
// B1 scope: the page store is a vector (page id == index). Split policy,
// byte accounting and invariants are the ones the on-disk format will
// use, so B2 only replaces the store -- the algorithm below is final.

#include "bptree.hpp"

#include <algorithm>
#include <stdexcept>

namespace mse {

namespace {

// Serialized node layout (little-endian, fixed by this spec):
//   header : leaf flag u8, key count u16, nextLeaf u32      -> 7 bytes
//   inner  : (count+1) page ids u32, then count key blobs
//   leaf   : count values u64, then count key blobs
//   blob   : length u8 (<= 255) + bytes
// The budget math below mirrors these sizes exactly.
constexpr std::size_t kNodeHeaderBytes = 7;
constexpr std::size_t kChildBytes = 4;    // one inner slot (page id)
constexpr std::size_t kValueBytes = 8;    // one leaf slot (u64)
constexpr std::size_t kKeyLenPrefix = 1;  // one length byte

// Largest possible entry (key blob + slot). With variable-length keys a
// split can land at most one entry away from the byte midpoint, so the
// provable per-page floor is pageSize/2 minus this bound (I3). For real
// term keys (~9 bytes) pages sit far above the floor; stats() reports the
// actual distribution.
constexpr std::size_t kMaxEntryBytes = kKeyLenPrefix + 255 + kValueBytes;

std::size_t keyBlobBytes(const std::string& key) {
    return kKeyLenPrefix + key.size();
}

// Leaf split point: cut [0, mid) + [mid, N) as close to the byte midpoint
// as possible while keeping both halves above the I3 floor. `floor` is the
// entry-byte count each side must retain (pageSize/2 - header, relaxed by
// kMaxEntryBytes at the caller).
std::size_t leafSplitMid(const std::vector<std::string>& keys,
                         std::size_t floorBytes) {
    std::size_t total = 0;
    for (const std::string& key : keys) {
        total += keyBlobBytes(key) + kValueBytes;
    }
    std::size_t prefix = 0;
    std::size_t best = keys.size() - 1;
    std::size_t bestDistance = static_cast<std::size_t>(-1);
    for (std::size_t mid = 1; mid < keys.size(); ++mid) {
        prefix += keyBlobBytes(keys[mid - 1]) + kValueBytes;
        const std::size_t right = total - prefix;
        // Both sides must clear the floor (when that is achievable at
        // all); among the valid cuts prefer the one nearest the middle.
        if (prefix < floorBytes || right < floorBytes) {
            continue;
        }
        const std::size_t distance = prefix > right ? prefix - right : right - prefix;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = mid;
        }
    }
    if (bestDistance == static_cast<std::size_t>(-1)) {
        // Pathological sizes: no cut satisfies the floor; take the cut
        // closest to the midpoint (deficit bounded by one max entry).
        prefix = 0;
        for (std::size_t mid = 1; mid < keys.size(); ++mid) {
            prefix += keyBlobBytes(keys[mid - 1]) + kValueBytes;
            const std::size_t right = total - prefix;
            const std::size_t distance =
                prefix > right ? prefix - right : right - prefix;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = mid;
            }
        }
    }
    return best;
}

// Inner split point: children [0, i] stay left, keys[i] is promoted, the
// rest goes right; nearest-to-even split of the remaining bytes.
std::size_t innerSplitMid(const std::vector<std::string>& keys) {
    const std::size_t n = keys.size();          // promote candidates 0..n-1
    std::vector<std::size_t> prefix(n + 1, 0);  // prefix[i] = blobs of keys[0..i)
    for (std::size_t i = 0; i < n; ++i) {
        prefix[i + 1] = prefix[i] + keyBlobBytes(keys[i]);
    }
    std::size_t best = n - 1;
    std::size_t bestDistance = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t left =
            kNodeHeaderBytes + kChildBytes * (i + 1) + prefix[i];
        const std::size_t right = kNodeHeaderBytes + kChildBytes * (n - i) +
                                  (prefix[n] - prefix[i + 1]);
        const std::size_t distance =
            left > right ? left - right : right - left;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

}  // namespace

BTree::BTree(std::uint32_t pageSize) : pageSize_(pageSize) {
    // Both halves of a split must be able to absorb one maximum-size
    // entry afterwards, or the "retry after split" assumption breaks.
    if (pageSize_ < 4 * (kMaxKeyLen + kValueBytes + kKeyLenPrefix)) {
        throw std::runtime_error("BTree page size must be at least 1056 bytes");
    }
    pages_.push_back(Node{});  // page 0: the initial empty root leaf
    leafCount_ = 1;
}

// ---------------------------------------------------------------------------
// byte budget
// ---------------------------------------------------------------------------

std::size_t BTree::nodeBytes(const Node& node) const {
    std::size_t bytes = kNodeHeaderBytes;
    bytes += (node.leaf ? node.values.size() : node.children.size()) *
             (node.leaf ? kValueBytes : kChildBytes);
    for (const std::string& key : node.keys) {
        bytes += keyBlobBytes(key);
    }
    return bytes;
}

bool BTree::nodeHasRoom(const Node& node, const std::string& key) const {
    if (node.keys.empty()) {
        return true;  // an empty node always accepts its first entry
    }
    const std::size_t slot = node.leaf ? kValueBytes : kChildBytes;
    return nodeBytes(node) + slot + keyBlobBytes(key) <= pageSize_;
}

std::uint32_t BTree::newPage(Node node) {
    pages_.push_back(std::move(node));
    return static_cast<std::uint32_t>(pages_.size() - 1);
}

// ---------------------------------------------------------------------------
// lookup helpers
// ---------------------------------------------------------------------------

std::size_t BTree::slotFor(const Node& node, const std::string& key) const {
    // First slot whose separator key is > key: that subtree owns key.
    return static_cast<std::size_t>(
        std::upper_bound(node.keys.begin(), node.keys.end(), key) -
        node.keys.begin());
}

const std::string* BTree::firstKeyOfSubtree(std::uint32_t id) const {
    const Node* node = &page(id);
    while (!node->leaf) {
        node = &page(node->children.front());
    }
    return node->keys.empty() ? nullptr : &node->keys.front();
}

const std::uint64_t* BTree::find(const std::string& key) const {
    lastFindPageReads_ = 0;
    if (key.size() > kMaxKeyLen) {
        return nullptr;
    }
    std::uint32_t id = root_;
    while (true) {
        const Node& node = page(id);  // read-only walk: no allocation here
        ++lastFindPageReads_;         // == one page read once B2 lands
        if (node.leaf) {
            const auto it =
                std::lower_bound(node.keys.begin(), node.keys.end(), key);
            if (it == node.keys.end() || *it != key) {
                return nullptr;
            }
            return &node.values[static_cast<std::size_t>(it - node.keys.begin())];
        }
        id = node.children[slotFor(node, key)];
    }
}

// ---------------------------------------------------------------------------
// insertion (upsert with splits)
// ---------------------------------------------------------------------------

std::optional<std::pair<std::string, std::uint32_t>> BTree::insertRec(
    std::uint32_t id, const std::string& key, std::uint64_t value, bool& inserted) {
    if (page(id).leaf) {
        Node& node = page(id);  // safe: leaf branch never allocates pages
        const auto it =
            std::lower_bound(node.keys.begin(), node.keys.end(), key);
        const std::size_t at =
            static_cast<std::size_t>(it - node.keys.begin());
        if (it != node.keys.end() && *it == key) {
            node.values[at] = value;  // overwrite (I5 upsert semantics)
            inserted = false;
            return std::nullopt;
        }
        // Absorb first, split afterwards: inserting into the (possibly now
        // over-budget) node and then halving it by count keeps both halves
        // within the budget (constructor guarantees pageSize >= 4*maxEntry)
        // and avoids any placement special-casing.
        node.keys.insert(node.keys.begin() + static_cast<std::ptrdiff_t>(at), key);
        node.values.insert(node.values.begin() + static_cast<std::ptrdiff_t>(at), value);
        inserted = true;
        if (nodeBytes(node) <= pageSize_) {
            return std::nullopt;
        }
        // Split at the nearest-to-mid cut that keeps both halves above
        // the I3 floor (count-mids can go under it for skewed sizes).
        const std::size_t floorBytes =
            pageSize_ / 2 > kNodeHeaderBytes + kMaxEntryBytes
                ? pageSize_ / 2 - kNodeHeaderBytes - kMaxEntryBytes
                : 0;
        const std::size_t mid = leafSplitMid(node.keys, floorBytes);
        Node right;
        right.leaf = true;
        right.keys.assign(node.keys.begin() + static_cast<std::ptrdiff_t>(mid),
                          node.keys.end());
        right.values.assign(node.values.begin() + static_cast<std::ptrdiff_t>(mid),
                            node.values.end());
        node.keys.resize(mid);
        node.values.resize(mid);
        right.nextLeaf = node.nextLeaf;  // splice into the leaf chain
        const std::string separator = right.keys.front();
        const std::uint32_t rightId = newPage(std::move(right));  // reallocates!
        page(id).nextLeaf = rightId;  // re-fetch after the allocation
        ++leafCount_;
        return std::make_pair(separator, rightId);
    }

    // Inner node: descend first, absorb any child split afterwards.
    {
        Node& node = page(id);  // read before recursion only
        const std::size_t slot = slotFor(node, key);
        const std::uint32_t child = node.children[slot];
        auto split = insertRec(child, key, value, inserted);
        if (!split.has_value()) {
            return std::nullopt;
        }
        // Absorb the child split into this node (same absorb-then-split
        // policy as the leaf case), then re-check the budget.
        Node& absorb = page(id);  // re-fetch (recursion may have reallocated)
        absorb.keys.insert(absorb.keys.begin() +
                               static_cast<std::ptrdiff_t>(slot),
                           std::move(split->first));
        absorb.children.insert(absorb.children.begin() +
                                   static_cast<std::ptrdiff_t>(slot) + 1,
                               split->second);
        if (nodeBytes(absorb) <= pageSize_) {
            return std::nullopt;
        }
    }
    // Split the over-budget inner node at the byte midpoint: children
    // [0..mid] stay left, keys[mid] is promoted, the rest goes right.
    Node& full = page(id);
    const std::size_t mid = innerSplitMid(full.keys);
    const std::string promoted = full.keys[mid];
    Node right;
    right.leaf = false;
    right.keys.assign(full.keys.begin() + static_cast<std::ptrdiff_t>(mid) + 1,
                      full.keys.end());
    right.children.assign(full.children.begin() +
                              static_cast<std::ptrdiff_t>(mid) + 1,
                          full.children.end());
    full.keys.resize(mid);
    full.children.resize(mid + 1);
    const std::uint32_t rightId = newPage(std::move(right));  // reallocates!
    return std::make_pair(promoted, rightId);
}

bool BTree::insert(const std::string& key, std::uint64_t value) {
    if (key.size() > kMaxKeyLen) {
        throw std::runtime_error("BTree key longer than 255 bytes");
    }
    validated_ = false;
    bool inserted = false;
    auto split = insertRec(root_, key, value, inserted);
    if (split.has_value()) {
        // The root split: grow the tree by one level (the only place the
        // height can increase).
        const std::uint32_t oldRoot = root_;
        Node newRoot;
        newRoot.leaf = false;
        newRoot.keys.push_back(std::move(split->first));
        newRoot.children.push_back(oldRoot);
        newRoot.children.push_back(split->second);
        root_ = newPage(std::move(newRoot));
    }
    if (inserted) {
        ++keyCount_;
    }
    return inserted;
}

// ---------------------------------------------------------------------------
// range scan (leaf chain walk)
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, std::uint64_t>> BTree::rangeScan(
    const std::string& lo, const std::string& hi) const {
    std::vector<std::pair<std::string, std::uint64_t>> out;
    std::uint32_t id = root_;
    while (!page(id).leaf) {
        id = page(id).children[slotFor(page(id), lo)];
    }
    while (true) {
        const Node& node = page(id);
        for (std::size_t i = 0; i < node.keys.size(); ++i) {
            if (node.keys[i] < lo) {
                continue;
            }
            if (!(node.keys[i] < hi)) {
                return out;  // keys are ascending: nothing further can match
            }
            out.emplace_back(node.keys[i], node.values[i]);
        }
        if (node.nextLeaf == 0) {
            break;
        }
        id = node.nextLeaf;
    }
    return out;
}

// ---------------------------------------------------------------------------
// bulk load (the index-build path)
// ---------------------------------------------------------------------------

BTree BTree::bulkLoad(const std::vector<std::pair<std::string, std::uint64_t>>& sorted,
                      std::uint32_t pageSize) {
    const auto ascending = [](const auto& a, const auto& b) {
        return a.first < b.first;
    };
    if (!std::is_sorted(sorted.begin(), sorted.end(), ascending)) {
        throw std::runtime_error("bulkLoad: input must be sorted by key");
    }
    for (const auto& entry : sorted) {
        if (entry.first.size() > kMaxKeyLen) {
            throw std::runtime_error("bulkLoad: key longer than 255 bytes");
        }
    }
    BTree tree(pageSize);
    tree.pages_.clear();
    tree.leafCount_ = 0;
    tree.keyCount_ = sorted.size();
    tree.validated_ = false;

    // Level 0: pack leaves greedily under the byte budget. Greedy packing
    // can leave the *last* page lighter than half; fixLastLeaf() below
    // rebalances (merge or borrow) so every page meets I3.
    std::vector<std::uint32_t> level;  // page ids of the level below
    {
        Node current;
        current.leaf = true;
        for (const auto& [key, value] : sorted) {
            if (!current.keys.empty() && !tree.nodeHasRoom(current, key)) {
                const std::uint32_t id = tree.newPage(std::move(current));
                if (!level.empty()) {
                    tree.page(level.back()).nextLeaf = id;
                }
                level.push_back(id);
                current = Node{};
                current.leaf = true;
            }
            current.keys.push_back(key);
            current.values.push_back(value);
        }
        if (!current.keys.empty() || level.empty()) {
            const std::uint32_t id = tree.newPage(std::move(current));
            if (!level.empty()) {
                tree.page(level.back()).nextLeaf = id;
            }
            level.push_back(id);
        }
    }
    tree.leafCount_ = level.size();

    // Rebalance the rightmost leaf: merge into its predecessor when both
    // fit on one page, otherwise borrow entries until it reaches half.
    // Combined > pageSize guarantees the predecessor stays >= half too.
    if (level.size() >= 2) {
        const std::uint32_t last = level.back();
        const std::uint32_t prev = level[level.size() - 2];
        if (tree.nodeBytes(tree.page(last)) + tree.nodeBytes(tree.page(prev)) <=
            pageSize) {
            Node merged = std::move(tree.page(prev));
            merged.nextLeaf = tree.page(last).nextLeaf;
            for (std::size_t k = 0; k < tree.page(last).keys.size(); ++k) {
                merged.keys.push_back(std::move(tree.page(last).keys[k]));
                merged.values.push_back(tree.page(last).values[k]);
            }
            tree.page(prev) = std::move(merged);
            level.pop_back();  // the light last leaf disappears
        } else {
            while (tree.nodeBytes(tree.page(last)) < pageSize / 2) {
                Node& p = tree.page(prev);
                Node& l = tree.page(last);
                l.keys.insert(l.keys.begin(), std::move(p.keys.back()));
                l.values.insert(l.values.begin(), p.values.back());
                p.keys.pop_back();
                p.values.pop_back();
            }
        }
    }

    // Inner levels: group children under the same budget; the separator
    // between two children is the first key of the right one's subtree.
    // The same rightmost-page rebalance keeps every inner node >= half.
    while (level.size() > 1) {
        std::vector<std::uint32_t> next;
        std::size_t i = 0;
        while (i < level.size()) {
            Node inner;
            inner.leaf = false;
            inner.children.push_back(level[i]);
            while (i + 1 < level.size()) {
                const std::string* first = tree.firstKeyOfSubtree(level[i + 1]);
                if (first == nullptr || !tree.nodeHasRoom(inner, *first)) {
                    break;  // this inner node is full: start a new one
                }
                inner.keys.push_back(*first);
                inner.children.push_back(level[i + 1]);
                ++i;
            }
            ++i;
            next.push_back(tree.newPage(std::move(inner)));
        }
        if (next.size() >= 2) {  // rebalance the rightmost inner node
            const std::uint32_t lastId = next.back();
            const std::uint32_t prevId = next[next.size() - 2];
            Node& last = tree.page(lastId);
            Node& prevInner = tree.page(prevId);
            if (tree.nodeBytes(last) + tree.nodeBytes(prevInner) <= pageSize) {
                const std::string* bridge = tree.firstKeyOfSubtree(last.children.front());
                prevInner.keys.push_back(bridge ? *bridge : std::string());
                for (std::size_t k = 0; k < last.keys.size(); ++k) {
                    prevInner.keys.push_back(std::move(last.keys[k]));
                    prevInner.children.push_back(last.children[k]);
                }
                prevInner.children.push_back(last.children.back());
                next.pop_back();
            } else {
                while (tree.nodeBytes(last) < pageSize / 2) {
                    // Move prevInner's last child to the front of `last`.
                    // The separator between the moved child and `last`'s old
                    // first child is the OLD first key of `last` (the boundary
                    // that lived in the parent), not the moved child's own
                    // first key. Using the moved key would place a separator
                    // equal to that subtree's minimum, so routing would send
                    // the subtree's keys to its right sibling (I2 violation,
                    // found by the 30M-key scale run).
                    const std::uint32_t child = prevInner.children.back();
                    const std::string* boundary =
                        tree.firstKeyOfSubtree(last.children.front());
                    last.keys.insert(last.keys.begin(),
                                     boundary ? *boundary : std::string());
                    last.children.insert(last.children.begin(), child);
                    prevInner.keys.pop_back();
                    prevInner.children.pop_back();
                }
            }
        }
        level = std::move(next);
    }
    tree.root_ = level.front();
    return tree;
}

// ---------------------------------------------------------------------------
// validation (I1-I5) and stats
// ---------------------------------------------------------------------------

void BTree::validate() const {
    cached_ = BTreeStats{};
    std::uint32_t leafDepth = 0;
    bool leafDepthKnown = false;

    // Recursive walk with an explicit stack (page id, depth, key bounds).
    struct Frame {
        std::uint32_t id;
        std::uint32_t depth;
        const std::string* lo;  // exclusive lower bound (separator to the left)
        const std::string* hi;  // inclusive... exclusive upper bound
    };
    std::vector<Frame> stack;
    stack.push_back(Frame{root_, 0, nullptr, nullptr});

    std::size_t seenKeys = 0;
    std::string previousLeafKey;  // chain order across leaves (I4)
    bool havePrevious = false;
    std::vector<std::uint32_t> leafOrder;

    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        const Node& node = page(frame.id);

        // I1: strictly ascending keys inside the node.
        for (std::size_t i = 1; i < node.keys.size(); ++i) {
            if (!(node.keys[i - 1] < node.keys[i])) {
                throw std::runtime_error("BTree I1 violated: keys not strictly "
                                         "ascending in page " +
                                         std::to_string(frame.id));
            }
        }
        // Slot counts must match the node kind.
        if (node.leaf ? (node.values.size() != node.keys.size() ||
                         !node.children.empty())
                      : (node.children.size() != node.keys.size() + 1 ||
                         !node.values.empty())) {
            throw std::runtime_error("BTree structure violated: slot counts do "
                                     "not match node kind in page " +
                                     std::to_string(frame.id));
        }
        // I3: non-root fill (root exempt). With variable-length keys the
        // provable floor is half the page minus one maximum-size entry --
        // a split can land at most one entry away from the byte midpoint.
        // Real term keys (~9 bytes) leave pages far above the floor; the
        // actual distribution is reported via stats().
        if (frame.id != root_) {
            const std::uint64_t bytes = nodeBytes(node);
            cached_.minFillBytes = std::min(cached_.minFillBytes, bytes);
            cached_.maxFillBytes = std::max(cached_.maxFillBytes, bytes);
            if (bytes < pageSize_ / 2 - kMaxEntryBytes) {
                throw std::runtime_error("BTree I3 violated: under-filled page "
                                         + std::to_string(frame.id) + " (" +
                                         std::to_string(bytes) + " bytes)");
            }
        }
        if (!node.leaf) {
            cached_.minFanout =
                std::min(cached_.minFanout,
                         static_cast<std::uint64_t>(node.children.size()));
            cached_.maxFanout =
                std::max(cached_.maxFanout,
                         static_cast<std::uint64_t>(node.children.size()));
        }

        if (node.leaf) {
            // I4: uniform depth.
            if (!leafDepthKnown) {
                leafDepth = frame.depth;
                leafDepthKnown = true;
            } else if (frame.depth != leafDepth) {
                throw std::runtime_error("BTree I4 violated: leaves at "
                                         "different depths");
            }
            // I2 bounds from the parent's separators. The interval is
            // half-open [lo, hi): a subtree's first key may EQUAL the
            // separator to its left, because separators are copies of
            // real keys (the first key of the right subtree at split
            // time) and that key keeps living in a leaf.
            for (const std::string& key : node.keys) {
                if (frame.lo != nullptr && key < *frame.lo) {
                    throw std::runtime_error("BTree I2 violated: key < left "
                                             "separator in page " +
                                             std::to_string(frame.id));
                }
                if (frame.hi != nullptr && !(key < *frame.hi)) {
                    throw std::runtime_error("BTree I2 violated: key >= right "
                                             "separator in page " +
                                             std::to_string(frame.id));
                }
            }
            seenKeys += node.keys.size();
            leafOrder.push_back(frame.id);
            (void)previousLeafKey;
            (void)havePrevious;
        } else {
            // Push children with their separator bounds (right to left so
            // the leftmost is processed first).
            for (std::size_t i = node.children.size(); i-- > 0;) {
                const std::string* lo =
                    i == 0 ? frame.lo : &node.keys[i - 1];
                const std::string* hi =
                    i == node.keys.size() ? frame.hi : &node.keys[i];
                stack.push_back(
                    Frame{node.children[i], frame.depth + 1, lo, hi});
            }
        }
    }

    // I4 chain: following nextLeaf from the leftmost leaf must visit the
    // leaves in key order and terminate at the last one.
    if (!leafOrder.empty()) {
        std::uint32_t id = leafOrder.front();
        const std::uint32_t firstId = id;
        for (std::size_t guard = 0; guard <= pages_.size() + 1; ++guard) {
            const Node& node = page(id);
            for (std::size_t i = 1; i < node.keys.size(); ++i) {
                if (!(node.keys[i - 1] < node.keys[i])) {
                    throw std::runtime_error("BTree I4 violated: leaf page " +
                                             std::to_string(id) +
                                             " not ascending");
                }
            }
            if (node.nextLeaf == 0) {
                break;
            }
            // The next leaf's first key must exceed this leaf's last key.
            const Node& next = page(node.nextLeaf);
            if (node.keys.empty() || next.keys.empty() ||
                !(node.keys.back() < next.keys.front())) {
                throw std::runtime_error("BTree I4 violated: leaf chain out of "
                                         "order at page " +
                                         std::to_string(id));
            }
            id = node.nextLeaf;
            if (id == firstId) {
                throw std::runtime_error("BTree I4 violated: leaf chain cycle");
            }
        }
    }

    if (seenKeys != keyCount_) {
        throw std::runtime_error("BTree I5 violated: stored keys " +
                                 std::to_string(seenKeys) + " != tracked " +
                                 std::to_string(keyCount_));
    }
    validated_ = true;
}

BTreeStats BTree::stats() const {
    BTreeStats out = validated_ ? cached_ : BTreeStats{};
    out.pageCount = pages_.size();
    out.leafCount = leafCount_;
    out.keyCount = keyCount_;
    out.pageSize = pageSize_;
    std::uint32_t depth = 1;
    for (std::uint32_t id = root_; !page(id).leaf; ++depth) {
        id = page(id).children.front();
    }
    out.height = depth;
    return out;
}

// ---------------------------------------------------------------------------
// B2: page-file serialization + the caching reader
// ---------------------------------------------------------------------------

namespace {

// Little-endian fixed-width writers/readers shared by header and nodes.
void putU16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>(value >> 8));
}
void putU32(std::string& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}
void putU64(std::string& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}
std::uint16_t getU16(const char* in) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(in[0]) |
        (static_cast<std::uint16_t>(static_cast<std::uint8_t>(in[1])) << 8));
}
std::uint32_t getU32(const char* in) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[i]))
                 << (8 * i);
    }
    return value;
}
std::uint64_t getU64(const char* in) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(in[i]))
                 << (8 * i);
    }
    return value;
}

}  // namespace

void BTree::writeFile(const std::string& path) const {
    // The tree must be self-consistent before it is frozen to disk.
    validate();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot create B+Tree file: " + path);
    }

    // Header page (page 0): magic, version, shape.
    std::string header;
    header += "BPT1";
    putU32(header, 1);                                  // version
    putU32(header, pageSize_);
    putU32(header, static_cast<std::uint32_t>(pages_.size()));
    putU32(header, root_);
    putU32(header, stats().height);
    putU64(header, static_cast<std::uint64_t>(leafCount_));
    putU64(header, static_cast<std::uint64_t>(keyCount_));
    header.resize(pageSize_, '\0');
    out.write(header.data(), static_cast<std::streamsize>(pageSize_));

    // Tree page i lives at file page i+1 (page 0 is the header).
    for (const Node& node : pages_) {
        // Reuse the node serializer through a light adapter: nodes and
        // NodeViews share the field layout the format defines.
        const std::string page = [this, &node]() {
            std::string bytes;
            bytes.push_back(node.leaf ? 1 : 0);
            putU16(bytes, static_cast<std::uint16_t>(node.keys.size()));
            putU32(bytes, node.nextLeaf);
            if (node.leaf) {
                for (const std::uint64_t value : node.values) {
                    putU64(bytes, value);
                }
            } else {
                for (const std::uint32_t child : node.children) {
                    putU32(bytes, child);
                }
            }
            for (const std::string& key : node.keys) {
                bytes.push_back(static_cast<char>(key.size()));
                bytes += key;
            }
            if (bytes.size() > pageSize_) {
                throw std::runtime_error("BTree node overflows its page");
            }
            bytes.resize(pageSize_, '\0');
            return bytes;
        }();
        out.write(page.data(), static_cast<std::streamsize>(pageSize_));
    }
    out.flush();
    if (!out) {
        throw std::runtime_error("error while writing B+Tree file: " + path);
    }
}

BTreeFile::BTreeFile(const std::string& path, std::size_t cachePages)
    : path_(path), cachePages_(std::max<std::size_t>(cachePages, 1)) {
    file_.open(path, std::ios::binary);
    if (!file_) {
        throw std::runtime_error("cannot open B+Tree file: " + path);
    }
    std::string page(4096, '\0');  // header fits in any legal page size
    file_.read(page.data(), static_cast<std::streamsize>(page.size()));
    // A shorter file only sets failbit; gcount() tells the real story
    // (the header needs 40 bytes, anything beyond is not read here).
    if (file_.gcount() < 40) {
        throw std::runtime_error("B+Tree file is truncated: " + path);
    }
    if (page.compare(0, 4, "BPT1") != 0) {
        throw std::runtime_error("bad magic (not a B+Tree file): " + path);
    }
    if (getU32(page.data() + 4) != 1) {
        throw std::runtime_error("unsupported B+Tree file version: " + path);
    }
    header_.pageSize = getU32(page.data() + 8);
    header_.pageCount = getU32(page.data() + 12);
    header_.rootId = getU32(page.data() + 16);
    header_.height = getU32(page.data() + 20);
    header_.leafCount = getU64(page.data() + 24);
    header_.keyCount = getU64(page.data() + 32);
    if (header_.pageSize < 128) {
        throw std::runtime_error("implausible page size in B+Tree file: " + path);
    }
}

const BTreeFile::NodeView& BTreeFile::load(std::uint32_t id) const {
    const auto hit = lruIndex_.find(id);
    if (hit != lruIndex_.end()) {
        lru_.splice(lru_.begin(), lru_, hit->second);  // promote to front
        ++cacheHits_;
        return hit->second->second;
    }
    // Miss: read file page id+1 (page 0 holds the header) and parse it.
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(id + 1) * header_.pageSize);
    std::string raw(header_.pageSize, '\0');
    file_.read(raw.data(), static_cast<std::streamsize>(raw.size()));
    if (!file_ || file_.gcount() != static_cast<std::streamsize>(header_.pageSize)) {
        throw std::runtime_error("failed to read B+Tree page " +
                                 std::to_string(id) + " from " + path_);
    }
    ++pageReads_;

    NodeView view;
    view.leaf = raw[0] != 0;
    const std::size_t count = getU16(raw.data() + 1);
    view.nextLeaf = getU32(raw.data() + 3);
    std::size_t offset = kNodeHeaderBytes;
    if (view.leaf) {
        view.values.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            view.values[i] = getU64(raw.data() + offset);
            offset += kValueBytes;
        }
    } else {
        view.children.resize(static_cast<std::size_t>(count) + 1);
        for (std::size_t i = 0; i <= count; ++i) {
            view.children[i] = getU32(raw.data() + offset);
            offset += kChildBytes;
        }
    }
    view.keys.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t len =
            static_cast<std::uint8_t>(raw[offset]);
        ++offset;
        view.keys[i].assign(raw, offset, len);
        offset += len;
    }

    lru_.emplace_front(id, std::move(view));
    lruIndex_[id] = lru_.begin();
    if (lru_.size() > cachePages_) {
        lruIndex_.erase(lru_.back().first);
        lru_.pop_back();
    }
    return lru_.front().second;
}

std::optional<std::uint64_t> BTreeFile::findValue(const std::string& key) const {
    std::uint32_t id = header_.rootId;
    while (true) {
        const NodeView& node = load(id);
        const auto it =
            std::lower_bound(node.keys.begin(), node.keys.end(), key);
        const std::size_t at =
            static_cast<std::size_t>(it - node.keys.begin());
        if (node.leaf) {
            if (it == node.keys.end() || *it != key) {
                return std::nullopt;
            }
            return node.values[at];
        }
        // Same routing as the in-memory tree: first separator > key.
        const std::size_t slot =
            static_cast<std::size_t>(std::upper_bound(node.keys.begin(),
                                                      node.keys.end(), key) -
                                     node.keys.begin());
        id = node.children[slot];
    }
}

std::vector<std::pair<std::string, std::uint64_t>> BTreeFile::rangeScan(
    const std::string& lo, const std::string& hi) const {
    std::vector<std::pair<std::string, std::uint64_t>> out;
    std::uint32_t id = header_.rootId;
    while (!load(id).leaf) {
        const NodeView& node = load(id);
        const std::size_t slot =
            static_cast<std::size_t>(std::upper_bound(node.keys.begin(),
                                                      node.keys.end(), lo) -
                                     node.keys.begin());
        id = node.children[slot];
    }
    while (true) {
        const NodeView& node = load(id);
        for (std::size_t i = 0; i < node.keys.size(); ++i) {
            if (node.keys[i] < lo) {
                continue;
            }
            if (!(node.keys[i] < hi)) {
                return out;
            }
            out.emplace_back(node.keys[i], node.values[i]);
        }
        if (node.nextLeaf == 0) {
            break;
        }
        id = node.nextLeaf;
    }
    return out;
}

}  // namespace mse
