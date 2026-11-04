// The fallback to the camera: the machine ID typed as 16 hex characters, as
// `rpiledconfig --show-id` prints it.
import { useRouter } from 'expo-router';
import { useState } from 'react';
import { StyleSheet, Text, TextInput } from 'react-native';
import { hexToBytes } from '@/protocol/bytes';
import { ID_HEX_LENGTH } from '@/protocol/constants';
import { fingerprint } from '@/protocol/keys';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Screen } from '@/ui/Screen';
import { colors, monospace, radius, space, type as typography } from '@/ui/theme';

/** The typed text as ID bytes, or undefined until it is 16 hex characters. */
export function parseTypedId(text: string): Uint8Array | undefined {
  const clean = text.replace(/[^0-9a-fA-F]/g, '').toLowerCase();
  return clean.length === ID_HEX_LENGTH ? hexToBytes(clean) : undefined;
}

export default function EnterScreen() {
  const router = useRouter();
  const { setMachineId } = useApp();
  const [text, setText] = useState('');
  const id = parseTypedId(text);

  return (
    <Screen title="Type the ID" eyebrow="Fallback" showBack>
      <Text style={styles.body}>
        On the Pi, `rpiledconfig --show-id` prints the 16 hex characters of its machine ID. This skips the LED, so it
        only proves Bluetooth range, not line of sight.
      </Text>
      <TextInput
        value={text}
        onChangeText={setText}
        placeholder="0123456789abcdef"
        placeholderTextColor={colors.textFaint}
        autoCapitalize="none"
        autoCorrect={false}
        maxLength={2 * ID_HEX_LENGTH}
        style={styles.input}
      />
      <Text style={styles.hint}>{id ? `Fingerprint ${fingerprint(id)}: the Pi should advertise as RPiLEDConfig-${fingerprint(id)}.` : 'Sixteen hex characters.'}</Text>
      <Button
        label="Pair"
        disabled={!id}
        onPress={() => {
          if (!id) return;
          setMachineId(id);
          router.push('/pair');
        }}
      />
    </Screen>
  );
}

const styles = StyleSheet.create({
  body: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
    marginBottom: space.xl,
  },
  input: {
    ...typography.title,
    fontFamily: monospace,
    color: colors.text,
    backgroundColor: colors.surface,
    borderRadius: radius.md,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.borderStrong,
    paddingHorizontal: space.lg,
    paddingVertical: space.md,
    minHeight: 54,
    letterSpacing: 2,
  },
  hint: {
    ...typography.caption,
    color: colors.textFaint,
    marginTop: space.sm,
    marginBottom: space.xl,
  },
});
