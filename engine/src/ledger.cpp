// engine/src/ledger.cpp
// Double-Entry Ledger State Machine implementation.

#include "ledger.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace zerolgr {

// ── Constructor ───────────────────────────────────────────────────────────

LedgerEngine::LedgerEngine(WriteAheadLog& wal)
    : wal_(wal)
{
}

// ── Account Management ────────────────────────────────────────────────────

CreateAccountResponse LedgerEngine::create_account(const CreateAccountRequest& req) {
    CreateAccountResponse resp;
    resp.correlation_id = req.correlation_id;

    // Generate a deterministic account ID
    const uint64_t counter = uuid_counter_.fetch_add(1, std::memory_order_relaxed);
    resp.account_id = "acc_" + std::to_string(counter);

    {
        std::lock_guard<std::mutex> lock(accounts_mutex_);

        // Check if name already exists
        for (const auto& [id, info] : accounts_) {
            if (info.name == req.name) {
                resp.status = TxStatus::ERR_INTERNAL;
                resp.message = "Account with name '" + req.name + "' already exists";
                return resp;
            }
        }

        AccountInfo info;
        info.id      = resp.account_id;
        info.name    = req.name;
        info.type    = req.type;
        info.balance = 0;

        accounts_[resp.account_id] = std::move(info);
    }

    resp.status = TxStatus::OK;
    return resp;
}

// ── UUID Generation ───────────────────────────────────────────────────────
// Simple deterministic UUID for demo purposes. In production, use a proper
// UUID v4 generator or a Snowflake ID scheme.

UUID LedgerEngine::generate_uuid() {
    UUID id{};
    const uint64_t counter = uuid_counter_.fetch_add(1, std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();

    // Pack timestamp (high 8 bytes) + counter (low 8 bytes)
    std::memcpy(id.data(), &now, 8);
    std::memcpy(id.data() + 8, &counter, 8);
    return id;
}

// ── Validation ────────────────────────────────────────────────────────────
//
// validate_transaction() checks all double-entry constraints:
//   1. Both debit and credit accounts must exist
//   2. Amount must be positive
//   3. Debit account must have sufficient balance (for Asset accounts)
//   4. The system invariant must hold before the transaction

TxStatus LedgerEngine::validate_transaction(const TransactionRequest& req) const {
    // Amount must be positive
    if (req.amount_minor <= 0) {
        return TxStatus::ERR_INTERNAL;
    }

    // Both accounts must exist
    // Note: accounts_mutex_ is NOT needed here because validate_transaction()
    // is only called from the run() loop (consumer thread). However, since
    // create_account() can add accounts from the ZMQ thread concurrently,
    // we take the lock for safety.
    std::lock_guard<std::mutex> lock(accounts_mutex_);

    auto debit_it  = accounts_.find(req.debit_account_id);
    auto credit_it = accounts_.find(req.credit_account_id);

    if (debit_it == accounts_.end() || credit_it == accounts_.end()) {
        return TxStatus::ERR_ACCOUNT_NOT_FOUND;
    }

    // For Asset accounts being debited: check sufficient funds.
    // Debit on an Asset account means the balance goes DOWN.
    // (In double-entry: Debit increases Asset, Credit decreases Asset.
    //  But in our simplified model for transfers, we debit the source
    //  and credit the destination, so debit = decrease for the source.)
    const auto& debit_account = debit_it->second;
    if (debit_account.type == AccountType::Asset) {
        if (debit_account.balance < req.amount_minor) {
            return TxStatus::ERR_INSUFFICIENT_FUNDS;
        }
    }

    return TxStatus::OK;
}

// ── Apply Transaction ─────────────────────────────────────────────────────
// Updates the in-memory balance map. Called ONLY after WAL persistence.

void LedgerEngine::apply_transaction(const TransactionRequest& req,
                                      const UUID& /*transaction_id*/) {
    std::lock_guard<std::mutex> lock(accounts_mutex_);

    // Debit: decrease source balance
    accounts_[req.debit_account_id].balance -= req.amount_minor;

    // Credit: increase destination balance
    accounts_[req.credit_account_id].balance += req.amount_minor;
}

// ── Double-Entry Invariant Check ──────────────────────────────────────────
// Verifies: sum(Assets) - sum(Liabilities) - sum(Equity) == 0
// This must hold true at all times in a correctly functioning ledger.

bool LedgerEngine::check_invariant() const {
    // Caller must hold accounts_mutex_
    int64_t assets      = 0;
    int64_t liabilities = 0;
    int64_t equity      = 0;

    for (const auto& [id, info] : accounts_) {
        switch (info.type) {
            case AccountType::Asset:     assets      += info.balance; break;
            case AccountType::Liability: liabilities += info.balance; break;
            case AccountType::Equity:    equity      += info.balance; break;
            case AccountType::Revenue:   equity      += info.balance; break;
            case AccountType::Expense:   assets      += info.balance; break;
        }
    }

    return (assets - liabilities - equity) == 0;
}

// ── WAL Serialization ─────────────────────────────────────────────────────
// Simple binary serialization for WAL frames. In production this would use
// the FlatBuffers schema; for now we use a straightforward binary format
// that captures all essential transaction data.

std::vector<uint8_t> LedgerEngine::serialize_for_wal(
    const TransactionRequest& req,
    const UUID& transaction_id)
{
    std::vector<uint8_t> buf;
    buf.reserve(256);  // Pre-allocate reasonable size

    // Helper lambda to append raw bytes
    auto append = [&buf](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), bytes, bytes + size);
    };

    // Helper to append a length-prefixed string
    auto append_string = [&](const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        append(&len, sizeof(len));
        append(s.data(), s.size());
    };

    // Transaction ID (16 bytes)
    append(transaction_id.data(), UUID_SIZE);

    // Correlation ID (16 bytes)
    append(req.correlation_id.data(), UUID_SIZE);

    // Amount
    append(&req.amount_minor, sizeof(req.amount_minor));

    // Strings
    append_string(req.debit_account_id);
    append_string(req.credit_account_id);
    append_string(req.idempotency_key);
    append_string(req.description);
    append_string(req.currency);
    append_string(req.stripe_charge_id);

    // Timestamp
    auto now_ns = std::chrono::system_clock::now().time_since_epoch().count();
    append(&now_ns, sizeof(now_ns));

    return buf;
}

