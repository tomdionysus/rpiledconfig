// Whether the app shows its diagnostics: the frame statistics in the log, the
// scan screen's save and torch controls, and the development section of the
// home screen (the fake Pi, the virtual LED, the recordings).
//
// Not __DEV__ alone: a development bundle embedded in the APK aborts at load
// in Expo 57 (expo/src/async-require/messageSocket.native.ts throws when the
// bundle did not come from a dev server), so a phone tested without Metro runs
// a production bundle, built with EXPO_PUBLIC_RPILEDCONFIG_DIAGNOSTICS=1, which
// Expo inlines when it bundles.
export const DIAGNOSTICS: boolean = __DEV__ || process.env.EXPO_PUBLIC_RPILEDCONFIG_DIAGNOSTICS === '1';
