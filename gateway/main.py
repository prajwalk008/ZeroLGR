from fastapi import FastAPI, WebSocket
from contextlib import asynccontextmanager
from .config import settings
from .zmq_client import ZMQClient
from .ws_streamer import MetricsCollector, broadcaster
from .saga import SagaOrchestrator
from .stripe_handler import router as stripe_router

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup
    zmq_client = ZMQClient(settings.zmq_endpoint)
    await zmq_client.connect()
    
    app.state.zmq = zmq_client
    app.state.saga = SagaOrchestrator(zmq_client)
    
    metrics = MetricsCollector(zmq_client)
    metrics.start()
    
    yield
    
    # Shutdown
    metrics.stop()
    await zmq_client.disconnect()


from .routes.accounts import router as accounts_router
from .routes.transactions import router as transactions_router
from .routes.benchmarks import router as benchmarks_router
from .routes.ledger_view import router as ledger_router

app = FastAPI(title="ZeroLGR Gateway", lifespan=lifespan)

app.include_router(stripe_router)
app.include_router(accounts_router)
app.include_router(transactions_router)
app.include_router(benchmarks_router)
app.include_router(ledger_router)

@app.websocket("/ws/metrics")
async def websocket_endpoint(websocket: WebSocket):
    await broadcaster.connect(websocket)
    try:
        # Keep connection open until client disconnects
        while True:
            await websocket.receive_text()
    except Exception:
        broadcaster.disconnect(websocket)

# A simple root route to verify it's working
@app.get("/")
def read_root():
    return {"status": "ZeroLGR Gateway is running."}
