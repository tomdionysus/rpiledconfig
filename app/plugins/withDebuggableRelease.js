// Lets a release build be made debuggable with -Prpiledconfig.debuggableRelease=true,
// so a test phone can be driven and its logs read without Metro (TODO/ACTIVE.md,
// Hardware). Kept as a config plugin because `expo prebuild` regenerates
// android/app/build.gradle and drops anything written there by hand.
const { withAppBuildGradle } = require('expo/config-plugins');

const LINE = "            debuggable((findProperty('rpiledconfig.debuggableRelease') ?: 'false').toBoolean())";

module.exports = function withDebuggableRelease(config) {
  return withAppBuildGradle(config, (gradle) => {
    if (gradle.modResults.contents.includes('rpiledconfig.debuggableRelease')) return gradle;
    gradle.modResults.contents = gradle.modResults.contents.replace(
      /(release \{\n(?:.*\n)*?\s*signingConfig signingConfigs\.debug\n)/,
      `$1${LINE}\n`,
    );
    return gradle;
  });
};
