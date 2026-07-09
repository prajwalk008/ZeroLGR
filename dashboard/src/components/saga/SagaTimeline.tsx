import React, { useState } from 'react';
import { useMetricsStore } from '../../store/metricsStore';
import { GitCommit, CreditCard, ShieldAlert } from 'lucide-react';

export const SagaTimeline = () => {
  const { sagaEvents, resetSagaEvents } = useMetricsStore();
  const [loading, setLoading] = useState(false);

  const triggerTestPayment = async (shouldFail: boolean) => {
    setLoading(true);
    resetSagaEvents();
    try {
      // In a real app we'd redirect to Stripe Checkout or send a real webhook.
      // Here we simulate the webhook arriving at our gateway for demo purposes.
      // We will trigger a fake webhook directly to the gateway's internal orchestrator
      // or we can use our /transactions/transfer endpoint to simulate the internal saga part.
      
      const res = await fetch('http://localhost:8000/transactions/transfer', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          from_id: shouldFail ? "acc_nonexistent" : "acc_stripe_escrow", // Force failure if requested
          to_id: "acc_merchant_revenue",
          amount: 5000,
          description: "Stripe Test Charge",
          idempotency_key: "test-charge-" + Date.now()
        })
      });
      // Gateway handles the ZMQ saga dispatch
    } catch (e) {
      console.error('Test payment failed', e);
    }
    setLoading(false);
  };

  return (
    <div className="panel">
      <div className="panel-header">
        <h2 className="panel-title">
          <GitCommit size={20} className="header-title-highlight" />
          Saga Orchestration Timeline
        </h2>
        <div className="flex-row">
          <button className="btn btn-secondary" onClick={() => triggerTestPayment(true)} disabled={loading}>
            <ShieldAlert size={16} className="text-danger" /> Simulate Failure
          </button>
          <button className="btn btn-primary" onClick={() => triggerTestPayment(false)} disabled={loading}>
            <CreditCard size={16} /> Test Payment
          </button>
        </div>
      </div>

      <div className="timeline" style={{ marginTop: '16px', maxHeight: '400px', overflowY: 'auto' }}>
        {sagaEvents.length === 0 ? (
          <div style={{ padding: '24px', textAlign: 'center', color: 'var(--text-muted)' }}>
            Trigger a payment to watch the Saga pattern coordinate the distributed transaction.
          </div>
        ) : (
          sagaEvents.map((ev, i) => (
            <div 
              key={i} 
              className={`timeline-item ${ev.status === 'SUCCESS' ? 'success' : ev.status === 'FAILED' ? 'error' : 'active'}`}
            >
              <div className="timeline-dot"></div>
              <div className="flex-row space-between">
                <strong>{ev.step.replace(/_/g, ' ')}</strong>
                <span className={`badge ${ev.status === 'SUCCESS' ? 'badge-success' : ev.status === 'FAILED' ? 'badge-danger' : 'badge-pending'}`}>
                  {ev.status}
                </span>
              </div>
              <div className="flex-col" style={{ marginTop: '8px', fontSize: '0.875rem', color: 'var(--text-secondary)' }}>
                {ev.charge_id && <span>Charge ID: <span className="mono">{ev.charge_id}</span></span>}
                {ev.tx_id && <span>Ledger Tx ID: <span className="mono">{ev.tx_id}</span></span>}
                {ev.latency_us && <span>Engine Latency: {ev.latency_us} µs</span>}
                {ev.reason && <span className="text-danger">Reason: {ev.reason}</span>}
              </div>
            </div>
          ))
        )}
      </div>
    </div>
  );
};
