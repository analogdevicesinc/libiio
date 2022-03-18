from ctypes import POINTER, Structure, cdll, c_char_p, c_int, c_uint, byref
from sys import argv

def _checkOpen(result, func, arguments):
	if result:
		return result
	else:
		raise IOError("Failed to open INI file: '%s'" % arguments[0])

def _checkRead(result, func, arguments):
	if result == -1:
		raise SyntaxError("Error occured while parsing INI file")
	return result

def _init():
	class _INI(Structure):
		pass
	IniPtr = POINTER(_INI)

	lib = cdll.LoadLibrary('libini.so.0')

	ini_open = lib.ini_open
	ini_open.restype = IniPtr
	ini_open.archtypes = (c_char_p, )
	ini_open.errcheck = _checkOpen
	global _ini_open
	_ini_open = ini_open

	ini_close = lib.ini_close
	ini_close.restype = None
	ini_close.archtypes = (IniPtr, )
	global _ini_close
	_ini_close = ini_close

	ini_next_section = lib.ini_next_section
	ini_next_section.restype = c_int
	ini_next_section.archtypes = (IniPtr, c_char_p, c_uint)
	ini_next_section.errcheck = _checkRead
	global _ini_next_section
	_ini_next_section = ini_next_section

	ini_read_pair = lib.ini_read_pair
	ini_read_pair.restype = c_int
	ini_read_pair.archtypes = (IniPtr, c_char_p, c_uint, c_char_p, c_uint)
	ini_read_pair.errcheck = _checkRead
	global _ini_read_pair
	_ini_read_pair = ini_read_pair

_init()

class INI(object):

	def __init__(self, path):
		self._ini = _ini_open(path)
	
	def __del__(self):
		_ini_close(self._ini)

	def next_section(self):
		s = c_char_p()
		u = c_uint()
		res = _ini_next_section(self._ini, byref(s), byref(u))
		if res == 1:
			return s.value[:u.value]

	def read_pair(self):
		key = c_char_p()
		key_len = c_uint()
		val = c_char_p()
		val_len = c_uint()
		res = _ini_read_pair(self._ini, \
				byref(key), byref(key_len), \
				byref(val), byref(val_len))
		if res == 1:
			return (key.value[:key_len.value], val.value[:val_len.value])
		return (None, None)


def read_ini(filename):
	ini = INI(filename)
	ini_dict = {}
	first_section = True

	while True:
		section = ini.next_section()
		if not first_section and not section:
			break
		first_section = False

		section_dict = {}
		while True:
			key, value = ini.read_pair()
			if not key:
				break
			section_dict[key] = value

		ini_dict[section] = section_dict

	return ini_dict


def main():
	if len(argv) != 2:
		print "Usage: ini.py [INI_FILE]..."
		return

	ini = read_ini(argv[1])
	print ini

if __name__ == '__main__':
	main()
