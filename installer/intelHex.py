"""
Module to read intel hex files into binary data blobs.
IntelHex files are commonly used to distribute firmware
See: http://en.wikipedia.org/wiki/Intel_HEX
"""
__copyright__ = "Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License"
import io

def readHex(filename):
	"""
	Read an verify an intel hex file. Return the data as an list of bytes.
	"""
	data = []
	extraAddr = 0
	f = io.open(filename, "r")
	for line in f:
		line = line.strip()
		if len(line) < 1:
			continue
		if line[0] != ':':
			raise Exception("Hex file has a line not starting with ':'")
		recLen = int(line[1:3], 16)
		addr = int(line[3:7], 16) + extraAddr
		recType = int(line[7:9], 16)
		if len(line) != recLen * 2 + 11:
			raise Exception("Error in hex file: " + line)
		checkSum = 0
		for i in xrange(0, recLen + 5):
			checkSum += int(line[i*2+1:i*2+3], 16)
		checkSum &= 0xFF
		if checkSum != 0:
			raise Exception("Checksum error in hex file: " + line)
		
		if recType == 0:#Data record
			while len(data) < addr + recLen:
				data.append(0)
			for i in xrange(0, recLen):
				data[addr + i] = int(line[i*2+9:i*2+11], 16)
		elif recType == 1:	#End Of File record
			pass
		elif recType == 2:	#Extended Segment Address Record
			extraAddr = int(line[9:13], 16) * 16
		else:
			print(recType, recLen, addr, checkSum, line)
	f.close()
	return data
