// ESLint for the app's TypeScript: the recommended rules, typescript-eslint's with the
// type checker (an unawaited promise in the Bluetooth code is a bug, not a style), and
// React's rules of hooks. Formatting is Prettier's (.prettierrc.json), so no rule here
// argues with it.
import js from '@eslint/js';
import prettier from 'eslint-config-prettier';
import reactHooks from 'eslint-plugin-react-hooks';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default tseslint.config(
  { ignores: ['android/', 'coverage/', 'modules/*/android/', 'modules/*/cpp/', 'node_modules/'] },
  js.configs.recommended,
  ...tseslint.configs.recommendedTypeChecked,
  {
    languageOptions: {
      parserOptions: { projectService: true, tsconfigRootDir: import.meta.dirname },
    },
  },
  reactHooks.configs.flat['recommended-latest'],
  {
    rules: {
      // `_name` marks an argument an interface requires and this implementation ignores.
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }],
      // Props and hook results declare their callbacks as methods and are destructured; they
      // are closures that never touch `this`, which is all this rule would catch.
      '@typescript-eslint/unbound-method': 'off',
      // Advice for the React Compiler, which the app does not use. The rules of hooks and
      // effect dependencies stay on.
      'react-hooks/set-state-in-effect': 'off',
      'react-hooks/preserve-manual-memoization': 'off',
      'react-hooks/use-memo': 'off',
    },
  },
  {
    // Fakes and stubs implement asynchronous interfaces with answers they already have.
    files: ['**/*.test.ts', 'src/test/**', 'src/ble/fake.ts'],
    rules: { '@typescript-eslint/require-await': 'off' },
  },
  {
    // Node's CommonJS: the Metro, Babel and entry files.
    files: ['**/*.{js,mjs,cjs}'],
    extends: [tseslint.configs.disableTypeChecked],
    languageOptions: { globals: globals.node },
    rules: { '@typescript-eslint/no-require-imports': 'off' },
  },
  prettier,
);
