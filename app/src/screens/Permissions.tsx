// Before anything else: what the app needs of the phone and why, asked for together, so that
// Android's prompts do not arrive one at a time in the middle of setting a Pi up. What is refused
// is explained, with another try, or a way to Settings where Android will not ask again.
import { useRouter } from '@/navigation';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { AppState, Linking, StyleSheet, Text, View } from 'react-native';
import { markPermissionsAsked } from '@/native/settings';
import { canContinue, checkNeeds, currentNeeds, requestNeeds, type NeedStatus } from '@/permissions/permissions';
import { SourceLink } from '@/ui/SourceLink';
import { Button } from '@/ui/Button';
import { Screen } from '@/ui/Screen';
import { colors, radius, space, type as typography } from '@/ui/theme';

export default function PermissionsScreen() {
  const router = useRouter();
  const needs = useMemo(currentNeeds, []);
  // Nothing refused is shown until the user has been asked here.
  const [statuses, setStatuses] = useState<Record<string, NeedStatus> | undefined>(undefined);
  const [asking, setAsking] = useState(false);

  const ask = useCallback(async () => {
    setAsking(true);
    try {
      const answers = await requestNeeds(needs);
      markPermissionsAsked();
      if (canContinue(needs, answers)) {
        router.replace('/');
        return;
      }
      setStatuses(answers);
    } finally {
      setAsking(false);
    }
  }, [needs, router]);

  // Back from Settings: what was turned on there counts, with nothing more to press. Android's own
  // prompt also takes the app out of the foreground, so not while it is up.
  useEffect(() => {
    if (!statuses || asking) return;
    const subscription = AppState.addEventListener('change', (state) => {
      if (state !== 'active') return;
      void checkNeeds(needs).then((now) => {
        if (canContinue(needs, now)) {
          router.replace('/');
          return;
        }
        // A need Android called blocked stays so until it is granted: a check cannot tell.
        setStatuses((before) =>
          Object.fromEntries(needs.map((need) => [need.id, now[need.id] === 'granted' ? 'granted' : (before?.[need.id] ?? now[need.id])])),
        );
      });
    });
    return () => subscription.remove();
  }, [asking, needs, router, statuses]);

  const refused = statuses ? needs.filter((need) => need.required && statuses[need.id] !== 'granted') : [];
  const blocked = refused.some((need) => statuses?.[need.id] === 'blocked');

  return (
    <Screen title={statuses ? 'Still needed' : 'Before you start'}>
      <Text style={styles.body}>
        {statuses
          ? 'The app cannot set up a Pi without these. Nothing is sent anywhere but to your Pi, and the app has no permission to use the internet.'
          : 'The app will ask Android for these. Nothing is sent anywhere but to your Pi, and the app has no permission to use the internet.'}
      </Text>

      <View style={styles.list}>
        {(statuses ? refused : needs).map((need) => (
          <View key={need.id} style={styles.need}>
            <Text style={styles.needTitle}>{need.title}</Text>
            <Text style={styles.needWhy}>{need.why}</Text>
            {statuses?.[need.id] === 'blocked' ? <Text style={styles.blocked}>Android will not ask again. Turn it on in the app's settings.</Text> : null}
          </View>
        ))}
      </View>

      {blocked ? (
        <Button label="Open settings" onPress={() => void Linking.openSettings()} />
      ) : (
        <Button label={statuses ? 'Try again' : 'Continue'} busy={asking} onPress={() => void ask()} />
      )}

      <SourceLink style={styles.source} />
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
  list: {
    gap: space.sm,
    marginBottom: space.xl,
  },
  need: {
    gap: space.xs,
    padding: space.lg,
    borderRadius: radius.lg,
    backgroundColor: colors.surface,
    borderWidth: StyleSheet.hairlineWidth,
    borderColor: colors.border,
  },
  needTitle: {
    ...typography.heading,
    color: colors.text,
  },
  needWhy: {
    ...typography.body,
    color: colors.textDim,
    lineHeight: 21,
  },
  blocked: {
    ...typography.body,
    color: colors.danger,
    lineHeight: 21,
    marginTop: space.xs,
  },
  source: {
    marginTop: space.lg,
  },
});
