//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2001 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"

#include <windows.h>
#include <vfw.h>
#include <richedit.h>
#include <vd2/system/registry.h>
#include <vd2/system/w32assist.h>

#include "resource.h"
#include "auxdlg.h"
#include "oshelper.h"
#include "gui.h"

#include <vd2/Priss/decoder.h>
#include <vd2/system/thread.h>
#include <vd2/system/profile.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include "LogWindow.h"
#include "RTProfileDisplay.h"
#include "projectui.h"

extern "C" unsigned long version_num;
extern "C" char version_time[];
extern "C" char version_date[];

extern HINSTANCE g_hInst;

static volatile HWND g_hwndLogWindow = NULL;
static volatile HWND g_hwndStatusWindow = NULL;
static volatile HWND g_hwndProfileWindow = NULL;
extern vdrefptr<VDProjectUI> g_projectui;

class VDLogWindowThread : public VDThread {
public:
	VDSignal mReady;

	VDLogWindowThread() : VDThread("Log Window") {}

	void ThreadRun() {
		mReady.signal();
		// There is a race condition here that can allow two log windows to appear,
		// but that is not a big deal.
		if (!g_hwndLogWindow) {
			DialogBox(g_hInst, MAKEINTRESOURCE(IDD_LOG), NULL, LogDlgProc);
			g_hwndLogWindow = 0;
		} else
			SetWindowPos(g_hwndLogWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
	}

	static INT_PTR CALLBACK LogDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch(msg) {
		case WM_INITDIALOG:
			VDGetILogWindowControl(GetDlgItem(hdlg, IDC_LOG))->AttachAsLogger(false);
			g_hwndLogWindow = hdlg;
			VDSetDialogDefaultIcons(hdlg);
		case WM_SIZE:
			{
				RECT r;

				GetClientRect(hdlg, &r);
				SetWindowPos(GetDlgItem(hdlg, IDC_LOG), NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE);
			}
			return TRUE;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hdlg, 0);
			return TRUE;
		}
		return FALSE;
	}
};

extern void VDOpenLogWindow() {
	VDLogWindowThread logwin;

	logwin.ThreadStart();
	if (logwin.isThreadAttached()) {
		logwin.mReady.wait();
		logwin.ThreadDetach();
	}
}

extern void ShutdownLogWindow() {
	if (!g_hwndLogWindow) return;
	DWORD thid = GetWindowThreadProcessId(g_hwndLogWindow,0);
	HANDLE h = OpenThread(SYNCHRONIZE,false,thid);
	PostMessage(g_hwndLogWindow,WM_CLOSE,0,0);
	WaitForSingleObject(h,1000);
}

class VDStatusWindowThread : public VDThread {
public:
	VDSignal mReady;
	VDStringA s;

	VDStatusWindowThread() : VDThread("Status Window") {}

	void ThreadRun() {
		VDStringA s = this->s;
		mReady.signal();
		// There is a race condition here that can allow two log windows to appear,
		// but that is not a big deal.
		if (!g_hwndStatusWindow) {
			DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_DUMPSTATUS), NULL, StatusDlgProc, (LPARAM)s.c_str());
			g_hwndStatusWindow = 0;
		} else {
			SetText(g_hwndStatusWindow,s.c_str());
			SetWindowPos(g_hwndStatusWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
		}
	}

	static void SetText(HWND hdlg, const char* s) {
		HWND hwndEdit = GetDlgItem(hdlg, IDC_EDIT);

		if (hwndEdit) {
			::SetFocus(hwndEdit);
			::SetWindowTextA(hwndEdit, s);
			::SendMessage(hwndEdit, EM_SETSEL, 0, 0);
		}
	}

	static INT_PTR CALLBACK StatusDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch(msg) {
		case WM_INITDIALOG:
			g_hwndStatusWindow = hdlg;
			VDSetDialogDefaultIcons(hdlg);
			SetText(hdlg,(const char *)lParam);
			return FALSE;

		case WM_SIZE:
			{
				int w = (int)LOWORD(lParam);
				int h = (int)HIWORD(lParam);

				HWND hwndEdit = GetDlgItem(hdlg, IDC_EDIT);

				if (hwndEdit)
					::SetWindowPos(hwndEdit, NULL, 0, 0, w, h, SWP_NOZORDER|SWP_NOACTIVATE);
			}
			return 0;

		case WM_COMMAND:
			switch(wParam) {
				case IDCANCEL:
				case IDOK:
					EndDialog(hdlg, 0);
					return TRUE;
			}
			break;
		}

		return FALSE;
	}
};

