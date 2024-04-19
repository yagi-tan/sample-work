'use strict';

/** Character device path object. To be used as &lt;path&gt;/&lt;major&gt;_&lt;minor + channel ID&gt;.
 * @typedef {Object} CdevPath
 * @property {string} path Directory path.
 * @property {number} major Device major ID.
 * @property {number} minor Device base minor ID, to be added to channel ID. */

import Channel from './channel.js';
const utils = await import('../utils.js');

const fs = await import('node:fs/promises');
const path = await import('node:path');

class LogicAnalyser {
	serverWs;
	
	#channels;
	#pathCdev;
	#pathSysfs;
	
	/** Constructor. */
	constructor() {
		console.log('Logic analyser object created.');
		
		this.serverWs = null;
		
		this.#channels = [];
		this.#pathCdev = {path: null, major: NaN, minor: NaN};
		this.#pathSysfs = null;
	}
	
	/** Getter for logic analyser channel count. */
	get channelCount() {
		return this.#channels.length;
	}
	
	/** Getter for character device directory path object. Fields may be null. */
	get pathCdev() {
		return this.#pathCdev;
	}
	
	/** Getter for sysfs-based configuration directory path. May be null. */
	get pathSysfs() {
		return this.#pathSysfs;
	}
	
	/** Get logic analyser channel configuration object.
	 * @param {number|string} chId Channel ID. Used directly as array index though.
	 * @return {Object} Channel config object or error instance if target channel not found. */
	getChannelConfig(chId) {
		if (typeof(chId) === 'string') {
			chId = Number.parseInt(chId);
		}
		if (!Number.isInteger(chId)) {
			return new TypeError("'chId' not valid integer.");
		}
		if (chId >= this.#channels.length) {
			return new RangeError(`Channel with ID ${chId} not found in list.`);
		}
		
		return this.#channels[chId].cfg;
	}
	
