// engine/tests/test_wal.cpp
// Unit tests for the memory-mapped Write-Ahead Log.

#include "wal.hpp"
#include "types.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace zerolgr;

static const std::string TEST_WAL_PATH = "./test_zerolgr.wal";

// Clean up test WAL file
static void cleanup() {
    std::filesystem::remove(TEST_WAL_PATH);
}

// ── Test 1: Create and append ─────────────────────────────────────────────

static void test_create_and_append() {
    cleanup();

    {
        WriteAheadLog wal(TEST_WAL_PATH, 4096);

        assert(wal.current_sequence() == 0);
        assert(wal.write_offset() == 0);

        // Append a simple payload
        std::string payload = "hello, WAL!";
        std::vector<uint8_t> data(payload.begin(), payload.end());

        uint64_t seq = wal.append(std::span<const uint8_t>(data));
        assert(seq == 1);
        assert(wal.current_sequence() == 1);
        assert(wal.write_offset() > 0);

        // Append another
        payload = "second entry";
        data.assign(payload.begin(), payload.end());
        seq = wal.append(std::span<const uint8_t>(data));
        assert(seq == 2);
    }

    cleanup();
    std::cout << "  [PASS] test_create_and_append" << std::endl;
}

// ── Test 2: Replay after reopen ───────────────────────────────────────────

static void test_replay() {
    cleanup();

    // Write some frames
    {
        WriteAheadLog wal(TEST_WAL_PATH, 4096);

        for (int i = 0; i < 10; ++i) {
            std::string payload = "frame_" + std::to_string(i);
            std::vector<uint8_t> data(payload.begin(), payload.end());
            wal.append(std::span<const uint8_t>(data));
        }
    }

    // Reopen and replay
    {
        WriteAheadLog wal(TEST_WAL_PATH, 4096);

        std::vector<std::string> replayed_data;
        uint64_t count = wal.replay([&](uint64_t seq, std::span<const uint8_t> data) {
            replayed_data.emplace_back(
                reinterpret_cast<const char*>(data.data()), data.size());
        });

        assert(count == 10);
        assert(replayed_data.size() == 10);

        for (int i = 0; i < 10; ++i) {
            assert(replayed_data[i] == "frame_" + std::to_string(i));
        }

        // After replay, we should be able to continue appending
        assert(wal.current_sequence() == 10);
        std::string next = "frame_10";
        std::vector<uint8_t> data(next.begin(), next.end());
        uint64_t seq = wal.append(std::span<const uint8_t>(data));
        assert(seq == 11);
    }

    cleanup();
    std::cout << "  [PASS] test_replay" << std::endl;
}

// ── Test 3: Large payloads ────────────────────────────────────────────────

static void test_large_payload() {
    cleanup();

    {
        WriteAheadLog wal(TEST_WAL_PATH, 4096);

        // Create a payload larger than one page
        std::vector<uint8_t> big_data(8192, 0xAB);
        uint64_t seq = wal.append(std::span<const uint8_t>(big_data));
        assert(seq == 1);
    }

    // Verify replay
    {
        WriteAheadLog wal(TEST_WAL_PATH);

        uint64_t count = wal.replay([&](uint64_t seq, std::span<const uint8_t> data) {
            assert(data.size() == 8192);
            for (auto b : data) {
                assert(b == 0xAB);
            }
        });
        assert(count == 1);
    }

    cleanup();
    std::cout << "  [PASS] test_large_payload" << std::endl;
}

// ── Test 4: Empty WAL replay ──────────────────────────────────────────────

static void test_empty_replay() {
    cleanup();

    {
        WriteAheadLog wal(TEST_WAL_PATH, 4096);
        uint64_t count = wal.replay([](uint64_t, std::span<const uint8_t>) {
            assert(false && "Should not be called");
        });
        assert(count == 0);
    }

    cleanup();
    std::cout << "  [PASS] test_empty_replay" << std::endl;
}

// ── Test 5: Multiple sequential appends ───────────────────────────────────

static void test_many_appends() {
    cleanup();

    constexpr int NUM_FRAMES = 1000;

    {
        WriteAheadLog wal(TEST_WAL_PATH);

        for (int i = 0; i < NUM_FRAMES; ++i) {
            std::string payload = "tx_" + std::to_string(i);
            std::vector<uint8_t> data(payload.begin(), payload.end());
            uint64_t seq = wal.append(std::span<const uint8_t>(data));
            assert(seq == static_cast<uint64_t>(i + 1));
        }
    }

    // Replay and verify
    {
        WriteAheadLog wal(TEST_WAL_PATH);

        int frame_count = 0;
        wal.replay([&](uint64_t seq, std::span<const uint8_t> data) {
            std::string expected = "tx_" + std::to_string(frame_count);
            std::string actual(reinterpret_cast<const char*>(data.data()), data.size());
            assert(actual == expected);
            frame_count++;
        });
        assert(frame_count == NUM_FRAMES);
    }

    cleanup();
    std::cout << "  [PASS] test_many_appends (" << NUM_FRAMES << " frames)" << std::endl;
}

// ── Main ──────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== WAL Tests ===" << std::endl;

    test_create_and_append();
    test_replay();
    test_large_payload();
    test_empty_replay();
    test_many_appends();

    std::cout << "=== All WAL tests passed ===" << std::endl;
    return 0;
}
