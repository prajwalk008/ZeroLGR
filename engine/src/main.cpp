// engine/src/main.cpp
// ZeroLGR Engine Daemon entry point.
//
// Usage: zerolgr_engine [--endpoint <zmq_endpoint>] [--wal-path <path>]
//
// Defaults:
//   endpoint: tcp://*:5555
//   wal-path: ./zerolgr.wal

#include "ledger.hpp"
#include "server.hpp"
#include "wal.hpp"

#include <csignal>
#include <iostream>
#include <string>
#include <atomic>

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int /*sig*/) {
    g_shutdown.store(true);
}

int main(int argc, char* argv[]) {
    // ── Parse CLI arguments ───────────────────────────────────────────
    std::string endpoint = "tcp://*:5555";
    std::string wal_path = "./zerolgr.wal";

    for (int i = 1; i < argc - 1; ++i) {
        std::string arg(argv[i]);
        if (arg == "--endpoint") {
            endpoint = argv[++i];
        } else if (arg == "--wal-path") {
            wal_path = argv[++i];
        }
    }

    // ── Register signal handlers for clean shutdown ───────────────────
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║           ZeroLGR Engine v0.1.0          ║" << std::endl;
    std::cout << "╠══════════════════════════════════════════╣" << std::endl;
    std::cout << "║  Endpoint : " << endpoint << std::endl;
    std::cout << "║  WAL Path : " << wal_path << std::endl;
    std::cout << "╚══════════════════════════════════════════╝" << std::endl;

    try {
        // ── Initialize WAL ────────────────────────────────────────────
        zerolgr::WriteAheadLog wal(wal_path);

        // ── Initialize Ledger Engine ──────────────────────────────────
        zerolgr::LedgerEngine engine(wal);

        // ── Replay WAL to restore state ───────────────────────────────
        std::cout << "[ZeroLGR] Replaying WAL..." << std::endl;
        uint64_t replayed = wal.replay([](uint64_t seq, std::span<const uint8_t> data) {
            // In a full implementation, we would deserialize the WAL frame
            // and replay the transaction to restore balances_.
            // For now, we just count frames.
            (void)data;
        });
        std::cout << "[ZeroLGR] Replayed " << replayed << " WAL frames." << std::endl;

        // ── Start Server ──────────────────────────────────────────────
        zerolgr::EngineServer server(engine, endpoint);
        server.start();

        // ── Wait for shutdown signal ──────────────────────────────────
        std::cout << "[ZeroLGR] Engine running. Press Ctrl+C to stop." << std::endl;
        while (!g_shutdown.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n[ZeroLGR] Shutting down..." << std::endl;
        server.stop();

    } catch (const std::exception& e) {
        std::cerr << "[ZeroLGR] Fatal error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[ZeroLGR] Goodbye." << std::endl;
    return 0;
}
