'use strict';

import {getStates} from './states.js';

import {Chart, LinearScale} from 'chart.js';
import {LineController, LineElement, PointElement} from 'chart.js';

Chart.register(LinearScale);
Chart.register(LineController, LineElement, PointElement);

const plugins = [
	{
		id: 'backgroundColour',
		beforeDraw: (chart, args, options) => {
			const {ctx} = chart;
			ctx.save();
			ctx.globalCompositeOperation = 'destination-over';
			ctx.fillStyle = options.colour || '#99ff00';
			ctx.fillRect(0, 0, chart.width, chart.height);
			ctx.restore();
		}
	}
];
let capturing = false, scrollerWidth = 0, tickCount = 0, zoom = 1;
let channelConfig = {};
let lineGraph, workerData;

/** Helper function to create channel label for chart.
 * @param {number} channel Channel ID.
 * @param {number} pin Pin index.
 * @return {string} Channel label. */
function createChannelLabel(channel, pin) {
	return `CH_${channel}_${pin}`;
}

/** Adds data from WebSocket traffic.
 * @param {Object} data Object containing channel id, sample levels and timestamps. */
function graphAddData(data) {
	if (!capturing) {
		return;
	}
	
	const pincount = getStates(channelConfig, data.channel, 'pincount');
	const elemQt = Math.min(data.levels.length, data.tss.length);
	const inserts = [], tss = [];
	
	if (data.levels.length !== data.tss.length) {
		console.warn("Graph data level and timestamp list length doesn't match.");
	}
	if (!elemQt) {
		console.error("Got empty list for graph data addition.");
		return;
	}
	
	//convert BigInt timestamps to number acceptable for Chart.js
	for (let idx = 0; idx < elemQt; ++idx) {
		tss.push(Number(BigInt.asUintN(53, data.tss[idx])));
	}
	
	//check for timestamp gap between current data and beginning of data batch
	//**********************************************************************************
	const label = createChannelLabel(data.channel, 0);				//pin 0 is always available
	let dataset = lineGraph.data.datasets.find(elem => (elem.label === label));
	
	if ((dataset !== undefined) && (dataset.data.length)) {			//channel exists and has data
		const lastEntry = dataset.data[dataset.data.length - 1];
		
		if ((lastEntry.x + 1) !== tss[0]) {							//put floating level at middle gap as mark
			for (let pin = 0; pin < pincount; ++pin) {
				inserts.push(graphInsertData(data.channel, pin, {
					x: lastEntry.x + ((tss[0] - lastEntry.x) / 2),
					y: 0.5
				}));
			}
		}
	}
	//**********************************************************************************
	
	//assume current data batch timestamps are continuous, so no need to check for gap
	for (let idx = 0; idx < elemQt; ++idx) {
		const level = data.levels[idx];
		
		for (let pin = 0; pin < pincount; ++pin) {
			inserts.push(graphInsertData(data.channel, pin, {
				x: tss[idx],
				y: (level >>> pin) & 0x01
			}));
		}
	}
	
	Promise.allSettled(inserts).then(results => {
		for (const result of results) {
			if (result.status === 'rejected') {
				console.error(`Error adding data to graph: ${result.reason}`);
			}
		}
	});
	
	graphUpdateTickCount();
	graphResize();
}

/** Clears all channels' data in graph, but leaving channel itself intact. */
function graphClear() {
	let hasUpdate = false;
	
	for (let dataset of lineGraph.data.datasets) {
		if (dataset.data.length) {
			dataset.data = [];
			hasUpdate = true;
		}
	}
	
	if (hasUpdate) {
		tickCount = 0;
		graphResize();
	}
}

//in-browser dummy data generator. deprecated after backend has its own generator.
//**************************************************************************************
let updateCycle = 1;

/** Generates dummy data for the graph. */
async function graphGetDataDummy() {
	const inserts = [];
	let hasUpdate = false;
	
	inserts.push(graphInsertData(0, 0, {x: updateCycle, y: Number(Math.random() >= 0.5)}));
	if (updateCycle % 2) {
		inserts.push(graphInsertData(1, 0, {x: updateCycle, y: Number(Math.random() >= 0.5)}));
	}
	for (const result of await Promise.allSettled(inserts)) {
		if (result.status === 'fulfilled') {
			hasUpdate = true;
		}
		else {
			console.error(`Error updating graph at cycle ${updateCycle}: ${result.reason}`);
		}
	}
	
	if (hasUpdate) {
		graphUpdateTickCount();
		graphResize();
	}
	++updateCycle;
}
//**************************************************************************************

async function graphInsertData(channel, pin, data) {
	const label = createChannelLabel(channel, pin);
	let dataset = lineGraph.data.datasets.find(elem => (elem.label === label));
	
	if (dataset === undefined) {									//channel not existed/had removed
		const axisID = 'y' + channel + pin;
		
		dataset = {
			label: label,
			data: [],
			order: channel,
			yAxisID: axisID
		};
		lineGraph.data.datasets.push(dataset);
		
		lineGraph.options.scales[axisID] = {						//add y-axis for this dataset
			grid: {
				display: false
			},
			max: 1,
			min: 0,
			offset: true,
			stack: 'channels',
			stepSize: 0.5,
			title: {
				display: true,
				text: label
			},
			type: 'linear'
		};
		
		console.log(`graphInsertData: create dataset channel:${channel} pin:${pin} label:${label}`);
	}
	
	const curData = dataset.data;
	curData.push(data);
}

