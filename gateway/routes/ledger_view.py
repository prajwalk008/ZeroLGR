from fastapi import APIRouter, Request, HTTPException

router = APIRouter(prefix="/ledger", tags=["Ledger"])

@router.get("/balance-sheet")
async def get_balance_sheet(request: Request):
    zmq_client = request.app.state.zmq
    try:
        resp = await zmq_client.get_balance_snapshot()
        return resp
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/wal-tail")
async def get_wal_tail():
    # In a real app we'd read the tail of the WAL file using mmap or seek
    # For now, we'll return a stub to be implemented if needed
    return {"frames": []}
