// Babel for Expo, plus the worklets-core plugin that compiles the `'worklet'`
// functions VisionCamera's frame processor runs off the JS thread. It is
// listed here, in `plugins`, so it runs before the `react-native-worklets`
// plugin that babel-preset-expo adds on its own for Reanimated (a dependency
// of expo-router): VisionCamera's documentation wants its plugin first, and
// Babel runs `plugins` before presets.
module.exports = function (api) {
  api.cache(true);
  return {
    presets: ['babel-preset-expo'],
    plugins: [['react-native-worklets-core/plugin']],
  };
};
