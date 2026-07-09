import React, { useEffect } from 'react';
import { useLedgerStore } from '../../store/ledgerStore';
import { Database, ShieldCheck, ShieldX } from 'lucide-react';

export const LedgerAuditor = () => {
  const { accounts, totalAssets, totalLiabilities, totalEquity, invariantOk, fetchBalanceSheet } = useLedgerStore();

  useEffect(() => {
    fetchBalanceSheet();
    const interval = setInterval(fetchBalanceSheet, 2000); // Poll balance sheet
    return () => clearInterval(interval);
  }, [fetchBalanceSheet]);

  const getAccountTypeName = (type: number) => {
    const types = ['Asset', 'Liability', 'Equity', 'Revenue', 'Expense'];
    return types[type] || 'Unknown';
  };

  return (
    <div className="panel">
      <div className="panel-header">
        <h2 className="panel-title">
          <Database size={20} className="header-title-highlight" />
          Double-Entry Ledger Auditor
        </h2>
        {invariantOk ? (
          <span className="badge badge-success"><ShieldCheck size={14} /> Invariant OK</span>
        ) : (
          <span className="badge badge-danger"><ShieldX size={14} /> Invariant Broken</span>
        )}
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: '16px', marginBottom: '24px' }}>
        <div className="metric-card">
          <span className="metric-label">Total Assets</span>
          <span className="metric-value">${(totalAssets / 100).toFixed(2)}</span>
        </div>
        <div className="metric-card">
          <span className="metric-label">Total Liabilities</span>
          <span className="metric-value">${(totalLiabilities / 100).toFixed(2)}</span>
        </div>
        <div className="metric-card">
          <span className="metric-label">Total Equity</span>
          <span className="metric-value">${(totalEquity / 100).toFixed(2)}</span>
        </div>
      </div>

      <div style={{ overflowX: 'auto' }}>
        <table className="data-table">
          <thead>
            <tr>
              <th>Account ID</th>
              <th>Name</th>
              <th>Type</th>
              <th className="text-right">Balance (USD)</th>
            </tr>
          </thead>
          <tbody>
            {accounts.length === 0 ? (
              <tr>
                <td colSpan={4} style={{ textAlign: 'center', color: 'var(--text-muted)' }}>
                  No accounts found. Start the C++ engine to seed the ledger.
                </td>
              </tr>
            ) : (
              accounts.map((acc) => (
                <tr key={acc.account_id}>
                  <td className="mono" style={{ fontSize: '0.75rem' }}>{acc.account_id}</td>
                  <td className="highlight">{acc.name}</td>
                  <td>
                    <span className="badge badge-pending">{getAccountTypeName(acc.type)}</span>
                  </td>
                  <td className={`text-right mono ${acc.balance < 0 ? 'text-danger' : 'text-success'}`}>
                    ${(acc.balance / 100).toFixed(2)}
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
};
