<script setup>
'use strict';

import {generateUniqueId} from './utils.js';

import {onBeforeMount} from 'vue';

const props = defineProps({
	//Config group name.
	name: {
		required: true,
		type: String,
		validator(value) {
			return value.length;									//need non-empty string
		}
	},
	//Section nested depth, starting from 0, 1, 2...
	depth: {
		required: true,
		type: Number,
		validator(value) {
			return value >= 0;
		}
	}
});
const theme = {};
const togglerId = generateUniqueId('coll-sect');

onBeforeMount(() => {
	theme.sectionBg = `hsl(220 100% ${95 - 5 * props.depth}%)`;
	theme.sectionLeft = `${2 * props.depth}em`;
});
</script>

<template>
<div class="section">
	<label class="name" :for="togglerId">{{props.name}}</label>
	<input class="toggler" :id="togglerId" type="checkbox">
	<div class="content">
		<slot />
	</div>
</div>
</template>

<style scoped>
.content {
	max-height: 0vb;
	overflow: hidden;
	transition: max-height 100ms linear;
}
.name {
	display: block;
	font-size: xx-large;
}
.section {
	background-color: v-bind('theme.sectionBg');
	margin-left: v-bind('theme.sectionLeft');
}
.toggler {
	display: none;
}
.toggler:checked + .content {
	max-height: 100vb;
}
</style>