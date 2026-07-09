// engine/include/types.hpp
// Shared primitive types and constants for the ZeroLGR engine.
//
// This header defines the internal C++ representations used across the engine.
// FlatBuffers-generated types are used for serialization/deserialization at
// the ZeroMQ boundary and WAL layer; these types are the native in-memory
// representations used by the ring buffer and ledger state machine.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <chrono>

namespace zerolgr {

// ── Constants ──────────────────────────────────────────────────────────────

static constexpr uint32_t WAL_MAGIC = 0xDEAD1E7C;

static constexpr std::size_t UUID_SIZE = 16;
using UUID = std::array<uint8_t, UUID_SIZE>;

// ── Account Types ─────────────────────────────────────────────────────────

enum class AccountType : uint8_t {
    Asset     = 0,
    Liability = 1,
    Equity    = 2,
    Revenue   = 3,
    Expense   = 4,
};

// ── Account Info ──────────────────────────────────────────────────────────

struct AccountInfo {
    std::string  id;           // String identifier (e.g., "acc_001")
    std::string  name;         // Human-readable name
    AccountType  type;
    int64_t      balance{0};   // Balance in minor units (cents)
};

// ── Transaction Status ────────────────────────────────────────────────────

enum class TxStatus : uint8_t {
    OK                     = 0,
    ERR_INSUFFICIENT_FUNDS = 1,
    ERR_ACCOUNT_NOT_FOUND  = 2,
    ERR_INVARIANT_VIOLATED = 3,
    ERR_INTERNAL           = 4,
};

// ── Transaction Request ───────────────────────────────────────────────────
// This is what flows through the SPSC ring buffer from the ZMQ receiver
// thread to the ledger consumer thread.

struct TransactionRequest {
    UUID         correlation_id;     // Echoed in response to match req/rep
    std::string  idempotency_key;
    std::string  description;

    std::string  debit_account_id;   // Account to debit
    std::string  credit_account_id;  // Account to credit
    int64_t      amount_minor{0};    // Amount in minor units, always positive
    std::string  currency;           // ISO 4217

    std::string  stripe_charge_id;   // Optional Stripe linkage
};

// ── Transaction Response ──────────────────────────────────────────────────
// Flows from the ledger consumer thread back to the ZMQ sender thread
// via the reply ring buffer.

struct TransactionResponse {
    UUID         correlation_id;
    UUID         transaction_id;     // Assigned by engine on success
    TxStatus     status{TxStatus::OK};
    std::string  message;
    int64_t      latency_us{0};      // Processing time in microseconds
};

// ── Response Envelope ─────────────────────────────────────────────────────
// Wraps a TransactionResponse with the ZMQ routing identity so the sender
// thread can route it back to the correct Python DEALER socket.

struct TxResponseEnvelope {
    std::string          zmq_identity;  // ZMQ ROUTER identity frame
    TransactionResponse  response;
};

// ── Create Account Request/Response ───────────────────────────────────────

struct CreateAccountRequest {
    UUID         correlation_id;
    std::string  name;
    AccountType  type;
};

struct CreateAccountResponse {
    UUID         correlation_id;
    std::string  account_id;
    TxStatus     status{TxStatus::OK};
    std::string  message;
};

// ── Metrics Sample ────────────────────────────────────────────────────────

struct MetricsSample {
    double   tps{0.0};
    double   avg_latency_us{0.0};
    uint64_t tx_count{0};
    bool     invariant_ok{true};
};

// ── Utility: high-resolution clock shorthand ──────────────────────────────

using SteadyClock = std::chrono::steady_clock;
using TimePoint   = SteadyClock::time_point;

inline int64_t elapsed_us(TimePoint start, TimePoint end) {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

}  // namespace zerolgr