extern void VDShowStatus(VDStringA& s) {
	VDStatusWindowThread logwin;
	logwin.s = s;

	logwin.ThreadStart();
	if (logwin.isThreadAttached()) {
		logwin.mReady.wait();
		logwin.ThreadDetach();
	}
}

extern void ShutdownStatusWindow() {
	if (!g_hwndStatusWindow) return;
	DWORD thid = GetWindowThreadProcessId(g_hwndStatusWindow,0);
	HANDLE h = OpenThread(SYNCHRONIZE,false,thid);
	PostMessage(g_hwndStatusWindow,WM_CLOSE,0,0);
	WaitForSingleObject(h,1000);
}

class VDProfileSampleThread : public VDThread {
public:
	int close;
	VDCPUUsageReader reader;
	VDProfileSampleThread(){ close=0; }

	void ThreadRun() {
		reader.Init();
		while (close==0) {
			int vd,sys;
			reader.read(vd,sys);
			if (g_pVDEventProfiler && sys!=-1) g_pVDEventProfiler->InsertSample(IVDEventProfiler::sample_cpu, sys);
			Sleep(20);
		}
	}
};

class VDProfileWindowThread : public VDThread {
public:
	VDSignal mReady;
	VDProfileSampleThread sample;
	int start_mode;

	VDProfileWindowThread() : VDThread("Profile Window") { start_mode=1; }

	void ThreadRun() {
		mReady.signal();
		// There is a race condition here that can allow two Profile windows to appear,
		// but that is not a big deal.
		if (!g_hwndProfileWindow) {
			VDInitProfilingSystem();
			sample.close = 0;
			sample.ThreadStart();
			DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_PROFILER), NULL, ProfileDlgProc, start_mode);
			g_hwndProfileWindow = 0;

			if(sample.isThreadActive()){
				sample.close = 1;
				sample.ThreadWait();
			}
		} else
			SetWindowPos(g_hwndProfileWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
	}

	static INT_PTR CALLBACK ProfileDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch(msg) {
		case WM_INITDIALOG:
			{
				g_hwndProfileWindow = hdlg;
				VDSetDialogDefaultIcons(hdlg);
				HWND w1 = GetDlgItem(hdlg, IDC_PROFILE);
				HWND w2 = GetDlgItem(hdlg, IDC_PROFILE_LIST);
				HWND w3 = GetDlgItem(hdlg, IDC_PROFILE_SUMMARY);
				SendMessage(w2,LB_SETCOLUMNWIDTH,400,0);
				SendMessage(w1,WM_USER+600,0,(LPARAM)w2);
				SendMessage(w1,WM_USER+603,0,(LPARAM)w3);
				SendMessage(w1,WM_USER+604,0,lParam);
				VDUIRestoreWindowPlacementW32(hdlg, "ProfileDisplay2", SW_SHOW);
			}
		case WM_SIZE:
			{
				RECT r;
				GetClientRect(hdlg, &r);
				HWND w1 = GetDlgItem(hdlg, IDC_PROFILE);
				HWND w2 = GetDlgItem(hdlg, IDC_PROFILE_LIST);
				HWND w3 = GetDlgItem(hdlg, IDC_PROFILE_SUMMARY);
				RECT r3;
				GetWindowRect(w3, &r3);
				int x1 = r3.right-r3.left+4;
				MINMAXINFO mmi;
				SendMessage(w1,WM_GETMINMAXINFO,0,(LPARAM)&mmi);
				int h1 = mmi.ptMaxTrackSize.y;
				if(r.bottom<h1) h1 = r.bottom;
				SetWindowPos(w1, NULL, x1, 0, r.right-x1, h1, SWP_NOZORDER|SWP_NOACTIVATE);
				if(r.bottom<=h1){
					ShowWindow(w2,SW_HIDE);
				} else {
					ShowWindow(w2,SW_SHOW);
					SetWindowPos(w2, NULL, x1, h1+4, r.right-x1, r.bottom-h1-4, SWP_NOZORDER|SWP_NOACTIVATE);
				}
				SetWindowPos(w3, NULL, 0, 0, x1-4, r.bottom, SWP_NOZORDER|SWP_NOACTIVATE);
			}
			return TRUE;
		case WM_DESTROY:
			VDUISaveWindowPlacementW32(hdlg, "ProfileDisplay2");
			return FALSE;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDCANCEL){
				SetForegroundWindow(g_projectui->GetHwnd());
				EndDialog(hdlg, 0);
			}

			if (LOWORD(wParam) == IDC_PROFILE_LIST){
				if(HIWORD(wParam) == LBN_SELCHANGE){
					HWND w1 = GetDlgItem(hdlg, IDC_PROFILE);
					SendMessage(w1,WM_USER+601,0,0);
				}
				if(HIWORD(wParam) == LBN_DBLCLK){
					HWND w1 = GetDlgItem(hdlg, IDC_PROFILE);
					SendMessage(w1,WM_USER+602,0,0);
				}
			}
			return TRUE;
		}
		return FALSE;
	}
};

