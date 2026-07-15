import asyncio
import uuid
import time
from fastapi import APIRouter, Request, HTTPException
from pydantic import BaseModel

router = APIRouter(prefix="/benchmark", tags=["Benchmarks"])

class BenchmarkStartModel(BaseModel):
    mode: str
    tx_count: int

# In-memory storage for benchmark runs
benchmarks = {}

async def run_optimized_benchmark(benchmark_id: str, tx_count: int, zmq_client):
    benchmarks[benchmark_id]["status"] = "running"
    
    start_time = time.time()
    
    # We fire them concurrently in batches to overwhelm the engine and test TPS
    batch_size = 1000
    
    # We'll just fire requests at the engine. It's meant to simulate a load test.
    # To keep things simple, we'll do asyncio.gather
    
    latencies = []
    
    async def worker(batch):
        local_latencies = []
        for _ in range(batch):
            resp = await zmq_client.send_transaction(
                debit_account_id="acc_benchmark_source",
                credit_account_id="acc_benchmark_dest",
                amount_minor=1,
                description="Benchmark TX"
            )
            local_latencies.append(resp.get("latency_us", 0))
        return local_latencies

    batches = [batch_size] * (tx_count // batch_size)
    rem = tx_count % batch_size
    if rem:
        batches.append(rem)

    tasks = [asyncio.create_task(worker(b)) for b in batches]
    results = await asyncio.gather(*tasks, return_exceptions=True)
    
    for r in results:
        if isinstance(r, list):
            latencies.extend(r)
            
    end_time = time.time()
    duration = end_time - start_time
    
    tps = tx_count / duration if duration > 0 else 0
    avg_latency = sum(latencies) / len(latencies) if latencies else 0
    
    benchmarks[benchmark_id].update({
        "status": "completed",
        "tps": tps,
        "avg_latency_us": avg_latency,
        "p99_latency_us": sorted(latencies)[int(len(latencies)*0.99)] if latencies else 0
    })

@router.post("/start")
async def start_benchmark(req: BenchmarkStartModel, request: Request):
    if req.mode not in ["optimized", "baseline"]:
        raise HTTPException(status_code=400, detail="Invalid mode")
        
    benchmark_id = str(uuid.uuid4())
    benchmarks[benchmark_id] = {"status": "starting"}
    
    if req.mode == "optimized":
        # Launch background task
        asyncio.create_task(run_optimized_benchmark(benchmark_id, req.tx_count, request.app.state.zmq))
    else:
        # Simulate baseline (Python/SQLite)
        async def run_baseline():
            benchmarks[benchmark_id]["status"] = "running"
            
            import sqlite3
            import os
            
            # Use a fresh file-based sqlite db for realism (in-memory is too fast to be a fair disk-based comparison, 
            # but memory mapping in C++ goes to disk. Let's use a temp file to simulate a traditional DB).
            db_path = f"baseline_{benchmark_id}.db"
            conn = sqlite3.connect(db_path, isolation_level=None) # Auto-commit mode or we can manage transactions
            cursor = conn.cursor()
            
            # Setup schema
            cursor.execute("CREATE TABLE accounts (id TEXT PRIMARY KEY, balance INTEGER)")
            cursor.execute("CREATE TABLE ledger (tx_id TEXT PRIMARY KEY, from_id TEXT, to_id TEXT, amount INTEGER)")
            cursor.execute("INSERT INTO accounts (id, balance) VALUES ('acc_benchmark_source', 1000000000)")
            cursor.execute("INSERT INTO accounts (id, balance) VALUES ('acc_benchmark_dest', 0)")
            
            conn.execute("PRAGMA synchronous = FULL") # To match WAL durability guarantees
            
            start_time = time.time()
            latencies = []
            
            # Sequential processing (standard Python web app loop bottleneck)
            for i in range(req.tx_count):
                tx_start = time.time()
                
                try:
                    conn.execute("BEGIN EXCLUSIVE TRANSACTION")
                    
                    # Read balances
                    cursor.execute("SELECT balance FROM accounts WHERE id = 'acc_benchmark_source'")
                    src_bal = cursor.fetchone()[0]
                    cursor.execute("SELECT balance FROM accounts WHERE id = 'acc_benchmark_dest'")
                    dst_bal = cursor.fetchone()[0]
                    
                    if src_bal >= 1:
                        # Write ledger entry
                        tx_id = str(uuid.uuid4())
                        cursor.execute("INSERT INTO ledger (tx_id, from_id, to_id, amount) VALUES (?, ?, ?, ?)", 
                                       (tx_id, 'acc_benchmark_source', 'acc_benchmark_dest', 1))
                        # Update accounts
                        cursor.execute("UPDATE accounts SET balance = balance - 1 WHERE id = 'acc_benchmark_source'")
                        cursor.execute("UPDATE accounts SET balance = balance + 1 WHERE id = 'acc_benchmark_dest'")
                    
                    conn.execute("COMMIT")
                except Exception as e:
                    conn.execute("ROLLBACK")
                
                tx_end = time.time()
                latencies.append((tx_end - tx_start) * 1_000_000) # µs
                
                # Yield to event loop occasionally so we don't completely block the gateway
                if i % 100 == 0:
                    await asyncio.sleep(0)
            
            end_time = time.time()
            conn.close()
            
            # Cleanup temp DB
            if os.path.exists(db_path):
                os.remove(db_path)
            
            duration = end_time - start_time
            tps = req.tx_count / duration if duration > 0 else 0
            avg_latency = sum(latencies) / len(latencies) if latencies else 0
            
            benchmarks[benchmark_id].update({
                "status": "completed",
                "tps": tps,
                "avg_latency_us": avg_latency,
                "p99_latency_us": sorted(latencies)[int(len(latencies)*0.99)] if latencies else 0
            })
            
        asyncio.create_task(run_baseline())

    return {"benchmark_id": benchmark_id}

@router.get("/{id}/status")
async def get_benchmark_status(id: str):
    if id not in benchmarks:
        raise HTTPException(status_code=404, detail="Benchmark not found")
    return benchmarks[id]
