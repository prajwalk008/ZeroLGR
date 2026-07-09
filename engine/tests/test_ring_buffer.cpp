// engine/tests/test_ring_buffer.cpp
// Unit tests for the lock-free SPSC ring buffer.

#include "ring_buffer.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace zerolgr;

// ── Test 1: Basic push/pop ────────────────────────────────────────────────

static void test_basic_push_pop() {
    SPSCRingBuffer<int, 4> rb;  // capacity = 3 (one slot reserved)

    assert(rb.empty());
    assert(rb.size_approx() == 0);

    assert(rb.push(10));
    assert(rb.push(20));
    assert(rb.push(30));
    assert(!rb.push(40));  // Full — only 3 usable slots

    assert(rb.size_approx() == 3);
    assert(!rb.empty());

    int val;
    assert(rb.pop(val) && val == 10);
    assert(rb.pop(val) && val == 20);
    assert(rb.pop(val) && val == 30);
    assert(!rb.pop(val));  // Empty

    assert(rb.empty());

    std::cout << "  [PASS] test_basic_push_pop" << std::endl;
}

// ── Test 2: Wrap-around ───────────────────────────────────────────────────

static void test_wraparound() {
    SPSCRingBuffer<int, 4> rb;

    // Fill and drain multiple times to test circular behavior
    for (int round = 0; round < 5; ++round) {
        assert(rb.push(round * 10 + 1));
        assert(rb.push(round * 10 + 2));
        assert(rb.push(round * 10 + 3));

        int val;
        assert(rb.pop(val) && val == round * 10 + 1);
        assert(rb.pop(val) && val == round * 10 + 2);
        assert(rb.pop(val) && val == round * 10 + 3);
    }

    std::cout << "  [PASS] test_wraparound" << std::endl;
}

// ── Test 3: Move semantics ────────────────────────────────────────────────

static void test_move_push() {
    SPSCRingBuffer<std::string, 4> rb;

    std::string s = "hello";
    assert(rb.push(std::move(s)));

    std::string out;
    assert(rb.pop(out));
    assert(out == "hello");

    std::cout << "  [PASS] test_move_push" << std::endl;
}

// ── Test 4: Multi-threaded producer/consumer ──────────────────────────────

static void test_concurrent() {
    constexpr std::size_t NUM_ITEMS = 100'000;
    SPSCRingBuffer<uint64_t, 1024> rb;

    std::vector<uint64_t> received;
    received.reserve(NUM_ITEMS);

    // Consumer thread
    std::thread consumer([&]() {
        uint64_t val;
        while (received.size() < NUM_ITEMS) {
            if (rb.pop(val)) {
                received.push_back(val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Producer thread (this thread)
    for (uint64_t i = 0; i < NUM_ITEMS; ++i) {
        while (!rb.push(i)) {
            std::this_thread::yield();
        }
    }

    consumer.join();

    // Verify all items received in order
    assert(received.size() == NUM_ITEMS);
    for (uint64_t i = 0; i < NUM_ITEMS; ++i) {
        assert(received[i] == i);
    }

    std::cout << "  [PASS] test_concurrent (" << NUM_ITEMS << " items)" << std::endl;
}

// ── Test 5: Capacity constraint ───────────────────────────────────────────

static void test_capacity() {
    SPSCRingBuffer<int, 8> rb;  // capacity = 7

    assert(SPSCRingBuffer<int, 8>::capacity() == 7);

    for (int i = 0; i < 7; ++i) {
        assert(rb.push(i));
    }
    assert(!rb.push(99));  // Must be full
    assert(rb.size_approx() == 7);

    std::cout << "  [PASS] test_capacity" << std::endl;
}

// ── Main ──────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== SPSC Ring Buffer Tests ===" << std::endl;

    test_basic_push_pop();
    test_wraparound();
    test_move_push();
    test_concurrent();
    test_capacity();

    std::cout << "=== All ring buffer tests passed ===" << std::endl;
    return 0;
}
