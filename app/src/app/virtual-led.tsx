// Development only: this phone's screen blinks a frame the way the Pi's LED
// would, so another phone's camera path can be tried without a Pi. The
// screen's refresh quantises the timing to 16 ms, well inside T.
import { useRouter } from 'expo-router';
import { useEffect, useMemo, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { FAKE_MACHINE_ID_HEX } from '@/ble/fake';
import { frameLevels, levelAt } from '@/led/frame';
import { bytesToHex, hexToBytes } from '@/protocol/bytes';
import { useApp } from '@/state/store';
import { colors, monospace, space, type as typography } from '@/ui/theme';

/** How often the level is re-read; a few times per T is plenty. */
const TICK_MS = 10;

export default function VirtualLedScreen() {
  const router = useRouter();
  const insets = useSafeAreaInsets();
  const { machineId } = useApp();
  const idBytes = useMemo(() => machineId ?? hexToBytes(FAKE_MACHINE_ID_HEX), [machineId]);
  const levels = useMemo(() => frameLevels(idBytes), [idBytes]);
  const [on, setOn] = useState(false);

  useEffect(() => {
    const started = Date.now();
    const timer = setInterval(() => setOn(levelAt(levels, Date.now() - started)), TICK_MS);
    return () => clearInterval(timer);
  }, [levels]);

  return (
    <Pressable style={styles.root} onPress={() => router.back()} accessibilityRole="button" accessibilityLabel="Stop the virtual LED">
      <View style={[styles.led, on && styles.on]} />
      <View style={[styles.caption, { bottom: insets.bottom + space.xl }]}>
        <Text style={styles.id}>{bytesToHex(idBytes)}</Text>
        <Text style={styles.hint}>Point the other phone at the square. Tap to stop.</Text>
      </View>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: '#000000',
    alignItems: 'center',
    justifyContent: 'center',
  },
  led: {
    width: 180,
    height: 180,
    borderRadius: 90,
    backgroundColor: '#000000',
  },
  on: {
    backgroundColor: '#ffffff',
  },
  caption: {
    position: 'absolute',
    alignItems: 'center',
    gap: space.xs,
  },
  id: {
    ...typography.label,
    fontFamily: monospace,
    color: colors.textFaint,
    letterSpacing: 2,
  },
  hint: {
    ...typography.caption,
    color: colors.textFaint,
  },
});
