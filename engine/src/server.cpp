// engine/src/server.cpp
// ZeroMQ Engine Server implementation.
//
// Architecture:
//   io_loop() thread:
//     - Binds a ROUTER socket
//     - Polls for incoming messages with a short timeout
//     - On receive: deserializes request, pushes to engine request_queue
//     - Between polls: drains engine reply_queue, serializes and sends responses
//   engine_thread_:
//     - Runs LedgerEngine::run() — the single-threaded consumer loop
//
// This design avoids the ZMQ thread-safety issue (sockets cannot be shared
// across threads) by doing all socket I/O on a single thread.

#include "server.hpp"

#include <zmq.hpp>
#include <cstring>
#include <iostream>
#include <chrono>

namespace zerolgr {

// ── Simple Binary Serialization Helpers ───────────────────────────────────
//
// These serialize/deserialize our C++ structs to/from a compact binary
// format for ZMQ transport. In a production system, we would use the
// FlatBuffers schema and generated code. For initial implementation,
// this straightforward binary protocol avoids the FlatBuffers build
// dependency and lets us iterate quickly.
//
// Wire format for TransactionRequest:
//   [1 byte: message_type]  0x01 = TransactionRequest
//                           0x02 = CreateAccountRequest
//                           0x03 = MetricsRequest
//                           0x04 = BalanceSnapshotRequest
//   [16 bytes: correlation_id]
//   [... type-specific payload]

static constexpr uint8_t MSG_TX_REQUEST       = 0x01;
static constexpr uint8_t MSG_CREATE_ACCOUNT   = 0x02;
static constexpr uint8_t MSG_METRICS_REQUEST  = 0x03;
static constexpr uint8_t MSG_BALANCE_REQUEST  = 0x04;

static constexpr uint8_t MSG_TX_RESPONSE      = 0x81;
static constexpr uint8_t MSG_CREATE_RESPONSE  = 0x82;
static constexpr uint8_t MSG_METRICS_RESPONSE = 0x83;
static constexpr uint8_t MSG_BALANCE_RESPONSE = 0x84;

// Helper: read a length-prefixed string from a buffer at offset
static std::string read_string(const uint8_t* data, std::size_t& offset) {
    uint32_t len;
    std::memcpy(&len, data + offset, 4);
    offset += 4;
    std::string s(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return s;
}

// Helper: write a length-prefixed string to a buffer
static void write_string(std::vector<uint8_t>& buf, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    const auto* lp = reinterpret_cast<const uint8_t*>(&len);
    buf.insert(buf.end(), lp, lp + 4);
    buf.insert(buf.end(), s.begin(), s.end());
}

// Helper: append raw bytes
static void write_bytes(std::vector<uint8_t>& buf, const void* data, std::size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    buf.insert(buf.end(), p, p + size);
}

// ── Deserialize TransactionRequest ────────────────────────────────────────

TransactionRequest EngineServer::deserialize_request(const uint8_t* data, std::size_t size) {
    TransactionRequest req;
    std::size_t offset = 1;  // Skip message type byte

    // Correlation ID (16 bytes)
    std::memcpy(req.correlation_id.data(), data + offset, UUID_SIZE);
    offset += UUID_SIZE;

    // Amount
    std::memcpy(&req.amount_minor, data + offset, sizeof(req.amount_minor));
    offset += sizeof(req.amount_minor);

    // Strings
    req.debit_account_id  = read_string(data, offset);
    req.credit_account_id = read_string(data, offset);
    req.idempotency_key   = read_string(data, offset);
    req.description       = read_string(data, offset);
    req.currency          = read_string(data, offset);
    req.stripe_charge_id  = read_string(data, offset);

    return req;
}

// ── Serialize TransactionResponse ─────────────────────────────────────────

std::vector<uint8_t> EngineServer::serialize_response(const TransactionResponse& resp) {
    std::vector<uint8_t> buf;
    buf.reserve(128);

    buf.push_back(MSG_TX_RESPONSE);
    write_bytes(buf, resp.correlation_id.data(), UUID_SIZE);
    write_bytes(buf, resp.transaction_id.data(), UUID_SIZE);

    uint8_t status = static_cast<uint8_t>(resp.status);
    buf.push_back(status);

    write_bytes(buf, &resp.latency_us, sizeof(resp.latency_us));
    write_string(buf, resp.message);

    return buf;
}

// ── Deserialize CreateAccountRequest ──────────────────────────────────────

CreateAccountRequest EngineServer::deserialize_create_account(const uint8_t* data, std::size_t size) {
    CreateAccountRequest req;
    std::size_t offset = 1;  // Skip message type byte

    std::memcpy(req.correlation_id.data(), data + offset, UUID_SIZE);
    offset += UUID_SIZE;

    req.name = read_string(data, offset);

    uint8_t type_byte = data[offset++];
    req.type = static_cast<AccountType>(type_byte);

    return req;
}

// ── Serialize CreateAccountResponse ───────────────────────────────────────

std::vector<uint8_t> EngineServer::serialize_create_account_response(const CreateAccountResponse& resp) {
    std::vector<uint8_t> buf;
    buf.reserve(64);

    buf.push_back(MSG_CREATE_RESPONSE);
    write_bytes(buf, resp.correlation_id.data(), UUID_SIZE);

    uint8_t status = static_cast<uint8_t>(resp.status);
    buf.push_back(status);

    write_string(buf, resp.account_id);
    write_string(buf, resp.message);

    return buf;
}

// ── Serialize MetricsSample ───────────────────────────────────────────────

std::vector<uint8_t> EngineServer::serialize_metrics(const UUID& corr_id, const MetricsSample& sample) {
    std::vector<uint8_t> buf;
    buf.reserve(48);

    buf.push_back(MSG_METRICS_RESPONSE);
    write_bytes(buf, corr_id.data(), UUID_SIZE);
    write_bytes(buf, &sample.tps, sizeof(sample.tps));
    write_bytes(buf, &sample.avg_latency_us, sizeof(sample.avg_latency_us));
    write_bytes(buf, &sample.tx_count, sizeof(sample.tx_count));

    uint8_t inv = sample.invariant_ok ? 1 : 0;
    buf.push_back(inv);

    return buf;
}

// ── Serialize BalanceSnapshot ─────────────────────────────────────────────

std::vector<uint8_t> EngineServer::serialize_balance_snapshot(
    const UUID& corr_id,
    const LedgerEngine::BalanceSnapshotData& snap)
{
    std::vector<uint8_t> buf;
    buf.reserve(512);

    buf.push_back(MSG_BALANCE_RESPONSE);
    write_bytes(buf, corr_id.data(), UUID_SIZE);

    uint8_t inv = snap.invariant_ok ? 1 : 0;
    buf.push_back(inv);

    write_bytes(buf, &snap.total_assets, sizeof(snap.total_assets));
    write_bytes(buf, &snap.total_liabilities, sizeof(snap.total_liabilities));
    write_bytes(buf, &snap.total_equity, sizeof(snap.total_equity));

    uint32_t count = static_cast<uint32_t>(snap.accounts.size());
    write_bytes(buf, &count, sizeof(count));

    for (const auto& acc : snap.accounts) {
        write_string(buf, acc.id);
        write_string(buf, acc.name);
        write_bytes(buf, &acc.balance, sizeof(acc.balance));
        uint8_t type = static_cast<uint8_t>(acc.type);
        buf.push_back(type);
    }

    return buf;
}

// ── Constructor / Destructor ──────────────────────────────────────────────

EngineServer::EngineServer(LedgerEngine& engine, const std::string& endpoint)
    : engine_(engine), endpoint_(endpoint)
{
}

EngineServer::~EngineServer() {
    stop();
}

// ── Start / Stop ──────────────────────────────────────────────────────────

void EngineServer::start() {
    if (running_.load()) return;
    running_.store(true);

    // Start the ledger consumer thread
    engine_thread_ = std::thread([this]() {
        engine_.run();
    });

    // Start the I/O thread (handles both send and receive on the ROUTER socket)
    io_thread_ = std::thread([this]() {
        io_loop();
    });

    std::cout << "[ZeroLGR] Engine server started on " << endpoint_ << std::endl;
}

void EngineServer::stop() {
    if (!running_.load()) return;
    running_.store(false);

    engine_.shutdown();

    if (engine_thread_.joinable()) engine_thread_.join();
    if (io_thread_.joinable())     io_thread_.join();

    std::cout << "[ZeroLGR] Engine server stopped." << std::endl;
}

// ── I/O Loop ──────────────────────────────────────────────────────────────
//
// Single-threaded event loop that:
//   1. Polls the ROUTER socket for incoming messages (with 10ms timeout)
//   2. On message: routes to appropriate handler
//   3. After each poll: drains the reply_queue and sends responses

void EngineServer::io_loop() {
    zmq::context_t ctx(1);
    zmq::socket_t router(ctx, zmq::socket_type::router);

    // Set socket options
    int linger = 0;
    router.set(zmq::sockopt::linger, linger);

    router.bind(endpoint_);

    zmq::pollitem_t poll_items[] = {
        { static_cast<void*>(router), 0, ZMQ_POLLIN, 0 }
    };

    while (running_.load(std::memory_order_relaxed)) {
        // ── Poll for incoming messages ────────────────────────────────
        zmq::poll(poll_items, 1, std::chrono::milliseconds(10));

        if (poll_items[0].revents & ZMQ_POLLIN) {
            // ROUTER receives: [identity][empty_delimiter][payload]
            zmq::message_t identity_msg;
            zmq::message_t delimiter_msg;
            zmq::message_t payload_msg;

            auto id_result = router.recv(identity_msg, zmq::recv_flags::none);
            if (!id_result) continue;

            auto delim_result = router.recv(delimiter_msg, zmq::recv_flags::none);
            if (!delim_result) continue;

            auto payload_result = router.recv(payload_msg, zmq::recv_flags::none);
            if (!payload_result) continue;

            const auto* data = static_cast<const uint8_t*>(payload_msg.data());
            const std::size_t size = payload_msg.size();

            if (size < 1) continue;

            const uint8_t msg_type = data[0];
            std::string identity(static_cast<const char*>(identity_msg.data()),
                                 identity_msg.size());

            switch (msg_type) {
                case MSG_TX_REQUEST: {
                    auto req = deserialize_request(data, size);
                    // Store the ZMQ identity so the reply can be routed back.
                    // We need to pass it through the ring buffer somehow.
                    // For now, we'll store it in a side map keyed by correlation_id.
                    // A cleaner approach: the reply_queue items carry the identity.

                    // Push request to the engine
                    if (!engine_.request_queue.push(std::move(req))) {
                        // Queue is full — send an immediate error response
                        TransactionResponse err_resp;
                        std::memcpy(err_resp.correlation_id.data(),
                                    data + 1, UUID_SIZE);
                        err_resp.status = TxStatus::ERR_INTERNAL;
                        err_resp.message = "Engine queue full";

                        auto resp_bytes = serialize_response(err_resp);
                        router.send(identity_msg, zmq::send_flags::sndmore);
                        router.send(zmq::message_t(), zmq::send_flags::sndmore);
                        router.send(zmq::buffer(resp_bytes), zmq::send_flags::none);
                    } else {
                        // Store identity for routing the reply later
                        // We peek into the request to get the correlation_id
                        // and store it with the ZMQ identity
                        // This is done by setting zmq_identity on the envelope
                        // when we drain replies below. We need a separate map.
                        // For simplicity: store in a local map here.
                        // The correlation_id was just pushed — extract it from the raw message.
                        UUID corr_id;
                        std::memcpy(corr_id.data(), data + 1, UUID_SIZE);

                        // Store identity in a thread-local map keyed by correlation_id
                        // Since io_loop is single-threaded, this is safe.
                        static thread_local std::unordered_map<
                            std::string, std::string> pending_identities;

                        std::string corr_key(reinterpret_cast<const char*>(corr_id.data()),
                                             UUID_SIZE);
                        pending_identities[corr_key] = identity;
                    }
                    break;
                }

                case MSG_CREATE_ACCOUNT: {
                    auto req = deserialize_create_account(data, size);
                    auto resp = engine_.create_account(req);
                    auto resp_bytes = serialize_create_account_response(resp);

                    router.send(identity_msg, zmq::send_flags::sndmore);
                    router.send(zmq::message_t(), zmq::send_flags::sndmore);
                    router.send(zmq::buffer(resp_bytes), zmq::send_flags::none);
                    break;
                }

                case MSG_METRICS_REQUEST: {
                    UUID corr_id;
                    std::memcpy(corr_id.data(), data + 1, UUID_SIZE);
                    auto sample = engine_.get_metrics_sample();
                    auto resp_bytes = serialize_metrics(corr_id, sample);

                    router.send(identity_msg, zmq::send_flags::sndmore);
                    router.send(zmq::message_t(), zmq::send_flags::sndmore);
                    router.send(zmq::buffer(resp_bytes), zmq::send_flags::none);

                    engine_.reset_metrics_window();
                    break;
                }

                case MSG_BALANCE_REQUEST: {
                    UUID corr_id;
                    std::memcpy(corr_id.data(), data + 1, UUID_SIZE);
                    auto snap = engine_.get_balance_snapshot();
                    auto resp_bytes = serialize_balance_snapshot(corr_id, snap);

                    router.send(identity_msg, zmq::send_flags::sndmore);
                    router.send(zmq::message_t(), zmq::send_flags::sndmore);
                    router.send(zmq::buffer(resp_bytes), zmq::send_flags::none);
                    break;
                }

                default:
                    std::cerr << "[ZeroLGR] Unknown message type: "
                              << static_cast<int>(msg_type) << std::endl;
                    break;
            }
        }

        // ── Drain reply queue and send responses ──────────────────────
        TxResponseEnvelope envelope;
        static thread_local std::unordered_map<
            std::string, std::string> pending_identities;

        while (engine_.reply_queue.pop(envelope)) {
            std::string corr_key(
                reinterpret_cast<const char*>(envelope.response.correlation_id.data()),
                UUID_SIZE);

            auto it = pending_identities.find(corr_key);
            if (it != pending_identities.end()) {
                auto resp_bytes = serialize_response(envelope.response);

                zmq::message_t id_msg(it->second.data(), it->second.size());
                router.send(id_msg, zmq::send_flags::sndmore);
                router.send(zmq::message_t(), zmq::send_flags::sndmore);
                router.send(zmq::buffer(resp_bytes), zmq::send_flags::none);

                pending_identities.erase(it);
            }
        }
    }
}

}  // namespace zerolgr
