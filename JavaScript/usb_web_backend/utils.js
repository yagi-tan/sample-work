'use strict';

const fs = await import('node:fs/promises');

/** Helper function to validate JSON payload content extracted into 'req.body'.
 * @param {string[]} keys JSON keys expected to be within payload.
 * @param {Object} req Express request object.
 * @return {Object|undefined} Error instance if there's error. */
async function validatePayload(keys, req) {
	if (typeof(req.body) !== 'object') {
		return new Error("Missing JSON payload?");
	}
	
	for (let key of keys) {
		if (!(key in req.body)) {
			return new Error(`Expecting '${key}' field in JSON payload.`);
		}
	}
}

/** Helper function to execute promises in bulk and gather errors.
 * @param {Promise[]} promises Array of promises.
 * @param {string} msg Error message field if needed.
 * @return {Object|undefined} AggregateError instance if there's error. */
async function waitPromises(promises, msg) {
	const results = [];
	
	for (const result of await Promise.allSettled(promises)) {
		if (result.status === 'fulfilled') {
			if (result.value instanceof Error) {
				results.push(result.value);
			}
		}
		else {
			results.push(result.reason);
		}
	}

	if (results.length) {
		return new AggregateError(results, msg);
	}
}

/** Helper function to write configuration data to configuration file.
 * @param {string} path Path to configuration file.
 * @param {string} data Configuration data to be written to.
 * @return {Object|undefined} Error instance if there's error. */
async function writeConfig(path, data) {
	let err;
	//require file to already exist instead of newly created due to write
	err = await fs.access(path, fs.constants.W_OK).catch(err => { return err; });
	if (err instanceof Error) {
		return err;
	}

	err = await fs.writeFile(path, data, 'ascii').catch(err => { return err; });
	if (err instanceof Error) {
		return err;
	}
}

export { validatePayload, waitPromises, writeConfig };