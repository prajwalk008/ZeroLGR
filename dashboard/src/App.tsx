
import { useWebSocket } from './hooks/useWebSocket';
import { BenchmarkPanel } from './components/benchmark/BenchmarkPanel';
import { SagaTimeline } from './components/saga/SagaTimeline';
import { LedgerAuditor } from './components/ledger/LedgerAuditor';
import { AccountManager } from './components/accounts/AccountManager';
import { Activity } from 'lucide-react';

function App() {
  // Initialize WebSocket connection
  useWebSocket();

  return (
    <div className="app-container">
      <header className="header">
        <h1 className="header-title">
          <Activity className="header-title-highlight" />
          ZeroLGR Dashboard
        </h1>
      </header>

      <main className="main-content">
        <BenchmarkPanel />
        <SagaTimeline />
        <AccountManager />
        <LedgerAuditor />
      </main>
    </div>
  );
}

export default App;
