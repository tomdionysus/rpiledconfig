import { defineConfig } from 'vitest/config';

// Logic tests only, in `node`: nothing under test touches a DOM. Native modules
// that logic reaches are aliased to stubs in src/test/, not mocked per file.
const stub = (name: string) => new URL(`./src/test/${name}.ts`, import.meta.url).pathname;

export default defineConfig({
  test: {
    environment: 'node',
    include: ['src/**/*.test.ts'],
  },
  resolve: {
    alias: [
      { find: /^@\/(.*)$/, replacement: `${new URL('./src/', import.meta.url).pathname}$1` },
      { find: 'react-native-ble-plx', replacement: stub('react-native-ble-plx') },
      { find: 'react-native-vision-camera', replacement: stub('react-native-vision-camera') },
      { find: 'react-native-worklets-core', replacement: stub('react-native-worklets-core') },
      { find: 'react-native', replacement: stub('react-native') },
      { find: 'expo-crypto', replacement: stub('expo-crypto') },
      { find: 'expo-constants', replacement: stub('expo-constants') },
    ],
  },
});
