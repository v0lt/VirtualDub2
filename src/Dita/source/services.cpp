//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2004 Avery Lee
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

#include <stdafx.h>
#include <map>
#include <vector>
#include <string.h>

#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/VDString.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>

#ifndef OPENFILENAME_SIZE_VERSION_400
#define OPENFILENAME_SIZE_VERSION_400 0x4c
#endif

#ifndef BIF_NEWDIALOGSTYLE
#define BIF_NEWDIALOGSTYLE     0x0040
#endif

///////////////////////////////////////////////////////////////////////////

#if 0
IVDUIContext *VDGetUIContext() {
	static vdautoptr<IVDUIContext> spctx(VDCreateUIContext());

	return spctx;
}
#endif

///////////////////////////////////////////////////////////////////////////

struct FilespecEntry {
	wchar_t szFile[MAX_PATH];
};

typedef std::map<long, FilespecEntry> tFilespecMap;

// Visual C++ 7.0 has a bug with lock initialization in the STL -- the lock
// code uses a normal constructor, and thus usually executes too late for
// static construction.
tFilespecMap *g_pFilespecMap;
VDCriticalSection g_csFilespecMap;

///////////////////////////////////////////////////////////////////////////

namespace {
	int FileFilterLength(const wchar_t *pszFilter) {
		const wchar_t *s = pszFilter;

		while(*s) {
			while(*s++);
			while(*s++);
		}

		return s - pszFilter;
	}

#pragma pack(push, 1)
	struct DialogTemplateHeader {		// DLGTEMPLATEEX psuedo-struct from MSDN
		WORD		signature;			// DOCBUG: This comes first!
		WORD		dlgVer;
		DWORD		helpID;
		DWORD		exStyle;
		DWORD		style;
		WORD		cDlgItems;
		short		x;
		short		y;
		short		cx;
		short		cy;
		WORD		menu;
		WORD		windowClass;
		WCHAR		title[1];
		WORD		pointsize;
		WORD		weight;
		BYTE		italic;
		BYTE		charset;
		WCHAR		typeface[13];
	};

	struct DialogTemplateItem {
		DWORD	helpID; 
		DWORD	exStyle; 
		DWORD	style; 
		short	x; 
		short	y; 
		short	cx; 
		short	cy; 
		DWORD	id;					//DOCERR
	};
#pragma pack(pop)

	struct DialogTemplateBuilder {
		std::vector<uint8> data;

		DialogTemplateBuilder();
		~DialogTemplateBuilder();

		void push(const void *p, int size) {
			int pos = data.size();
			data.resize(pos + size);
			memcpy(&data[pos], p, size);
		}

		void SetRect(int x, int y, int cx, int cy);
		void AddControlBase(DWORD exStyle, DWORD style, int x, int y, int cx, int cy, int id);
		void AddLabel(int id, int x, int y, int cx, int cy, const wchar_t *text);
		void AddCheckbox(int id, int x, int y, int cx, int cy, const wchar_t *text);
		void AddNumericEdit(int id, int x, int y, int cx, int cy);
	};

	DialogTemplateBuilder::DialogTemplateBuilder() {
		static const DialogTemplateHeader hdr = {
			1,
			0xFFFF,
			0,
			0,
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | DS_3DLOOK | DS_CONTROL | DS_SHELLFONT,
			0,
			0, 0, 0, 0,
			0,
			0,
			0,
			8,
			FW_NORMAL,
			FALSE,
			ANSI_CHARSET,
			L"MS Shell Dlg"
		};

		push(&hdr, sizeof hdr);
	}

	DialogTemplateBuilder::~DialogTemplateBuilder() {
	}

	void DialogTemplateBuilder::SetRect(int x, int y, int cx, int cy) {
		DialogTemplateHeader& hdr = (DialogTemplateHeader&)data.front();

		hdr.x = x;
		hdr.y = y;
		hdr.cx = cx;
		hdr.cy = cy;
	}

	void DialogTemplateBuilder::AddControlBase(DWORD exStyle, DWORD style, int x, int y, int cx, int cy, int id) {
		data.resize((data.size()+3)&~3, 0);

		const DialogTemplateItem item = {
			0,
			exStyle,
			style,
			x,
			y,
			cx,
			cy,
			id
		};

		push(&item, sizeof item);

		DialogTemplateHeader& hdr = (DialogTemplateHeader&)data.front();
		++hdr.cDlgItems;

		hdr.cx = std::max<int>(hdr.cx, x+cx);
		hdr.cy = std::max<int>(hdr.cy, y+cy);
	}

