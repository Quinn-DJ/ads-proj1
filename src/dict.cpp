// dict.cpp -- splay-tree and hash dictionaries (see dict.hpp).

#include "dict.hpp"

#include <algorithm>
#include <utility>

namespace mse {

// ---------------------------------------------------------------------------
// SplayDictionary
// ---------------------------------------------------------------------------

SplayDictionary::~SplayDictionary() {
    destroy(root_);
}

int SplayDictionary::compare(const std::string& a, const std::string& b) {
    ++stats_.stringComparisons;
    return a.compare(b);
}

// Single rotation around x (x must have a parent). Handles the four links:
// parent, grandparent, and x's inner child.
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

// Move x to the root with the three splay steps (zig / zig-zig / zig-zag).
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

// In-order traversal (iterative, so a deep tree cannot overflow the stack).
// This is also the "freeze" step: BST order == lexicographic order.
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

std::string SplayDictionary::rootTermForTesting() const {
    return root_ == nullptr ? std::string() : root_->entry.term;
}

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

HashDictionary::~HashDictionary() {
    for (Node* head : buckets_) {
        destroyChain(head);
    }
}

std::uint64_t HashDictionary::hashOf(const std::string& term) {
    // FNV-1a, 64 bit: offset basis and prime from the FNV specification.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : term) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

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
    Node* node = new Node(term);
    node->next = buckets_[index];
    buckets_[index] = node;
    ++size_;
    if (size_ * 4 > buckets_.size() * 3) {  // load factor > 0.75
        rehash(buckets_.size() * 2);
    }
    return node->entry;
}

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

std::vector<TermEntry*> HashDictionary::sortedEntries() {
    std::vector<TermEntry*> out;
    out.reserve(size_);
    for (Node* head : buckets_) {
        for (Node* node = head; node != nullptr; node = node->next) {
            out.push_back(&node->entry);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const TermEntry* a, const TermEntry* b) { return a->term < b->term; });
    return out;
}

void HashDictionary::destroyChain(Node* node) {
    while (node != nullptr) {
        Node* next = node->next;
        delete node;
        node = next;
    }
}

}  // namespace mse
