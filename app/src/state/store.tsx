// What the screens share: the machine ID read from the LED, the Pi chosen to
// read it from, the session once paired, and the services. Plain React
// context; the app is one linear flow and needs nothing more.
import React, { createContext, useContext, useMemo, useState } from 'react';
import type { FoundPi } from '@/pairing/discover';
import type { Session } from '@/pairing/session';
import { services, type Services } from './services';

interface AppState {
  /** The 8 bytes of the machine ID, once read or typed. */
  machineId?: Uint8Array;
  setMachineId(id: Uint8Array | undefined): void;
  /** The Pi chosen on the find screen and asked to blink: the LED read must be its. */
  target?: FoundPi;
  setTarget(target: FoundPi | undefined): void;
  session?: Session;
  setSession(session: Session | undefined): void;
  services: Services;
}

const AppContext = createContext<AppState | undefined>(undefined);

export function AppProvider({ children }: { children: React.ReactNode }) {
  const [machineId, setMachineId] = useState<Uint8Array | undefined>(undefined);
  const [target, setTarget] = useState<FoundPi | undefined>(undefined);
  const [session, setSession] = useState<Session | undefined>(undefined);

  const value = useMemo<AppState>(
    () => ({ machineId, setMachineId, target, setTarget, session, setSession, services }),
    [machineId, target, session],
  );
  return <AppContext.Provider value={value}>{children}</AppContext.Provider>;
}

export function useApp(): AppState {
  const state = useContext(AppContext);
  if (!state) throw new Error('useApp outside AppProvider');
  return state;
}