	void DialogTemplateBuilder::AddLabel(int id, int x, int y, int cx, int cy, const wchar_t *text) {
		AddControlBase(0, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, x, y, cx, cy, id);

		const WORD wclass[2]={0xffff,0x0082};
		push(wclass, sizeof wclass);

		push(text, (wcslen(text)+1)*sizeof(WORD));

		const WORD extradata = 0;
		push(&extradata, sizeof(WORD));
	}

	void DialogTemplateBuilder::AddCheckbox(int id, int x, int y, int cx, int cy, const wchar_t *text) {
		AddControlBase(0, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, x, y, cx, cy, id);

		const WORD wclass[2]={0xffff,0x0080};
		push(wclass, sizeof wclass);

		push(text, (wcslen(text)+1)*sizeof(WORD));

		const WORD extradata = 0;
		push(&extradata, sizeof(WORD));
	}

	void DialogTemplateBuilder::AddNumericEdit(int id, int x, int y, int cx, int cy) {
		AddControlBase(WS_EX_CLIENTEDGE, WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_TABSTOP, x, y, cx, cy, id);

		const WORD wclassandtitle[4]={0xffff,0x0081,0x0030,0x0000};
		push(wclassandtitle, sizeof wclassandtitle);

		const WORD extradata = 0;
		push(&extradata, sizeof(WORD));
	}
}

#if 0
struct tester {
	static BOOL CALLBACK dlgproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
		return FALSE;
	}
	tester() {
		DialogTemplateBuilder builder;

		builder.AddLabel(1, 7, 7, 50, 14, L"hellow");
		builder.AddLabel(1, 7, 21, 50, 14, L"byebye");

		DialogBoxIndirect(GetModuleHandle(0), (LPCDLGTEMPLATE)&builder.data.front(), NULL, dlgproc);
	}
} g;
#endif

///////////////////////////////////////////////////////////////////////////

void VDInitFilespecSystem() {
	if (!g_pFilespecMap) {
		g_pFilespecMap = new tFilespecMap;
	}
}

void VDSaveFilespecSystemData() {
	vdsynchronized(g_csFilespecMap) {
		if (g_pFilespecMap) {
			VDRegistryAppKey key("Saved filespecs");

			for(tFilespecMap::const_iterator it(g_pFilespecMap->begin()), itEnd(g_pFilespecMap->end()); it!=itEnd; ++it) {
				long id = it->first;
				const FilespecEntry& fse = it->second;
				char buf[16];

				sprintf(buf, "%08x", id);
				key.setString(buf, VDFileSplitPathLeft(VDStringW(fse.szFile)).c_str());
			}
		}
	}
}

void VDLoadFilespecSystemData() {
	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();

		if (g_pFilespecMap) {
			VDRegistryAppKey key("Saved filespecs", false);
			VDRegistryValueIterator it(key);

			VDStringW value;
			while(const char *s = it.Next()) {
				unsigned long specKey = strtoul(s, NULL, 16);
				if (key.getString(s, value))
					VDSetLastLoadSavePath(specKey, value.c_str());
			}
		}
	}
}

void VDClearFilespecSystemData() {
	vdsynchronized(g_csFilespecMap) {
		if (g_pFilespecMap) {
			g_pFilespecMap->clear();
		}

		VDRegistryAppKey key("Saved filespecs");

		VDRegistryValueIterator it(key);

		VDStringW value;
		while(const char *s = it.Next()) {
			key.removeValue(s);
		}
	}
}

struct VDGetFileNameHook {
	static UINT_PTR CALLBACK HookFn(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam) {
		VDGetFileNameHook *pThis = (VDGetFileNameHook *)GetWindowLongPtr(hdlg, DWLP_USER);

		switch(uiMsg) {
		case WM_INITDIALOG:
			pThis = (VDGetFileNameHook *)(((const OPENFILENAMEW*)lParam)->lCustData);
			SetWindowLongPtr(hdlg, DWLP_USER, (LONG_PTR)pThis);
			pThis->Init(hdlg);
			return 0;

		case WM_NOTIFY:
			switch(((const NMHDR *)lParam)->code) {
			case CDN_FILEOK:
				return !pThis->Writeback(hdlg);
			}
		}

		return 0;
	}

