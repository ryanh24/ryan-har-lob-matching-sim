#pragma once
#include "lob/types.hpp"
#include "lob/limit.hpp"
#include "lob/pool.hpp"
#include <algorithm>   // std::max
#include <cstdlib>     // std::abs
#include <vector>      // test helpers

// A hand-rolled AVL tree of Limit price levels — one per side, replacing std::map<Price, Limit>.
// Recursive AVL with intrusive links on Limit (left/right/height); no parent pointer needed.
// min()/max() give best ask / best bid. Deletion splices in the successor NODE (not its value)
// so any Orders whose parent points at that Limit stay valid.

namespace lob {

class PriceTree {
    static constexpr size_t CAP = 1u << 16;   // up to 65,536 active price levels
    Pool<Limit, CAP> pool_;
    Limit* root_ = nullptr;

    // Height = longest path down to a leaf (empty subtree = 0, leaf = 1).
    static int  height(Limit* n)  { return n ? n->height : 0; }
    // Balance = how much taller the left subtree is than the right.
    static int  balance(Limit* n) { return n ? height(n->left) - height(n->right) : 0; }
    // A node's height is 1 plus its taller child's height (the longest path leaves via that child).
    static void update_height(Limit* n) { n->height = 1 + std::max(height(n->left), height(n->right)); }

    Limit* new_limit(Price p) {
        Limit* n = pool_.allocate();
        *n = Limit{};        // reset a recycled slot to clean defaults (head/tail null, height 1, …)
        n->price = p;
        return n;
    }

    // Left rotation around x (right-heavy): x's right child y becomes the new subtree root,
    // x drops to y's left, and y's old left subtree moves to x's right. Returns y.
    static Limit* rotate_left(Limit* x) {
        Limit* y    = x->right;
        Limit* temp = y->left;
        y->left  = x;
        x->right = temp;
        update_height(x);    // x is now the lower node → update it first
        update_height(y);
        return y;
    }

    // Right rotation around y (left-heavy): the mirror of rotate_left. Returns x.
    static Limit* rotate_right(Limit* y) {
        Limit* x    = y->left;
        Limit* temp = x->right;
        x->right = y;
        y->left  = temp;
        update_height(y);
        update_height(x);
        return x;
    }

    // Restore the AVL invariant at n after a change below it. Returns the new subtree root.
    static Limit* rebalance(Limit* n) {
        update_height(n);
        int b = balance(n);
        if (b > 1) {                                                    // left-heavy
            if (balance(n->left) < 0) n->left = rotate_left(n->left);   // left-right → reduce to left-left
            return rotate_right(n);
        }
        if (b < -1) {                                                   // right-heavy
            if (balance(n->right) > 0) n->right = rotate_right(n->right); // right-left → reduce to right-right
            return rotate_left(n);
        }
        return n;                                                       // already within balance
    }

    // BST-insert an already-allocated node, rebalancing each ancestor on the way back up.
    // (find_or_create guarantees the price is new, so there is no equal case here.)
    Limit* insert_node(Limit* root, Limit* node) {
        if (root == nullptr) return node;
        if (node->price < root->price) root->left  = insert_node(root->left,  node);
        else                           root->right = insert_node(root->right, node);
        return rebalance(root);
    }

    // Remove the minimum node of node's subtree; hand it back via out. Returns the new root.
    Limit* erase_min(Limit* node, Limit*& out) {
        if (node->left == nullptr) { out = node; return node->right; }  // min found; its right child fills in
        node->left = erase_min(node->left, out);
        return rebalance(node);
    }

    // Delete the node with price p. The two-children case grafts in the in-order successor NODE
    // (not its value), so Orders whose parent points at that Limit keep pointing at a live node.
    Limit* erase_rec(Limit* node, Price p) {
        if (node == nullptr) return nullptr;
        if (p < node->price)      node->left  = erase_rec(node->left, p);
        else if (p > node->price) node->right = erase_rec(node->right, p);
        else {
            if (node->left == nullptr || node->right == nullptr) {
                Limit* child = node->left ? node->left : node->right;   // 0 or 1 child
                pool_.deallocate(node);
                return child;                                           // parent frame rebalances
            }
            Limit* succ = nullptr;
            Limit* new_right = erase_min(node->right, succ);            // detach in-order successor
            succ->left  = node->left;                                  // graft succ into node's slot
            succ->right = new_right;
            pool_.deallocate(node);
            node = succ;
        }
        return rebalance(node);
    }

    // recursive validators / traversal used only by tests
    static int actual_height(Limit* n) { return n ? 1 + std::max(actual_height(n->left), actual_height(n->right)) : 0; }
    static bool check_balanced(Limit* n) {
        if (!n) return true;
        if (std::abs(actual_height(n->left) - actual_height(n->right)) > 1) return false;
        return check_balanced(n->left) && check_balanced(n->right);
    }
    static void inorder(Limit* n, std::vector<Price>& out) {
        if (!n) return;
        inorder(n->left, out); out.push_back(n->price); inorder(n->right, out);
    }

public:
    bool empty() const { return root_ == nullptr; }

    Limit* find(Price p) const {
        Limit* n = root_;
        while (n) {
            if (p < n->price)      n = n->left;
            else if (p > n->price) n = n->right;
            else return n;
        }
        return nullptr;
    }

    // Return the level at price p, creating it if absent.
    Limit* find_or_create(Price p) {
        if (Limit* e = find(p)) return e;
        Limit* n = new_limit(p);
        root_ = insert_node(root_, n);
        return n;
    }

    void erase(Price p) { root_ = erase_rec(root_, p); }

    Limit* min() const { Limit* n = root_; if (!n) return nullptr; while (n->left)  n = n->left;  return n; } // best ask
    Limit* max() const { Limit* n = root_; if (!n) return nullptr; while (n->right) n = n->right; return n; } // best bid

    // test-only helpers
    bool balanced() const { return check_balanced(root_); }
    void collect_inorder(std::vector<Price>& out) const { inorder(root_, out); }
};

} // namespace lob
