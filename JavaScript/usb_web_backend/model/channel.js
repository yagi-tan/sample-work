'use strict';

const utils = await import('../utils.js');
const wasmIntf = await import('../wasm_cpp/interface.js');

const ws = await import('ws');

const fs = await import('node:fs/promises');
const path = await import('node:path');
import {argv} from 'node:process';
const timers = await import('node:timers');

const CH_DATA_SIZE = wasmIntf.ChData.SIZE_IN_BYTES * 16,
	CH_SAMPLE_SIZE = wasmIntf.ChSample.SIZE_IN_BYTES * wasmIntf.ChData.SAMPLE_PER_READING;
const USE_DUMMY_DATA = argv.includes('useDummyData');

if (USE_DUMMY_DATA) {
	console.warn("Channel will use dummy data.");
}
else {
	console.log("Channel will get data from cdev.");
}

class Channel {
	#buf;
	#cfg;
	#id;
	#timer;
	#timerFd;
	#wasmMemData;
	#wasmMemSmp;
	
	/** Constructor.
	 * @param {number} id Channel ID. Valid value is 0-14. */
	constructor(id) {
		this.#buf = null;
		this.#cfg = {pinbase: 0, pincount: 0, rate: 0};
		this.#id = id;
		this.#timer = null;
		this.#timerFd = null;
		this.#wasmMemData = null;
		this.#wasmMemSmp = null;
		
		console.log(`Channel ${id} created.`);
	}
	
	/** Getter for channel config object. */
	get cfg() {
		return this.#cfg;
	}
	
	/** Getter for channel ID. */
	get id() {
		return this.#id;
	}
	