	void Init(HWND hdlg) {
		for(int nOpts = 0; mpOpts[nOpts].mType; ++nOpts) {
			const VDFileDialogOption& opt = mpOpts[nOpts];
			const int id = 1000 + 16*nOpts;

			switch(opt.mType) {
			case VDFileDialogOption::kBool:
				CheckDlgButton(hdlg, id, !!mpOptVals[opt.mDstIdx]);
				break;
			case VDFileDialogOption::kInt:
				SetDlgItemInt(hdlg, id, mpOptVals[opt.mDstIdx], TRUE);
				break;
			case VDFileDialogOption::kEnabledInt:
				CheckDlgButton(hdlg, id, !!mpOptVals[opt.mDstIdx]);
				SetDlgItemInt(hdlg, id+1, mpOptVals[opt.mDstIdx+1], TRUE);
				break;
			}
		}
	}

	bool Writeback(HWND hdlg) {
		for(int nOpts = 0; mpOpts[nOpts].mType; ++nOpts) {
			const VDFileDialogOption& opt = mpOpts[nOpts];
			const int id = 1000 + 16*nOpts;
			BOOL bOk;
			INT v;

			switch(opt.mType) {
			case VDFileDialogOption::kBool:
				mpOptVals[opt.mDstIdx] = !!IsDlgButtonChecked(hdlg, id);
				break;
			case VDFileDialogOption::kInt:
				v = GetDlgItemInt(hdlg, id, &bOk, TRUE);
				if (!bOk) {
					MessageBeep(MB_ICONEXCLAMATION);
					SetFocus(GetDlgItem(hdlg, id));
					return false;
				}
				mpOptVals[opt.mDstIdx] = v;
				break;
			case VDFileDialogOption::kEnabledInt:
				mpOptVals[opt.mDstIdx] = !!IsDlgButtonChecked(hdlg, id);
				if (mpOptVals[opt.mDstIdx]) {
					v = GetDlgItemInt(hdlg, id+1, &bOk, TRUE);
					if (!bOk) {
						MessageBeep(MB_ICONEXCLAMATION);
						SetFocus(GetDlgItem(hdlg, id+1));
						return false;
					}
					mpOptVals[opt.mDstIdx+1] = v;
				}
				break;
			}
		}
		return true;
	}

	const VDFileDialogOption *mpOpts;
	int *mpOptVals;
};

//--------------------------------------------------------------------------------------------------------------
// hookOptions is not used for actual API call, any relevant fields are just copied from it:
//   flags
//   template, hook and stuff
//   filename

