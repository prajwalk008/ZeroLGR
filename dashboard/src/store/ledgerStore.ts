import { create } from 'zustand';

export interface Account {
  account_id: string;
  name: string;
  balance: number;
  type: number;
}

interface LedgerState {
  accounts: Account[];
  totalAssets: number;
  totalLiabilities: number;
  totalEquity: number;
  invariantOk: boolean;
  
  setBalanceSheet: (data: any) => void;
  fetchBalanceSheet: () => Promise<void>;
}

export const useLedgerStore = create<LedgerState>((set) => ({
  accounts: [],
  totalAssets: 0,
  totalLiabilities: 0,
  totalEquity: 0,
  invariantOk: true,

  setBalanceSheet: (data) => set({
    accounts: data.accounts,
    totalAssets: data.total_assets,
    totalLiabilities: data.total_liabilities,
    totalEquity: data.total_equity,
    invariantOk: data.invariant_ok,
  }),

  fetchBalanceSheet: async () => {
    try {
      const res = await fetch('http://localhost:8000/ledger/balance-sheet');
      if (res.ok) {
        const data = await res.json();
        set({
          accounts: data.accounts,
          totalAssets: data.total_assets,
          totalLiabilities: data.total_liabilities,
          totalEquity: data.total_equity,
          invariantOk: data.invariant_ok,
        });
      }
    } catch (e) {
      console.error('Failed to fetch balance sheet', e);
    }
  }
}));
