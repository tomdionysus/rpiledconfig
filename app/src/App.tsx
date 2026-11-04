// The root: providers, the status bar, and the screens in the order the setup runs:
// home, find, scan (or enter), pair, wifi.
import React from 'react';
import { StatusBar, StyleSheet, View } from 'react-native';
import { SafeAreaProvider } from 'react-native-safe-area-context';
import { Navigator } from '@/navigation';
import EnterScreen from '@/screens/Enter';
import FindScreen from '@/screens/Find';
import HomeScreen from '@/screens/Home';
import PairScreen from '@/screens/Pair';
import ScanScreen from '@/screens/Scan';
import WifiScreen from '@/screens/Wifi';
import { AppProvider } from '@/state/store';
import { colors } from '@/ui/theme';

const SCREENS = {
  '/': HomeScreen,
  '/find': FindScreen,
  '/scan': ScanScreen,
  '/pair': PairScreen,
  '/wifi': WifiScreen,
  '/enter': EnterScreen,
};

export function App() {
  return (
    <View style={styles.root}>
      <SafeAreaProvider>
        <StatusBar barStyle="light-content" backgroundColor="transparent" translucent />
        <AppProvider>
          <Navigator screens={SCREENS} />
        </AppProvider>
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
