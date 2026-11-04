// Just enough React Native for logic under test. `Platform.OS` defaults to
// ios. Keep this to environment facts; a module needing more should separate
// its logic from its runtime.
export const Platform = {
  OS: 'ios' as 'ios' | 'android' | 'web',
  Version: 17 as number | string,
  select<T>(spec: { ios?: T; android?: T; web?: T; default?: T }): T | undefined {
    return spec[Platform.OS] ?? spec.default;
  },
};

/** Grants everything: the permission flow is not what the tests exercise. */
export const PermissionsAndroid = {
  PERMISSIONS: {
    BLUETOOTH_SCAN: 'android.permission.BLUETOOTH_SCAN',
    BLUETOOTH_CONNECT: 'android.permission.BLUETOOTH_CONNECT',
    ACCESS_FINE_LOCATION: 'android.permission.ACCESS_FINE_LOCATION',
  },
  RESULTS: { GRANTED: 'granted', DENIED: 'denied', NEVER_ASK_AGAIN: 'never_ask_again' },
  async request(_permission: string): Promise<string> {
    return 'granted';
  },
  async requestMultiple(permissions: string[]): Promise<Record<string, string>> {
    return Object.fromEntries(permissions.map((p) => [p, 'granted']));
  },
};
