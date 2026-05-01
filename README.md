This is a basic implementation of a custom Red-Black Tree and Bloom Filter that generally serves as a high-performance MemTable for LSM-trees, 
commonly used in write-intensive databases like RocksDB or Cassandra to ensure $O(\log n)$ sorted writes and fast negative lookup filtering.

MemTable: Custom Red-Black Tree for sorted, high-speed memory writes O(log N) in memory.

SSTable: Persistent, sorted storage segments created from MemTable flushes after exceeding threshold.

Bloom Filter: Probabilistic data structure used to skip levels during GET requests.

Compaction: Background process that merges multiple SSTables to reclaim space and deduplicate keys to maintain read efficiency.