static const VDStringW VDGetFileName(bool bSaveAs, long nKey, VDGUIHandle ctxParent, const wchar_t *pszTitle, const wchar_t *pszFilters, const wchar_t *pszExt, const VDFileDialogOption *pOptions, int *pOptVals, OPENFILENAMEW *hookOptions) {
	FilespecEntry fsent;
	tFilespecMap::iterator it;

	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();
		
		it = g_pFilespecMap->find(nKey);

		if (it == g_pFilespecMap->end()) {
			std::pair<tFilespecMap::iterator, bool> r = g_pFilespecMap->insert(tFilespecMap::value_type(nKey, FilespecEntry()));

			if (!r.second) {
				VDStringW empty;
				return empty;
			}

			it = r.first;

			(*it).second.szFile[0] = 0;
		}

		fsent = (*it).second;
	}

	OPENFILENAMEW ofn = {};

	// Slight annoyance: If we want to use custom templates and still keep the places
	// bar, the lStructSize parameter must be greater than OPENFILENAME_SIZE_VERSION_400.
	// But if sizeof(OPENFILENAME) is used under Windows 95/98, the open call fails.
	// Argh.

	ofn.lStructSize			= sizeof(OPENFILENAMEW);
	ofn.hwndOwner			= (HWND)ctxParent;
	ofn.lpstrCustomFilter	= NULL;
	ofn.nFilterIndex		= 0;
	ofn.lpstrFileTitle		= NULL;
	ofn.lpstrInitialDir		= NULL;
	ofn.Flags				= OFN_PATHMUSTEXIST|OFN_ENABLESIZING|OFN_EXPLORER|OFN_OVERWRITEPROMPT|OFN_HIDEREADONLY;

	if (bSaveAs) {
		ofn.Flags |= OFN_OVERWRITEPROMPT;
	} else {
		ofn.Flags |= OFN_FILEMUSTEXIST;
	}

	DialogTemplateBuilder builder;
	VDGetFileNameHook hook = { pOptions, pOptVals };
	int nReadOnlyIndex = -1;
	int nSelectedFilterIndex = -1;
	bool selectedFilterAlways = false;

	if (pOptions) {
		int y = 0;
		bool force_template = false;

		for(int nOpts = 0; pOptions[nOpts].mType; ++nOpts) {
			const VDFileDialogOption& opt = pOptions[nOpts];
			const wchar_t *s = opt.mpLabel;
			const int id = 1000 + 16*nOpts;
			const int sw = s ? 4*wcslen(s) : 0;

			switch(pOptions[nOpts].mType) {
			case VDFileDialogOption::kBool:
				builder.AddCheckbox(id, 5, y, 10+sw, 12, s);
				y += 12;
				break;
			case VDFileDialogOption::kInt:
				builder.AddLabel(0, 5, y, sw, 12, s);
				builder.AddNumericEdit(id, 9+sw, y, 50, 12);
				y += 12;
				break;
			case VDFileDialogOption::kEnabledInt:
				builder.AddCheckbox(id, 5, y+1, 10+sw, 12, s);
				builder.AddNumericEdit(id+1, 19+sw, y+1, 50, 12);
				y += 14;
				break;
			case VDFileDialogOption::kReadOnly:
				VDASSERT(nReadOnlyIndex < 0);
				nReadOnlyIndex = opt.mDstIdx;
				ofn.Flags &= ~OFN_HIDEREADONLY;
				if (pOptVals[nReadOnlyIndex])
					ofn.Flags |= OFN_READONLY;
				break;
			case VDFileDialogOption::kSelectedFilter_always:
				selectedFilterAlways = true;
			case VDFileDialogOption::kSelectedFilter:
				VDASSERT(nSelectedFilterIndex < 0);
				nSelectedFilterIndex = opt.mDstIdx;
				ofn.nFilterIndex = pOptVals[opt.mDstIdx];
				break;
			case VDFileDialogOption::kConfirmFile:
				if (!pOptVals[opt.mDstIdx])
					ofn.Flags &= ~OFN_OVERWRITEPROMPT;
				break;
			case VDFileDialogOption::kForceTemplate:
				force_template = pOptVals[opt.mDstIdx]!=0;
				break;
			}
		}

		if (y > 0 || force_template) {
			ofn.Flags		|= OFN_ENABLETEMPLATEHANDLE | OFN_ENABLEHOOK;
			ofn.hInstance	= (HINSTANCE)&builder.data.front();
			ofn.lpfnHook	= VDGetFileNameHook::HookFn;
			ofn.lCustData	= (LPARAM)&hook;
		}
	}

	VDStringW init_filename(fsent.szFile);

	if (hookOptions) {
		ofn.Flags			|= hookOptions->Flags;
		ofn.hInstance		= hookOptions->hInstance;
		ofn.lpTemplateName	= hookOptions->lpTemplateName;
		ofn.lpfnHook		= hookOptions->lpfnHook;
		ofn.lCustData		= hookOptions->lCustData;

		if (hookOptions->lpstrFile)
			init_filename = hookOptions->lpstrFile;
	}

	bool existingFileName = false;
	if (!init_filename.empty()) {
		wchar_t lastChar = init_filename.end()[-1];

		if (lastChar != '\\' && lastChar != ':')
			existingFileName = true;

		if (selectedFilterAlways) {
			const wchar_t* ext = VDFileSplitExt(init_filename.c_str());
			if (ext) {
				init_filename.erase(ext-init_filename.c_str()+1);
				init_filename += pszExt;
			}
		}
	}

	VDStringW strFilename;
	bool bSuccess = false;

	// A little gotcha here:
	//
	// Docs for OPENFILENAME say that lpstrInitialFile is used, otherwise a path in
	// lpstrFile is used (2000/XP). What it doesn't mention is that if lpstrFile
	// points to only a directory, not only won't it use that path, but it will also
	// ignore any path in lpstrInitialDir. So we have to make sure that we either
	// supply a full file path in lpstrFile or a directory path in lpstrInitialDir,
	// but never the same in both.

	{
		wchar_t wszFile[MAX_PATH];

		if (existingFileName)
			wcsncpyz(wszFile, init_filename.c_str(), MAX_PATH);
		else
			wszFile[0] = 0;

		ofn.lpstrFilter		= pszFilters;
		ofn.lpstrFile		= wszFile;
		ofn.nMaxFile		= MAX_PATH;
		ofn.lpstrTitle		= pszTitle;
		ofn.lpstrDefExt		= pszExt;
		ofn.lpstrInitialDir	= existingFileName ? NULL : init_filename.c_str();

		BOOL (WINAPI *pfn)(OPENFILENAMEW *) = (bSaveAs ? GetSaveFileNameW : GetOpenFileNameW);
		BOOL result = pfn(&ofn);

		// If the last path is no longer valid the dialog may fail to initialize, so if it's not
		// a cancel we retry with no preset filename.
		if (!result && CommDlgExtendedError()) {
			wszFile[0] = 0;
			ofn.lpstrInitialDir = NULL;
			result = pfn(&ofn);
		}

		if (result) {
			wcsncpyz(fsent.szFile, wszFile, MAX_PATH);
			bSuccess = true;
		}
	}

	if (bSuccess) {
		if (nReadOnlyIndex >= 0)
			pOptVals[nReadOnlyIndex] = !!(ofn.Flags & OFN_READONLY);

		strFilename = fsent.szFile;
	}

	if (bSuccess || selectedFilterAlways) {
		if (nSelectedFilterIndex >= 0)
			pOptVals[nSelectedFilterIndex] = ofn.nFilterIndex;
	}

	if (bSuccess) {
		vdsynchronized(g_csFilespecMap) {
			(*it).second = fsent;
		}
	}

	return strFilename;
}

