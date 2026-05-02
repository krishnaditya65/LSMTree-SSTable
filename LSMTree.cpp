#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <bitset>
#include <map>

using namespace std;
using namespace std::chrono;

// --- PROFILER ---
struct Profiler {
    steady_clock::time_point start;
    Profiler() : start(steady_clock::now()) {}
    long long elapsed() { return duration_cast<microseconds>(steady_clock::now() - start).count(); }
};

// --- BLOOM FILTER ---
class BloomFilter {
private:
    bitset<1000> bits;
    size_t hash(const string& s, int seed) const {
        size_t h = seed;
        for (char c : s) h = h * 31 + c;
        return h % 1000;
    }
public:
    void add(const string& key) { bits.set(hash(key, 7)); bits.set(hash(key, 13)); }
    bool possiblyContains(const string& key) const { return bits.test(hash(key, 7)) && bits.test(hash(key, 13)); }
    void clear() { bits.reset(); }
};

// --- RED-BLACK TREE (MEMTABLE) ---
enum Color { RED, BLACK };
struct Node {
    string key, value;
    Color color;
    Node *left, *right, *parent;
    Node(string k, string v) : key(k), value(v), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
private:
    Node* root;
    void rotateLeft(Node*& x);
    void rotateRight(Node*& x);
    void fixInsert(Node*& z);
    void deleteTree(Node* node) { if (!node) return; deleteTree(node->left); deleteTree(node->right); delete node; }
public:
    RedBlackTree() : root(nullptr) {}
    ~RedBlackTree() { deleteTree(root); }
    void insert(string key, string value);
    string search(string key);
    void traverse(Node* node, vector<pair<string, string>>& entries);
    Node* getRoot() { return root; }
    void clear() { deleteTree(root); root = nullptr; }
};

// --- SSTABLE STRUCTURE ---
struct SSTable {
    int id;
    vector<pair<string, string>> data;
    BloomFilter filter;
};

// --- LEVELED LSM TREE ---
class LeveledLSMTree {
private:
    RedBlackTree memTable;
    BloomFilter memFilter;
    vector<SSTable*> levels[3]; // 0: L0 (Flushes), 1: L1 (Compacted), 2: L2 (Long-term)
    int memThreshold;
    int tableCounter = 0;

    void compactLevel(int level) {
        if (level >= 2 || levels[level].size() < 2) return;

        string currentL = "L" + to_string(level);
        string nextL = "L" + to_string(level + 1);
        cout << "\n!!! [COMPACTION] Moving " << currentL << " -> " << nextL << "..." << endl;
        Profiler p;

        // Multi-way merge: Higher index (newer table) wins
        map<string, string> mergedMap;
        for (auto* table : levels[level]) {
            for (auto& entry : table->data) mergedMap[entry.first] = entry.second;
            delete table;
        }
        levels[level].clear();

        // Include existing data in the next level for a true merge
        for (auto* table : levels[level+1]) {
            for (auto& entry : table->data) {
                if (mergedMap.find(entry.first) == mergedMap.end()) 
                    mergedMap[entry.first] = entry.second;
            }
            delete table;
        }
        levels[level+1].clear();

        // Build new compacted table for next level
        SSTable* compacted = new SSTable();
        compacted->id = ++tableCounter;
        for (auto const& [key, val] : mergedMap) {
            compacted->data.push_back({key, val});
            compacted->filter.add(key);
        }
        levels[level+1].push_back(compacted);

        cout << "!!! [" << nextL << " UPDATED] " << compacted->data.size() << " keys total." << endl;
        cout << "  >> Performance: " << p.elapsed() << " us" << endl;

        // Cascade compaction if the next level is now too full
        if (levels[level+1].size() >= 2 && (level+1) < 2) compactLevel(level+1);
    }

public:
    LeveledLSMTree(int threshold) : memThreshold(threshold) {
        cout << "--- [SYSTEM START] Leveled LSM Tree (Mem -> L0 -> L1 -> L2) ---" << endl;
    }

    void put(string key, string value) {
        Profiler p;
        memTable.insert(key, value);
        memFilter.add(key);
        cout << "[PUT] Key: " << left << setw(8) << key << " | Time: " << p.elapsed() << " us" << endl;

        static int count = 0;
        if (++count >= memThreshold) {
            flushToL0();
            count = 0;
        }
    }