	/** Getter for channel sampling operation status based on timer existence.
	 * @return {boolean} Operation status. */
	get running() {
		return (this.#timer !== null);
	}
	
	/** Sets channel config. Will stop sampling before channel reconfiguration is done.
	 * @param {ChConfig} cfg Config object, possibly from REST API request.
	 * @param {string} pathSysfs sysfs-based configuration directory path. Must be valid.
	 * @return {Object|undefined} Error instance if there's error. */
	async setConfig(cfg, pathSysfs) {
		if (!Number.isInteger(cfg.pinbase) || (cfg.pinbase < 0) || (cfg.pinbase >= 26)) {
			return new RangeError(`Invalid pin base as channel ${this.#id} config.`);
		}
		if (!Number.isInteger(cfg.pincount) || ([1, 2, 4, 8].indexOf(cfg.pincount) === -1)) {
			return new RangeError(`Invalid pin count as channel ${this.#id} config.`);
		}
		if (!Number.isInteger(cfg.rate) || (cfg.rate <= 0) || (cfg.rate > 125000000)) {
			return new RangeError(`Invalid rate as channel ${this.#id} config.`);
		}
		
		if ((this.#cfg.pinbase === cfg.pinbase) && (this.#cfg.pincount === cfg.pincount) &&
		(this.#cfg.rate === cfg.rate)) {
			console.log(`New channel ${this.#id} config same as current.`);
			return;
		}
		
		await this.stop();
		
		const cfgTmp = new wasmIntf.ChConfig(this.#id, cfg.pinbase, cfg.pincount, cfg.rate);
		
		if (USE_DUMMY_DATA && !wasmIntf.setConfig(cfgTmp)) {
			return new Error(`Error setting channel ${this.#id} dummy data generator config.`);
		}
		
		const err = await utils.writeConfig(path.join(pathSysfs, `ch${this.#id}`),
			`${cfg.pinbase} ${cfg.pincount} ${cfg.rate}`);
		if (err instanceof Error) {
			return err;
		}
		
		this.#cfg = cfgTmp;
	}
	
	/** Update channel config directly from sysfs attribute.
	 * @param {string} pathSysfs sysfs-based configuration directory path. Must be valid.
	 * @return {Object|undefined} Error instance if there's error. */
	async updateConfig(pathSysfs) {
		const data = await fs.readFile(path.join(pathSysfs, `ch${this.#id}`), 'ascii')
			.catch((err) => { return err; });
		if (data instanceof Error) {
			return data;
		}
		
		const vals = data.split(' ').map(val => Number.parseInt(val));
		if ((vals.length !== 3) || (vals.map(val => isNaN(val)).indexOf(true) !== -1)) {
			return new Error(`Channel ${this.#id} sysfs file has unexpected content: ${data}`);
		}
		
		return await this.setConfig({pinbase: vals[0], pincount: vals[1], rate: vals[2]}, pathSysfs);
	}
	
	/** Starts sampling operation.
	 * @param {CdevPath} pathCdev Path object. Must be valid.
	 * @param {object} serverWs WebSocket server instance to broadcast sampling data. Must be valid. */
	async start(pathCdev, serverWs) {
		if (this.running) {											//already running
			return;
		}
		
		//setting up channel data/sample memory and interpreter
		//******************************************************************************
		if (!this.#wasmMemData) {
			this.#wasmMemData = wasmIntf.allocMem(CH_DATA_SIZE);
			if (!this.#wasmMemData) {
				return new Error(`Channel ${this.#id} data memory alloc error.`);
			}
		}
		if (!this.#wasmMemSmp) {
			this.#wasmMemSmp = wasmIntf.allocMem(CH_SAMPLE_SIZE);
			if (!this.#wasmMemSmp) {
				return new Error(`Channel ${this.#id} sample memory alloc error.`);
			}
		}
		
		if (!wasmIntf.resetProc(this.#id)) {
			return new Error(` Error resetting channel ${this.#id} channel interpreter.`);
		}
		//******************************************************************************
		
		if (!USE_DUMMY_DATA) {
			if (!this.#buf) {
				this.#buf = new Uint8Array(CH_DATA_SIZE);
			}
			
			//preparing cdev file operation
			//**************************************************************************
			const cdev = path.join(pathCdev.path, `${pathCdev.major}_${pathCdev.minor + this.#id}`);

			const stat = await fs.stat(cdev).catch(err => { return err; });
			if (stat instanceof Error) {
				return stat;
			}
			if (!stat.isCharacterDevice()) {
				return new Error(`'${cdev}' not a character device.`);
			}

			const err = await fs.access(cdev, fs.constants.R_OK).catch(err => { return err; });
			if (err instanceof Error) {
				return err;
			}

			this.#timerFd = await fs.open(cdev, 'r').catch(err => { return err; });
			if (this.#timerFd instanceof Error) {
				const err = this.#timerFd;
				this.#timerFd = null;
				return err;
			}
			//**************************************************************************
		}
		
		this.#timer = timers.setInterval(async () => {
			let dataQt;
			
			if (USE_DUMMY_DATA) {
				dataQt = wasmIntf.getData(this.#id, this.#wasmMemData, CH_DATA_SIZE);
				
				if (dataQt < 0) {
					console.warn(`Channel ${this.#id} error getting channel dummy data.`);
				}
			}
			else {
				const data = await this.#timerFd.read(this.#buf, 0, CH_DATA_SIZE, 0)
					.catch(err => { return err; });
				
				if (data instanceof Error) {
					console.warn(`Channel ${this.#id} sampling error: ${data}`);
					dataQt = 0;
				}
				else {
					dataQt = data.bytesRead;
					wasmIntf.copyToMem(this.#buf.slice(0, dataQt), this.#wasmMemData);
				}
			}
			
			if (dataQt) {
				const chSmps = wasmIntf.procData(this.#id, this.#wasmMemData, dataQt, this.#wasmMemSmp,
					CH_SAMPLE_SIZE);
				const strs = [];
				
				chSmps.forEach(elem => {
					strs.push(`${elem.ts}-${elem.level}`);
				});
				if (strs.length) {
					const str = `${this.#id}:${strs.join(',')}`;
				
					serverWs.clients.forEach(client => {				//broadcast data
						if (client.readyState === ws.WebSocket.OPEN) {
							client.send(str);
						}
					});
				}
			}
		}, 3000);
		
		console.log(`Channel ${this.#id} started.`);
	}
	
	/** Stops sampling operation, usually before removing the instance. */
	async stop() {
		if (!this.running) {										//already stopped
			return;
		}
		
		timers.clearInterval(this.#timer);
		this.#timer = null;
		
		if (this.#timerFd) {
			await this.#timerFd.close().catch(err => { return err; });
			this.#timerFd = null;
		}
		
		this.#buf = null;
		if (this.#wasmMemData) {
			wasmIntf.freeMem(this.#wasmMemData);
			this.#wasmMemData = null;
		}
		if (this.#wasmMemSmp) {
			wasmIntf.freeMem(this.#wasmMemSmp);
			this.#wasmMemSmp = null;
		}
		
		console.log(`Channel ${this.#id} stopped.`);
	}
}

export default Channel;