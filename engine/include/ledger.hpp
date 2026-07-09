// engine/include/ledger.hpp
// Double-Entry Ledger State Machine.
//
// This is the single-threaded consumer that:
//   1. Reads TransactionRequests from the SPSC ring buffer
//   2. Validates double-entry constraints
//   3. Writes durable frames to the WAL
//   4. Updates the in-memory balance map
//   5. Pushes TransactionResponses to the reply ring buffer
//
// Design: The run() loop executes on a dedicated thread. It is the ONLY
// thread that reads from the request queue and the ONLY thread that mutates
// balances_ and accounts_. This eliminates the need for any locks on the
// hot-path state.

#pragma once

#include "ring_buffer.hpp"
#include "types.hpp"
#include "wal.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace zerolgr {

class LedgerEngine {
public:
    explicit LedgerEngine(WriteAheadLog& wal);

    // ── Account Management ────────────────────────────────────────────────
    // Thread-safe: guarded by accounts_mutex_ since these may be called
    // from the ZMQ receiver thread.

    CreateAccountResponse create_account(const CreateAccountRequest& req);

    // ── Processing ────────────────────────────────────────────────────────

    // Main processing loop — call from a dedicated consumer thread.
    // Spins on the request queue, validates and commits transactions,
    // pushes responses to the reply queue.
    void run();

    // Signal the run() loop to stop cleanly after draining pending items.
    void shutdown();

    // ── Snapshot / Metrics (thread-safe, reads atomic counters) ───────────

    // Returns a consistent snapshot of all account balances.
    // Uses accounts_mutex_ for thread safety.
    struct BalanceSnapshotData {
        std::vector<AccountInfo> accounts;
        int64_t total_assets{0};
        int64_t total_liabilities{0};
        int64_t total_equity{0};
        bool    invariant_ok{true};
    };
    BalanceSnapshotData get_balance_snapshot() const;

    // Returns real-time metrics (lock-free atomic reads).
    MetricsSample get_metrics_sample() const;

    // Reset the per-second TPS counter window (called by metrics poller).
    void reset_metrics_window();

    // ── Ring Buffers (public for ZMQ server to access) ────────────────────

    // Request queue: ZMQ receiver thread pushes, ledger consumer pops.
    SPSCRingBuffer<TransactionRequest, 1024> request_queue;

    // Reply queue: ledger consumer pushes, ZMQ sender thread pops.
    SPSCRingBuffer<TxResponseEnvelope, 1024> reply_queue;

private:
    // ── Validation ────────────────────────────────────────────────────────

    TxStatus validate_transaction(const TransactionRequest& req) const;

    // ── Application ───────────────────────────────────────────────────────

    void apply_transaction(const TransactionRequest& req,
                           const UUID& transaction_id);

    // ── Double-Entry Invariant ────────────────────────────────────────────
    // Checks that: sum(Asset balances) - sum(Liability balances) - sum(Equity balances) == 0
    bool check_invariant() const;

    // ── UUID Generation ───────────────────────────────────────────────────
    UUID generate_uuid();

    // ── Serialization ─────────────────────────────────────────────────────
    // Serialize a TransactionRequest + assigned transaction_id into a
    // FlatBuffer-compatible byte vector for WAL storage.
    std::vector<uint8_t> serialize_for_wal(const TransactionRequest& req,
                                           const UUID& transaction_id);

    // ── State ─────────────────────────────────────────────────────────────

    WriteAheadLog&   wal_;
    std::atomic<bool> running_{true};

    // Account state — only mutated by the consumer thread in run(),
    // but read by get_balance_snapshot() from other threads.
    mutable std::mutex accounts_mutex_;
    std::unordered_map<std::string, AccountInfo> accounts_;

    // Idempotency cache — maps idempotency_key -> cached response.
    // Only accessed from the consumer thread (no lock needed).
    std::unordered_map<std::string, TransactionResponse> idempotency_cache_;

    // ── Metrics Counters (lock-free) ──────────────────────────────────────

    std::atomic<uint64_t> tx_count_total_{0};
    std::atomic<uint64_t> tx_count_window_{0};  // Reset each metrics interval
    std::atomic<uint64_t> total_latency_us_window_{0};

    // ── UUID counter for deterministic IDs ────────────────────────────────
    std::atomic<uint64_t> uuid_counter_{0};
};

}  // namespace zerolgr