    void flushToL0() {
        cout << ">>> [MEMTABLE FULL] Flushing to Level 0..." << endl;
        Profiler p;
        SSTable* table = new SSTable();
        table->id = ++tableCounter;
        memTable.traverse(memTable.getRoot(), table->data);
        for (auto& entry : table->data) table->filter.add(entry.first);
        
        levels[0].push_back(table);
        memTable.clear();
        memFilter.clear();
        
        cout << ">>> [L0] SSTable #" << table->id << " created. Level 0 size: " << levels[0].size() << endl;
        if (levels[0].size() >= 2) compactLevel(0);
    }

    void get(string key) {
        cout << "\n[GET] Search: '" << key << "'" << endl;
        Profiler p;

        // 1. Check MemTable
        if (memFilter.possiblyContains(key)) {
            string val = memTable.search(key);
            if (val != "") { cout << "  Found in [MemTable] | Time: " << p.elapsed() << " us" << endl; return; }
        }

        // 2. Check Levels L0, L1, L2
        for (int l = 0; l < 3; l++) {
            for (int i = levels[l].size() - 1; i >= 0; i--) {
                if (levels[l][i]->filter.possiblyContains(key)) {
                    for (auto& entry : levels[l][i]->data) {
                        if (entry.first == key) {
                            cout << "  Found in [Level " << l << "] | Time: " << p.elapsed() << " us" << endl;
                            return;
                        }
                    }
                }
            }
        }
        cout << "  Result: NOT_FOUND | Time: " << p.elapsed() << " us" << endl;
    }
};

// --- RBT IMPLEMENTATION HELPER (Simplified for logic) ---
void RedBlackTree::insert(string k, string v) {
    Node* z = new Node(k, v); Node* y = nullptr; Node* x = root;
    while (x) { y = x; x = (z->key < x->key) ? x->left : x->right; }
    z->parent = y;
    if (!y) root = z; else if (z->key < y->key) y->left = z; else y->right = z;
    fixInsert(z);
}
void RedBlackTree::fixInsert(Node*& z) {
    while (z != root && z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            Node* y = z->parent->parent->right;
            if (y && y->color == RED) { z->parent->color = BLACK; y->color = BLACK; z->parent->parent->color = RED; z = z->parent->parent; }
            else { if (z == z->parent->right) { z = z->parent; rotateLeft(z); } z->parent->color = BLACK; z->parent->parent->color = RED; rotateRight(z->parent->parent); }
        } else {
            Node* y = z->parent->parent->left;
            if (y && y->color == RED) { z->parent->color = BLACK; y->color = BLACK; z->parent->parent->color = RED; z = z->parent->parent; }
            else { if (z == z->parent->left) { z = z->parent; rotateRight(z); } z->parent->color = BLACK; z->parent->parent->color = RED; rotateLeft(z->parent->parent); }
        }
    }
    root->color = BLACK;
}
void RedBlackTree::rotateLeft(Node*& x) { Node* y = x->right; x->right = y->left; if (y->left) y->left->parent = x; y->parent = x->parent; if (!x->parent) root = y; else if (x == x->parent->left) x->parent->left = y; else x->parent->right = y; y->left = x; x->parent = y; }
void RedBlackTree::rotateRight(Node*& x) { Node* y = x->left; x->left = y->right; if (y->right) y->right->parent = x; y->parent = x->parent; if (!x->parent) root = y; else if (x == x->parent->right) x->parent->right = y; else x->parent->left = y; y->right = x; x->parent = y; }
void RedBlackTree::traverse(Node* node, vector<pair<string, string>>& entries) { if (!node) return; traverse(node->left, entries); entries.push_back({node->key, node->value}); traverse(node->right, entries); }
string RedBlackTree::search(string k) { Node* c = root; while(c) { if (c->key == k) return c->value; c = (k < c->key) ? c->left : c->right; } return ""; }

int main() {
    LeveledLSMTree db(2); // Flush every 2 keys
    
    db.put("u1", "v1"); db.put("u2", "v1"); // L0 Created
    db.put("u3", "v1"); db.put("u4", "v1"); // L0 Full -> Compact to L1
    db.put("u5", "v1"); db.put("u6", "v1"); // L0 Created
    db.put("u1", "v2"); db.put("u7", "v1"); // L0 Full -> Compact L1 Full -> Cascade to L2

    db.get("u1"); // Should find v2 in Level 2
    db.get("u5"); // Found in Level 1 (if not yet cascaded)
    db.get("unknown"); // Bloom filter skip
    return 0;
}