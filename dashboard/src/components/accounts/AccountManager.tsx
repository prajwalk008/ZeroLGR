import React, { useState } from 'react';
import { useLedgerStore } from '../../store/ledgerStore';
import { Users, Plus, Send } from 'lucide-react';

export const AccountManager = () => {
  const { accounts, fetchBalanceSheet } = useLedgerStore();
  
  // Create Account State
  const [newAccName, setNewAccName] = useState('');
  const [newAccType, setNewAccType] = useState('0'); // 0=Asset, 1=Liability, etc.
  const [isCreating, setIsCreating] = useState(false);

  // Transfer State
  const [transferFrom, setTransferFrom] = useState('');
  const [transferTo, setTransferTo] = useState('');
  const [amount, setAmount] = useState('');
  const [description, setDescription] = useState('');
  const [isTransferring, setIsTransferring] = useState(false);
  const [transferMsg, setTransferMsg] = useState<{text: string, isError: boolean} | null>(null);

  const handleCreateAccount = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!newAccName) return;
    setIsCreating(true);
    try {
      const res = await fetch('http://localhost:8000/accounts', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: newAccName, type: parseInt(newAccType) })
      });
      if (res.ok) {
        setNewAccName('');
        await fetchBalanceSheet();
      } else {
        const err = await res.json();
        alert('Failed to create account: ' + (err.detail || 'Unknown error'));
      }
    } catch (err) {
      console.error(err);
    }
    setIsCreating(false);
  };

  const handleTransfer = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!transferFrom || !transferTo || !amount) return;
    
    setIsTransferring(true);
    setTransferMsg(null);
    try {
      const res = await fetch('http://localhost:8000/transactions/transfer', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          from_id: transferFrom,
          to_id: transferTo,
          amount: Math.round(parseFloat(amount) * 100), // Convert to minor units (cents)
          description: description,
          idempotency_key: `manual-${Date.now()}`
        })
      });
      
      const data = await res.json();
      
      if (res.ok) {
        setTransferMsg({ text: `Success: TX ${data.transaction_id}`, isError: false });
        setAmount('');
        setDescription('');
        await fetchBalanceSheet();
      } else {
        setTransferMsg({ text: `Failed: ${data.detail || 'Unknown Error'}`, isError: true });
      }
    } catch (err) {
      setTransferMsg({ text: 'Network error occurred', isError: true });
    }
    setIsTransferring(false);
  };

  return (
    <div className="panel" style={{ gridColumn: '1 / -1', display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px' }}>
      {/* Create Account Form */}
      <div>
        <div className="panel-header" style={{ marginBottom: '16px' }}>
          <h2 className="panel-title">
            <Users size={20} className="header-title-highlight" />
            Create Account
          </h2>
        </div>
        <form onSubmit={handleCreateAccount} className="flex-col" style={{ gap: '16px' }}>
          <div className="flex-col" style={{ gap: '8px' }}>
            <label className="metric-label">Account Name</label>
            <input 
              type="text" 
              className="btn btn-secondary" 
              style={{ width: '100%', textAlign: 'left', cursor: 'text' }}
              value={newAccName}
              onChange={(e) => setNewAccName(e.target.value)}
              placeholder="e.g. User Wallet A"
              required
            />
          </div>
          <div className="flex-col" style={{ gap: '8px' }}>
            <label className="metric-label">Account Type</label>
            <select 
              className="btn btn-secondary" 
              style={{ width: '100%', textAlign: 'left' }}
              value={newAccType}
              onChange={(e) => setNewAccType(e.target.value)}
            >
              <option value="0">Asset (e.g. Cash, Wallets)</option>
              <option value="1">Liability (e.g. Escrow, Deposits)</option>
              <option value="2">Equity</option>
              <option value="3">Revenue</option>
              <option value="4">Expense</option>
            </select>
          </div>
          <button type="submit" className="btn btn-primary" disabled={isCreating}>
            <Plus size={16} /> {isCreating ? 'Creating...' : 'Create Account'}
          </button>
        </form>
      </div>

      {/* Manual Transfer Form */}
      <div>
        <div className="panel-header" style={{ marginBottom: '16px' }}>
          <h2 className="panel-title">
            <Send size={20} className="header-title-highlight" />
            Manual Transfer
          </h2>
        </div>
        <form onSubmit={handleTransfer} className="flex-col" style={{ gap: '16px' }}>
          <div className="flex-row space-between" style={{ gap: '16px' }}>
            <div className="flex-col w-full" style={{ gap: '8px' }}>
              <label className="metric-label">From Account (Debit)</label>
              <select 
                className="btn btn-secondary" 
                style={{ width: '100%', textAlign: 'left' }}
                value={transferFrom}
                onChange={(e) => setTransferFrom(e.target.value)}
                required
              >
                <option value="">Select Account...</option>
                {accounts.map(acc => (
                  <option key={acc.account_id} value={acc.account_id}>{acc.name} (${(acc.balance / 100).toFixed(2)})</option>
                ))}
              </select>
            </div>
            <div className="flex-col w-full" style={{ gap: '8px' }}>
              <label className="metric-label">To Account (Credit)</label>
              <select 
                className="btn btn-secondary" 
                style={{ width: '100%', textAlign: 'left' }}
                value={transferTo}
                onChange={(e) => setTransferTo(e.target.value)}
                required
              >
                <option value="">Select Account...</option>
                {accounts.map(acc => (
                  <option key={acc.account_id} value={acc.account_id}>{acc.name}</option>
                ))}
              </select>
            </div>
          </div>
          
          <div className="flex-row space-between" style={{ gap: '16px' }}>
            <div className="flex-col" style={{ gap: '8px', flex: 1 }}>
              <label className="metric-label">Amount (USD)</label>
              <input 
                type="number" 
                step="0.01"
                min="0.01"
                className="btn btn-secondary" 
                style={{ width: '100%', textAlign: 'left', cursor: 'text' }}
                value={amount}
                onChange={(e) => setAmount(e.target.value)}
                placeholder="0.00"
                required
              />
            </div>
            <div className="flex-col" style={{ gap: '8px', flex: 2 }}>
              <label className="metric-label">Description</label>
              <input 
                type="text" 
                className="btn btn-secondary" 
                style={{ width: '100%', textAlign: 'left', cursor: 'text' }}
                value={description}
                onChange={(e) => setDescription(e.target.value)}
                placeholder="Transfer notes..."
              />
            </div>
          </div>
          
          <div className="flex-row space-between">
            <button type="submit" className="btn btn-primary" disabled={isTransferring}>
              <Send size={16} /> {isTransferring ? 'Processing...' : 'Execute Transfer'}
            </button>
            {transferMsg && (
              <span className={`badge ${transferMsg.isError ? 'badge-danger' : 'badge-success'}`}>
                {transferMsg.text}
              </span>
            )}
          </div>
        </form>
      </div>
    </div>
  );
};
