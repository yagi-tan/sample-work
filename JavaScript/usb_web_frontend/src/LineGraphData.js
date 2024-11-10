'use strict';

let socket;

/** Processes raw data into proper graph data.
 * @param {string} data Raw signal data from backend. */
function procData(data) {
	if (typeof(data) === 'string') {
		const [channel, seq] = data.split(':', 2);
		const pts = seq.split(',');
		const levels = new Uint8Array(pts.length), tss = new BigUint64Array(pts.length);
		
		pts.forEach((elem, idx) => {
			const [ts, level] = elem.split('-', 2);
			levels[idx] = Number(level);
			tss[idx] = BigInt(ts);
		});
		
		postMessage({
			'action': 'data',
			'data': {
				'channel': channel,
				'levels': levels,
				'tss': tss
			}
		}, [levels.buffer, tss.buffer]);
	}
	else {
		console.error(`Got unsupported graph data type '${typeof(data)}'.`);
	}
}

function setup(url) {
	let result;
	
	try {
		//create WebSocket connection
		socket = new WebSocket(url);
		socket.onclose = function(evt) {
			console.log("Graph data WebSocket disconnected.");
		};
		socket.onerror = function(evt) {
			console.error("Graph data WebSocket has error: ", evt);
		};
		socket.onmessage = function(evt) {
			console.debug("Graph data WebSocket got message: ", evt.data);
			procData(evt.data);
		};
		socket.onopen = function(evt) {
			console.log("Graph data WebSocket connected.");
		};
		
		result = true;
	}
	catch (e) {
		console.error("Graph data WebSocket connection failed: ", e);
		result = false;
	}
	
	postMessage({
		'action': 'setup',
		'data': result
	});
}

function shutdown() {
	if (socket) {
		socket.close();
	}
	close();
}

onmessage = evt => {
	switch (evt.data.action) {
		case 'setup':
			setup(evt.data.data);
			break;
		case 'shutdown':
			shutdown();
			break;
		default:
			console.log(`Graph data got unknown action '${evt.data.action}'.`);
			break;
	}
};
