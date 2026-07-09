import stripe
from dataclasses import dataclass
from .ws_streamer import broadcaster
from .zmq_client import ZMQClient, ZMQTimeoutError
from .config import settings

stripe.api_key = settings.stripe_api_key

@dataclass
class SagaResult:
    status: str

class SagaOrchestrator:
    def __init__(self, zmq_client: ZMQClient):
        self.zmq_client = zmq_client

    async def execute_payment_saga(
        self,
        amount_minor: int,
        currency: str,
        stripe_charge_id: str
    ) -> SagaResult:
        # Step 1: Broadcast Saga step
        await broadcaster.broadcast({
            "type": "SAGA_EVENT",
            "step": "DISPATCH_ZMQ",
            "status": "IN_PROGRESS",
            "charge_id": stripe_charge_id
        })
        
        try:
            # Send to C++ Engine
            # Debit: Stripe Escrow Account
            # Credit: Merchant Revenue Account
            resp = await self.zmq_client.send_transaction(
                debit_account_id=settings.stripe_escrow_account_id,
                credit_account_id=settings.merchant_revenue_account_id,
                amount_minor=amount_minor,
                currency=currency,
                description=f"Stripe Charge {stripe_charge_id}",
                idempotency_key=f"charge-{stripe_charge_id}",
                stripe_charge_id=stripe_charge_id
            )
            
            if resp["status"] == 0:  # TxStatus::OK
                await broadcaster.broadcast({
                    "type": "SAGA_EVENT",
                    "step": "COMMIT",
                    "status": "SUCCESS",
                    "charge_id": stripe_charge_id,
                    "tx_id": resp["transaction_id"],
                    "latency_us": resp["latency_us"]
                })
                return SagaResult(status="SUCCESS")
            else:
                # Validation Failed
                await broadcaster.broadcast({
                    "type": "SAGA_EVENT",
                    "step": "VALIDATION_FAILED",
                    "status": "FAILED",
                    "charge_id": stripe_charge_id,
                    "reason": resp["message"]
                })
                await self.compensate_stripe_charge(stripe_charge_id, amount_minor)
                return SagaResult(status="COMPENSATED")
                
        except ZMQTimeoutError:
            await broadcaster.broadcast({
                "type": "SAGA_EVENT",
                "step": "ENGINE_TIMEOUT",
                "status": "FAILED",
                "charge_id": stripe_charge_id
            })
            await self.compensate_stripe_charge(stripe_charge_id, amount_minor)
            return SagaResult(status="TIMEOUT_COMPENSATED")

    async def compensate_stripe_charge(self, charge_id: str, amount: int):
        try:
            # Sync Stripe API call wrapped in an async-friendly way or we can just use the stripe library's synchronous methods inside a threadpool.
            # stripe 8.x supports async out of the box using `stripe.Refund.create_async` if properly configured, but let's use the sync one.
            # Since stripe library has async methods now (from 7.x onwards), we use them.
            if hasattr(stripe.Refund, 'create_async'):
                await stripe.Refund.create_async(
                    charge=charge_id,
                    amount=amount,
                    idempotency_key=f"refund-{charge_id}"
                )
            else:
                stripe.Refund.create(
                    charge=charge_id,
                    amount=amount,
                    idempotency_key=f"refund-{charge_id}"
                )
                
            await broadcaster.broadcast({
                "type": "SAGA_EVENT",
                "step": "STRIPE_REFUND",
                "status": "SUCCESS",
                "charge_id": charge_id
            })
        except Exception as e:
            await broadcaster.broadcast({
                "type": "SAGA_EVENT",
                "step": "STRIPE_REFUND",
                "status": "FAILED",
                "charge_id": charge_id,
                "reason": str(e)
            })