static VDProfileWindowThread profwin;


// 1 = normal
// 0 = suspended
// 2 = recycling
// 3 = clear, then set normal
extern void VDSetProfileMode(int mode) {
	if (!g_hwndProfileWindow) return;
	HWND w1 = GetDlgItem(g_hwndProfileWindow, IDC_PROFILE);
	SendMessage(w1,WM_USER+604,0,mode);
}

extern void VDOpenProfileWindow(int mode) {
	if(profwin.isThreadActive()){
		extern void VDRestartEventProfiler();
		VDRestartEventProfiler();
		VDSetProfileMode(mode);
	} else {
		profwin.ThreadDetach();
		profwin.start_mode = mode;
		profwin.ThreadStart();
	}
}

extern void VDCloseProfileWindow() {
	if(profwin.isThreadActive()){
		PostThreadMessage(profwin.getThreadID(),WM_QUIT,0,0);
		profwin.ThreadWait();
	}
}

INT_PTR CALLBACK ShowTextDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	HRSRC hRSRC;

    switch (message)
    {
        case WM_INITDIALOG:
			if (hRSRC = FindResourceA(NULL, (LPSTR)lParam, "STUFF")) { ///Hmm
				HGLOBAL hGlobal = LoadResource(NULL, hRSRC);
				if (hGlobal) {
					LPVOID lpData = LockResource(hGlobal);
					if (lpData) {
						char *s = (char *)lpData;
						char *ttl = new char[256];

						while(*s!='\r') ++s;
						if (ttl) {
							memcpy(ttl, (char *)lpData, s - (char *)lpData);
							ttl[s-(char*)lpData]=0;
							SendMessage(hDlg, WM_SETTEXT, 0, (LPARAM)ttl);
							delete[] ttl;
						}
						s+=2;
						SendMessage(GetDlgItem(hDlg, IDC_CHANGES), WM_SETFONT, (WPARAM)GetStockObject(SYSTEM_FIXED_FONT), MAKELPARAM(TRUE, 0));
						SendMessageA(GetDlgItem(hDlg, IDC_CHANGES), WM_SETTEXT, 0, (LPARAM)s);
						return TRUE;
					}
				}
			}
            return FALSE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hDlg, TRUE);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK WelcomeDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_COMMAND:
			switch(LOWORD(wParam)) {
			case IDOK: case IDCANCEL:
                EndDialog(hDlg, TRUE);
                return TRUE;
			case IDC_HELP2:
				VDShowHelp(hDlg);
				return TRUE;
            }
            break;
    }
    return FALSE;
}

void Welcome() {
	VDRegistryAppKey key;

	if (!key.getInt("SeenWelcome", 0)) {
		DialogBox(g_hInst, MAKEINTRESOURCE(IDD_WELCOME), NULL, WelcomeDlgProc);

		key.setInt("SeenWelcome", 1);
	}
}

INT_PTR CALLBACK AnnounceExperimentalDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_COMMAND:
			switch(LOWORD(wParam)) {
			case IDOK: case IDCANCEL:
                EndDialog(hDlg, TRUE);
                return TRUE;
			case IDC_VERIFY:
				EnableWindow(GetDlgItem(hDlg, IDOK), IsDlgButtonChecked(hDlg, IDC_VERIFY));
				return TRUE;
            }
            break;
    }
    return FALSE;
}

void AnnounceExperimental() {
#if 0
	DWORD dwSeenIt;

	if (!QueryConfigDword(NULL, "SeenExperimental 1.9.X", &dwSeenIt) || !dwSeenIt) {
		DialogBox(g_hInst, MAKEINTRESOURCE(IDD_EXPERIMENTAL), NULL, AnnounceExperimentalDlgProc);

		SetConfigDword(NULL, "SeenExperimental 1.9.X", 1);
	}
#endif
}