const VDStringW VDGetLoadFileName(long nKey, VDGUIHandle ctxParent, const wchar_t *pszTitle, const wchar_t *pszFilters, const wchar_t *pszExt, const VDFileDialogOption *pOptions, int *pOptVals, OPENFILENAMEW *hookOptions) {
	return VDGetFileName(false, nKey, ctxParent, pszTitle, pszFilters, pszExt, pOptions, pOptVals, hookOptions);
}

const VDStringW VDGetSaveFileName(long nKey, VDGUIHandle ctxParent, const wchar_t *pszTitle, const wchar_t *pszFilters, const wchar_t *pszExt, const VDFileDialogOption *pOptions, int *pOptVals, OPENFILENAMEW *hookOptions) {
	return VDGetFileName(true, nKey, ctxParent, pszTitle, pszFilters, pszExt, pOptions, pOptVals, hookOptions);
}

void VDSetLastLoadSavePath(long nKey, const wchar_t *path) {
	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();
		
		tFilespecMap::iterator it = g_pFilespecMap->find(nKey);

		if (it == g_pFilespecMap->end()) {
			std::pair<tFilespecMap::iterator, bool> r = g_pFilespecMap->insert(tFilespecMap::value_type(nKey, FilespecEntry()));

			if (!r.second)
				return;

			it = r.first;
		}

		FilespecEntry& fsent = (*it).second;

		wcsncpyz(fsent.szFile, path, std::size(fsent.szFile));
	}
}

const VDStringW VDGetLastLoadSavePath(long nKey) {
	VDStringW result;

	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();
		
		tFilespecMap::iterator it = g_pFilespecMap->find(nKey);

		if (it != g_pFilespecMap->end()) {
			FilespecEntry& fsent = (*it).second;

			result = fsent.szFile;
		}
	}

	return result;
}

void VDSetLastLoadSaveFileName(long nKey, const wchar_t *fileName) {
	vdsynchronized(g_csFilespecMap) {
		VDInitFilespecSystem();
		
		tFilespecMap::iterator it = g_pFilespecMap->find(nKey);

		if (it == g_pFilespecMap->end()) {
			std::pair<tFilespecMap::iterator, bool> r = g_pFilespecMap->insert(tFilespecMap::value_type(nKey, FilespecEntry()));

			if (!r.second)
				return;

			it = r.first;
		}

		FilespecEntry& fsent = (*it).second;

		VDStringW newPath(VDMakePath(VDFileSplitPathLeft(VDStringW(fsent.szFile)).c_str(), fileName));

		wcsncpyz(fsent.szFile, newPath.c_str(), std::size(fsent.szFile));
	}
}

