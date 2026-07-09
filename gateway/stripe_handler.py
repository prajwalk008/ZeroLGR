import stripe
from fastapi import Request, HTTPException, APIRouter
from .config import settings
from .saga import SagaOrchestrator

stripe.api_key = settings.stripe_api_key
router = APIRouter()

def get_saga_orchestrator(request: Request) -> SagaOrchestrator:
    return request.app.state.saga

@router.post("/webhooks/stripe")
async def handle_stripe_webhook(request: Request):
    payload = await request.body()
    sig_header = request.headers.get("stripe-signature")

    try:
        event = stripe.Webhook.construct_event(
            payload, sig_header, settings.stripe_webhook_secret
        )
    except ValueError as e:
        raise HTTPException(status_code=400, detail="Invalid payload")
    except stripe.error.SignatureVerificationError as e:
        raise HTTPException(status_code=400, detail="Invalid signature")

    saga = get_saga_orchestrator(request)
    
    event_type = event['type']
    
    if event_type == "charge.succeeded":
        charge = event['data']['object']
        # Dispatch to saga
        await saga.execute_payment_saga(
            amount_minor=charge['amount'],
            currency=charge['currency'],
            stripe_charge_id=charge['id']
        )
    elif event_type == "charge.refunded":
        charge = event['data']['object']
        # Reverse the ledger entry
        try:
            await saga.zmq_client.send_transaction(
                debit_account_id=settings.merchant_revenue_account_id,
                credit_account_id=settings.stripe_escrow_account_id,
                amount_minor=charge['amount'],
                currency=charge['currency'],
                description=f"Refund {charge['id']}",
                idempotency_key=f"reversal-{charge['id']}",
                stripe_charge_id=charge['id']
            )
        except Exception as e:
            print(f"Failed to reverse charge in ledger: {e}")
            # If ZMQ fails here, we might need manual reconciliation, 
            # but Stripe handles the refund side correctly.
    else:
        # Unhandled event types
        pass

    # Always ACK to Stripe
    return {"status": "success"}
