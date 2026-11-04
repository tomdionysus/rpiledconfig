// Whether the app shows its diagnostics: the reader's statistics in the log, the join
// dialog's keystrokes, and the platform's own words under an error.
//
// A development bundle, or a build made with -Prpiledconfig.diagnostics=true (android/app/
// build.gradle), which the app's own RpiRandom module reports as a constant.
import { NativeModules } from 'react-native';

interface WithConstants {
  getConstants?(): { diagnostics?: boolean };
}

function fromBuild(): boolean {
  try {
    const module = (NativeModules as { RpiRandom?: WithConstants }).RpiRandom;
    return module?.getConstants?.().diagnostics === true;
  } catch {
    return false;
  }
}

export const DIAGNOSTICS: boolean = (typeof __DEV__ !== 'undefined' && __DEV__) || fromBuild();
