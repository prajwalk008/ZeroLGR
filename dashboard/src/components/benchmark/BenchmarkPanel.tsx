import React, { useState } from 'react';
import { useMetricsStore } from '../../store/metricsStore';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer, AreaChart, Area } from 'recharts';
import { Play, Activity, Zap } from 'lucide-react';

export const BenchmarkPanel = () => {
  const { tps, avgLatencyUs, tpsHistory, latencyHistory } = useMetricsStore();
  const [loading, setLoading] = useState(false);
  const [mode, setMode] = useState<'optimized' | 'baseline'>('optimized');
  const [txCount, setTxCount] = useState(10000);

  const runBenchmark = async () => {
    setLoading(true);
    try {
      await fetch('http://localhost:8000/benchmark/start', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode, tx_count: txCount })
      });
      // In a real app we'd poll the status endpoint, but for now we just observe the WS metrics spike
    } catch (e) {
      console.error('Benchmark failed', e);
    }
    setTimeout(() => setLoading(false), 2000); // UI visual reset
  };

  return (
    <div className="panel" style={{ gridColumn: '1 / -1' }}>
      <div className="panel-header">
        <h2 className="panel-title">
          <Zap size={20} className="header-title-highlight" />
          Live Load Test Benchmark
        </h2>
        <div className="flex-row">
          <select 
            value={mode} 
            onChange={(e) => setMode(e.target.value as 'optimized' | 'baseline')}
            className="btn btn-secondary"
          >
            <option value="optimized">C++ Core (Optimized)</option>
            <option value="baseline">SQLite/Python (Baseline)</option>
          </select>
          <select 
            value={txCount} 
            onChange={(e) => setTxCount(Number(e.target.value))}
            className="btn btn-secondary"
          >
            <option value={1000}>1K Transactions</option>
            <option value={10000}>10K Transactions</option>
            <option value={100000}>100K Transactions</option>
          </select>
          <button 
            className="btn btn-primary"
            onClick={runBenchmark}
            disabled={loading}
          >
            <Play size={16} /> {loading ? 'Running...' : 'Run Load Test'}
          </button>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px' }}>
        {/* TPS Gauge & Chart */}
        <div className="metric-card">
          <div className="flex-row space-between">
            <span className="metric-label">Throughput (TPS)</span>
            <Activity size={16} className="text-success" />
          </div>
          <span className="metric-value">{tps.toLocaleString(undefined, { maximumFractionDigits: 0 })}</span>
          <div style={{ height: '120px', marginTop: '16px' }}>
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={tpsHistory}>
                <defs>
                  <linearGradient id="colorTps" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="var(--success)" stopOpacity={0.3}/>
                    <stop offset="95%" stopColor="var(--success)" stopOpacity={0}/>
                  </linearGradient>
                </defs>
                <Area type="monotone" dataKey="value" stroke="var(--success)" fillOpacity={1} fill="url(#colorTps)" isAnimationActive={false} />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* Latency Chart */}
        <div className="metric-card">
          <div className="flex-row space-between">
            <span className="metric-label">Avg Latency (µs)</span>
            <Activity size={16} className="text-danger" />
          </div>
          <span className="metric-value">{avgLatencyUs.toLocaleString(undefined, { maximumFractionDigits: 1 })}</span>
          <div style={{ height: '120px', marginTop: '16px' }}>
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={latencyHistory}>
                <defs>
                  <linearGradient id="colorLat" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="var(--danger)" stopOpacity={0.3}/>
                    <stop offset="95%" stopColor="var(--danger)" stopOpacity={0}/>
                  </linearGradient>
                </defs>
                <Area type="monotone" dataKey="value" stroke="var(--danger)" fillOpacity={1} fill="url(#colorLat)" isAnimationActive={false} />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </div>
      </div>
    </div>
  );
};
