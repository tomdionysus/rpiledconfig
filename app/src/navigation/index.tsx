// The app's screens as a stack, with the small API the screens use: push, replace, back,
// and the parameters a route was opened with. Six screens in one straight line need no
// navigation library. Only the top screen is mounted; the one under it comes back when
// the top is popped. The hardware back pops; a deep link (rpiledconfig://find?...) opens
// that route over Home.
import React, { createContext, useCallback, useContext, useEffect, useMemo, useState, type ComponentType } from 'react';
import { BackHandler, Linking } from 'react-native';

export type Params = Record<string, string | undefined>;

export interface Route {
  pathname: string;
  params?: Params;
}

export type Href = string | Route;

export interface Router {
  push(href: Href): void;
  replace(href: Href): void;
  back(): void;
  canGoBack(): boolean;
}

interface Navigation {
  router: Router;
  current: Route;
}

const NavigationContext = createContext<Navigation | undefined>(undefined);

export function useRouter(): Router {
  const navigation = useContext(NavigationContext);
  if (!navigation) throw new Error('useRouter outside Navigator');
  return navigation.router;
}

/** The parameters the current screen was opened with, as strings. */
export function useLocalSearchParams<T extends Params = Params>(): T {
  const navigation = useContext(NavigationContext);
  if (!navigation) throw new Error('useLocalSearchParams outside Navigator');
  return (navigation.current.params ?? {}) as T;
}

function toRoute(href: Href): Route {
  return typeof href === 'string' ? { pathname: href } : href;
}

/** `rpiledconfig://find?symbolMs=45&fps=30` to a route, or undefined for anything else. */
export function routeFromUrl(url: string, known: readonly string[]): Route | undefined {
  const match = /^[a-z][a-z0-9+.-]*:\/\/([^?#]*)(?:\?([^#]*))?/i.exec(url);
  if (!match) return undefined;
  const pathname = `/${match[1].replace(/^\/+|\/+$/g, '')}`;
  if (!known.includes(pathname)) return undefined;
  const params: Params = {};
  for (const pair of (match[2] ?? '').split('&')) {
    if (!pair) continue;
    const [key, value = ''] = pair.split('=');
    params[decodeURIComponent(key)] = decodeURIComponent(value);
  }
  return { pathname, params };
}

interface NavigatorProps {
  screens: Record<string, ComponentType>;
  initial?: string;
}

export function Navigator({ screens, initial = '/' }: NavigatorProps) {
  const [stack, setStack] = useState<Route[]>([{ pathname: initial }]);
  const known = useMemo(() => Object.keys(screens), [screens]);

  const router = useMemo<Router>(
    () => ({
      push: (href) => setStack((s) => [...s, toRoute(href)]),
      replace: (href) => setStack((s) => [...s.slice(0, -1), toRoute(href)]),
      back: () => setStack((s) => (s.length > 1 ? s.slice(0, -1) : s)),
      canGoBack: () => stack.length > 1,
    }),
    // canGoBack reads the stack it closed over; the object changes with the stack.
    [stack.length],
  );

  // The hardware back button pops; on the first screen Android's default (leave the app).
  useEffect(() => {
    const subscription = BackHandler.addEventListener('hardwareBackPress', () => {
      if (stack.length <= 1) return false;
      setStack((s) => s.slice(0, -1));
      return true;
    });
    return () => subscription.remove();
  }, [stack.length]);

  // Deep links, at launch and while running.
  const open = useCallback(
    (url: string | null) => {
      const route = url ? routeFromUrl(url, known) : undefined;
      if (route) setStack([{ pathname: initial }, route]);
    },
    [initial, known],
  );
  useEffect(() => {
    void Linking.getInitialURL().then(open, () => {});
    const subscription = Linking.addEventListener('url', ({ url }) => open(url));
    return () => subscription.remove();
  }, [open]);

  const current = stack[stack.length - 1];
  const Screen = screens[current.pathname] ?? screens[initial];
  const value = useMemo<Navigation>(() => ({ router, current }), [router, current]);
  return (
    <NavigationContext.Provider value={value}>
      <Screen key={`${stack.length}-${current.pathname}`} />
    </NavigationContext.Provider>
  );
}
