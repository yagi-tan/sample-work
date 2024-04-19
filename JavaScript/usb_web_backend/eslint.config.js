import js from '@eslint/js';

export default [
	js.configs.recommended,
	{
		ignores: ['docs/', 'nbproject/', 'test/']
	},
	{
		files: ['**/*.js'],
		languageOptions: {
			ecmaVersion: 'latest',
			globals: {
				console: 'readonly'
			}
		},
		rules: {
			"no-unused-vars": ["error", {"argsIgnorePattern": "^.+Ignored$"}]
		}
	}
];