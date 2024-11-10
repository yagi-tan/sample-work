'use strict';

let uniqueId = 0;

/** Generates unique HTML element ID in '&ltprefix&gtseparator&ltsequential number&gt' format.
 * @param {String} [prefix=''] Prefix to the ID.
 * @param {String} [separator='-'] Separator between prefix and ID.
 * @return {String} Unique ID.
 */
export function generateUniqueId(prefix, separator) {
	if (!prefix) {
		prefix = '';
	}
	if (!separator) {
		separator = '-';
	}
	return prefix + separator + uniqueId++;
}
