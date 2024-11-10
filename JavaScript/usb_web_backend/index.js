'use strict';

import logicAnalyser from './model/logicAnalyser.js';
import routeConfig from './routes/configuration.js';
import routeCtrl from './routes/control.js';
const wasmIntf = await import('./wasm_cpp/interface.js');

import express from 'express';
const app = express();
const port = 60001;

const ws = await import('ws');

import http from 'node:http';
import process from 'node:process';

if (!wasmIntf.initSys()) {
	throw new Error("Error initialising WASM interface.");
}

app.use(express.json());
app.use(express.static('public'));
app.use('/configuration', routeConfig);
app.use('/control', routeCtrl);

//generic error handler
app.use((err, req, res, nextIgnored) => {
	if (err instanceof AggregateError) {
		res.status(400).send({
			type: err.name, msg: err.message, errors: err.errors.map(error => error.message)
		});
	}
	else {
		res.status(400).send({
			type: err.name, msg: err.message
		});
	}
});

const server = http.createServer(app);
const serverWs = new ws.WebSocketServer({ server });

serverWs.on('connection', (client, req) => {
	console.log(`WebSocket @${req.socket.remoteAddress} connected.`);
	
	client.on('close', (code, reason) => {
		console.log(`WebSocket @${req.socket.remoteAddress} disconnected: ${code} - ${reason}`);
	});
	client.on('error', console.error);
	client.on('message', (data, isBinary) => {
		console.log(`WebSocket @${req.socket.remoteAddress} binary:${isBinary}\n${data.toString()}.`);
	});
});
serverWs.on('error', err => console.error(`WebSocket server error: ${err}`));
serverWs.on('listening', () => console.log('WebSocket server is listening.'));

logicAnalyser.serverWs = serverWs;

server.listen(port, () => {
	console.log(`App server is listening on port ${port}.`);
});

process.on('SIGTERM', async () => {
	const err = await logicAnalyser.stopChannels();
	if (err instanceof AggregateError) {
		console.warn(`${err.message}\n${err.errors.map(error => error.message).join('\n')}`);
	}
	
	serverWs.clients.forEach(client => client.close(1001, 'Server closing.'));
	serverWs.close(() => console.log('WebSocket server closed.'));
	
	server.close(() => console.log('App server closed.'));
	
	wasmIntf.exitSys();
});
