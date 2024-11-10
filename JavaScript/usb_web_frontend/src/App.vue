<script setup>
'use strict';

import {setStates} from './states.js';
import CollapsibleSection from './CollapsibleSection.vue';
import ConfigItem from './ConfigItem.vue';
import LineGraph from './LineGraph.vue';

import {provide, shallowReactive, shallowRef} from 'vue';

const channelConfig = shallowReactive({change: 0, states: {}}), channelCount = shallowRef(0);

function updateChannelConfig(url, value) {
	if (typeof(value) !== 'object') {
		console.error(`Invalid 'value' type for channel config update: ${value}`);
		return;
	}
	
	const match = /^\/configuration\/channel\/(\d+)\/config\/?$/.exec(url);
	
	if (match) {
		const channelId = Number.parseInt(match[1]);
		for (const [k, v] of Object.entries(value)) {
			setStates(channelConfig.states, channelId, k, v);
		}
		++channelConfig.change;
	}
	else {
		console.error(`Invalid URL for channel config update: ${url}`);
	}
}
function updateChannelCount(url, value) {
	console.log(`Channel count '${channelCount.value}' -> '${value}'.`);
	channelCount.value = value;
}

if (process.env.NODE_ENV === 'development') {
	console.log('in development mode');
}
if (process.env.NODE_ENV === 'production') {
	console.log('in production mode');
}

provide('channelConfig', channelConfig);
</script>

<template>
<LineGraph />
<CollapsibleSection name="Paths" :depth="0">
	<ConfigItem name="cdev" url="/configuration/cdev/path" set-method="POST">
		<label>Path: <input target="path"></label>
		<label>Major ID: <input target="major" type="number"></label>
		<label>Minor ID: <input target="minor" type="number"></label>
	</ConfigItem>
	<ConfigItem name="sysfs" url="/configuration/sysfs/path" set-method="POST">
		<label>Path: <input target="path"></label>
	</ConfigItem>
</CollapsibleSection>
<CollapsibleSection name="Channels" :depth="0">
	<ConfigItem name="Channel count" url="/configuration/channel/count" set-method="PATCH"
		@update-value="updateChannelCount"
	>
		<label>Count: <input type="number"></label>
	</ConfigItem>
	<CollapsibleSection :name="'Channel ' + (chIdx - 1)" :depth="1" v-for="chIdx in channelCount">
		<ConfigItem name="Control" :url="`/control/channel/${chIdx - 1}`" set-method="PATCH">
			<label>Running: <input type="checkbox"></label>
		</ConfigItem>
		<ConfigItem name="Configuration" :url="`/configuration/channel/${chIdx - 1}/config`" set-method="POST"
			@update-value="updateChannelConfig"
		>
			<label>Pin base index: <input target="pinbase" type="number"></label>
			<label>Pin count: <input target="pincount" type="number"></label>
			<label>Sampling rate (Hz): <input target="rate" type="number"></label>
		</ConfigItem>
	</CollapsibleSection>
</CollapsibleSection>
</template>

<style scoped>

</style>