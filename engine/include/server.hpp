// engine/include/server.hpp
// ZeroMQ Engine Server.
//
// Runs two I/O threads alongside the LedgerEngine consumer thread:
//
//   Thread 1 (receiver_loop): ROUTER socket receives FlatBuffer-serialized
//     TransactionRequest messages from the Python DEALER socket, deserializes
//     them, and pushes them into the LedgerEngine's request ring buffer.
//
//   Thread 2 (sender_loop): Pops TransactionResponse envelopes from the
//     LedgerEngine's reply ring buffer, serializes them, and sends them
//     back through the ROUTER socket to the correct Python client identity.
//
// Socket pattern: ROUTER-DEALER
//   - Python (DEALER): async, non-blocking sends with correlation IDs
//   - C++ (ROUTER): tracks client identities for routing replies
//
// The ROUTER socket is shared between receiver and sender via ZeroMQ's
// internal thread-safe message queue. However, since ZMQ sockets are NOT
// thread-safe for concurrent send/recv from multiple threads, we use
// a single ROUTER socket on the receiver thread and a separate PUSH/PULL
// inproc pair to ferry responses from the sender thread to the receiver
// thread for actual socket transmission.

#pragma once

#include "ledger.hpp"

#include <atomic>
#include <string>
#include <thread>

namespace zerolgr {

class EngineServer {
public:
    // endpoint: ZMQ bind address (e.g., "tcp://*:5555" or "ipc:///tmp/zerolgr.ipc")
    explicit EngineServer(LedgerEngine& engine,
                          const std::string& endpoint = "tcp://*:5555");

    ~EngineServer();

    // Non-copyable
    EngineServer(const EngineServer&) = delete;
    EngineServer& operator=(const EngineServer&) = delete;

    // Starts the receiver, sender, and ledger consumer threads.
    void start();

    // Signals all threads to stop and joins them.
    void stop();

private:
    // ── Thread Entry Points ───────────────────────────────────────────────

    // Receives ZMQ messages, deserializes, pushes to request_queue.
    void io_loop();

    // ── Serialization Helpers ─────────────────────────────────────────────

    static TransactionRequest deserialize_request(const uint8_t* data, std::size_t size);
    static std::vector<uint8_t> serialize_response(const TransactionResponse& resp);

    static CreateAccountRequest deserialize_create_account(const uint8_t* data, std::size_t size);
    static std::vector<uint8_t> serialize_create_account_response(const CreateAccountResponse& resp);
    static std::vector<uint8_t> serialize_metrics(const MetricsSample& sample);
    static std::vector<uint8_t> serialize_balance_snapshot(const LedgerEngine::BalanceSnapshotData& snap);

    // ── State ─────────────────────────────────────────────────────────────

    LedgerEngine&    engine_;
    std::string      endpoint_;
    std::atomic<bool> running_{false};

    std::thread io_thread_;
    std::thread engine_thread_;
};

}  // namespace zerolgr
