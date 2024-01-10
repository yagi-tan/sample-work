import asyncio
import logging
import random
import socket
import struct
import sys

RTP_SEND_PAYLOAD_SIZE = 960					#in bytes
RTP_SEND_RATE = 30							#in milliseconds

class RtpPacket:
	packer = struct.Struct("!BBHII")
	
	def __init__(self, payloadType: int, ssrc: int, timestampStep: int):
		self.payload = None
		self.payloadType = payloadType
		self.seqNumber = random.randint(0x0000, 0xFFFF)			#2 bytes range
		self.ssrc = ssrc
		self.timestamp = 0
		self.timestampStep = timestampStep
	
	@classmethod
	def interpret_data(cls, data: bytes) -> object:
		result = None
		
		if len(data) >= 12:
			values = cls.packer.unpack(data[0 : 12])
			result = cls(values[1] & 0x7F, values[4], 0)
			result.payload = data[cls.packer.size : ]
			result.seqNumber = values[2]
			result.timestamp = values[3]
		else:
			logging.error(f"Input data too small (expecting {cls.packer.size} bytes, got {len(data)}.")
		
		return result
	
	def generate_packet(self, payload: bytes) -> bytes:
		packet: bytes = self.packer.pack(0x80, 0x7F & self.payloadType, self.seqNumber, self.timestamp,
			self.ssrc)
		packet = packet + payload
		
		self.seqNumber = 0 if self.seqNumber >= 0xFFFF else self.seqNumber + 1
		self.timestamp = self.timestamp + self.timestampStep
		
		return packet

doneSend = False
gotSsrc = None

async def udp_receive(target: str, port: int):
	global doneSend, gotSsrc
	
	logging.info(f"target:{target} port:{port}")
	
	rxSocket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	rxSocket.bind((target, port))
	rxSocket.setblocking(0)
	
	while not doneSend:
		try:
			data, addr = rxSocket.recvfrom(2048)
			
			if len(data):
				logging.info(f"Got {len(data)} byte(s) from '{addr}'.")
				
				rtpPacket = RtpPacket.interpret_data(data)
				if rtpPacket:
					logging.info(f"Got RTP packet with payload size {len(rtpPacket.payload)}")
					
					if not gotSsrc:						#SSRC not yet set
						gotSsrc = rtpPacket.ssrc
			
		except socket.error:
			pass
		
		await asyncio.sleep(0.2)

async def udp_send(target: str, port: int, audioFile: str):
	global doneSend, gotSsrc
	
	logging.info(f"target:{target} port:{port}")
	
	data = None
	try:
		with open(audioFile, 'rb') as fd:
			data = fd.read()
		
		logging.info(f"Audio data length:'{len(data)}'.")
	except OSError:
		logging.error(f"Error reading audio file '{audioFile}'.")
	
	if data:
		txSocket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		
		dataOffset = 0
		sendQt = 0
		
		while not gotSsrc:
			await asyncio.sleep(1)
		
		rtpPacket = RtpPacket(111, gotSsrc, 480)
		
		while True:
			sendQt = sendQt + 1
			sendData = rtpPacket.generate_packet(data[dataOffset : dataOffset + RTP_SEND_PAYLOAD_SIZE])
			sentBytes = txSocket.sendto(sendData, (target, port))
			
			logging.info(f"send count:{sendQt} expect:{len(sendData)} actual:{sentBytes}")
			
			if sentBytes:
				dataOffset = dataOffset + RTP_SEND_PAYLOAD_SIZE
				await asyncio.sleep(RTP_SEND_RATE / 1000)
			
			if dataOffset >= len(data):
				logging.warning("Reached end of audio data to be sent.")
				break
		
	doneSend = True

async def main(target: str, recvPort: int, sendPort: int, sendFile: str):
	'''
	Main function.
		target		Target network interface name.
		recvPort	UDP receive port.
		sendPort	UDP send port.
		sendFile	Raw audio file to be sent.
	'''

	logging.info("Start communication...")
	await asyncio.gather(udp_receive(target, recvPort), udp_send(target, sendPort, sendFile))
	logging.info("End communication.")

if __name__ == '__main__':
	if len(sys.argv) < 5:
		print(f"usage: {sys.argv[0]} <target network interface> <UDP receive port> <UDP send port>"
			" <raw audio file to be sent>")
	else:
		logFmt = logging.Formatter(datefmt = '%Y%m%dT%H%M%S',
		fmt = '%(asctime)s,%(msecs)03d [%(levelname)s] %(filename)s:%(lineno)d - %(message)s')
		fh = logging.FileHandler('udp_rtp.log')
		fh.setFormatter(logFmt)
		fh.setLevel(logging.DEBUG)
		strmh = logging.StreamHandler()
		strmh.setFormatter(logFmt)
		strmh.setLevel(logging.INFO)
		logging.basicConfig(level = logging.DEBUG, handlers = [fh, strmh])
		
		try:
			asyncio.run(main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]))
		except ValueError:
			logging.error(f"Error converting port(s) parameter value '{sys.argv[2]}'/'{sys.argv[3]}'"
				" to integer.")
		except KeyboardInterrupt:
			logging.info("Got Ctrl-C.")
