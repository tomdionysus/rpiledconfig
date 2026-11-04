const path = require('path');
const { getDefaultConfig, mergeConfig } = require('@react-native/metro-config');

/**
 * Metro, with the `@/` alias for src/ that tsconfig.json's paths give the compiler.
 * @type {import('@react-native/metro-config').MetroConfig}
 */
const config = {
  resolver: {
    resolveRequest(context, moduleName, platform) {
      if (moduleName.startsWith('@/')) {
        return context.resolveRequest(context, path.join(__dirname, 'src', moduleName.slice(2)), platform);
      }
      return context.resolveRequest(context, moduleName, platform);
    },
  },
};

module.exports = mergeConfig(getDefaultConfig(__dirname), config);
