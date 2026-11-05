// What the screens share: the one-time key read from the LED, the Pi chosen to
// read it from, the session once paired, and the services. Plain React
// context; the app is one linear flow and needs nothing more.
import React, { createContext, useCallback, useContext, useMemo, useState } from 'react';
import type { LedRate } from '@/led/rates';
import { loadRate, saveRate } from '@/native/settings';
import type { FoundPi } from '@/pairing/discover';
import type { Session } from '@/pairing/session';
import { services, type Services } from './services';

interface AppState {
  /** The 8 bytes of the key, once read. */
  pairingKey?: Uint8Array;
  setPairingKey(id: Uint8Array | undefined): void;
  /** The Pi chosen on the find screen and asked to blink: the LED read must be its. */
  target?: FoundPi;
  setTarget(target: FoundPi | undefined): void;
  session?: Session;
  setSession(session: Session | undefined): void;
  services: Services;
  /** The LED rate to ask for, remembered between launches. */
  rate: LedRate;
  setRate(rate: LedRate): void;
}

const AppContext = createContext<AppState | undefined>(undefined);

export function AppProvider({ children }: { children: React.ReactNode }) {
  const [pairingKey, setPairingKey] = useState<Uint8Array | undefined>(undefined);
  const [target, setTarget] = useState<FoundPi | undefined>(undefined);
  const [session, setSession] = useState<Session | undefined>(undefined);
  const [rate, setRateState] = useState<LedRate>(loadRate);
  const setRate = useCallback((next: LedRate) => {
    setRateState(next);
    saveRate(next);
  }, []);

  const value = useMemo<AppState>(
    () => ({ pairingKey, setPairingKey, target, setTarget, session, setSession, services, rate, setRate }),
    [pairingKey, target, session, rate, setRate],
  );
  return <AppContext.Provider value={value}>{children}</AppContext.Provider>;
}

export function useApp(): AppState {
  const state = useContext(AppContext);
  if (!state) throw new Error('useApp outside AppProvider');
  return state;
}