///////////////////////////////////////////////////////////////////////////

// stupid common dialog returns unparsed string
// this solves some common cases

VDStringW OpenSave_GetFileName(HWND dlg) {
	wchar_t buf[MAX_PATH];
	CommDlg_OpenSave_GetSpec(GetParent(dlg),buf,MAX_PATH);
	VDStringW s(buf);
	if (s.length()>=2 && s[0]=='"') {
		int x0 = 1;
		{for (int i=s.length()-1; i>x0; i--){
			int c = s[i];
			if (c==' ') continue;
			if (c=='"'){
				s = s.subspan(x0,i-x0); 
				break;
			}
		}}
	}

	return s;
}

VDStringW OpenSave_GetFilePath(HWND dlg) {
	wchar_t buf[MAX_PATH];
	CommDlg_OpenSave_GetFolderPath(GetParent(dlg),buf,MAX_PATH);
	VDStringW s(buf);
	if (s.empty()) return VDStringW();
	VDStringW name = OpenSave_GetFileName(dlg);
	if (name.empty()) return VDStringW();
	VDParsedPath pname(name.c_str());
	if (pname.IsRelative()) 
		s = VDFileGetCanonicalPath((s + name).c_str());
	else
		s = pname.ToString();
	return s;
}

///////////////////////////////////////////////////////////////////////////

typedef std::map<long, DirspecEntry> tDirspecMap;
tDirspecMap *g_pDirspecMap;

void VDShutdownFilespecSystem() {
	delete g_pFilespecMap;
	g_pFilespecMap = 0;
	delete g_pDirspecMap;
	g_pDirspecMap = 0;
}

///////////////////////////////////////////////////////////////////////////

DirspecEntry* VDGetDirSpec(long nKey) {
	if (!g_pDirspecMap)
		g_pDirspecMap = new tDirspecMap;

	tDirspecMap::iterator it = g_pDirspecMap->find(nKey);

	if (it == g_pDirspecMap->end()) {
		std::pair<tDirspecMap::iterator, bool> r = g_pDirspecMap->insert(tDirspecMap::value_type(nKey, DirspecEntry()));

		if (!r.second) {
			return 0;
		}

		it = r.first;

		(*it).second.szFile[0] = 0;
	}

	DirspecEntry& fsent = (*it).second;
	return &fsent;
}

const VDStringW VDGetDirectory(long nKey, VDGUIHandle ctxParent, const wchar_t *pszTitle) {
	DirspecEntry& fsent = *VDGetDirSpec(nKey);
	if (!&fsent) return VDStringW();
	bool bSuccess = false;

	if (SUCCEEDED(CoInitialize(NULL))) {
		IMalloc *pMalloc;

		if (SUCCEEDED(SHGetMalloc(&pMalloc))) {

			{
				IFileOpenDialog *pFileOpen;
				HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

				if (SUCCEEDED(hr)) {
					pFileOpen->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
					pFileOpen->SetTitle(pszTitle);

					if (fsent.szFile[0]) {
						HMODULE hmod = GetModuleHandleA("shell32.dll");
						typedef HRESULT (APIENTRY *tpSHCreateItemFromParsingName)(PCWSTR pszPath, IBindCtx *pbc, REFIID riid, void **ppv);
						tpSHCreateItemFromParsingName pSHCreateItemFromParsingName = (tpSHCreateItemFromParsingName)GetProcAddress(hmod, "SHCreateItemFromParsingName");

						IShellItem *dir = 0;
						pSHCreateItemFromParsingName(fsent.szFile,0,IID_IShellItem,(void**)&dir);
						pFileOpen->SetFolder(dir);
						dir->Release();
					}

					hr = pFileOpen->Show(NULL);

					if (SUCCEEDED(hr)) {
						IShellItem *pItem;
						hr = pFileOpen->GetResult(&pItem);
						if (SUCCEEDED(hr)) {
							PWSTR pszFilePath;
							hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
							if (SUCCEEDED(hr)) {
								wcscpy(fsent.szFile, pszFilePath);
								bSuccess = true;
								CoTaskMemFree(pszFilePath);
							}
							pItem->Release();
						}
					}
					pFileOpen->Release();
				}

			}
		}

		CoUninitialize();
	}

	VDStringW strDir;

	if (bSuccess)
		strDir = fsent.szFile;

	return strDir;
}
