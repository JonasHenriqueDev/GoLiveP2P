import js from '@eslint/js';
import tseslint from 'typescript-eslint';
import globals from 'globals';
export default tseslint.config(
  {
    ignores: [
      'dist/**',
      'out/**',
      'release/**',
      'node_modules/**',
      'work/**',
      'native/windows/sdk/**',
      'native/windows/runtime/**',
    ],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    languageOptions: { globals: { ...globals.browser, ...globals.node } },
    rules: { '@typescript-eslint/no-explicit-any': 'off' },
  },
);
