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
    const permission = PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION;
    if (!(await PermissionsAndroid.check(permission))) {
      const answer = await PermissionsAndroid.request(permission, {
        title: 'Name of this phone\u2019s WiFi',
        message: 'Android shares the name of the WiFi network this phone is on only with apps allowed to use location. It is used to mark that network in the list, and nothing else.',
        buttonPositive: 'Allow',
        buttonNegative: 'Not now',
      });
      if (answer !== PermissionsAndroid.RESULTS.GRANTED) return undefined;
    }
    const ssid = await module.ssid();
    return ssid ?? undefined;
  } catch {
    return undefined;
  }
}
