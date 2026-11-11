// The root: providers, the status bar, and the screens in the order the setup runs:
// home, find, scan, pair, wifi; before them all, the permissions the app needs, when any is missing.
import React, { useEffect, useState } from 'react';
import { StatusBar, StyleSheet, View } from 'react-native';
import { SafeAreaProvider } from 'react-native-safe-area-context';
import { Navigator } from '@/navigation';
import FindScreen from '@/screens/Find';
import HomeScreen from '@/screens/Home';
import PairScreen from '@/screens/Pair';
import PermissionsScreen from '@/screens/Permissions';
import ScanScreen from '@/screens/Scan';
import WifiScreen from '@/screens/Wifi';
import { permissionsAsked } from '@/native/settings';
import { checkNeeds, currentNeeds, showAtLaunch } from '@/permissions/permissions';
import { AppProvider } from '@/state/store';
import { colors } from '@/ui/theme';

const SCREENS = {
  '/': HomeScreen,
  '/find': FindScreen,
  '/scan': ScanScreen,
  '/pair': PairScreen,
  '/wifi': WifiScreen,
  '/permissions': PermissionsScreen,
};

export function App() {
  // Decided before the first screen, so that Home does not flash up and go. On the splash's
  // background, for the moment Android takes to answer.
  const [initial, setInitial] = useState<string | undefined>(undefined);
  useEffect(() => {
    const needs = currentNeeds();
    void checkNeeds(needs).then(
      (statuses) => setInitial(showAtLaunch(needs, statuses, permissionsAsked()) ? '/permissions' : '/'),
      () => setInitial('/'),
    );
  }, []);

  return (
    <View style={styles.root}>
      <SafeAreaProvider>
        <StatusBar barStyle="light-content" backgroundColor="transparent" translucent />
        <AppProvider>{initial ? <Navigator screens={SCREENS} initial={initial} /> : null}</AppProvider>
      </SafeAreaProvider>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: colors.background,
  },
});
