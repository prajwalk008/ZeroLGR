from fastapi import APIRouter, Request, HTTPException
from pydantic import BaseModel

router = APIRouter(prefix="/transactions", tags=["Transactions"])

class TransferModel(BaseModel):
    from_id: str
    to_id: str
    amount: int
    description: str = ""
    idempotency_key: str = ""

@router.post("/transfer")
async def create_transfer(req: TransferModel, request: Request):
    zmq_client = request.app.state.zmq
    try:
        resp = await zmq_client.send_transaction(
            debit_account_id=req.from_id,
            credit_account_id=req.to_id,
            amount_minor=req.amount,
            description=req.description,
            idempotency_key=req.idempotency_key
        )
        if resp["status"] != 0:
            raise HTTPException(status_code=400, detail=resp["message"])
        return resp
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
