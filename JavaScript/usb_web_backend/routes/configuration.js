'use strict';

import logicAnalyser from '../model/logicAnalyser.js';
const utils = await import('../utils.js');

import express from 'express';
const router = express.Router();

/** @openapi
 * /configuration/cdev/path:
 *   get:
 *     description: Gets character device base path.
 *     operationId: getCdevPath
 *     tags:
 *     - Cdev
 *     responses:
 *       '200':
 *         description: OK.
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/CdevObject'
 *       '400':
 *         description: Error has occurred.
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/ErrorObject'
 *   post:
 *     description: Sets character device base path.
 *     operationId: setCdevPath
 *     tags:
 *     - Cdev
 *     requestBody:
 *       content:
 *         application/json:
 *           schema:
 *             $ref: '#/components/schemas/CdevObject'
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
router.get('/cdev/path', (req, res) => {
	res.status(200).json(logicAnalyser.pathCdev);
});
router.post('/cdev/path', async (req, res, next) => {
	let err;
	
	err = await utils.validatePayload(['path', 'major', 'minor'], req);
	if (err instanceof Error) {
		next(err);
		return;
	}
	
	err = await logicAnalyser.setPathCdev(req.body);
	if (err instanceof Error) {
		next(err);
		return;
	}
	
	res.sendStatus(200);
});

/** @openapi
 * /configuration/channel/{chId}/config:
 *   parameters:
 *   - $ref: '#/components/parameters/ChannelId'
 *   get:
 *     description: Gets specific channel configuration.
 *     operationId: getChannelConfig
 *     tags:
 *     - Channel
 *     responses:
 *       '200':
 *         description: OK.
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/ChannelConfigObject'
 *       '400':
 *         description: Error has occurred.
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/ErrorObject'
 *   post:
 *     description: Sets specific channel configuration.
 *     operationId: setChannelConfig
 *     tags:
 *     - Channel
 *     requestBody:
 *       content:
 *         application/json:
 *           schema:
 *             $ref: '#/components/schemas/ChannelConfigObject'
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
router.get('/channel/:chId(\\d+)/config', (req, res, next) => {
	const cfg = logicAnalyser.getChannelConfig(req.params.chId);
	
	if (cfg instanceof Error) {
		next(cfg);
	}
	else {
		res.status(200).json({
			'pinbase': cfg.pinbase,
			'pincount': cfg.pincount,
			'rate': cfg.rate
		});
	}
});
router.post('/channel/:chId(\\d+)/config', async (req, res, next) => {
	let err;
	
	err = await utils.validatePayload(['pinbase', 'pincount', 'rate'], req);
	if (err instanceof Error) {
		next(err);
		return;
	}
	
	err = await logicAnalyser.setChannelConfig(req.params.chId, req.body);
	if (err instanceof Error) {
		next(err);
		return;
	}
	
	res.sendStatus(200);
});

/** @openapi
 * /configuration/channel/count:
 *   get:
 *     description: Gets device channel count.
 *     operationId: getChannelCount
 *     tags:
 *     - Channel
 *     responses:
 *       '200':
 *         description: OK.
 *         content:
 *           text/plain:
 *             schema:
 *               type: string
 */
router.get('/channel/count', (req, res) => {
	res.set('Content-Type', 'text/plain').status(200).send(`${logicAnalyser.channelCount}`);
});

/** @openapi
 * /configuration/channel/count/{count}:
 *   parameters:
 *   - $ref: '#/components/parameters/ChannelCount'
 *   patch:
 *     description: Sets device channel count.
 *     operationId: setChannelCount
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
router.patch('/channel/count/:count(\\d+)', async (req, res, next) => {
	const err = await logicAnalyser.setChannelCount(req.params.count);
	
	if (err instanceof Error) {
		next(err);
		return;
	}
	
	res.sendStatus(200);
});

/** @openapi
 * /configuration/sysfs/path:
 *   get:
 *     description: Gets sysfs attributes folder path.
 *     operationId: getSysfsPath
 *     tags:
 *     - Sysfs
 *     responses:
 *       '200':
 *         description: OK.
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/SysfsObject'
 *       '400':
 *         description: Error has occurred.
 *         content:
 *           application/json:
 *             schema:
 *               $ref: '#/components/schemas/ErrorObject'
 *   post:
 *     description: Sets sysfs attributes folder path.
 *     operationId: setSysfsPath
 *     tags:
 *     - Sysfs
 *     requestBody:
 *       content:
 *         application/json:
 *           schema:
 *             $ref: '#/components/schemas/SysfsObject'
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
router.get('/sysfs/path', (req, res) => {
	res.status(200).json({path: logicAnalyser.pathSysfs});
});
router.post('/sysfs/path', async (req, res, next) => {
	let err;
	
	err = await utils.validatePayload(['path'], req);
	if (err instanceof Error) {
		next(err);
		return;
	}
	
	err = await logicAnalyser.setPathSysfs(req.body.path);
	if (err instanceof Error) {
		next(err);
		return;
	}
	
	res.sendStatus(200);
});

export default router;