# ZeroLGR: High-Performance Distributed Ledger

ZeroLGR is a high-performance, double-entry financial ledger engine designed to showcase ultra-low latency backend system optimization and modern microservice web gateway engineering.

## 🎯 Architecture & Technologies
- **Core Engine (C++20)**: Implements lock-free concurrency, memory-mapped write-ahead logging (mmap/msync), and a strict double-entry state machine to enforce accounting invariants.
- **Microservice Gateway (Python FastAPI)**: Asynchronous ZeroMQ integration, Stripe payment webhook reconciliation, and a Saga pattern orchestrator for distributed transaction rollbacks.
- **Real-Time Dashboard (React/Vite)**: Custom premium dark-mode UI streaming live throughput (TPS) metrics, latency benchmarks, and animated Saga timelines via WebSockets.

---

## 📋 Development Plan & Features

The project is structured into four major phases:

### Phase 1: Core Concurrency (C++20)
- **Lock-Free Ring Buffer**: A Single-Producer Single-Consumer (SPSC) circular queue using C++ `std::memory_order` fences to avoid expensive thread locking.
- **Memory Optimization**: `alignas(64)` alignment to prevent false sharing and keep cache-lines separated.

### Phase 2: Zero-Copy WAL (Write-Ahead Log)
- **Memory-Mapped Persistence**: Implements POSIX `mmap` / Windows `CreateFileMapping` to persist transactional journal frames directly to disk.
- **Crash Recovery**: Enforces transactional durability before in-memory ledger updates using CRC32 checksums for replay integrity.

### Phase 3: Python Web Gateway & Saga Orchestration
- **ZMQ Async Multiplexing**: A `DEALER`/`ROUTER` topology handling non-blocking IPC communication between the Python gateway and C++ engine.
- **Stripe Webhooks & Saga**: Standardizes distributed transactions. If the C++ engine rejects a transaction (e.g., failed invariant), the Saga orchestrator automatically triggers compensating actions (Stripe refunds) using idempotency keys.
- **Live Metrics Streaming**: A background collector that pushes real-time latency and TPS updates to WebSocket clients.

### Phase 4: React Dashboard
- **Live Benchmarks**: Live comparison of C++ memory-mapped ledger throughput vs traditional SQLite backends using Recharts area graphs.
- **Saga Timeline**: An animated, vertical visualization tracking the state machine progression of distributed Stripe payments and potential rollbacks.
- **Ledger Auditor**: A real-time balance sheet proving the fundamental accounting equation ($\text{Assets} - \text{Liabilities} - \text{Equity} = 0$).

---

## 🚀 How to Run

1. **Start the C++ Engine**
   ```bash
   cd engine
   mkdir build && cd build
   cmake .. && make
   ./zerolgr_engine
   ```

2. **Start the Python Gateway**
   ```bash
   cd gateway
   pip install -r requirements.txt
   uvicorn main:app --reload --port 8000
   ```

3. **Start the React Dashboard**
   ```bash
   cd dashboard
   npm install
   npm run dev
   ```
