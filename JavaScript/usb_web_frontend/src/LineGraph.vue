<script setup>
'use strict';

import {inject, onMounted, onUnmounted, ref, shallowRef, watch} from 'vue';

const canvas = shallowRef(), container = shallowRef(), graphCss = ref({
	'height': 10,
	'width': 0
}), scroller = shallowRef();
const channelConfig = inject('channelConfig');
let obsvContainer, obsvScroller, capturing = shallowRef(false);
let workerUi;

/** Clear button handler. */
function graphClear() {
	workerUi.postMessage({
		'action': 'clear',
		'data': null
	});
}

/** Zoom buttons handler.
 * @param {number} delta Zoom delta (+1 or -1). */
function graphZoom(delta) {
	workerUi.postMessage({
		'action': 'zoom',
		'data': delta
	});
}

/** Starts/Stops data capture button handler.*/
function toggleCapture() {
	capturing.value = !capturing.value;
	
	workerUi.postMessage({
		'action': 'capture',
		'data': capturing.value
	});
}

onMounted(() => {
	const offCanvas = canvas.value.transferControlToOffscreen();
	
	workerUi = new Worker(new URL('LineGraphUi.js', import.meta.url), {'type': 'module'});
	workerUi.onmessage = evt => {
		switch (evt.data.action) {
			case 'resize':
				graphCss.value.height = evt.data.data.height;
				graphCss.value.width = evt.data.data.width;
				break;
			case 'setup':
				console.log(`Graph UI worker ${evt.data.data ? 'successfully' : 'failed to'} set up.`);
				break;
			default:
				console.log(`Graph UI worker sent unknown action '${evt.data.action}'.`);
				break;
		}
	};
	workerUi.postMessage({
		'action': 'setup',
		'data': {
			'canvas': offCanvas,
			'dataUrl': `ws://${(new URL(document.URL)).host}`
		}
	}, [offCanvas]);
	
	//this will register element resize event and do graph update for the first time
	obsvContainer = new ResizeObserver(() => {
		workerUi.postMessage({
			'action': 'resizeContainer',
			'data': {
				'height': container.value.clientHeight,
				'width': graphCss.value.width
			}
		});
	});
	obsvContainer.observe(container.value);
	obsvScroller = new ResizeObserver(() => {
		workerUi.postMessage({
			'action': 'resizeScroller',
			'data': scroller.value.clientWidth
		});
	});
	obsvScroller.observe(scroller.value);
	
	watch(channelConfig, newValue => {
		workerUi.postMessage({
			'action': 'updateChannelConfig',
			'data': newValue.states
		});
	}, {deep: true});
});
onUnmounted(() => {
	obsvContainer.disconnect();
	obsvScroller.disconnect();
	
	if (workerUi) {
		workerUi.postMessage({
			'action': 'shutdown'
		});
	}
});
</script>

<template>
<div class="scroller" ref="scroller">
	<div class="container" ref="container">
		<canvas ref="canvas"></canvas>
	</div>
</div>
<div class="controls">
	<button class="buttons" type="button" @click="toggleCapture">
		{{capturing ? 'Stop capture' : 'Start capture'}}
	</button>
	<button class="buttons" type="button" @click="graphZoom(+1)">Zoom in</button>
	<button class="buttons" type="button" @click="graphZoom(-1)">Zoom out</button>
	<button class="buttons" type="button" @click="graphClear">Clear graph</button>
</div>
</template>

<style scoped>
.buttons {
	flex: auto;
}
.container {
	position: relative;
	height: v-bind("graphCss.height + 'vh'");
	width: v-bind("graphCss.width + 'px'");
}
.controls {
	display: flex;
}
.scroller {
	width: 100%;
	overflow-x: auto;
	overflow-y: hidden;
}
</style>
