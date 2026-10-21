#!/usr/bin/env python3
"""
Windows only: assigns the WinUSB driver of Windows (inbox, Microsoft signed winusb.inf) to the radio in DFU mode
(USB 0483:DF11), so the firmware loader can reach it through libusb.

This is what the Device Manager does with "Update driver > Let me pick > Universal Serial Bus devices > WinUsb Device".
It replaces the old ST driver (STTub30), which the current versions of Windows are refusing to load.

Has to be run as administrator, with the radio in DFU mode. The result is written in winusb_dfu_driver.log.
"""
import ctypes
import ctypes.wintypes as wt
import os
import sys
import traceback

LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'winusb_dfu_driver.log')
HARDWARE_ID = 'USB\\VID_0483&PID_DF11'
WINUSB_INF = os.path.join(os.environ.get('SystemRoot', 'C:\\Windows'), 'INF', 'winusb.inf')

DIGCF_PRESENT = 0x02
DIGCF_ALLCLASSES = 0x04
SPDRP_HARDWAREID = 0x01
SPDRP_CLASSGUID = 0x08
USBDEVICE_CLASS_GUID = '{88BAE032-5A81-49F0-BC3D-A4FF138216D6}'
SPDIT_CLASSDRIVER = 0x01
DI_ENUMSINGLEINF = 0x00010000
DI_FLAGSEX_ALLOWEXCLUDEDDRVS = 0x00000800
MAX_PATH = 260
LINE_LEN = 256
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


class GUID(ctypes.Structure):
	_fields_ = [('Data1', wt.DWORD), ('Data2', wt.WORD), ('Data3', wt.WORD), ('Data4', ctypes.c_ubyte * 8)]


class SP_DEVINFO_DATA(ctypes.Structure):
	_fields_ = [('cbSize', wt.DWORD), ('ClassGuid', GUID), ('DevInst', wt.DWORD), ('Reserved', ctypes.c_void_p)]


class SP_DEVINSTALL_PARAMS_W(ctypes.Structure):
	_fields_ = [('cbSize', wt.DWORD), ('Flags', wt.DWORD), ('FlagsEx', wt.DWORD), ('hwndParent', wt.HWND),
			('InstallMsgHandler', ctypes.c_void_p), ('InstallMsgHandlerContext', ctypes.c_void_p), ('FileQueue', ctypes.c_void_p),
			('ClassInstallReserved', ctypes.c_void_p), ('Reserved', wt.DWORD), ('DriverPath', ctypes.c_wchar * MAX_PATH)]


class SP_DRVINFO_DATA_W(ctypes.Structure):
	_fields_ = [('cbSize', wt.DWORD), ('DriverType', wt.DWORD), ('Reserved', ctypes.c_void_p), ('Description', ctypes.c_wchar * LINE_LEN),
			('MfgName', ctypes.c_wchar * LINE_LEN), ('ProviderName', ctypes.c_wchar * LINE_LEN), ('DriverDate', wt.FILETIME), ('DriverVersion', ctypes.c_ulonglong)]


def log(*args):
	line = ' '.join(str(a) for a in args)
	print(line)
	with open(LOG_FILE, 'a', encoding='utf-8') as f:
		f.write(line + '\n')


def check(result, what):
	if not result:
		raise OSError('%s failed: %s' % (what, ctypes.WinError(ctypes.get_last_error())))