/** Resizes canvas container based on graph tick count. Usually called after graph data has changed or window
 *  has been resized. */
function graphResize() {
	//heights are in 'vh', widths in pixels. canvas min width follows scroller width, max unlimited.
	const MAX_CANVAS_HEIGHT = 40, MIN_CANVAS_HEIGHT = 10, MIN_CANVAS_WIDTH = scrollerWidth;
	const CHANNEL_ROW_HEIGHT = 10, TICK_GAP_WIDTH = 20;

	let height = lineGraph.data.datasets.length * CHANNEL_ROW_HEIGHT;
	height = Math.min(Math.max(height, MIN_CANVAS_HEIGHT), MAX_CANVAS_HEIGHT);
	
	let width = tickCount * TICK_GAP_WIDTH * zoom;
	width = Math.max(width, MIN_CANVAS_WIDTH);
	
	postMessage({
		'action': 'resize',
		'data': {
			'height': height,
			'width': width
		}
	});
}

/** Resizes graph canvas itself after container has been updated since container height is in 'vh' instead of
 *  pixels.
 *  @param {number} height Container height, in pixels.
 *  @param {number} width Container width, in pixels. */
function graphResizeCanvas(height, width) {
	lineGraph.canvas.height = height;
	lineGraph.canvas.width = width;
	lineGraph.resize();
	lineGraph.update('none');
}

/** Updates tick count range used across datasets. Usually called after graph data has changed. */
function graphUpdateTickCount() {
	let maxTicks = 0, minTicks = Number.MAX_SAFE_INTEGER;
	
	for (let dataset of lineGraph.data.datasets) {					//find min/max ticks
		if (dataset.data.length) {
			const first = dataset.data[0].x, last = dataset.data[dataset.data.length - 1].x;
			maxTicks = Math.max(maxTicks, last);
			minTicks = Math.min(minTicks, first);
		}
	}
	tickCount = (maxTicks > minTicks) ? maxTicks - minTicks : 0;	//possible if graph got cleared
}

/** Updates graph zoom factor.
 * @param {number} delta Value to be added to current zoom. Usually +1 or -1. */
function graphZoom(delta) {
	zoom = Math.min(Math.max(zoom + delta, 1), 10);					//clamp zoom to 1 <= x <= 10
	graphResize();
}

function setup(canvas, dataUrl) {
	lineGraph = new Chart(
		canvas, {
			type: 'line',
			data: {
				datasets: []
			},
			options: {
				animation: false,
				elements: {
					line: {
						borderColor: 'red',
						borderWidth: 5,
						stepped: true
					},
					point: {
						radius: 0
					}
				},
				maintainAspectRatio: false,
				parsing: false,
				scales: {
					x: {
						grid: {
							color: 'grey'
						},
						ticks: {
							stepSize: 1
						},
						type: 'linear'
					}
				},
				spanGaps: true
			},
			plugins: plugins
		}
	);
	
	workerData = new Worker(new URL('LineGraphData.js', import.meta.url), {'type': 'module'});
	workerData.onmessage = evt => {
		switch (evt.data.action) {
			case 'data':
				graphAddData(evt.data.data);
				break;
			case 'setup':
				console.log(`Graph data worker ${evt.data.data ? 'successfully' : 'failed to'} set up.`);
				break;
			default:
				console.log(`Graph data worker sent unknown action '${evt.data.action}'.`);
				break;
		}
	};
	workerData.postMessage({
		'action': 'setup',
		'data': dataUrl
	});
	
	postMessage({
		'action': 'setup',
		'data': true
	});
}

function shutdown() {
	if (workerData) {
		workerData.postMessage({
			'action': 'shutdown'
		});
	}
	
	close();
}

onmessage = evt => {
	switch (evt.data.action) {
		case 'resizeContainer':
			const dim = evt.data.data;
			graphResizeCanvas(dim.height, dim.width);
			break;
		case 'resizeScroller':
			scrollerWidth = evt.data.data;
			graphResize();
			break;
		case 'capture':
			capturing = evt.data.data;
			break;
		case 'clear':
			graphClear();
			break;
		case 'zoom':
			graphZoom(evt.data.data);
			break;
		case 'updateChannelConfig':
			console.log('LineGraphUi updateChannelConfig: ', channelConfig, ' -> ', evt.data.data);
			channelConfig = evt.data.data;
			break;
		case 'setup':
			setup(evt.data.data.canvas, evt.data.data.dataUrl);
			break;
		case 'shutdown':
			shutdown();
			break;
		default:
			console.log(`Graph UI got unknown action '${evt.data.action}'.`);
			break;
	}
};
