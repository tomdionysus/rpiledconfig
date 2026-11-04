// Android's Wi-Fi Easy Connect configurator, opened with the Pi's DPP URI
// (docs/protocol.md, Copying the phone's WiFi). Android 10 and later have it;
// on a Blackview A85 (Android 12) the action resolves to
// com.android.settings/.wifi.dpp.WifiDppConfiguratorActivity. React Native's
// Linking.sendIntent cannot set an intent's data URI, which this needs, hence
// expo-intent-launcher.
import { ResultCode, startActivityAsync } from 'expo-intent-launcher';
import { Platform } from 'react-native';
import type { EasyConnectLaunch } from '@/pairing/copyWifi';

export const EASY_CONNECT_ACTION = 'android.settings.PROCESS_WIFI_EASY_CONNECT_URI';

/** Android 10 (API 29) added the configurator screen. */
export function easyConnectSupported(): boolean {
  return Platform.OS === 'android' && Number(Platform.Version) >= 29;
}

export const launchEasyConnect: EasyConnectLaunch = async (uri) => {
  const result = await startActivityAsync(EASY_CONNECT_ACTION, { data: uri });
  return result.resultCode === ResultCode.Success ? 'done' : 'cancelled';
};
