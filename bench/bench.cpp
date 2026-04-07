// bench/bench.cpp — correctness check first
#include "art.h"
#include <cassert>
#include <iostream>

int main() {
    ART tree;
    // Insert 1M dense integers
    for (uint64_t i = 0; i < 1'000'000; i++) {
        uint64_t key = __builtin_bswap64(i);  // big-endian for lexicographic order
        tree.insert((uint8_t*)&key, 8, (void*)(i + 1));
    }
    // Verify all keys found
    for (uint64_t i = 0; i < 1'000'000; i++) {
        uint64_t key = __builtin_bswap64(i);
        void* val = tree.lookup((uint8_t*)&key, 8);
        assert(val == (void*)(i + 1));
    }
    std::cout << "Correctness: PASS\n";
}