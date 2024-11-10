'use strict';

import logicAnalyser from '../model/logicAnalyser.js';

import express from 'express';
const router = express.Router();

/** @openapi
 * /control/channel/{chId}:
 *   parameters:
 *   - $ref: '#/components/parameters/ChannelId'
 *   get:
 *     description: Gets specific channel operating status.
 *     operationId: getChannelStatus
 *     tags:
 *     - Channel
 *     responses:
 *       '200':
 *         description: OK.
 *         content:
 *           text/plain:
 *             schema:
 *               type: boolean
 *       '400':
 *         description: Error has occurred.
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/ErrorObject'
 */
router.get('/channel/:chId(\\d+)', (req, res, next) => {
	const status = logicAnalyser.getChannelStatus(req.params.chId);
	
	if (status instanceof Error) {
		next(status);
	}
	else {
		res.set('Content-Type', 'text/plain').status(200).send(status);
	}
});

/** @openapi
 * /control/channel/{chId}/{status}:
 *   parameters:
 *   - $ref: '#/components/parameters/ChannelId'
 *   - name: status
 *     description: Channel operating status.
 *     in: path
 *     required: true
 *     schema:
 *       type: boolean
 *   patch:
 *     description: Sets specific channel operating status.
 *     operationId: setChannelStatus
 *     tags:
 *     - Channel
 *     responses:
 *       '200':
 *         description: OK.
 *       '400':
 *         description: Error has occurred.
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/ErrorObject'
 */
router.patch('/channel/:chId(\\d+)/:status', async (req, res, next) => {
	const err = await logicAnalyser.setChannelStatus(req.params.chId, req.params.status);
	
	if (err instanceof Error) {
		next(err);
		return;
	}
	
	res.sendStatus(200);
});

export default router;