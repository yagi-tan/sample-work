'use strict';

const wasmIntf = await import('./interface.js');

const CH_TARGET = 1;
const MAX_READING_COUNT = 2;

//read/process some sampling data
async function doData(chDataR, chDataRSz, chSmpR, chSmpRSz) {
	const dataQt = wasmIntf.getData(CH_TARGET, chDataR, chDataRSz);
	
	if (dataQt > 0) {
		const chDatas = wasmIntf.interpretRawData(chDataR, dataQt);
		console.log('Readings:');
		chDatas.forEach((elem, idx) => {
			console.log(`\tReading ${idx}:` +
				`\n\t\ttag:${elem.tag}` +
				`\n\t\tvalid:${elem.valid.toString(2)}` +
				`\n\t\tdata:${elem.listData()}`);
		});

		const chSmps = wasmIntf.procData(CH_TARGET, chDataR, dataQt, chSmpR, chSmpRSz);
		chSmps.forEach((elem, idx) => {
			console.log(`\tSample ${idx}:` +
				`\n\t\tlevel:${elem.level.toString(2)}` +
				`\n\t\tts:${elem.ts}`);
		});
	}
	else if (dataQt < 0) {
		console.error("Error getting channel data.");
	}
	else {
		console.log("Got no channel data.");
	}
}

//read back config
function getConfig() {
	const chConfig = wasmIntf.getConfig(CH_TARGET);

	if (chConfig instanceof wasmIntf.ChConfig) {
		console.log(`Got channel config:` +
			`\n\tid:${chConfig.id}` +
			`\n\tpinbase:${chConfig.pinbase}` +
			`\n\tpincount:${chConfig.pincount}` +
			`\n\trate:${chConfig.rate}`
		);
	}
}

console.log("init sys");
wasmIntf.initSys();

if (wasmIntf.setConfig(new wasmIntf.ChConfig(CH_TARGET, 6, 4, 1000))) {
	getConfig();
	
	if (wasmIntf.resetProc(CH_TARGET)) {
		const CH_DATA_SIZE = wasmIntf.ChData.SIZE_IN_BYTES * MAX_READING_COUNT,
			CH_SAMPLE_SIZE = wasmIntf.ChSample.SIZE_IN_BYTES * wasmIntf.ChData.SAMPLE_PER_READING;
		const chDataR = wasmIntf.allocMem(CH_DATA_SIZE), chSmpR = wasmIntf.allocMem(CH_SAMPLE_SIZE);
		
		if (chDataR && chSmpR) {
			const fxs = [];
			
			for (let idx = 0; idx < 3; ++idx) {
				fxs.push(new Promise(resolve => setTimeout(() => {
					doData(chDataR, CH_DATA_SIZE, chSmpR, CH_SAMPLE_SIZE);
					resolve();
				}, 500 * idx)));
			}
			Promise.allSettled(fxs).finally(() => {
				wasmIntf.freeMem(chDataR);
				wasmIntf.freeMem(chSmpR);
			});
		}
		else {
			console.error("Error allocating memory for channel data/sample.");
			result = false;
		}
	}
	else {
		console.error("Error reset channel interpreter.");
	}
}
else {
	console.error("Error setting channel config.");
}

setTimeout(() => {
	wasmIntf.exitSys();
}, 3000);
