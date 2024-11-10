<script setup>
'use strict';

import {onMounted, shallowRef} from 'vue';

const emits = defineEmits({
	//Used when parent needs value updated from this component. Function shall accept 2 parameters: config URL
	//and new value of type depending on input element type.
	//	PATCH: a primitive value, usually number or string.
	//	POST: an object with format {<target>: <value>, ...}.
	updateValue: (url, value) => {
		if ((url !== null) && (value !== null)) {
			return true;
		}
		console.error(`Invalid 'updateValue' event parameter to be emitted: ${url} -> ${value}`);
		return false;
	}
});
const mappings = shallowRef();
const props = defineProps({
	//Config name.
	name: {
		required: true,
		type: String,
		validator(value) {
			return value.length;									//need non-empty string
		}
	},
	//HTTP method used to set this config. Either 'PATCH' or 'POST' is accepted. Data and corresponding type
	//will be gathered from <input> element(s).
	//	PATCH: string data will be appended to endpoint, such as "${url}/${data}".
	//	POST: object data will be sent as JSON object directly at endpoint.
	setMethod: {
		required: true,
		type: String,
		validator(value) {
			return ['PATCH', 'POST'].includes(value);
		}
	},
	//Config endpoint.
	url: {
		required: true,
		type: String,
		validator(value) {
			return value.length;									//need non-empty string
		}
	}
});
let inputs;

function btnGet() {
	fetch(props.url)
		.then(async resp => {
			if (!resp.ok) {
				throw new Error((await resp.json()).msg);
			}
			
			const ct = resp.headers.get('Content-Type');
			
			if (ct === null) {
				return resp.text();
			}
			else if (ct.includes('application/json')) {
				return resp.json();
			}
			else if (ct.includes('text/plain')) {
				return resp.text();
			}
			
			throw new Error(`Got unhandled content type '${ct}'.`);
		})
		.then(data => {
			switch (typeof(data)) {
				case 'object':
					for (const [target, value] of Object.entries(data)) {
						inputSet(target, value);
					}
					break;
				case 'string':
					inputSet(null, data);
					break;
				default:
					throw new Error(`Got unhandled data type '${typeof(data)}'.`);
			}
			
			doReporting();
		})
		.catch(e => {
			console.error(`Error get '${props.url}': ${e.message}`);
		});
}

function btnSet() {
	let options = {};
	let resource = props.url;
	let value;
	
	switch (props.setMethod) {
		case 'PATCH':
			value = inputGet(null);
			
			if (value === null) {
				console.error(`Error set '${props.url}': Invalid value.`);
				return;
			}
			if (!resource.endsWith('/')) {
				resource += '/';
			}
			resource += value;
			break;
		case 'POST':
			value = {};
			
			for (const target in inputs) {
				const tmp = inputGet(target);
				
				if (tmp === null) {
					console.error(`Error set '${props.url}': Invalid '${target}' value.`);
					return;
				}
				value[target] = tmp;
			}
			
			options.body = JSON.stringify(value);
			options.headers = {
				'Content-Type': 'application/json'
			};
			break;
	}
	options.method = props.setMethod;
	
	fetch(resource, options)
		.then(async resp => {
			if (!resp.ok) {
				throw new Error((await resp.json()).msg);
			}
			
			doReporting();
		})
		.catch(e => {
			console.error(`Error set '${props.url}' to '${JSON.stringify(value)}': ${e.message}`);
		});
}

/** Helper function to handle reporting as event. Depends on 'inputs' to have latest value. */
function doReporting() {
	switch (props.setMethod) {
		case 'PATCH':
			const value = inputGet(null);
			if (value !== null) {
				emits('updateValue', props.url, value);
			}
			break;
		case 'POST':
			let values = null;

			for (const target of Object.keys(inputs)) {
				const value = inputGet(target);
				if (value !== null) {
					if (values === null) {							//not yet initialised
						values = {};
					}
					values[target] = value;
				}
			}
			if (values !== null) {
				emits('updateValue', props.url, values);
			}

			break;
	}
}

/** Retrieves value from corresponding <input> element, converted as per element type.
 * @param {string} target Element 'target' attribute value. Ignored if `props.setMethod === 'PATCH'`.
 * @return {*} Current input value, or null if invalid target/value or not set yet. */
function inputGet(target) {
	let result = null, elem;
	
	switch (props.setMethod) {
		case 'PATCH':
			elem = inputs;
			break;
		case 'POST':
			elem = inputs[target];
			break;
	}
	
	if (elem) {
		switch (elem.type) {
			case 'text':											//used as-is
				result = (elem.value === getTypeDefaultValue('string')) ? null : elem.value;
				break;
			case 'number':
				result = Number.parseInt(elem.value);
				if (isNaN(result)) {
					console.error(`${props.url} '${target}' UI '${elem.value}' is not a number.`);
					result = null;
				}
				else if (result === getTypeDefaultValue('number')) {
					result = null;
				}
				break;
			case 'checkbox':
				result = elem.checked;
				break;
			default:
				console.error(`${props.url} '${target}' UI type '${elem.type}' not handled yet.`);
				result = null;
		}
	}
	else {
		console.error(`Invalid input target '${target} to get for ${props.url}.`);
	}
	
	return result;
}

