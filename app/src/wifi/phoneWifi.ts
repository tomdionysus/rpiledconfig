// The name of the WiFi network this phone is on, to suggest it. Android only:
// the SSID needs ACCESS_FINE_LOCATION granted and location switched on, and
// without them Android reports "<unknown ssid>". iOS needs the Access WiFi
// Information entitlement (a provisioning profile change) and location
// permission, so this returns nothing there for now.
import NetInfo from '@react-native-community/netinfo';
import { PermissionsAndroid, Platform } from 'react-native';

export async function phoneSsid(): Promise<string | undefined> {
  if (Platform.OS !== 'android') return undefined;
  try {
    const permission = PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION;
    if (!(await PermissionsAndroid.check(permission))) {
      const answer = await PermissionsAndroid.request(permission, {
        title: 'Name of this phone’s WiFi',
        message: 'Android shares the name of the WiFi network this phone is on only with apps allowed to use location. It is used to suggest that network for the Pi, and nothing else.',
        buttonPositive: 'Allow',
        buttonNegative: 'Not now',
      });
      if (answer !== PermissionsAndroid.RESULTS.GRANTED) return undefined;
    }
    const state = await NetInfo.fetch('wifi');
    if (state.type !== 'wifi') return undefined;
    const ssid = state.details?.ssid;
    if (!ssid || ssid === '<unknown ssid>') return undefined;
    return ssid.replace(/^"(.*)"$/, '$1');
  } catch {
    return undefined;
  }
}
