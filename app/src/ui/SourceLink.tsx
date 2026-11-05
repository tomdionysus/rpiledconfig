// That the app is open source, and the link to it. Android's browser opens it: the app itself has
// no permission to use the internet.
import { Linking, StyleSheet, Text, View, type StyleProp, type ViewStyle } from 'react-native';
import { Button } from './Button';
import { colors, space, type as typography } from './theme';

export const SOURCE_URL = 'https://github.com/tomdionysus/rpiledconfig';

export function SourceLink({ style }: { style?: StyleProp<ViewStyle> }) {
  return (
    <View style={[styles.root, style]}>
      <Text style={styles.text}>RPi LED Config is open source: read what it does, or build it yourself and run that.</Text>
      <Button label="Source code on GitHub" variant="quiet" onPress={() => void Linking.openURL(SOURCE_URL)} />
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    gap: space.xs,
  },
  text: {
    ...typography.caption,
    color: colors.textFaint,
    textAlign: 'center',
  },
});
