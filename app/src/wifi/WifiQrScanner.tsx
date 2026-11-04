// A full-screen camera that reads one WiFi QR code: the one Android's "Share"
// screen shows for a saved network on another phone, or a router's sticker.
// VisionCamera's code scanner does the reading (ML Kit on Android, AVFoundation
// on iOS); src/wifi/wifiQr.ts parses what it reads.
import { useMemo, useRef, useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { Camera, useCameraDevices, useCameraPermission, useCodeScanner } from 'react-native-vision-camera';
import { chooseScanCamera } from '@/led/camera';
import { Button } from '@/ui/Button';
import { colors, radius, space, type as typography } from '@/ui/theme';
import { parseWifiQr, WifiQrError, type WifiCredentials } from './wifiQr';

interface Props {
  onRead(credentials: WifiCredentials): void;
  onCancel(): void;
}

export function WifiQrScanner({ onRead, onCancel }: Props) {
  const insets = useSafeAreaInsets();
  const { hasPermission, requestPermission } = useCameraPermission();
  const devices = useCameraDevices();
  const device = useMemo(() => chooseScanCamera(devices, 30), [devices]);
  const [problem, setProblem] = useState<string | undefined>(undefined);
  const done = useRef(false);

  const codeScanner = useCodeScanner({
    codeTypes: ['qr'],
    onCodeScanned: (codes) => {
      if (done.current) return;
      for (const code of codes) {
        if (!code.value) continue;
        try {
          const credentials = parseWifiQr(code.value);
          done.current = true;
          onRead(credentials);
          return;
        } catch (error) {
          setProblem(error instanceof WifiQrError ? error.message : 'That QR code could not be read.');
        }
      }
    },
  });

  return (
    <View style={[StyleSheet.absoluteFill, styles.root]}>
      {hasPermission && device ? (
        <Camera style={StyleSheet.absoluteFill} device={device} isActive codeScanner={codeScanner} onError={(error) => setProblem(error.message)} />
      ) : null}
      <View style={[styles.overlay, { paddingTop: insets.top + space.xl, paddingBottom: insets.bottom + space.xl }]} pointerEvents="box-none">
        <Text style={styles.instruction}>
          On a phone that is on the network, open its WiFi settings, choose Share for the network, and point this camera at the QR code it shows.
        </Text>
        <View style={styles.panel}>
          {!hasPermission ? (
            <>
              <Text style={styles.body}>Reading the QR code needs the camera.</Text>
              <Button label="Allow camera" onPress={() => void requestPermission()} />
            </>
          ) : !device ? (
            <Text style={styles.body}>This phone has no back camera the app can use. Type the network instead.</Text>
          ) : null}
          {problem ? <Text style={styles.error}>{problem}</Text> : null}
          <Button label="Cancel" variant="quiet" onPress={onCancel} />
        </View>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    backgroundColor: colors.background,
    zIndex: 10,
  },
  overlay: {
    flex: 1,
    justifyContent: 'space-between',
    paddingHorizontal: space.xl,
  },
  instruction: {
    ...typography.body,
    color: colors.text,
    textAlign: 'center',
    backgroundColor: colors.scrim,
    borderRadius: radius.md,
    overflow: 'hidden',
    paddingHorizontal: space.lg,
    paddingVertical: space.md,
  },
  panel: {
    gap: space.md,
    backgroundColor: colors.scrim,
    borderRadius: radius.lg,
    padding: space.lg,
  },
  body: {
    ...typography.body,
    color: colors.textDim,
  },
  error: {
    ...typography.body,
    color: colors.danger,
  },
});
