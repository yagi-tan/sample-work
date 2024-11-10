'use strict';

const mod = await ((await import('./channelData.js')).default());

/** Channel configuration object.
 * @typedef {Object} ChConfig
 * @property {number} id Channel ID. Valid value is 0-14.
 * @property {number} pinbase Pin base index. 0 <= x <= 25 (Pico has 26 GPIO).
 * @property {number} pincount Pin count. Value must be either 1, 2, 4 or 8.
 * @property {number} rate Sampling rate, in Hz. 1 <= x <= 125,000,000 (default Pico system clock). */
export class ChConfig {
	static SIZE_IN_BYTES = 7;
	
	#id;
	#pinbase;
	#pincount;
	#rate;
	
	constructor(id, pinbase, pincount, rate) {
		this.#id = id;
		this.#pinbase = pinbase;
		this.#pincount = pincount;
		this.#rate = rate;
	}
	
	/** Getter for channel ID. */
	get id() {
		return this.#id;
	}
	
	/** Getter for channel pin base. */
	get pinbase() {
		return this.#pinbase;
	}
	
	/** Getter for channel pin count. */
	get pincount() {
		return this.#pincount;
	}
	
	/** Getter for channel sampling rate. */
	get rate() {
		return this.#rate;
	}
	
	/** Gets current config to fill raw buffer.
	 * @param {Object} dv DataView object for raw buffer. */
	getToRaw(dv) {
		dv.setUint8(0, this.#id);
		dv.setUint8(1, this.#pinbase);
		dv.setUint8(2, this.#pincount);
		dv.setUint32(3, this.#rate, true);
	}
	
	/** Sets current config from raw buffer.
	 * @param {Object} dv DataView object for raw buffer. */
	setFromRaw(dv) {
		this.#id = dv.getUint8(0);
		this.#pinbase = dv.getUint8(1);
		this.#pincount = dv.getUint8(2);
		this.#rate = dv.getUint32(3, true);
	}
}

export class ChData {
	static SAMPLE_PER_READING = 4;
	static SIZE_IN_BYTES = 8;
	
	#data;
	#tag;
	#valid;
	
	/** Constructor. */
	constructor() {
		this.#valid = 0b0000;
		this.#tag = 0;
		this.#data = new Uint8Array(4);
	}
	
	/** Getter for sampling data. Array length is 4. Usability depends on valid bits. */
	get data() {
		return this.#data;
	}
	
	/** Getter for sampling tag. Value range is 0 <= x <= (2^28 - 1). May overflow. */
	get tag() {
		return this.#tag;
	}
	
	/** Getter for data valid bits. Only rightmost 4 bits are usable. */
	get valid() {
		return this.#valid;
	}
	
	/** Lists out data bits as string. Invalid data will be marked as 'X'.
	 * @return {string} Data bits. */
	listData() {
		let result = '';
		
		for (let idx = 0; idx < ChData.SAMPLE_PER_READING; ++idx) {
			if (idx) {
				result += ' ';
			}
			
			if (this.#valid & (0b1000 >>> idx)) {
				const sample = this.#data[idx];
				let mask = 0x80;
				
				for (let bit = 0; bit < 8; ++bit) {
					result += ((sample & mask) ? '1' : '0');
					mask >>>= 1;
				}
			}
			else {
				result += 'X';
			}
		}
		
		return result;
	}
	
	/** Sets current object from raw buffer.
	 * @param {Object} dv DataView object for raw buffer.
	 * @param {number} offset Raw buffer offset for current data. */
	setFromRaw(dv, offset) {
		const prop = dv.getUint32(offset, true);
		this.#valid = prop & 0x0F;									//rightmost 4 bits
		this.#tag = prop >>> 4;										//leftmost 28 bits
		
		for (let idx = 0; idx < ChData.SAMPLE_PER_READING; ++idx) {
			if (this.#valid & (0b1000 >>> idx)) {
				this.#data[idx] = dv.getUint8(offset + 4 + idx);
			}
		}
	}
}

export class ChSample {
	static SIZE_IN_BYTES = 8;
	
	#level;
	#ts;
	
	/** Constructor. */
	constructor() {
		this.#level = 0x00;
		this.#ts = 0n;
	}
	
	/** Getter for sample level (true or false). */
	get level() {
		return this.#level;
	}
	
	/** Getter for monotonically increasing sample timestamp. */
	get ts() {
		return this.#ts;
	}
	
	/** Sets current object from raw buffer.
	 * @param {Object} dv DataView object for raw buffer.
	 * @param {number} offset Raw buffer offset for current data. */
	setFromRaw(dv, offset) {
		const prop = dv.getBigUint64(offset, true);
		this.#level = BigInt.asUintN(8, prop & 0xFFn);				//rightmost 8 bits
		//leftmost 56 bits via divide by 256 as unsigned shift right is not available for BitInt
		this.#ts = prop / 0x0100n;
	}
}

/** Allocates raw memory for interface system usage.
 * @param {number} size Requested memory size, in bytes.
 * @return {number} Pointer to allocated memory, or 0 if error has occurred. */
export function allocMem(size) {
	return mod._malloc(size);
}

/** Copies entire array content into raw memory.
 * @param {Object} arr Uint8Array object.
 * @param {number} raw Pointer to allocated memory. */
export function copyToMem(arr, raw) {
	mod.HEAPU8.set(arr, raw);
}

/** Shuts down overall interface system. */
export function exitSys() {
	mod.ccall('exitSys', null);
}

/** Frees allocated raw memory.
 * @param {number} raw Pointer to allocated memory. */
export function freeMem(raw) {
	mod._free(raw);
}

/** Gets generator config for specific channel.
 * @param {number} id Channel ID. Valid value is 0-14.
 * @return {ChConfig|undefined} Channel config object or none if there's error. */
export function getConfig(id) {
	const chConfigR = mod._malloc(ChConfig.SIZE_IN_BYTES);

	if (chConfigR) {
		let chConfig = new ChConfig(0, 0, 0, 0);
		
		if (mod.ccall('getConfig', 'boolean', ['number', 'number', 'number'],
		[id, chConfigR, ChConfig.SIZE_IN_BYTES])) {
			const chConfigV = new DataView(mod.HEAPU8.buffer, chConfigR, ChConfig.SIZE_IN_BYTES);
			chConfig.setFromRaw(chConfigV);
		}
		else {
			console.error("Error getting channel generator config.");
			chConfig = undefined;
		}
		
		mod._free(chConfigR);
		
		return chConfig;
	}
	else {
		console.error("Error allocating memory for channel generator config.");
	}
}

/** Generates channel dummy data.
 * @param {number} id Channel ID. Valid value is 0-14.
 * @param {number} raw Pointer to allocated memory for channel data.
 * @param {number} rawSz Size of allocated memory for channel data, in bytes.
 * @return {number} Size of valid data in memory, in bytes, or -1 if there's error. */
export function getData(id, raw, rawSz) {
	return mod.ccall('getData', 'number', ['number', 'number', 'number'], [id, raw, rawSz]);
}

/** Initialises overall interface system.
 * @return {boolean} False if there's error. */
export function initSys() {
	return mod.ccall('initSys', 'boolean');
}

/** Helper function to interpret/convert raw data into ChData objects.
 * @param {number} raw Pointer to allocated memory for channel data.
 * @param {number} dataQt Size of valid data in memory, in bytes.
 * @return {Array} Array containing ChData objects. */
export function interpretRawData(raw, dataQt) {
	const chDataV = new DataView(mod.HEAPU8.buffer, raw, dataQt);
	const readQt = dataQt / ChData.SIZE_IN_BYTES;
	let result = [];
	
	for (let idx = 0; idx < readQt; ++idx) {
		const chData = new ChData();
		chData.setFromRaw(chDataV, ChData.SIZE_IN_BYTES * idx);
		result.push(chData);
	}
	
	return result;
}

/** Processes channel data into channel samples.
 * @param {number} id Channel ID. Valid value is 0-14.
 * @param {number} rawData Pointer to allocated memory for channel data.
 * @param {number} dataQt Size of valid data in channel data memory, in bytes.
 * @param {number} rawSmp Pointer to allocated memory for channel sample.
 * @param {number} rawSmpSz Size of allocated memory for channel sample, in bytes.
 *							Should be >= (ChSample.SIZE_IN_BYTES * ChData.SAMPLE_PER_READING).
 * @return {Array} Array containing ChSample objects. */
export function procData(id, rawData, dataQt, rawSmp, rawSmpSz) {
	const chSmpV = new DataView(mod.HEAPU8.buffer, rawSmp, rawSmpSz);
	const result = [];
	let dataStart = 0, dataEnd = ChData.SIZE_IN_BYTES;
	
	while (dataEnd <= dataQt) {
		const dataSmpQt = mod.ccall('procData', 'number', ['number', 'number', 'number', 'number'],
			[id, rawData + dataStart, rawSmp, rawSmpSz]);
		
		if (dataSmpQt > 0) {
			const smpQt = dataSmpQt / ChSample.SIZE_IN_BYTES;
			
			for (let idx = 0; idx < smpQt; ++idx) {
				const chSmp = new ChSample();
				chSmp.setFromRaw(chSmpV, ChSample.SIZE_IN_BYTES * idx);
				result.push(chSmp);
			}
		}
		else {
			console.warn("Error processing channel data into samples.");
		}
		
		dataStart = dataEnd;
		dataEnd += ChData.SIZE_IN_BYTES;
	}
	
	return result;
}

/** Resets/Initialises channel data processor.
 * @param {number} id Channel ID. Valid value is 0-14.
 * @return {boolean} True if processor is reset successfully. */
export function resetProc(id) {
	return mod.ccall('resetProc', 'boolean', ['number'], [id]);
}

/** Sets generator config for specific channel.
 * @param {ChConfig} cfg Config object, possibly from REST API request.
 * @return {boolean} True if config is set successfully. */
export function setConfig(cfg) {
	const chConfigR = mod._malloc(ChConfig.SIZE_IN_BYTES);
	let result;
	
	if (chConfigR) {
		const chConfigV = new DataView(mod.HEAPU8.buffer, chConfigR, ChConfig.SIZE_IN_BYTES);
		
		cfg.getToRaw(chConfigV);
		result = mod.ccall('setConfig', 'boolean', ['number', 'number'], [chConfigR, ChConfig.SIZE_IN_BYTES]);
		
		mod._free(chConfigR);
	}
	else {
		console.error("Error allocating memory for channel generator config.");
		result = false;
	}
	
	return result;
}