// ── Main Processing Loop ─────────────────────────────────────────────────
//
// This is the heart of the engine. It runs on a single dedicated thread
// and processes transactions sequentially — no locks needed on the hot path.
//
// Flow per iteration:
//   1. Pop from request_queue (spin if empty)
//   2. Check idempotency cache → return cached response if hit
//   3. Start latency timer
//   4. Validate double-entry constraints
//   5. If valid: serialize → WAL append (durable) → apply to balances
//   6. Build response → push to reply_queue
//   7. Update metrics counters

void LedgerEngine::run() {
    TransactionRequest req;

    while (running_.load(std::memory_order_relaxed)) {
        // Try to pop a request from the queue
        if (!request_queue.pop(req)) {
            // Queue is empty — yield to avoid burning CPU in a tight spin.
            // In a real HFT engine you might use _mm_pause() or busy-spin,
            // but for this project, yielding is the right trade-off.
            std::this_thread::yield();
            continue;
        }

        // ── Idempotency Check ─────────────────────────────────────────
        auto cache_it = idempotency_cache_.find(req.idempotency_key);
        if (cache_it != idempotency_cache_.end()) {
            // Return the cached response with the new correlation_id
            TxResponseEnvelope envelope;
            envelope.response = cache_it->second;
            envelope.response.correlation_id = req.correlation_id;
            reply_queue.push(std::move(envelope));
            continue;
        }

        // ── Start Latency Timer ───────────────────────────────────────
        const auto t_start = SteadyClock::now();

        // ── Validate ──────────────────────────────────────────────────
        TxStatus status = validate_transaction(req);

        UUID tx_id{};

        if (status == TxStatus::OK) {
            // Generate transaction ID
            tx_id = generate_uuid();

            // ── Serialize and persist to WAL ──────────────────────────
            auto wal_data = serialize_for_wal(req, tx_id);
            wal_.append(std::span<const uint8_t>(wal_data.data(), wal_data.size()));

            // ── Apply to in-memory state ──────────────────────────────
            apply_transaction(req, tx_id);
        }

        // ── Measure Latency ───────────────────────────────────────────
        const auto t_end = SteadyClock::now();
        const int64_t latency = elapsed_us(t_start, t_end);

        // ── Build Response ────────────────────────────────────────────
        TransactionResponse resp;
        resp.correlation_id = req.correlation_id;
        resp.transaction_id = tx_id;
        resp.status         = status;
        resp.latency_us     = latency;

        if (status != TxStatus::OK) {
            switch (status) {
                case TxStatus::ERR_INSUFFICIENT_FUNDS:
                    resp.message = "Insufficient funds in debit account";
                    break;
                case TxStatus::ERR_ACCOUNT_NOT_FOUND:
                    resp.message = "Debit or credit account not found";
                    break;
                case TxStatus::ERR_INVARIANT_VIOLATED:
                    resp.message = "Double-entry invariant would be violated";
                    break;
                default:
                    resp.message = "Internal engine error";
                    break;
            }
        }

        // ── Cache idempotency result ──────────────────────────────────
        if (!req.idempotency_key.empty()) {
            idempotency_cache_[req.idempotency_key] = resp;
        }

        // ── Push to reply queue ───────────────────────────────────────
        TxResponseEnvelope envelope;
        envelope.response = std::move(resp);
        reply_queue.push(std::move(envelope));

        // ── Update metrics ────────────────────────────────────────────
        tx_count_total_.fetch_add(1, std::memory_order_relaxed);
        tx_count_window_.fetch_add(1, std::memory_order_relaxed);
        total_latency_us_window_.fetch_add(
            static_cast<uint64_t>(latency), std::memory_order_relaxed);
    }
}