	/** Restarts currently running channels, possibly due to cdev path reconfiguration.
	 * @return {Object|undefined} Error object if there's error in starting channel(s). */
	async restartChannels() {
		if (this.serverWs === null) {
			return new Error("'serverWs' object not set yet.");
		}
		
		const channels = [];
		
		for (const channel of this.#channels) {
			if (channel.running) {
				channels.push((async () => {
					await channel.stop();
					return await channel.start(this.#pathCdev, this.serverWs);
				})());
			}
		}
		
		return await utils.waitPromises(channels, 'Error restarting channels.');
	}
	
	/** Sets logic analyser channel configuration.
	 * @param {number|string} chId Channel ID. Used directly as array index though.
	 * @param {Object} cfg Channel configuration object.
	 * @return {Object|undefined} Error instance if there's error. */
	async setChannelConfig(chId, cfg) {
		if (typeof(chId) === 'string') {
			chId = Number.parseInt(chId);
		}
		if (!Number.isInteger(chId)) {
			return new TypeError("'chId' not valid integer.");
		}
		if (chId >= this.#channels.length) {
			return new RangeError(`Channel with ID ${chId} not found in list.`);
		}
		
		if (this.#pathSysfs === null) {
			return new Error("'sysfs' path not set yet.");
		}
		
		return await this.#channels[chId].setConfig(cfg, this.#pathSysfs);
	}
	
	/** Setter for logic analyser channel count.
	 * @param {number|string} count Channel count.
	 * @return {Object|undefined} Error instance if there's error. */
	async setChannelCount(count) {
		if (typeof(count) === 'string') {
			count = Number.parseInt(count);
		}
		if (!Number.isInteger(count)) {
			return new TypeError("'count' not valid integer.");
		}
		if ((count < 0) || (count > 15)) {
			return new RangeError("'count' must be 0 <= x <= 15.");
		}
		
		if (this.#channels.length === count) {						//no change needed
			return;
		}
		
		const err = await this.updateChannelCount(count);
		if (err instanceof Error) {
			return err;
		}
	}
	
	/** Get logic analyser channel running status.
	 * @param {number|string} chId Channel ID. Used directly as array index though.
	 * @return {Object|boolean} Channel status or error instance if target channel not found. */
	getChannelStatus(chId) {
		if (typeof(chId) === 'string') {
			chId = Number.parseInt(chId);
		}
		if (!Number.isInteger(chId)) {
			return new TypeError("'chId' not valid integer.");
		}
		if (chId >= this.#channels.length) {
			return new RangeError(`Channel with ID ${chId} not found in list.`);
		}
		
		return this.#channels[chId].running;
	}
	
	/** Sets logic analyser channel running status (to start/stop sampling).
	 * @param {number|string} chId Channel ID. Used directly as array index though.
	 * @param {boolean|string} status Channel status. Either 'true', 'false', 0 or 1 is acceptable.
	 * @return {Object|undefined} Error instance if there's error. */
	async setChannelStatus(chId, status) {
		if (typeof(chId) === 'string') {
			chId = Number.parseInt(chId);
		}
		if (!Number.isInteger(chId)) {
			return new TypeError("'chId' not valid integer.");
		}
		if (chId >= this.#channels.length) {
			return new RangeError(`Channel with ID ${chId} not found in list.`);
		}
		
		if (typeof(status) === 'string') {
			switch (status.toLowerCase()) {
				case 'true':
				case '1':
					status = true;
					break;
				case 'false':
				case '0':
					status = false;
					break;
				default:
					return new TypeError("'status' not valid boolean string.");
			}
		}
		else if (Number.isInteger(status)) {
			status = Boolean(status);								//regardless of 0, 1 or etc.
		}
		else if (typeof(status) !== 'boolean') {
			return new TypeError("'status' not valid boolean value.");
		}
		
		if (status) {
			if (this.serverWs === null) {
				return new Error("'serverWs' object not set yet.");
			}
			if (this.#pathCdev.path === null) {
				return new Error('cdev path not set yet.');
			}
			return await this.#channels[chId].start(this.#pathCdev, this.serverWs);
		}
		else {
			return await this.#channels[chId].stop();
		}
	}
	
	/** Setter for directory containing character device files.
	 * @param {CdevPath} pathCdev Path object, possibly from REST API request. Must have expected properties.
	 * @return {Object|undefined} Error instance if there's error. */
	async setPathCdev(pathCdev) {
		if (typeof(pathCdev.path) !== 'string') {
			return new TypeError('Invalid cdev path config.');
		}
		if (!Number.isInteger(pathCdev.major)) {
			return new TypeError('Invalid cdev major config.');
		}
		if (!Number.isInteger(pathCdev.minor)) {
			return new TypeError('Invalid cdev minor config.');
		}
		
		if ((pathCdev.path === this.#pathCdev.path) && (pathCdev.major === this.#pathCdev.major) &&
		(pathCdev.minor === this.#pathCdev.minor)) {
			console.log("New cdev path same as current.");
			return;
		}
		
		const stat = await fs.stat(pathCdev.path).catch((err) => { return err; });
		if (stat instanceof Error) {
			return stat;
		}
		if (!stat.isDirectory()) {
			return new Error('Not a directory.');
		}
		
		this.#pathCdev = pathCdev;
		return await this.restartChannels();
	}
	
	/** Setter for sysfs-based configuration directory. Should contain 'chcount' file from the start.
	 * @param {string} pathSysfs Directory path.
	 * @return {Object|undefined} Error instance if there's error. */
	async setPathSysfs(pathSysfs) {
		const stat = await fs.stat(pathSysfs).catch((err) => { return err; } );
		if (stat instanceof Error) {
			return stat;
		}
		if (!stat.isDirectory()) {
			return new Error('Not a directory.');
		}
		
		//read current 'chcount' value to update own channel count
		//******************************************************************************
		const data = await fs.readFile(path.join(pathSysfs, 'chcount'), 'ascii')
			.catch((err) => { return err; });
		if (data instanceof Error) {
			return data;
		}
		
		const count = Number.parseInt(data);
		if (isNaN(count)) {
			return new Error(`'chcount' file content '${data}' is not a valid number.`);
		}
		//******************************************************************************
		
		this.#pathSysfs = pathSysfs;
		const err = await this.updateChannelCount(count);
		if (err instanceof Error) {
			this.#pathSysfs = null;									//revert value
			return err;
		}
	}
	
	/** Stops all channels' operation, possibly due to program exiting.
	 * @return {Object|undefined} Aggregate error object if there's error in stopping channel(s). */
	async stopChannels() {
		const channels = [];
		
		for (const channel of this.#channels) {
			if (channel.running) {
				channels.push(channel.stop());
			}
		}
		
		return await utils.waitPromises(channels, 'Error stopping channels.');
	}
	
	/** Updates logic analyser channel count by adding/removing channel objects in array.
	 * @param {number} count New channel count.
	 * @return {Object|undefined} Error instance if there's error. */
	async updateChannelCount(count) {
		if (this.#pathSysfs === null) {
			return new Error("'sysfs' path not set yet.");
		}
		
		if (this.#channels.length > count) {						//remove from end of list
			//need to stop/remove channel object(s) first before updating 'chcount' file
			await Promise.allSettled(this.#channels.splice(count).map((channel) => channel.stop()));
			
			const err = await utils.writeConfig(path.join(this.#pathSysfs, 'chcount'), count.toString());
			if (err instanceof Error) {
				return err;
			}
		}
		else if (this.#channels.length < count) {					//need adding at end of list
			//need to update 'chcount' file first before adding/start channel object(s)
			const err = await utils.writeConfig(path.join(this.#pathSysfs, 'chcount'), count.toString());
			if (err instanceof Error) {
				return err;
			}
			
			const updates = [];										//await config updates all at once
			for (let i = this.#channels.length; i < count; ++i) {
				const channel = new Channel(i);
				this.#channels.push(channel);
				updates.push(channel.updateConfig(this.#pathSysfs));
			}
			
			for (const result of await Promise.allSettled(updates)) {
				if (result.status === 'fulfilled') {
					if (result.value instanceof Error) {
						console.warn(result.value.message);
					}
				}
				else {
					console.warn(result.reason);
				}
			}
		}
	}
}

const logicAnalyser = new LogicAnalyser();
export default logicAnalyser;