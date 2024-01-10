from socketserver import BaseRequestHandler, ThreadingTCPServer
from typing import Optional
import socket
import threading
import time

class ServerHandler(BaseRequestHandler):
	def handle(self):
		name = threading.current_thread().name
		connected = True
		
		while self.server.running:
			line = f"Handler{name} server.val:{self.server.get_value()}\n"
			print(line, end='')
			self.request.sendall(bytes(line, 'utf-8'))
			
			try:
				data = self.request.recv(4096, socket.MSG_DONTWAIT)
				if len(data):
					print(f"\tHandler{name} data {len(data)} byte(s)")
				else:
					print(f"Handler{name} client got disconnected")
					connected = False
					break
			except BlockingIOError as e:
				print(f"\tHandler{name} no data: {e}")
			
			time.sleep(1)
		
		if connected:
			print(f"Handler{threading.current_thread().name} closing")
			self.request.shutdown(socket.SHUT_RDWR)
			self.request.close()

class Server(ThreadingTCPServer):
	allow_reuse_address = True
	
	def __init__(self, host, port):
		super().__init__((host, port), ServerHandler)
		
		self.lock = threading.Lock()
		self.running = True
		self.val = None
	
	def get_value(self) -> Optional[int]:
		with self.lock:
			val = self.val
		return val
	
	def set_value(self, val):
		with self.lock:
			self.val = val
	
	def shutdown(self):
		self.running = False
		super().shutdown()

if __name__ == '__main__':
	server = Server('localhost', 57000)
	serverThd = threading.Thread(target = server.serve_forever)
	serverThd.start()
	
	for i in range(3):
		server.set_value(i)
		time.sleep(5)
	
	print("shutting down server")
	server.shutdown()
	serverThd.join()
	print("done")
