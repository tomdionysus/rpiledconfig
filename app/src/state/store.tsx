// What the screens share: the machine ID read from the LED, the session once
// paired, and the services (real or fake). Plain React context; the app is
// one linear flow and needs nothing more.
import React, { createContext, useContext, useMemo, useState } from 'react';
import type { Recording } from '@/led/recording';
import type { Session } from '@/pairing/session';
import { createServices, fakeByDefault, type Services } from './services';

interface AppState {
  /** The 8 bytes of the machine ID, once read or typed. */
  machineId?: Uint8Array;
  setMachineId(id: Uint8Array | undefined): void;
  session?: Session;
  setSession(session: Session | undefined): void;
  fake: boolean;
  setFake(fake: boolean): void;
  services: Services;
  /** The last scan's samples, kept for the development recordings screen to save. */
  lastScan?: Recording;
  setLastScan(recording: Recording | undefined): void;
}

const AppContext = createContext<AppState | undefined>(undefined);

export function AppProvider({ children }: { children: React.ReactNode }) {
  const [machineId, setMachineId] = useState<Uint8Array | undefined>(undefined);
  const [session, setSession] = useState<Session | undefined>(undefined);
  const [fake, setFake] = useState(fakeByDefault());
  const services = useMemo(() => createServices(fake), [fake]);
  const [lastScan, setLastScan] = useState<Recording | undefined>(undefined);

  const value = useMemo<AppState>(
    () => ({ machineId, setMachineId, session, setSession, fake, setFake, services, lastScan, setLastScan }),
    [machineId, session, fake, services, lastScan],
  );
  return <AppContext.Provider value={value}>{children}</AppContext.Provider>;
}

export function useApp(): AppState {
  const state = useContext(AppContext);
  if (!state) throw new Error('useApp outside AppProvider');
  return state;
}