/** Sets <input> element value with specified value, converted as per element type.
 * @param {string} target Element 'target' attribute value. Ignored if `props.setMethod === 'PATCH'`.
 * @param {*} value New value to be set. May be null, where default value will be used. */
function inputSet(target, value) {
	let elem;
	
	switch (props.setMethod) {
		case 'PATCH':
			elem = inputs;
			break;
		case 'POST':
			elem = inputs[target];
			break;
	}
	
	if (elem) {
		switch (elem.type) {
			case 'text':
				if (value === null) {
					value = getTypeDefaultValue('string');
				}
				elem.value = value;
				break;
			case 'number':
				switch (typeof(value)) {
					case 'number':
						break;
					case 'string':
						const tmp = Number.parseInt(value);
						
						if (isNaN(tmp)) {
							console.error(`'${value}' for '${props.url}' ${target}' not a number.`);
							value = null;
						}
						else {
							value = tmp;
						}
						break;
					default:
						if (value === null) {
							value = getTypeDefaultValue('number');
						}
						else {
							console.error(`'${value}' for '${props.url}' '${target}' has unhandled type.`);
						}
						break;
				}
				
				if (value !== null) {
					elem.value = value;
				}
				break;
			case 'checkbox':
				switch (typeof(value)) {
					case 'boolean':
						break;
					case 'string':
						if ((value.toLowerCase() === 'true') || (value === '1')) {
							value = true;
						}
						else if ((value.toLowerCase() === 'false') || (value === '0')) {
							value = false;
						}
						else {
							console.error(`'${value}' for '${props.url}' '${target}' not a boolean.`);
							value = null;
						}
						break;
					case 'number':
						value = !!value;
						break;
					default:
						if (value === null) {
							value = getTypeDefaultValue('boolean');
						}
						else {
							console.error(`'${value}' for '${props.url}' '${target}' has unhandled type.`);
						}
						break;
				}
				
				if (value !== null) {
					elem.checked = value;
				}
				break;
			default:
				console.error(`'${props.url}' '${target}' UI type '${elem.type}' not handled yet.`);
		}
	}
	else {
		console.error(`Invalid input target '${target}' to set for '${props.url}'.`);
	}
}

/** Helper function to specify default value, depending on UI or variable type.
 * @param {string} type UI/Variable type.
 * @return {*} Default value. */
function getTypeDefaultValue(type) {
	switch (type) {
		case 'string':
		case 'text':
			return 'Not set';
		case 'number':
			return -1;
		case 'boolean':
		case 'checkbox':
			return false;
		default:
			console.error(`Got unsupported type '${type}'.`);
			return 'Not set';
	}
}

onMounted(() => {
	const elems = mappings.value.querySelectorAll('input');
	
	if (!elems.length) {
		console.error(`${props.url} has no input.`);
	}
	else if (props.setMethod === 'PATCH') {
		if (elems.length !== 1) {
			console.warn(`${props.url} PATCH has multiple inputs.`);
		}
		
		inputs = elems[0];											//only first element is used
	}
	else if (props.setMethod === 'POST') {
		inputs = {};
		
		for (const elem of elems) {
			const target = elem.getAttribute('target');
			
			if (target) {
				inputs[target] = elem;
			}
			else {
				console.error(`${props.url} has input with invalid target.`);
			}
		}
	}
	
	//set UI to default value in case server is down from the start
	if (props.setMethod === 'PATCH') {
		inputSet(null, getTypeDefaultValue(inputs.type));
	}
	else if (props.setMethod === 'POST') {
		for (const [target, elem] of Object.entries(inputs)) {
			inputSet(target, getTypeDefaultValue(elem.type));
		}
	}
	
	btnGet();														//initialize config value from server
});
</script>

<template>
<div>
	<p class="name">{{name}}</p>
	<div class="mappings" ref="mappings">
		<slot />
		<div class="cell">
			<button class="buttons" type="button" @click="btnSet">Set</button>
			<button class="buttons" type="button" @click="btnGet">Get</button>
		</div>
	</div>
</div>
</template>

<style scoped>
:slotted(label) {
	display: flex;
	grid-column: 1 / 2;
}
:slotted(input), .buttons {
	flex: auto;
	margin-left: 0.5em;
}
.cell {
	display: flex;
	grid-column: 2 / 3;
}
.mappings {
	display: grid;
	grid-template-columns: 5fr 1fr;
}
.name {
	font-size: x-large;
}
</style>