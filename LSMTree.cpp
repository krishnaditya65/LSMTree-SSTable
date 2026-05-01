#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <bitset>
#include <map>

using namespace std;
using namespace std::chrono;

// --- UTILS ---
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
    void add(const string& key) {
        bits.set(hash(key, 7));
        bits.set(hash(key, 13));
    }
    bool possiblyContains(const string& key) const {
        return bits.test(hash(key, 7)) && bits.test(hash(key, 13));
    }
    void clear() { bits.reset(); }
};

// --- CUSTOM RED-BLACK TREE ---
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
    void deleteTree(Node* node) {
        if (!node) return;
        deleteTree(node->left);
        deleteTree(node->right);
        delete node;
    }

public:
    RedBlackTree() : root(nullptr) {}
    ~RedBlackTree() { deleteTree(root); }
    void insert(string key, string value);
    string search(string key);
    void traverse(Node* node, vector<pair<string, string>>& entries);
    Node* getRoot() { return root; }
    void clear() { deleteTree(root); root = nullptr; }
};

// --- LSM TREE ---
struct SSTable {
    int id;
    vector<pair<string, string>> data; 
    BloomFilter filter;
};

class LSMTree {
private:
    RedBlackTree memTable;
    BloomFilter memFilter;
    vector<SSTable*> diskLevels;
    int threshold;
    int tableCounter;

public:
    LSMTree(int memThreshold) : threshold(memThreshold), tableCounter(0) {
        cout << "--- [SYSTEM START] LSM Tree (RBT + Bloom + Compaction) ---" << endl;
    }

    void put(string key, string value) {
        Profiler p;
        cout << "\n[PUT] Key: '" << key << "' Value: '" << value << "'" << endl;
        memTable.insert(key, value);
        memFilter.add(key);
        cout << "  >> Performance: " << p.elapsed() << " us (MemTable Update)" << endl;

        static int count = 0;
        if (++count >= threshold) {
            flushToDisk();
            count = 0;
        }
    }

    void flushToDisk() {
        cout << ">>> [MEMTABLE FULL] Flushing to SSTable..." << endl;
        Profiler p;
        SSTable* table = new SSTable();
        table->id = ++tableCounter;
        memTable.traverse(memTable.getRoot(), table->data);
        for (auto& entry : table->data) table->filter.add(entry.first);
        
        diskLevels.push_back(table);
        memTable.clear();
        memFilter.clear();
        
        cout << ">>> [DISK] SSTable #" << table->id << " created (" << table->data.size() << " keys)." << endl;
        cout << "  >> Performance: " << p.elapsed() << " us" << endl;

        // Trigger compaction if we have more than 2 tables
        if (diskLevels.size() >= 3) {
            compact();
        }
    }

    void compact() {
        cout << "!!! [COMPACTION] Merging " << diskLevels.size() << " SSTables..." << endl;
        Profiler p;
        
        // Multi-way merge: We use a map to keep the newest value for each key
        // In a real LSM, you'd use a priority queue for efficiency
        map<string, string> mergedMap;
        for (auto* table : diskLevels) {
            for (auto& entry : table->data) {
                mergedMap[entry.first] = entry.second; // Newer tables overwrite older ones
            }
        }

        // Clean up old tables
        for (auto* table : diskLevels) delete table;
        diskLevels.clear();

        // Create new compacted SSTable
        SSTable* compacted = new SSTable();
        compacted->id = 999; // ID for compacted table
        for (auto const& [key, val] : mergedMap) {
            compacted->data.push_back({key, val});
            compacted->filter.add(key);
        }
        diskLevels.push_back(compacted);

        cout << "!!! [COMPACTION COMPLETE] Result: 1 Table, " << compacted->data.size() << " unique keys." << endl;
        cout << "  >> Performance: " << p.elapsed() << " us" << endl;
    }

    void get(string key) {
        cout << "\n[GET] Searching for '" << key << "'..." << endl;
        Profiler p;

        if (!memFilter.possiblyContains(key)) {
            cout << "  [Bloom] Skip: Not in MemTable." << endl;
        } else {
            string val = memTable.search(key);
            if (val != "") {
                cout << "Found in [MemTable]: " << val << " | Time: " << p.elapsed() << " us" << endl;
                return;
            }
        }

        for (int i = diskLevels.size() - 1; i >= 0; i--) {
            if (!diskLevels[i]->filter.possiblyContains(key)) {
                cout << "  [Bloom] Skip: Not in SSTable #" << diskLevels[i]->id << endl;
                continue;
            }
            for (auto& entry : diskLevels[i]->data) {
                if (entry.first == key) {
                    cout << "Found in [SSTable #" << diskLevels[i]->id << "]: " << entry.second << " | Time: " << p.elapsed() << " us" << endl;
                    return;
                }
            }
        }
        cout << "Result: NOT FOUND | Total Time: " << p.elapsed() << " us" << endl;
    }
};

// --- RED-BLACK TREE LOGIC ---
void RedBlackTree::insert(string key, string value) {
    Node* z = new Node(key, value);
    Node* y = nullptr;
    Node* x = root;
    while (x != nullptr) {
        y = x;
        if (z->key < x->key) x = x->left;
        else x = x->right;
    }
    z->parent = y;
    if (y == nullptr) root = z;
    else if (z->key < y->key) y->left = z;
    else y->right = z;
    fixInsert(z);
}

void RedBlackTree::fixInsert(Node*& z) {
    while (z != root && z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            Node* y = z->parent->parent->right;
            if (y && y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) { z = z->parent; rotateLeft(z); }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rotateRight(z->parent->parent);
            }
        } else {
            Node* y = z->parent->parent->left;
            if (y && y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) { z = z->parent; rotateRight(z); }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rotateLeft(z->parent->parent);
            }
        }
    }
    root->color = BLACK;
}

void RedBlackTree::rotateLeft(Node*& x) {
    Node* y = x->right; x->right = y->left;
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (!x->parent) root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x; x->parent = y;
}

void RedBlackTree::rotateRight(Node*& x) {
    Node* y = x->left; x->left = y->right;
    if (y->right) y->right->parent = x;
    y->parent = x->parent;
    if (!x->parent) root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x; x->parent = y;
}

void RedBlackTree::traverse(Node* node, vector<pair<string, string>>& entries) {
    if (!node) return;
    traverse(node->left, entries);
    entries.push_back({node->key, node->value});
    traverse(node->right, entries);
}

string RedBlackTree::search(string key) {
    Node* curr = root;
    while(curr) {
        if (curr->key == key) return curr->value;
        curr = (key < curr->key) ? curr->left : curr->right;
    }
    return "";
}

int main() {
    LSMTree db(2);
    db.put("user1", "V1");
    db.put("user2", "V1"); // Flush 1
    db.put("user1", "V2"); // Update
    db.put("user3", "V1"); // Flush 2
    db.put("user4", "V1");
    db.put("user5", "V1"); // Flush 3 + Compaction

    db.get("user1"); // Should return V2
    db.get("user99"); // Bloom Filter should skip
    return 0;
}