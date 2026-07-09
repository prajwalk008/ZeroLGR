from fastapi import APIRouter, Request, HTTPException
from pydantic import BaseModel

router = APIRouter(prefix="/accounts", tags=["Accounts"])

class CreateAccountModel(BaseModel):
    name: str
    type: int

@router.post("")
async def create_account(req: CreateAccountModel, request: Request):
    zmq_client = request.app.state.zmq
    try:
        resp = await zmq_client.create_account(req.name, req.type)
        if resp["status"] != 0:
            raise HTTPException(status_code=400, detail=resp["message"])
        return resp
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("")
async def list_accounts(request: Request):
    zmq_client = request.app.state.zmq
    try:
        resp = await zmq_client.get_balance_snapshot()
        return resp["accounts"]
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
