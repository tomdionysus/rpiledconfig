// The root layout: providers and the stack of screens, in the order the
// setup runs: home, scan (or enter), pair, wifi.
import { Stack } from 'expo-router';
import { StatusBar } from 'expo-status-bar';
import { StyleSheet } from 'react-native';
import { GestureHandlerRootView } from 'react-native-gesture-handler';
import { SafeAreaProvider } from 'react-native-safe-area-context';
import { AppProvider } from '@/state/store';
import { colors } from '@/ui/theme';

export default function RootLayout() {
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
            <Stack.Screen name="recordings" />
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