void AnnounceCaptureExperimental(VDGUIHandle h) {
}

static const char g_szDivXWarning[]=
	"VirtualDub warning: \"DivX\" codec detected\0"
	"One or more of the \"DivX 3\" drivers have been detected on your system. These drivers are illegal binary hacks "
	"of legitimate drivers:\r\n"
	"\r\n"
	"* DivX 3 low motion/fast motion: Microsoft MPEG-4 V3 video\r\n"
	"* DivX audio: Microsoft Windows Media Audio\r\n"
	"* \"Radium\" MP3: Fraunhofer-IIS MPEG layer III audio\r\n"
	"\r\n"
	"These drivers are known to be problematic, with stability issues and interference "
	"with the original drivers. VirtualDub's stability cannot be guaranteed when "
	"these drivers. Please do not notify the author of crashes involving these drivers "
	"as the problems cannot be corrected in VirtualDub itself.\r\n"
	"\r\n"
	"This is only a warning. Aside from bug workarounds and warnings, VirtualDub does not take "
	"further action in response to DivX being loaded.\r\n"
	"\r\n"
	"NOTE: This warning does NOT apply to the DivX 4.0 and later releases, which are completely "
	"different codecs."
	;

static const char g_szAPWarning[]=
	"VirtualDub warning: \"AngelPotion Definitive\" codec detected\0"
	"The \"AngelPotion Definitive\" codec has been detected on your system. This driver is an illegal binary hack "
	"of the following legitimate drivers:\r\n"
	"\r\n"
	"* Microsoft MPEG-4 V3 video\r\n"
	"\r\n"
	"The AngelPotion codec is a particularly poor hack of the MS MPEG-4 V3 codec and is known "
	"to cause a number of serious conflicts, including but not limited to:\r\n"
	"* Excessive disk usage on temporary drive\r\n"
	"* Incorrectly responding to compressed formats of other codecs, even uncompressed RGB\r\n"
	"* Preventing some applications from loading AVI files at all\r\n"
	"* Inhibiting Windows Media Player automatic codec download\r\n"
	"\r\n"
	"The author cannot guarantee the stability of VirtualDub in any way when AngelPotion is loaded, "
	"even if the codec is not in use. All crash dumps indicating AP is loaded will be promptly discarded. "
	"It is recommended that you uninstall AngelPotion when possible.\r\n"
	"\r\n"
	"This is only a warning. Aside from bug workarounds and warnings, VirtualDub does not take "
	"further action in response to AngelPotion being loaded."
	;

INT_PTR CALLBACK DivXWarningDlgProc( HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	const char *s;

    switch (message)
    {
		case WM_INITDIALOG:
			s = (const char *)lParam; ///Hmm
			SetWindowTextA(hdlg, s);
			while(*s++);
			SendDlgItemMessageA(hdlg, IDC_WARNING, WM_SETTEXT, 0, (LPARAM)s);
			return TRUE;

        case WM_COMMAND:
			switch(LOWORD(wParam)) {
			case IDOK: case IDCANCEL:
                EndDialog(hdlg, TRUE);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

static bool DetectDriver(const char *pszName) {
	char szDriverANSI[256];
	ICINFO info = { sizeof(ICINFO) };

	for(int i=0; ICInfo(ICTYPE_VIDEO, i, &info); ++i) {
		if (WideCharToMultiByte(CP_ACP, 0, info.szDriver, -1, szDriverANSI, sizeof szDriverANSI, NULL, NULL)
			&& !_stricmp(szDriverANSI, pszName))

			return true;
	}

	return false;
}

void DetectDivX() {
	VDRegistryAppKey key;

	if (!key.getInt("SeenDivXWarning", 0)) {
		if (DetectDriver("divxc32.dll") || DetectDriver("divxc32f.dll")) {
			DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_DIVX_WARNING), NULL, DivXWarningDlgProc, (LPARAM)g_szDivXWarning);

			key.setInt("SeenDivXWarning", 1);
		}
	}
	if (!key.getInt("SeenAngelPotionWarning", 0)) {
		if (DetectDriver("APmpg4v1.dll")) {

			DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_DIVX_WARNING), NULL, DivXWarningDlgProc, (LPARAM)g_szAPWarning);

			key.setInt("SeenAngelPotionWarning", 1);
		}
	}
}