def main():
	open(LOG_FILE, 'w').close()

	if not ctypes.windll.shell32.IsUserAnAdmin():
		log('ERROR: not run as administrator')
		return 2

	setupapi = ctypes.WinDLL('setupapi', use_last_error=True)
	newdev = ctypes.WinDLL('newdev', use_last_error=True)

	setupapi.SetupDiGetClassDevsW.restype = ctypes.c_void_p
	setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.c_void_p, wt.LPCWSTR, wt.HWND, wt.DWORD]
	setupapi.SetupDiEnumDeviceInfo.argtypes = [ctypes.c_void_p, wt.DWORD, ctypes.POINTER(SP_DEVINFO_DATA)]
	setupapi.SetupDiGetDeviceRegistryPropertyW.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD)]
	setupapi.SetupDiSetDeviceRegistryPropertyW.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), wt.DWORD, ctypes.c_void_p, wt.DWORD]
	setupapi.SetupDiGetDeviceInstallParamsW.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), ctypes.POINTER(SP_DEVINSTALL_PARAMS_W)]
	setupapi.SetupDiSetDeviceInstallParamsW.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), ctypes.POINTER(SP_DEVINSTALL_PARAMS_W)]
	setupapi.SetupDiBuildDriverInfoList.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), wt.DWORD]
	setupapi.SetupDiEnumDriverInfoW.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), wt.DWORD, wt.DWORD, ctypes.POINTER(SP_DRVINFO_DATA_W)]
	setupapi.SetupDiSetSelectedDriverW.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), ctypes.POINTER(SP_DRVINFO_DATA_W)]
	setupapi.SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]
	newdev.DiInstallDevice.argtypes = [wt.HWND, ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), ctypes.POINTER(SP_DRVINFO_DATA_W), wt.DWORD, ctypes.POINTER(wt.BOOL)]

	devices = setupapi.SetupDiGetClassDevsW(None, 'USB', None, DIGCF_PRESENT | DIGCF_ALLCLASSES)
	if devices in (None, INVALID_HANDLE_VALUE):
		raise OSError('SetupDiGetClassDevs failed: %s' % ctypes.WinError(ctypes.get_last_error()))

	try:
		# Looks for the radio in DFU mode
		device = SP_DEVINFO_DATA()
		device.cbSize = ctypes.sizeof(SP_DEVINFO_DATA)
		found = False
		index = 0
		while setupapi.SetupDiEnumDeviceInfo(devices, index, ctypes.byref(device)):
			index += 1
			buffer = ctypes.create_unicode_buffer(1024)
			if setupapi.SetupDiGetDeviceRegistryPropertyW(devices, ctypes.byref(device), SPDRP_HARDWAREID, None, buffer, ctypes.sizeof(buffer), None):
				hardwareIds = [s for s in ctypes.wstring_at(buffer, 1024).split('\x00') if s]
				if any(h.upper() == HARDWARE_ID for h in hardwareIds[:4]):
					found = True
					break

		if not found:
			log('ERROR: the radio in DFU mode (%s) is not connected' % HARDWARE_ID)
			return 3
		log('Device found:', HARDWARE_ID)

		# The class drivers list is built for the setup class of the device: it has to be the one of winusb.inf ("USBDevice"),
		# not the one of the previous driver. This is the device type choice of the Device Manager.
		classGuidText = ctypes.create_unicode_buffer(USBDEVICE_CLASS_GUID)
		check(setupapi.SetupDiSetDeviceRegistryPropertyW(devices, ctypes.byref(device), SPDRP_CLASSGUID, classGuidText, ctypes.sizeof(classGuidText)), 'SetupDiSetDeviceRegistryProperty (class)')
		check(ctypes.windll.ole32.CLSIDFromString(USBDEVICE_CLASS_GUID, ctypes.byref(device.ClassGuid)) == 0, 'CLSIDFromString')

		# Only the drivers of winusb.inf
		params = SP_DEVINSTALL_PARAMS_W()
		params.cbSize = ctypes.sizeof(SP_DEVINSTALL_PARAMS_W)
		check(setupapi.SetupDiGetDeviceInstallParamsW(devices, ctypes.byref(device), ctypes.byref(params)), 'SetupDiGetDeviceInstallParams')
		params.Flags |= DI_ENUMSINGLEINF
		params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS
		params.DriverPath = WINUSB_INF
		check(setupapi.SetupDiSetDeviceInstallParamsW(devices, ctypes.byref(device), ctypes.byref(params)), 'SetupDiSetDeviceInstallParams')
		check(setupapi.SetupDiBuildDriverInfoList(devices, ctypes.byref(device), SPDIT_CLASSDRIVER), 'SetupDiBuildDriverInfoList')

		driver = SP_DRVINFO_DATA_W()
		driver.cbSize = ctypes.sizeof(SP_DRVINFO_DATA_W)
		check(setupapi.SetupDiEnumDriverInfoW(devices, ctypes.byref(device), SPDIT_CLASSDRIVER, 0, ctypes.byref(driver)), 'SetupDiEnumDriverInfo (no driver in winusb.inf?)')
		log('Driver: "%s" from "%s" (%s)' % (driver.Description, driver.MfgName, driver.ProviderName))

		check(setupapi.SetupDiSetSelectedDriverW(devices, ctypes.byref(device), ctypes.byref(driver)), 'SetupDiSetSelectedDriver')

		reboot = wt.BOOL(False)
		check(newdev.DiInstallDevice(None, devices, ctypes.byref(device), ctypes.byref(driver), 0, ctypes.byref(reboot)), 'DiInstallDevice')
		log('OK: WinUSB driver installed on the device (reboot needed: %s)' % bool(reboot.value))
		return 0
	finally:
		setupapi.SetupDiDestroyDeviceInfoList(devices)


if __name__ == '__main__':
	try:
		code = main()
	except Exception:
		log('ERROR:\n' + traceback.format_exc())
		code = 1
	sys.exit(code)
