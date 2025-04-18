#include <windows.h>

#pragma comment(lib, "vcruntime.lib")

struct VDLaunchIpcData {
	int version;
	int programOffsetA;
	int programOffsetW;
	int commandLineOffsetA;
	int commandLineOffsetW;
	int _pad;
	unsigned __int64 hStdInput;
	unsigned __int64 hStdOutput;
	unsigned __int64 hStdError;
	unsigned __int64 hParentProcess;
	unsigned __int64 hLaunchEvent;
};

DWORD g_childProcessId;

BOOL WINAPI CtrlHandler(DWORD dwCtrlType)
{
	if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
		GenerateConsoleCtrlEvent(dwCtrlType, g_childProcessId);
		return TRUE;
	}

	return FALSE;
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	WCHAR mappingName[32];

	wsprintfW(mappingName, L"vdlaunch-data-%08x", GetCurrentProcessId());

	HANDLE hFileMapping = OpenFileMappingW(GENERIC_READ | GENERIC_WRITE, FALSE, mappingName);
	if (!hFileMapping) {
		const DWORD err = GetLastError();
		return HRESULT_FROM_WIN32(err);
	}

	// map it
	VDLaunchIpcData *ipcData = (VDLaunchIpcData *)MapViewOfFile(hFileMapping, FILE_MAP_WRITE, 0, 0, 0);
	if (!ipcData) {
		const DWORD err = GetLastError();
		return HRESULT_FROM_WIN32(err);
	}

	// extract and change handles
	const HANDLE hStdInput = (HANDLE)ipcData->hStdInput;
	const HANDLE hStdOutput = (HANDLE)ipcData->hStdOutput;
	const HANDLE hStdError = (HANDLE)ipcData->hStdError;
	const HANDLE hParentProcess = (HANDLE)ipcData->hParentProcess;
	const HANDLE hLaunchEvent = (HANDLE)ipcData->hLaunchEvent;

	SetStdHandle(STD_INPUT_HANDLE, hStdInput);
	SetStdHandle(STD_OUTPUT_HANDLE, hStdOutput);
	SetStdHandle(STD_ERROR_HANDLE, hStdError);

	// kill error dialogs
	UINT dwOldErrorMode = SetErrorMode(0);
	SetErrorMode(dwOldErrorMode | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

	// launch main VirtualDub exe
	PROCESS_INFORMATION pi;

	DWORD rc = 20;

	BOOL success;

	HANDLE hStdInput2;
	HANDLE hStdOutput2;
	HANDLE hStdError2;

	HANDLE hProcess = GetCurrentProcess();
	if (!DuplicateHandle(hProcess, hStdInput, hProcess, &hStdInput2, 0, TRUE, DUPLICATE_SAME_ACCESS) ||
		!DuplicateHandle(hProcess, hStdOutput, hProcess, &hStdOutput2, 0, TRUE, DUPLICATE_SAME_ACCESS) ||
		!DuplicateHandle(hProcess, hStdError, hProcess, &hStdError2, 0, TRUE, DUPLICATE_SAME_ACCESS))
	{
		DWORD err = GetLastError();
		return HRESULT_FROM_WIN32(err);
	}

	STARTUPINFOW siw = {sizeof(STARTUPINFOW)};
	siw.wShowWindow	= SW_SHOWMINNOACTIVE;
	siw.hStdInput	= hStdInput2;
	siw.hStdOutput	= hStdOutput2;
	siw.hStdError	= hStdError2;
	siw.dwFlags		= STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;

	const wchar_t *exepathW = ipcData->programOffsetW ? (const wchar_t *)((const char *)ipcData + ipcData->programOffsetW) : NULL;
	const wchar_t *commandLineW = ipcData->commandLineOffsetW ? (const wchar_t *)((const char *)ipcData + ipcData->commandLineOffsetW) : NULL;
	success = CreateProcessW(exepathW, (LPWSTR)commandLineW, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &siw, &pi);

	if (success) {
		SetEvent(hLaunchEvent);
		CloseHandle(pi.hThread);

		// set Ctrl+C/Ctrl+Break handler
		g_childProcessId = pi.dwProcessId;
		SetConsoleCtrlHandler(CtrlHandler, TRUE);

		HANDLE h[2] = {
			pi.hProcess,
			hParentProcess
		};

		if (WAIT_OBJECT_0 + 1 == WaitForMultipleObjects(2, h, FALSE, INFINITE)) {
			GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId);
		}

		GetExitCodeProcess(pi.hProcess, &rc);

		CloseHandle(pi.hProcess);
	} else {
		DWORD err = GetLastError();
		rc = HRESULT_FROM_WIN32(err);
	}

	CloseHandle(hStdError2);
	CloseHandle(hStdOutput2);
	CloseHandle(hStdInput2);
	CloseHandle((HANDLE)ipcData->hStdInput);
	CloseHandle((HANDLE)ipcData->hStdOutput);
	CloseHandle((HANDLE)ipcData->hStdError);

	UnmapViewOfFile(ipcData);
	CloseHandle(hFileMapping);

	ExitProcess(rc);
	return (int)rc;
}
