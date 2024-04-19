'use strict';

const utils = await import('../utils.js');

const ws = await import('ws');

const fs = await import('node:fs/promises');
const path = await import('node:path');
const timers = await import('node:timers');

/** Channel configuration object.
 * @typedef {Object} ChConfig
 * @property {number} pinbase Pin base index. 0 <= x <= 25 (Pico has 26 GPIO).
 * @property {number} pincount Pin count. Value must be either 1, 2, 4 or 8.
 * @property {number} rate Sampling rate, in Hz. 1 <= x <= 125,000,000 (default Pico system clock). */

const BUF_SIZE = 4;

class Channel {
	#buf;
	#cfg;
	#id;
	#timer;
	#timerFd;
	
	/** Constructor.
	 * @param {number} id Channel ID. Valid value is 0-15. */
	constructor(id) {
		this.#buf = new Uint8Array(BUF_SIZE);
		this.#cfg = {pinbase: 0, pincount: 0, rate: 0};
		this.#id = id;
		this.#timer = null;
		this.#timerFd = null;
		
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
		
		const err = await utils.writeConfig(path.join(pathSysfs, `ch${this.#id}`),
			`${cfg.pinbase} ${cfg.pincount} ${cfg.rate}`);
		if (err instanceof Error) {
			return err;
		}
		
		this.#cfg = cfg;
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
		
		//preparing cdev file operation
		//******************************************************************************
		const cdev = path.join(pathCdev.path, `${pathCdev.major}_${pathCdev.minor + this.#id}`);
		
		const stat = await fs.stat(cdev).catch((err) => { return err; });
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
		//******************************************************************************
		
		this.#timer = timers.setInterval(async () => {
			const data = await this.#timerFd.read(this.#buf, 0, BUF_SIZE, 0).catch(err => { return err; });
			if (data instanceof Error) {
				console.warn(`Channel ${this.#id} sampling error: ${data}`);
			}
			else {
				const got = this.#buf.slice(0, data.bytesRead);
				console.log(`Channel ${this.#id} sampling @${Date.now()}: ` + got.toString());
				
				serverWs.clients.forEach(client => {				//broadcast data
					if (client.readyState === ws.WebSocket.OPEN) {
						client.send(got, {binary: true});
					}
				});
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
		await this.#timerFd.close().catch(err => { return err; });
		this.#timerFd = null;
		
		console.log(`Channel ${this.#id} stopped.`);
	}
}

export default Channel;