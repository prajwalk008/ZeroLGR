import asyncio
import time
from fastapi import WebSocket
from typing import Set, Dict, Any

class WSBroadcaster:
    def __init__(self):
        self.clients: Set[WebSocket] = set()

    async def connect(self, ws: WebSocket):
        await ws.accept()
        self.clients.add(ws)

    def disconnect(self, ws: WebSocket):
        self.clients.discard(ws)

    async def broadcast(self, message: Dict[str, Any]):
        disconnected = set()
        for ws in self.clients:
            try:
                await ws.send_json(message)
            except Exception:
                disconnected.add(ws)
        
        for ws in disconnected:
            self.disconnect(ws)

broadcaster = WSBroadcaster()

class MetricsCollector:
    def __init__(self, zmq_client):
        self.zmq_client = zmq_client
        self._task = None

    def start(self):
        self._task = asyncio.create_task(self.poll_loop())

    def stop(self):
        if self._task:
            self._task.cancel()

    async def poll_loop(self):
        try:
            while True:
                await asyncio.sleep(0.25)
                try:
                    sample = await self.zmq_client.get_metrics()
                    await broadcaster.broadcast({
                        "type": "METRICS",
                        "tps": sample["tps"],
                        "avg_latency_us": sample["avg_latency_us"],
                        "tx_count": sample["tx_count"],
                        "invariant_ok": sample["invariant_ok"],
                        "timestamp": time.time()
                    })
                except Exception as e:
                    print(f"[MetricsCollector] Error fetching metrics: {e}")
        except asyncio.CancelledError:
            pass
