// The name of the WiFi network this phone is on, to mark it in the list. Android only:
// the SSID needs ACCESS_FINE_LOCATION granted and location switched on, and without them
// Android reports "<unknown ssid>". The app's own RpiWifiInfo module
// (android/.../RpiPackage.kt) asks WifiManager. iOS needs the Access WiFi Information
// entitlement (a provisioning profile change) and location permission, so this returns
// nothing there for now.
import { NativeModules, PermissionsAndroid, Platform } from 'react-native';

interface RpiWifiInfo {
  ssid(): Promise<string | null>;
}

export async function phoneSsid(): Promise<string | undefined> {
  if (Platform.OS !== 'android') return undefined;
  const module = (NativeModules as { RpiWifiInfo?: RpiWifiInfo }).RpiWifiInfo;
  if (!module) return undefined;
  try {
    // Asked for at first launch (screens/Permissions.tsx); declined, the network just goes unmarked.
    if (!(await PermissionsAndroid.check(PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION))) return undefined;
    const ssid = await module.ssid();
    return ssid ?? undefined;
  } catch {
    return undefined;
  }
}