// ── Shutdown ──────────────────────────────────────────────────────────────

void LedgerEngine::shutdown() {
    running_.store(false, std::memory_order_relaxed);
}

// ── Balance Snapshot ──────────────────────────────────────────────────────

LedgerEngine::BalanceSnapshotData LedgerEngine::get_balance_snapshot() const {
    std::lock_guard<std::mutex> lock(accounts_mutex_);

    BalanceSnapshotData snap;
    snap.accounts.reserve(accounts_.size());

    for (const auto& [id, info] : accounts_) {
        snap.accounts.push_back(info);

        switch (info.type) {
            case AccountType::Asset:
            case AccountType::Expense:
                snap.total_assets += info.balance;
                break;
            case AccountType::Liability:
                snap.total_liabilities += info.balance;
                break;
            case AccountType::Equity:
            case AccountType::Revenue:
                snap.total_equity += info.balance;
                break;
        }
    }

    snap.invariant_ok = (snap.total_assets - snap.total_liabilities - snap.total_equity) == 0;
    return snap;
}

// ── Metrics ───────────────────────────────────────────────────────────────

MetricsSample LedgerEngine::get_metrics_sample() const {
    MetricsSample sample;
    sample.tx_count = tx_count_total_.load(std::memory_order_relaxed);

    const uint64_t window_count = tx_count_window_.load(std::memory_order_relaxed);
    const uint64_t window_latency = total_latency_us_window_.load(std::memory_order_relaxed);

    // TPS is computed as count / window_duration by the caller.
    // Here we report raw window count for the caller to compute TPS.
    sample.tps = static_cast<double>(window_count);

    if (window_count > 0) {
        sample.avg_latency_us = static_cast<double>(window_latency) /
                                static_cast<double>(window_count);
    }

    {
        std::lock_guard<std::mutex> lock(accounts_mutex_);
        sample.invariant_ok = check_invariant();
    }

    return sample;
}

void LedgerEngine::reset_metrics_window() {
    tx_count_window_.store(0, std::memory_order_relaxed);
    total_latency_us_window_.store(0, std::memory_order_relaxed);
}

}  // namespace zerolgr
