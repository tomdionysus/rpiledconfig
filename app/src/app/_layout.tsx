// The root layout: providers and the stack of screens, in the order the
// setup runs: home, find, scan (or enter), pair, wifi.
import { Stack } from 'expo-router';
import * as SplashScreen from 'expo-splash-screen';
import { useEffect } from 'react';
import { StatusBar } from 'expo-status-bar';
import { StyleSheet } from 'react-native';
import { GestureHandlerRootView } from 'react-native-gesture-handler';
import { SafeAreaProvider } from 'react-native-safe-area-context';
import { AppProvider } from '@/state/store';
import { colors } from '@/ui/theme';

// The native splash (app.json, expo-splash-screen: the app's background colour,
// so nothing white shows at launch) stays up for a quarter of a second.
const SPLASH_MS = 250;
void SplashScreen.preventAutoHideAsync();

export default function RootLayout() {
  useEffect(() => {
    const timer = setTimeout(() => void SplashScreen.hideAsync(), SPLASH_MS);
    return () => clearTimeout(timer);
  }, []);

  return (
    <GestureHandlerRootView style={styles.root}>
      <SafeAreaProvider>
        <StatusBar style="light" />
        <AppProvider>
          <Stack
            screenOptions={{
              headerShown: false,
              contentStyle: { backgroundColor: colors.background },
              animation: 'slide_from_right',
            }}>
            <Stack.Screen name="index" options={{ animation: 'fade' }} />
            <Stack.Screen name="find" />
            <Stack.Screen name="scan" options={{ animation: 'fade' }} />
            <Stack.Screen name="pair" options={{ gestureEnabled: false }} />
            <Stack.Screen name="wifi" options={{ gestureEnabled: false }} />
          </Stack>
        </AppProvider>
      </SafeAreaProvider>
    </GestureHandlerRootView>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: colors.background,
  },
});