///////////////////////////////////////////////////////////////////////////

namespace {
	struct StreamInData {
		const char *pos;
		int len;
	};

#pragma pack(push, 4)
	struct EDITSTREAM_fixed {
		DWORD_PTR	dwCookie;
		DWORD	dwError;
		EDITSTREAMCALLBACK pfnCallback;		// WinXP x64 build 1290 calls this at [rax+0Ch]!
	};
#pragma pack(pop)

	DWORD CALLBACK TextToRichTextControlCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb) {
		StreamInData& sd = *(StreamInData *)dwCookie;

		if (cb > sd.len)
			cb = sd.len;

		memcpy(pbBuff, sd.pos, cb);
		sd.pos += cb;
		sd.len -= cb;

		*pcb = cb;
		return 0;
	}

	typedef vdfastvector<char> tTextStream;

	void append(tTextStream& stream, const char *string) {
		stream.insert(stream.end(), string, string+strlen(string));
	}

	void append_cooked(tTextStream& stream, const char *string, const char *stringEnd, bool rtfEscape) {
		while(string != stringEnd) {
			const char *s = string;

			if (*s == '%') {
				const char *varbase = ++s;

				while(s != stringEnd && *s != '%')
					++s;

				const ptrdiff_t len = s - varbase;

				if (len == 5 && !memcmp(varbase, "build", 5)) {
					char buf[32];
					stream.insert(stream.end(), buf, buf+sprintf(buf, "%u", version_num));
				} else if (len == 4 && !memcmp(varbase, "date", 4)) {
					stream.insert(stream.end(), version_date, version_date + strlen(version_date));
				} else if (!len) {
					stream.push_back('%');
				} else {
					VDASSERT(false);
				}

				if (s != stringEnd)
					++s;

				string = s;
				continue;
			}

			if (rtfEscape) {
				if (*s == '{' || *s == '\\' || *s == '}')
					stream.push_back('\\');

				++s;
				while(s != stringEnd && *s != '{' && *s != '\\' && *s != '}' && *s != '%')
					++s;
			} else {
				++s;
				while(s != stringEnd && *s != '%')
					++s;
			}

			stream.insert(stream.end(), string, s);
			string = s;
		}
	}

	void TextToRichTextControl(LPCTSTR resName, HWND hdlg, HWND hwndText) {
		HRSRC hResource = FindResourceW(NULL, resName, L"STUFF");

		if (!hResource)
			return;

		HGLOBAL hGlobal = LoadResource(NULL, hResource);
		if (!hGlobal)
			return;

		LPVOID lpData = LockResource(hGlobal);
		if (!lpData)
			return;

		const char *const title = (const char *)lpData;
		const char *s = title;

		while(*s!='\r') ++s;

		SetWindowTextA(hdlg, VDStringA(title, s-title).c_str());
		s+=2;

		tTextStream rtf;

		static const char header[]=
					"{\\rtf"
					"{\\fonttbl{\\f0\\fswiss;}{\\f1\\fnil\\fcharset2 Symbol;}}"
					"{\\colortbl;\\red0\\green64\\blue128;}"
					"\\fs20 "
					;
		static const char listStart[]="{\\*\\pn\\pnlvlblt\\pnindent0{\\pntxtb\\'B7}}\\fi-240\\li540 ";
		static const char bulletCompat[]="{\\pntext\\f1\\'B7\\tab}";

		append(rtf, header);

		bool list_active = false;

		while(*s) {
			// parse line
			int spaces = 0;

			while(*s == ' ') {
				++s;
				++spaces;
			}

			const char *end = s, *t;
			while(*end && *end != '\r' && *end != '\n')
				++end;

			// check for header, etc.
			if (*s == '[') {
				t = ++s;
				while(t != end && *t != ']')
					++t;

				append(rtf, "\\cf1\\li300\\i ");
				append_cooked(rtf, s, t, true);
				append(rtf, "\\i0\\cf0\\par ");
			} else {
				if (*s == '*') {
					if (!list_active) {
						list_active = true;
						append(rtf, listStart);
					} else
						append(rtf, "\\par ");

					append_cooked(rtf, s + 2, end, true);
				} else {
					if (list_active) {
						rtf.push_back(' ');
						if (s == end) {
							list_active = false;
							append(rtf, "\\par\\pard");
						}
					}

					if (!list_active) {
						if (spaces)
							append(rtf, "\\li300 ");
						else
							append(rtf, "\\li0 ");
					}

					append_cooked(rtf, s, end, true);

					if (!list_active)
						append(rtf, "\\par ");
				}
			}

			// skip line termination
			s = end;
			if (*s == '\r' || *s == '\n') {
				++s;
				if ((s[0] ^ s[-1]) == ('\r' ^ '\n'))
					++s;
			}
		}

		rtf.push_back('}');

		SendMessage(hwndText, EM_EXLIMITTEXT, 0, (LPARAM)rtf.size());

		EDITSTREAM_fixed es;

		StreamInData sd={rtf.data(), rtf.size()};

		es.dwCookie = (DWORD_PTR)&sd;
		es.dwError = 0;
		es.pfnCallback = (EDITSTREAMCALLBACK)TextToRichTextControlCallback;

		SendMessage(hwndText, EM_STREAMIN, SF_RTF, (LPARAM)&es);
		SendMessage(hwndText, EM_SETSEL, 0, 0);
		SetFocus(hwndText);
	}
}

