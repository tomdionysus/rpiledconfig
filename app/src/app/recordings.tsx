// Development only: the LED recordings saved on this phone, to list, export
// and delete, and the last scan to save if it was not saved from the scan
// screen. A recording exported from here and dropped into src/led/fixtures/
// with `expected` filled in becomes a decoder test.
import { useCallback, useEffect, useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import type { Recording } from '@/led/recording';
import { deleteRecording, exportRecording, listRecordings, readRecording, saveRecording, type SavedRecording } from '@/led/recordingStore';
import { useApp } from '@/state/store';
import { Button } from '@/ui/Button';
import { Screen } from '@/ui/Screen';
import { colors, monospace, radius, space, type as typography } from '@/ui/theme';

function summary(recording: Recording): string {
  const seconds = recording.t.length > 0 ? recording.t[recording.t.length - 1] / 1000 : 0;
  const decoded = recording.decoded ? `decoded ${recording.decoded.idHex} (${recording.decoded.framesCombined} stretches combined)` : 'nothing decoded';
  return `${recording.platform}, ${recording.t.length} samples over ${seconds.toFixed(1)} s at ${recording.measuredFps} fps measured (${recording.fps} asked), ${decoded}`;
}

export default function RecordingsScreen() {
  const { lastScan, setLastScan } = useApp();
  const [saved, setSaved] = useState<SavedRecording[]>([]);
  const [details, setDetails] = useState<Record<string, string>>({});
  const [message, setMessage] = useState<string | undefined>(undefined);

  const reload = useCallback(() => {
    try {
      setSaved(listRecordings());
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    }
  }, []);

  useEffect(reload, [reload]);

  const attempt = async (action: () => Promise<void> | void) => {
    setMessage(undefined);
    try {
      await action();
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    }
    reload();
  };

  return (
    <Screen title="LED recordings" eyebrow="Development" showBack>
      <Text style={styles.body}>
        A scan's luminance samples, saved as JSON in this app's documents. Export one to a folder, copy it into
        app/src/led/fixtures/, add "expected": {'{'} "idHex": "..." {'}'} from rpiledconfig --show-id, and it is a decoder test.
      </Text>

      {lastScan ? (
        <View style={styles.card}>
          <Text style={styles.cardLabel}>LAST SCAN, NOT YET SAVED</Text>
          <Text style={styles.detail}>{summary(lastScan)}</Text>
          <Button
            label="Save it"
            variant="secondary"
            onPress={() =>
              void attempt(() => {
                saveRecording(lastScan);
                setLastScan(undefined);
              })
            }
          />
        </View>
      ) : null}

      {saved.length === 0 ? <Text style={styles.body}>No recordings saved yet.</Text> : null}
      {saved.map((entry) => (
        <View key={entry.uri} style={styles.card}>
          <Text style={styles.name}>{entry.name}</Text>
          <Text style={styles.detail}>
            {(entry.size / 1024).toFixed(1)} KB{entry.modified ? `, ${new Date(entry.modified).toLocaleString()}` : ''}
          </Text>
          {details[entry.uri] ? <Text style={styles.detail}>{details[entry.uri]}</Text> : null}
          <View style={styles.actions}>
            <Button
              label="Details"
              variant="quiet"
              onPress={() =>
                void attempt(async () => {
                  const recording = await readRecording(entry);
                  setDetails((current) => ({ ...current, [entry.uri]: summary(recording) }));
                })
              }
            />
            <Button
              label="Export"
              variant="quiet"
              onPress={() =>
                void attempt(async () => {
                  const name = await exportRecording(entry);
                  if (name) setMessage(`Exported ${name}.`);
                })
              }
            />
            <Button label="Delete" variant="quiet" onPress={() => void attempt(() => deleteRecording(entry))} />
          </View>
        </View>
      ))}

      {message ? <Text style={styles.message}>{message}</Text> : null}
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
  card: {
    padding: space.lg,
    borderRadius: radius.lg,
    backgroundColor: colors.surface,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
    gap: space.xs,
    marginBottom: space.md,
  },
  cardLabel: {
    ...typography.micro,
    color: colors.textFaint,
  },
  name: {
    ...typography.label,
    fontFamily: monospace,
    color: colors.text,
  },
  detail: {
    ...typography.caption,
    color: colors.textDim,
  },
  actions: {
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  message: {
    ...typography.body,
    color: colors.textDim,
    marginTop: space.lg,
  },
});
