from select import select
import socket
import threading
import time

class Client:
	def __init__(self, host, port):
		self.address = (host, port)
		self.running = True
		self.val = None
		
		self.thd = threading.Thread(target = self.proc)
		self.thd.start()
		
	def close(self):
		self.running = False
		self.thd.join()
	
	def proc(self):
		connected = False
		sock = None
		lastUpdate = time.time()
		
		while self.running:
			if sock is None:
				print("creating TCP socket")
				sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
				sock = [sock]
			elif connected:
				selRead, selWrite, selErr = select(sock, sock, sock, 0.5)
				
				if selRead:
					data = sock[0].recv(4096, socket.MSG_DONTWAIT)
						
					if len(data):
						print(f"received data {len(data)} byte(s): {data}")
					else:
						print(f"client got disconnected")
						
						connected = False
						sock[0].shutdown(socket.SHUT_RDWR)
						sock[0].close()
						sock = None
				
				if connected and selWrite:
					curTime = time.time()
					
					if (curTime - lastUpdate) >= 1.0:
						line = f"val:{self.get_value()}\n"
						print(line, end='')
						
						try:
							sock[0].sendall(bytes(line, 'utf-8'))
							lastUpdate = curTime
						except Exception as e:
							print(f"\terror sending data: {e}")
				
				if connected and selErr:
					print(f"got socket select error, disconnected?")
			else:
				print(f"connecting to {self.address}...")
				
				res = sock[0].connect_ex(self.address)
				if res:
					print(f"\terror connecting to {self.address}: {res}")
					time.sleep(1)
				else:
					print("\tconnected")
					connected = True
		
		if sock:
			if connected:
				sock[0].shutdown(socket.SHUT_RDWR)
			sock[0].close()
	
	def get_value(self) -> int:
		return self.val
	
	def set_value(self, val):
		self.val = val
	
if __name__ == '__main__':
	client = Client('localhost', 57001)
	
	for i in range(5):
		client.set_value(i)
		time.sleep(3)
	
	print("closing client")
	client.close()
	print("done")
	