INT_PTR CALLBACK VDShowChangeLogDlgProcW32(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_INITDIALOG:
		TextToRichTextControl((LPCTSTR)lParam, hdlg, GetDlgItem(hdlg, IDC_TEXT));
		return FALSE;
	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDOK: case IDCANCEL:
			EndDialog(hdlg, 0);
			return TRUE;
		}
		break;
	}

	return FALSE;
}

void VDShowChangeLog(VDGUIHandle hParent) {
	HMODULE hmod = VDLoadSystemLibraryW32("riched32.dll");
	DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_CHANGE_LOG), (HWND)hParent, VDShowChangeLogDlgProcW32, (LPARAM)MAKEINTRESOURCE(IDR_CHANGES));
	FreeLibrary(hmod);
}

void VDShowReleaseNotes(VDGUIHandle hParent) {
	HMODULE hmod = VDLoadSystemLibraryW32("riched32.dll");
	DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_CHANGE_LOG), (HWND)hParent, VDShowChangeLogDlgProcW32, (LPARAM)MAKEINTRESOURCE(IDR_RELEASE_NOTES));
	FreeLibrary(hmod);
}

void VDDumpChangeLog() {
	HRSRC hResource = FindResourceW(NULL, MAKEINTRESOURCE(IDR_CHANGES), L"STUFF");

	if (!hResource)
		return;

	HGLOBAL hGlobal = LoadResource(NULL, hResource);
	if (!hGlobal)
		return;

	LPVOID lpData = LockResource(hGlobal);
	if (!lpData)
		return;

	const char *s = (const char *)lpData;

	while(*s!='\r') ++s;

	while(*s == '\r' || *s == '\n') {
		const char other = (*s++ ^ ('\r' ^ '\n'));

		if (*s == other)
			++s;
	}

	tTextStream lineBuffer;
	VDStringA breakLineBuffer;

	bool foundNonIndentedLine = false;

	while(*s) {
		// parse line
		if (*s != ' ' && *s != '\r' && *s != '\n') {
			if (foundNonIndentedLine)
				break;

			foundNonIndentedLine = true;
		}

		const char *end = s;
		while(*end && *end != '\r' && *end != '\n')
			++end;

		lineBuffer.clear();
		append_cooked(lineBuffer, s, end, false);

		// skip line termination
		s = end;
		if (*s == '\r' || *s == '\n') {
			++s;
			if ((s[0] ^ s[-1]) == ('\r' ^ '\n'))
				++s;
		}

		lineBuffer.push_back(0);

		// break into lines
		const char *t = lineBuffer.data();
		int maxLine = 78;

		breakLineBuffer.clear();

		do {
			const char *lineStart = t;
			const char *break1 = NULL;
			const char *break2 = NULL;

			do {
				while(*t && *t != ' ')
					++t;

				const char *u = t;

				while(*t && *t == ' ')
					++t;

				if (u - lineStart > maxLine) {
					if (!break1) {
						break1 = u + maxLine;
						break2 = break1;
					}

					break;
				}

				break1 = u;
				break2 = t;
			} while(*t);

			breakLineBuffer.append(lineStart, break1);
			VDLog(kVDLogInfo, VDTextAToW(breakLineBuffer.data(), breakLineBuffer.size()));

			t = break2;
			breakLineBuffer.clear();
			breakLineBuffer.resize(5, ' ');

			maxLine = 73;
		} while(*t);
	}
}
