// engine/include/ring_buffer.hpp
// Lock-free Single-Producer Single-Consumer (SPSC) circular queue.
//
// Design rationale:
// - Uses C++20 concepts to enforce power-of-two capacity at compile time.
// - alignas(64) on head_ and tail_ to prevent false sharing across CPU cache
//   lines (64 bytes on x86_64). Without this, the CPU would ping-pong the
//   shared cache line between cores on every access, destroying performance.
// - Memory orders are the MINIMAL correct set:
//     relaxed  — for loading an index owned exclusively by the calling thread
//     acquire  — for loading a cross-thread index (must see prior writes)
//     release  — for publishing an index update (prior writes become visible)
//   seq_cst is deliberately avoided: it emits a full MFENCE on x86, which is
//   3–7× slower than release, and is unnecessary for SPSC queues.
// - Capacity must be a power of two so that modular arithmetic can use a
//   single AND instruction (& MASK) instead of an expensive division (% N).

#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <new>          // std::hardware_destructive_interference_size

namespace zerolgr {

// Cache line size constant. Use the C++17 standard constant if available,
// otherwise fall back to 64 (correct for x86_64 and most ARM64).
#ifdef __cpp_lib_hardware_interference_size
    static constexpr std::size_t CACHE_LINE_SIZE =
        std::hardware_destructive_interference_size;
#else
    static constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

template<typename T, std::size_t N>
    requires (N >= 2 && (N & (N - 1)) == 0)
class SPSCRingBuffer {
    static constexpr std::size_t MASK = N - 1;

    // ── Cache-line-separated atomics to prevent false sharing ─────────────
    // head_ is written ONLY by the Consumer thread.
    // tail_ is written ONLY by the Producer thread.
    // Placing them on separate cache lines means each core can modify its
    // own atomic without invalidating the other core's cache.
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_{0};

    // The buffer itself. Pre-allocated, fixed-size, zero heap allocation
    // during push/pop operations.
    alignas(CACHE_LINE_SIZE) std::array<T, N> buf_;

public:
    // ── Producer thread ONLY ──────────────────────────────────────────────
    //
    // Returns true if the item was enqueued, false if the buffer is full.
    // The producer must not call pop().
    //
    // Memory order reasoning:
    //   tail_ load  = relaxed  (only this thread writes tail_)
    //   head_ load  = acquire  (must see consumer's latest head_ store)
    //   tail_ store = release  (buf_[t] write must happen-before consumer sees new tail_)
    //
    bool push(const T& val) noexcept {
        const auto t    = tail_.load(std::memory_order_relaxed);
        const auto next = (t + 1) & MASK;
        if (next == head_.load(std::memory_order_acquire))
            return false;  // full
        buf_[t] = val;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // Move-push variant for types with expensive copy.
    bool push(T&& val) noexcept {
        const auto t    = tail_.load(std::memory_order_relaxed);
        const auto next = (t + 1) & MASK;
        if (next == head_.load(std::memory_order_acquire))
            return false;
        buf_[t] = std::move(val);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // ── Consumer thread ONLY ──────────────────────────────────────────────
    //
    // Returns true if an item was dequeued into `out`, false if empty.
    // The consumer must not call push().
    //
    // Memory order reasoning:
    //   head_ load  = relaxed  (only this thread writes head_)
    //   tail_ load  = acquire  (must see producer's buf_[t] write before its tail_ store)
    //   head_ store = release  (buf_[h] read must happen-before producer sees new head_)
    //
    bool pop(T& out) noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        if (h == tail_.load(std::memory_order_acquire))
            return false;  // empty
        out = std::move(buf_[h]);
        head_.store((h + 1) & MASK, std::memory_order_release);
        return true;
    }

    // ── Query methods (safe from any thread, but values may be stale) ─────

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_relaxed);
        return (t - h + N) & MASK;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return N - 1;  // One slot is always reserved to distinguish full from empty
    }
};

}  // namespace zerolgr
