import asyncio
import struct
import uuid
import zmq
import zmq.asyncio
from typing import Dict, Any, Optional

# Message Types matching C++ Engine
MSG_TX_REQUEST       = 0x01
MSG_CREATE_ACCOUNT   = 0x02
MSG_METRICS_REQUEST  = 0x03
MSG_BALANCE_REQUEST  = 0x04

MSG_TX_RESPONSE      = 0x81
MSG_CREATE_RESPONSE  = 0x82
MSG_METRICS_RESPONSE = 0x83
MSG_BALANCE_RESPONSE = 0x84

class ZMQTimeoutError(Exception):
    pass

class EngineError(Exception):
    def __init__(self, status: int, message: str):
        self.status = status
        self.message = message
        super().__init__(f"Engine Error (Status {status}): {message}")

class ZMQClient:
    def __init__(self, endpoint: str):
        self.endpoint = endpoint
        self.ctx = zmq.asyncio.Context()
        self.socket = None
        self.pending_requests: Dict[bytes, asyncio.Future] = {}
        self._receiver_task: Optional[asyncio.Task] = None

    async def connect(self):
        self.socket = self.ctx.socket(zmq.DEALER)
        self.socket.setsockopt(zmq.LINGER, 0)
        self.socket.connect(self.endpoint)
        self._receiver_task = asyncio.create_task(self._receiver_loop())

    async def disconnect(self):
        if self._receiver_task:
            self._receiver_task.cancel()
            try:
                await self._receiver_task
            except asyncio.CancelledError:
                pass
        
        if self.socket:
            self.socket.close()
            self.socket = None
        
        self.ctx.term()

    def _write_string(self, s: str) -> bytes:
        encoded = s.encode('utf-8')
        return struct.pack('<I', len(encoded)) + encoded

    def _read_string(self, data: bytes, offset: int) -> tuple[str, int]:
        length = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        s = data[offset:offset+length].decode('utf-8')
        offset += length
        return s, offset

    async def send_transaction(
        self,
        debit_account_id: str,
        credit_account_id: str,
        amount_minor: int,
        currency: str = "USD",
        description: str = "",
        idempotency_key: str = "",
        stripe_charge_id: str = ""
    ) -> dict:
        corr_id = uuid.uuid4().bytes
        
        # Serialize Request
        # 1 byte type, 16 byte corr_id, 8 byte amount (int64)
        msg = struct.pack('<B16sq', MSG_TX_REQUEST, corr_id, amount_minor)
        msg += self._write_string(debit_account_id)
        msg += self._write_string(credit_account_id)
        msg += self._write_string(idempotency_key)
        msg += self._write_string(description)
        msg += self._write_string(currency)
        msg += self._write_string(stripe_charge_id)

        response = await self._send_and_wait(corr_id, msg)
        
        # Parse MSG_TX_RESPONSE
        if response[0] != MSG_TX_RESPONSE:
            raise RuntimeError(f"Unexpected response type: {response[0]}")
            
        offset = 1 + 16 # skip type and corr_id
        tx_id = uuid.UUID(bytes=response[offset:offset+16])
        offset += 16
        
        status = response[offset]
        offset += 1
        
        latency_us = struct.unpack_from('<q', response, offset)[0]
        offset += 8
        
        message, _ = self._read_string(response, offset)
        
        return {
            "transaction_id": str(tx_id),
            "status": status,
            "latency_us": latency_us,
            "message": message
        }

    async def create_account(self, name: str, account_type: int) -> dict:
        corr_id = uuid.uuid4().bytes
        
        msg = struct.pack('<B16s', MSG_CREATE_ACCOUNT, corr_id)
        msg += self._write_string(name)
        msg += struct.pack('<B', account_type)

        response = await self._send_and_wait(corr_id, msg)
        
        if response[0] != MSG_CREATE_RESPONSE:
            raise RuntimeError(f"Unexpected response type: {response[0]}")
            
        offset = 1 + 16
        status = response[offset]
        offset += 1
        
        account_id, offset = self._read_string(response, offset)
        message, _ = self._read_string(response, offset)
        
        return {
            "account_id": account_id,
            "status": status,
            "message": message
        }

    async def get_metrics(self) -> dict:
        corr_id = uuid.uuid4().bytes
        msg = struct.pack('<B16s', MSG_METRICS_REQUEST, corr_id)
        
        response = await self._send_and_wait(corr_id, msg)
        
        if response[0] != MSG_METRICS_RESPONSE:
            raise RuntimeError(f"Unexpected response type: {response[0]}")
            
        offset = 1
        tps, avg_latency, tx_count, inv_ok = struct.unpack_from('<ddQB', response, offset)
        
        return {
            "tps": tps,
            "avg_latency_us": avg_latency,
            "tx_count": tx_count,
            "invariant_ok": bool(inv_ok)
        }

    async def get_balance_snapshot(self) -> dict:
        corr_id = uuid.uuid4().bytes
        msg = struct.pack('<B16s', MSG_BALANCE_REQUEST, corr_id)
        
        response = await self._send_and_wait(corr_id, msg)
        
        if response[0] != MSG_BALANCE_RESPONSE:
            raise RuntimeError(f"Unexpected response type: {response[0]}")
            
        offset = 1
        inv_ok = bool(response[offset])
        offset += 1
        
        total_assets, total_liabilities, total_equity, count = struct.unpack_from('<qqqI', response, offset)
        offset += 8 * 3 + 4
        
        accounts = []
        for _ in range(count):
            acc_id, offset = self._read_string(response, offset)
            name, offset = self._read_string(response, offset)
            balance = struct.unpack_from('<q', response, offset)[0]
            offset += 8
            acc_type = response[offset]
            offset += 1
            
            accounts.append({
                "account_id": acc_id,
                "name": name,
                "balance": balance,
                "type": acc_type
            })
            
        return {
            "invariant_ok": inv_ok,
            "total_assets": total_assets,
            "total_liabilities": total_liabilities,
            "total_equity": total_equity,
            "accounts": accounts
        }

    async def _send_and_wait(self, corr_id: bytes, msg: bytes) -> bytes:
        future = asyncio.Future()
        self.pending_requests[corr_id] = future
        
        await self.socket.send(msg)
        
        try:
            return await asyncio.wait_for(future, timeout=5.0)
        except asyncio.TimeoutError:
            self.pending_requests.pop(corr_id, None)
            raise ZMQTimeoutError("Timed out waiting for C++ engine response")

    async def _receiver_loop(self):
        try:
            while True:
                msg = await self.socket.recv()
                
                # Minimum size is 1 byte type + 16 bytes corr_id
                if len(msg) < 17:
                    continue
                    
                msg_type = msg[0]
                corr_id = msg[1:17]
                
                future = self.pending_requests.pop(corr_id, None)
                if future and not future.done():
                    future.set_result(msg)
                    
        except asyncio.CancelledError:
            pass
