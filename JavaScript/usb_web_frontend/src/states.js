'use strict';

/** Getter for state object.
 * @param {object} states State object.
 * @param {String} id State entry ID.
 * @param {String} key State key.
 * @return {number|String|undefined} Specific channel state value, or undefined if channel and/or key not
 *									 exists. */
export function getStates(states, id, key) {
	if (id in states) {
		if (key in states[id]) {
			return states[id][key];
		}
		else {
			console.error(`Key '${key}' missing from state entry ${id}.`);
		}
	}
	else {
		console.error(`State entry ${id} missing from states:`, states);
	}
}

/** Setter for channel states.
 * @param {object} states State object.
 * @param {String} id State entry ID.
 * @param {String} key State entry key.
 * @param {number|String} value Channel state value. */
export function setStates(states, id, key, value) {
	let state;
	
	if (id in states) {
		state = states[id];
	}
	else {
		state = {};
		states[id] = state;
		
		console.log(`State entry ${id} added into states.`);
	}
	
	if (!(key in state)) {
		console.log(`Key '${key}' added to channel ${id} states.`);
	}
	
	state[key] = value;
}