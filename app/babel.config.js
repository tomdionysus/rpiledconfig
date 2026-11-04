// React Native's preset, plus the worklets-core plugin that compiles the `'worklet'`
// functions VisionCamera's frame processor runs off the JS thread.
module.exports = {
  presets: ['module:@react-native/babel-preset'],
  plugins: [['react-native-worklets-core/plugin']],
};
