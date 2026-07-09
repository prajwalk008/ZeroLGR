import { create } from 'zustand';

export interface MetricsSample {
  tps: number;
  avg_latency_us: number;
  tx_count: number;
  invariant_ok: boolean;
  timestamp: number;
}

export interface SagaEvent {
  step: string;
  status: 'IN_PROGRESS' | 'SUCCESS' | 'FAILED';
  charge_id: string;
  tx_id?: string;
  latency_us?: number;
  reason?: string;
  timestamp: number;
}

interface MetricsState {
  tps: number;
  avgLatencyUs: number;
  txCount: number;
  invariantOk: boolean;
  latencyHistory: { t: number; value: number }[];
  tpsHistory: { t: number; value: number }[];
  sagaEvents: SagaEvent[];

  onMetricsSample: (sample: MetricsSample) => void;
  onSagaEvent: (event: SagaEvent) => void;
  resetSagaEvents: () => void;
}

const MAX_HISTORY = 60;

export const useMetricsStore = create<MetricsState>((set) => ({
  tps: 0,
  avgLatencyUs: 0,
  txCount: 0,
  invariantOk: true,
  latencyHistory: [],
  tpsHistory: [],
  sagaEvents: [],

  onMetricsSample: (sample) => set((state) => {
    const newLatencyHistory = [...state.latencyHistory, { t: sample.timestamp, value: sample.avg_latency_us }].slice(-MAX_HISTORY);
    const newTpsHistory = [...state.tpsHistory, { t: sample.timestamp, value: sample.tps }].slice(-MAX_HISTORY);
    
    return {
      tps: sample.tps,
      avgLatencyUs: sample.avg_latency_us,
      txCount: sample.tx_count,
      invariantOk: sample.invariant_ok,
      latencyHistory: newLatencyHistory,
      tpsHistory: newTpsHistory,
    };
  }),

  onSagaEvent: (event) => set((state) => {
    // Keep only the last 50 events
    const newEvents = [{ ...event, timestamp: Date.now() }, ...state.sagaEvents].slice(0, 50);
    return { sagaEvents: newEvents };
  }),

  resetSagaEvents: () => set({ sagaEvents: [] })
}));
