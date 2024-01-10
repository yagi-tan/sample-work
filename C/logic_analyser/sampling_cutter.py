import re

samplingFile = 'sampling.bin'

with open(samplingFile, 'rb') as fp:
	data = str(fp.read(),'ascii')
	rgx = re.compile('{base:(\d+) count:(\d+) sample:(\d+) rate:(\d+) (start|end)}')
	
	mtc = rgx.search(data)
	offset = 0
	inSect = False
	
	while mtc:
		print(f"offset:{offset} inSect:{inSect} mtc:{mtc.group(5)}")
		
		if mtc.group(5) == 'start':
			inSect = True
		elif mtc.group(5) == 'end':
			if inSect:
				with open(f"sampling_{mtc.group(1)}_{mtc.group(2)}_{mtc.group(3)}_{mtc.group(4)}.bin", 'wb') \
				as ofp:
					ofp.write(data[offset:offset + mtc.start(0)].encode('ascii'))
			
				inSect = False
			
		offset += mtc.end(0)
		mtc = rgx.search(data[offset:])
