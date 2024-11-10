import plugin_vue from '@vitejs/plugin-vue'

export default {
	define: {
		__VUE_OPTIONS_API__: 'false'
	},
	plugins: [plugin_vue()]
}
