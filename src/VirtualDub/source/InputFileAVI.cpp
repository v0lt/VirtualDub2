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
#include <process.h>

#include <windows.h>
#include <vfw.h>
#include <commdlg.h>

#include "InputFile.h"
#include "InputFileAVI.h"
#include "AudioSource.h"
#include "AudioSourceAVI.h"
#include "VideoSource.h"
#include "VideoSourceAVI.h"
#include <vd2/system/debug.h>
#include <vd2/system/error.h>
#include <vd2/system/filesys.h>
#include <vd2/system/thread.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/resources.h>
#include <vd2/Dita/services.h>
#include <vd2/Riza/audioformat.h>
#include <vd2/Riza/bitmap.h>
#include "AVIStripeSystem.h"
#include "AVIReadHandler.h"

#include "gui.h"
#include "oshelper.h"
#include "prefs.h"
#include "misc.h"
#include <vd2/VDLib/Dialog.h>
#include <aviriff.h>

#include "resource.h"

extern HINSTANCE g_hInst;
extern const wchar_t fileFiltersAppendAVI[];
extern HWND g_hWnd;

bool VDPreferencesIsPreferInternalVideoDecodersEnabled();
bool VDPreferencesAVIRekey();
bool VDPreferencesAVIIgnoreIndex();

namespace {
	enum { kVDST_InputFileAVI = 4 };

	enum {
		kVDM_OpeningFile,			// AVI: Opening file "%ls"
		kVDM_RekeyNotSpecified,		// AVI: Keyframe flag reconstruction was not specified in open options and the video stream is not a known keyframe-only type.  Seeking in the video stream may be extremely slow.
		kVDM_Type1DVNoSound,		// AVI: Type-1 DV file detected -- VirtualDub cannot extract audio from this type of interleaved stream.
		kVDM_InvalidBlockAlign		// AVI: An invalid nBlockAlign value of zero in the audio stream format was fixed.
	};
}

int detect_avi(VDXMediaInfo& info, const void *pHeader, int32 nHeaderSize)
{
	if(nHeaderSize<64) return -1;
	uint8* data = (uint8*)pHeader;
	int rsize = nHeaderSize;

	RIFFCHUNK ch;
	memcpy(&ch,data,sizeof(ch)); data+=sizeof(ch); rsize-=sizeof(ch);
	if(ch.fcc!=0x46464952) return -1; //RIFF
	DWORD fmt;
	memcpy(&fmt,data,4); data+=4; rsize-=4;
	if(fmt!=0x20495641) return -1; //AVI
	memcpy(&ch,data,sizeof(ch)); data+=sizeof(ch); rsize-=sizeof(ch);
	if(ch.fcc!=0x5453494C) return -1; //LIST
	memcpy(&fmt,data,4); data+=4; rsize-=4;
	if(fmt!=0x6C726468) return -1; //hdrl

	memcpy(&ch,data,sizeof(ch));
	if(ch.fcc!=ckidMAINAVIHEADER) return -1; //avih

	if(rsize<sizeof(AVIMAINHEADER)){
		AVIMAINHEADER mh = {0};
		memcpy(&mh,data,rsize); data+=rsize; rsize=0;
		return 1;
	}

	AVIMAINHEADER mh;
	memcpy(&mh,data,sizeof(mh)); data+=sizeof(mh); rsize-=sizeof(mh);
	info.width = mh.dwWidth;
	info.height = mh.dwHeight;
	wcscpy(info.format_name,L"AVI");

	if(rsize<sizeof(ch)) return 1;
	memcpy(&ch,data,sizeof(ch)); data+=sizeof(ch); rsize-=sizeof(ch);
	if(ch.fcc!=0x5453494C) return -1; //LIST

	if(rsize<sizeof(fmt)) return 1;
	memcpy(&fmt,data,4); data+=4; rsize-=4;
	if(fmt!=ckidSTREAMLIST) return -1; //strl

	if(rsize<sizeof(AVISTREAMHEADER)) return 1;
	AVISTREAMHEADER sh;
	memcpy(&sh,data,sizeof(sh)); data+=sizeof(sh); rsize-=sizeof(sh);
	if(sh.fcc!=ckidSTREAMHEADER) return -1; //strh

	// reject if there is unsupported video codec
	if(sh.fccType==streamtypeVIDEO){
		DWORD h1 = sh.fccHandler;
		char* ch2 = (char*)(&h1);
		{for(int i=0; i<4; i++){
			int v = ch2[i];
			info.vcodec_name[i] = (wchar_t)v;
			info.vcodec_name[i+1] = 0;
		}}
	}

	return 1;
}

/////////////////////////////////////////////////////////////////////

VDAVIStreamSource::VDAVIStreamSource(InputFileAVI *pParent)
	: mpParent(pParent)
{
	mpParent->Attach(this);
}

VDAVIStreamSource::VDAVIStreamSource(const VDAVIStreamSource& src)
	: mpParent(src.mpParent)
{
	mpParent->Attach(this);
}

VDAVIStreamSource::~VDAVIStreamSource() {
	mpParent->Detach(this);
}

/////////////////////////////////////////////////////////////////////

class VDInputDriverAVI2 : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return L"Audio/video interleave input driver (internal)"; }

	int GetDefaultPriority() {
		return 0;
	}

	uint32 GetFlags() { return kF_Video | kF_Audio | kF_SupportsOpts; }

	const wchar_t *GetFilenamePattern() {
		return L"Audio/video interleave (*.avi,*.divx)\0*.avi;*.divx\0";
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		if (nHeaderSize < 12) return kDC_Error_MoreData;
		if (nHeaderSize >= 12) {
			if (!memcmp(pHeader, "RIFF", 4) && !memcmp((char*)pHeader+8, "AVI ", 4))
				return kDC_High;
		}

		return kDC_None;
	}

	DetectionConfidence DetectBySignature3(VDXMediaInfo& info, const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize, const wchar_t* fileName) {
		detect_avi(info, pHeader, nHeaderSize);
		return DetectBySignature(pHeader,nHeaderSize,pFooter,nFooterSize,nFileSize);
	}

	InputFile *CreateInputFile(uint32 flags) {
		InputFileAVI *pf = new_nothrow InputFileAVI;

		if (!pf)
			throw MyMemoryError();

		if (flags & kOF_Quiet)
			pf->setAutomated(true);

		if (flags & kOF_AutoSegmentScan)
			pf->EnableSegmentAutoscan();

		return pf;
	}
};

class VDInputDriverAVI1 : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return L"AVIFile/Avisynth input driver (internal)"; }

	int GetDefaultPriority() {
		return -4;
	}

	uint32 GetFlags() { return kF_Video | kF_Audio | kF_SupportsOpts; }

	const wchar_t *GetFilenamePattern() {
		return L"AVIFile input driver (compat.) (*.avs,*.vdr,*.vpy)\0*.avs;*.vdr;*.vpy\0";
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		size_t l = wcslen(pszFilename);
		if (l > 4) {
			if (!_wcsicmp(pszFilename + l - 4, L".avs"))
				return true;
			if (!_wcsicmp(pszFilename + l - 4, L".vdr"))
				return true;
			if (!_wcsicmp(pszFilename + l - 4, L".vpy"))
				return true;
		}

		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		if (nHeaderSize >= 12) {
			if (!memcmp(pHeader, "RIFF", 4) && !memcmp((char*)pHeader+8, "VDRM", 4))
				return kDC_High;
		}

		return kDC_None;
	}

	InputFile *CreateInputFile(uint32 flags) {
		InputFileAVI *pf = new_nothrow InputFileAVI;

		if (!pf)
			throw MyMemoryError();

		pf->ForceCompatibility();

		if (flags & kOF_Quiet)
			pf->setAutomated(true);

		if (flags & kOF_AutoSegmentScan)
			pf->EnableSegmentAutoscan();

		return pf;
	}
};

extern IVDInputDriver *VDCreateInputDriverAVI1() { return new VDInputDriverAVI1; }
extern IVDInputDriver *VDCreateInputDriverAVI2() { return new VDInputDriverAVI2; }

/////////////////////////////////////////////////////////////////////

char InputFileAVI::szME[]="AVI Import Filter";

InputFileAVI::InputFileAVI() {
	fInternalDecoder = VDPreferencesIsPreferInternalVideoDecodersEnabled();

	fIgnoreIndex = VDPreferencesAVIIgnoreIndex();
	fRedoKeyFlags = VDPreferencesAVIRekey();
}

InputFileAVI::~InputFileAVI() {
	if (pAVIFile)
		pAVIFile->Release();
}

///////////////////////////////////////////////

class InputFileAVIOptions : public InputFileOptions {
public:
	struct InputFileAVIOpts {
		int len;
		int iMJPEGMode;
		FOURCC fccForceVideo;
		FOURCC fccForceVideoHandler;
		long lForceAudioHz;

		bool fCompatibilityMode;
		bool fAcceptPartial;
		char indexFlags;
		bool fInternalDecoder;
		bool fDisableFastIO;

		InputFileAVIOpts() {
			iMJPEGMode = 0;
			fccForceVideo = 0;
			fccForceVideoHandler = 0;
			lForceAudioHz = 0;
			fCompatibilityMode = false;
			fAcceptPartial = false;
			indexFlags = 0;
			if (VDPreferencesAVIRekey()) indexFlags|=1;
			if (VDPreferencesAVIIgnoreIndex()) indexFlags|=2;
			fInternalDecoder = VDPreferencesIsPreferInternalVideoDecodersEnabled();
			fDisableFastIO = false;
		}
	} opts;

	~InputFileAVIOptions();

	bool read(const char *buf);
	int write(char *buf, int buflen) const;

	static INT_PTR APIENTRY SetupDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};

InputFileAVIOptions::~InputFileAVIOptions() {
}

bool InputFileAVIOptions::read(const char *buf) {
	const InputFileAVIOpts *pp = (const InputFileAVIOpts *)buf;

	if (pp->len != sizeof(InputFileAVIOpts))
		return false;

	opts = *pp;

	return true;
}

int InputFileAVIOptions::write(char *buf, int buflen) const {
	if (buf) {
		InputFileAVIOpts *pp = (InputFileAVIOpts *)buf;

		if (buflen<sizeof(InputFileAVIOpts))
			return 0;

		*pp = opts;
		pp->len = sizeof(InputFileAVIOpts);
	}

	return sizeof(InputFileAVIOpts);
}

///////

INT_PTR APIENTRY InputFileAVIOptions::SetupDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
 	InputFileAVIOptions *thisPtr = (InputFileAVIOptions *)GetWindowLongPtr(hDlg, DWLP_USER);

	switch(message) {
		case WM_INITDIALOG:
			thisPtr = (InputFileAVIOptions *)lParam;
			SetWindowLongPtr(hDlg, DWLP_USER, lParam);
			SendDlgItemMessage(hDlg, IDC_FORCE_FOURCC, EM_LIMITTEXT, 4, 0);
			CheckDlgButton(hDlg, IDC_IF_NORMAL, BST_CHECKED);
			CheckDlgButton(hDlg, IDC_AVI_INTERNALDECODER, thisPtr->opts.fInternalDecoder);
			CheckDlgButton(hDlg, IDC_AVI_REKEY, (thisPtr->opts.indexFlags&1)!=0);
			CheckDlgButton(hDlg, IDC_AVI_IGNIDX, (thisPtr->opts.indexFlags&2)!=0);
			return TRUE;

		case WM_COMMAND:
			switch(LOWORD(wParam)) {
			case IDCANCEL:
				if (GetDlgItem(hDlg, IDC_ACCEPTPARTIAL))
					thisPtr->opts.fAcceptPartial = !!IsDlgButtonChecked(hDlg, IDC_ACCEPTPARTIAL);

				thisPtr->opts.fCompatibilityMode = !!IsDlgButtonChecked(hDlg, IDC_AVI_COMPATIBILITYMODE);
				thisPtr->opts.indexFlags = !!IsDlgButtonChecked(hDlg, IDC_AVI_REKEY);
				if (IsDlgButtonChecked(hDlg, IDC_AVI_IGNIDX)) thisPtr->opts.indexFlags |= 2;
				thisPtr->opts.fInternalDecoder = !!IsDlgButtonChecked(hDlg, IDC_AVI_INTERNALDECODER);
				thisPtr->opts.fDisableFastIO = !!IsDlgButtonChecked(hDlg, IDC_AVI_DISABLEOPTIMIZEDIO);

				if (IsDlgButtonChecked(hDlg, IDC_IF_NORMAL))
					thisPtr->opts.iMJPEGMode = VideoSourceAVI::IFMODE_NORMAL;
				else if (IsDlgButtonChecked(hDlg, IDC_IF_SWAP))
					thisPtr->opts.iMJPEGMode = VideoSourceAVI::IFMODE_SWAP;
				else if (IsDlgButtonChecked(hDlg, IDC_IF_SPLITNOSWAP))
					thisPtr->opts.iMJPEGMode = VideoSourceAVI::IFMODE_SPLIT1;
				else if (IsDlgButtonChecked(hDlg, IDC_IF_SPLITSWAP))
					thisPtr->opts.iMJPEGMode = VideoSourceAVI::IFMODE_SPLIT2;
				else if (IsDlgButtonChecked(hDlg, IDC_IF_DISCARDFIRST))
					thisPtr->opts.iMJPEGMode = VideoSourceAVI::IFMODE_DISCARD1;
				else if (IsDlgButtonChecked(hDlg, IDC_IF_DISCARDSECOND))
					thisPtr->opts.iMJPEGMode = VideoSourceAVI::IFMODE_DISCARD2;

				if (IsDlgButtonChecked(hDlg, IDC_FORCE_FOURCC)) {
					union {
						char c[5];
						FOURCC fccType;
					};
					int i;

					i = SendDlgItemMessageA(hDlg, IDC_FOURCC, WM_GETTEXT, sizeof c, (LPARAM)c);

					memset(c+i, ' ', 5-i);

					if (fccType == 0x20202020)
						fccType = ' BID';		// force nothing to DIB, since 0 means no force

					thisPtr->opts.fccForceVideo = fccType;
				} else
					thisPtr->opts.fccForceVideo = 0;

				if (IsDlgButtonChecked(hDlg, IDC_FORCE_HANDLER)) {
					union {
						char c[5];
						FOURCC fccType;
					};
					int i;

					i = SendDlgItemMessageA(hDlg, IDC_FOURCC2, WM_GETTEXT, sizeof c, (LPARAM)c);

					memset(c+i, ' ', 5-i);

					if (fccType == 0x20202020)
						fccType = ' BID';		// force nothing to DIB, since 0 means no force

					thisPtr->opts.fccForceVideoHandler = fccType;
				} else
					thisPtr->opts.fccForceVideoHandler = 0;

				if (IsDlgButtonChecked(hDlg, IDC_FORCE_SAMPRATE))
					thisPtr->opts.lForceAudioHz = GetDlgItemInt(hDlg, IDC_AUDIORATE, NULL, FALSE);
				else
					thisPtr->opts.lForceAudioHz = 0;

				EndDialog(hDlg, 0);
				return TRUE;

			case IDC_FORCE_FOURCC:
				EnableWindow(GetDlgItem(hDlg, IDC_FOURCC), IsDlgButtonChecked(hDlg, IDC_FORCE_FOURCC));
				return TRUE;

			case IDC_FORCE_HANDLER:
				EnableWindow(GetDlgItem(hDlg, IDC_FOURCC2), IsDlgButtonChecked(hDlg, IDC_FORCE_HANDLER));
				return TRUE;

			case IDC_FORCE_SAMPRATE:
				EnableWindow(GetDlgItem(hDlg, IDC_AUDIORATE), IsDlgButtonChecked(hDlg, IDC_FORCE_SAMPRATE));
				return TRUE;
			}
			break;
	}

	return FALSE;
}

void InputFileAVI::setOptions(InputFileOptions *_ifo) {
	InputFileAVIOptions *ifo = (InputFileAVIOptions *)_ifo;

	fCompatibilityMode	= ifo->opts.fCompatibilityMode;
	fAcceptPartial		= ifo->opts.fAcceptPartial;
	fIgnoreIndex		= (ifo->opts.indexFlags & 2)!=0;
	fRedoKeyFlags		= (ifo->opts.indexFlags & 1)!=0;
	fInternalDecoder	= ifo->opts.fInternalDecoder;
	fDisableFastIO		= ifo->opts.fDisableFastIO;
	iMJPEGMode			= ifo->opts.iMJPEGMode;
	fccForceVideo		= ifo->opts.fccForceVideo;
	fccForceVideoHandler= ifo->opts.fccForceVideoHandler;
	lForceAudioHz		= ifo->opts.lForceAudioHz;
}

InputFileOptions *InputFileAVI::createOptions(const void *buf, uint32 len) {
	InputFileAVIOptions *ifo = new InputFileAVIOptions();

	if (!ifo) throw MyMemoryError();

	if (!ifo->read((const char *)buf)) {
		delete ifo;
		return NULL;
	}

	return ifo;
}

InputFileOptions *InputFileAVI::promptForOptions(VDGUIHandle hwnd) {
	InputFileAVIOptions *ifo = new InputFileAVIOptions();

	if (!ifo) throw MyMemoryError();

	DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_EXTOPENOPTS_AVI),
			(HWND)hwnd, InputFileAVIOptions::SetupDlgProc, (LPARAM)ifo);

	// if we were forced to AVIFile mode (possibly due to an Avisynth script),
	// propagate that condition to the options
	ifo->opts.fCompatibilityMode |= fCompatibilityMode;

	return ifo;
}

///////////////////////////////////////////////

void InputFileAVI::EnableSegmentAutoscan() {
	fAutoscanSegments = true;
}

void InputFileAVI::ForceCompatibility() {
	fCompatibilityMode = true;
}

void InputFileAVI::setAutomated(bool fAuto) {
	fAutomated = fAuto;
}

void InputFileAVI::Init(const wchar_t *szFile) {
	VDLogAppMessage(kVDLogMarker, kVDST_InputFileAVI, kVDM_OpeningFile, 1, &szFile);

	HRESULT err;
	PAVIFILE paf;

	AddFilename(szFile);

	if (fCompatibilityMode) {

		{
			VDExternalCodeBracket bracket(L"An AVIFile input handler", __FILE__, __LINE__);

			err = AVIFileOpenW(&paf, szFile, OF_READ, NULL);
		}

		if (err)
			throw MyAVIError(szME, err);

		if (!(pAVIFile = CreateAVIReadHandler(paf))) {
			AVIFileRelease(paf);
			throw MyMemoryError();
		}
	} else {
		if (!(pAVIFile = CreateAVIReadHandler(szFile, fIgnoreIndex)))
			throw MyMemoryError();
	}

	if (fDisableFastIO)
		pAVIFile->EnableFastIO(false);

	if (fAutoscanSegments) {
		const wchar_t *pszName = VDFileSplitPath(szFile);
		VDStringW sPathBase(szFile, pszName - szFile);
		VDStringW sPathTail(pszName);

		if (sPathTail.size() >= 7 && !_wcsicmp(sPathTail.c_str() + sPathTail.size() - 7, L".00.avi")) {
			int nSegment = 0;

			sPathTail.resize(sPathTail.size() - 7);

			VDStringW sPathPattern(VDMakePath(sPathBase.c_str(), sPathTail.c_str()));

			try {
				const char *pszPathHint;

				while(pAVIFile->getSegmentHint(&pszPathHint)) {

					++nSegment;

					VDStringW sPath(sPathPattern + VDswprintf(L".%02d.avi", 1, &nSegment));

					if (!VDDoesPathExist(sPath.c_str())) {
						if (pszPathHint && *pszPathHint) {
							sPathPattern = VDTextAToW(pszPathHint) + sPathTail;
						}

						sPath = (sPathPattern + VDswprintf(L".%02d.avi", 1, &nSegment));
					}

					if (!VDDoesPathExist(sPath.c_str())) {
						char szPath[MAX_PATH];
						wchar_t szTitle[MAX_PATH];

						swprintf(szTitle, MAX_PATH, L"Cannot find file %s", sPath.c_str());

						strcpy(szPath, VDTextWToA(sPath).c_str());

						const VDStringW fname(VDGetLoadFileName(VDFSPECKEY_LOADVIDEOFILE, (VDGUIHandle)g_hWnd, szTitle, fileFiltersAppendAVI, L"avi", 0, 0));

						if (fname.empty())
							throw MyUserAbortError();

						if (!Append(fname.c_str(),0))
							break;

						sPathPattern = VDMakePath(VDFileSplitPathLeft(fname).c_str(), sPathTail.c_str());
					} else if (!Append(sPath.c_str(),0))
						break;
				}
			} catch(const MyError& e) {
				char err[128];
				sprintf(err, "Cannot load video segment %02d", nSegment);

				e.post(NULL, err);
			}
		}
	}

	if (fRedoKeyFlags) {
		vdrefptr<IVDVideoSource> vs;
		vdfastvector<uint32> keyflags;
		for(int index=0; GetVideoSource(index, ~vs); ++index) {
			keyflags.clear();
			static_cast<VideoSourceAVI *>(&*vs)->redoKeyFlags(keyflags);
			mNewKeyFlags.push_back(keyflags);
		}
	} else if (pAVIFile->isIndexFabricated()) {
		VDLogAppMessage(kVDLogWarning, kVDST_InputFileAVI, kVDM_RekeyNotSpecified);
	}
}

bool InputFileAVI::Append(const wchar_t *szFile, uint32 flags) {
	if (fCompatibilityMode)
		return false;

	if (!szFile)
		return true;

	if (pAVIFile->AppendFile(szFile)) {
		for(Streams::iterator it(mStreams.begin()), itEnd(mStreams.end()); it!=itEnd; ++it) {
			VDAVIStreamSource *p = *it;

			p->Reinit();
		}

		AddFilename(szFile);

		return true;
	}

	return false;
}

void InputFileAVI::getAppendFilters(wchar_t *filters, int filters_max) {
	const wchar_t* s = fileFiltersAppendAVI;
	const wchar_t* p = s;
	while(1){
		p += wcslen(p);
		p++;
		if(!*p) break;
	}
	memcpy(filters,s,(p-s+1)*2);
}

void InputFileAVI::GetTextInfo(tFileTextInfo& info) {
	pAVIFile->GetTextInfo(info);
}

bool InputFileAVI::isOptimizedForRealtime() {
	return pAVIFile->isOptimizedForRealtime();
}

bool InputFileAVI::isStreaming() {
	return pAVIFile->isStreaming();
}

bool InputFileAVI::GetVideoSource(int index, IVDVideoSource **ppSrc) {
	if (index < 0)
		return false;

	vdrefptr<VideoSourceAVI> videoSrc;
	const uint32 *key_flags = NULL;

	if ((uint32)index < mNewKeyFlags.size())
		key_flags = mNewKeyFlags[index].data();

	if (!(videoSrc = new VideoSourceAVI(this, pAVIFile, NULL, NULL, fInternalDecoder, iMJPEGMode, fccForceVideo, fccForceVideoHandler, key_flags)))
		throw MyMemoryError();

	if (!videoSrc->Init(index))
		return false;

	*ppSrc = videoSrc.release();
	return true;
}

bool InputFileAVI::GetAudioSource(int index, AudioSource **ppSrc) {
	vdrefptr<AudioSource> as;

	as = new AudioSourceAVI(this, pAVIFile, index, fAutomated);
	if (static_cast<AudioSourceAVI *>(&*as)->init()) {
		WAVEFORMATEX *pwfex = (WAVEFORMATEX *)as->getFormat();

		if (pwfex->wFormatTag == WAVE_FORMAT_MPEGLAYER3 && pwfex->nBlockAlign == 0) {
			VDLogAppMessage(kVDLogWarning, kVDST_InputFileAVI, kVDM_InvalidBlockAlign);
			pwfex->nBlockAlign = 1;
		}

		if (lForceAudioHz) {
			pwfex->nAvgBytesPerSec = MulDiv(pwfex->nAvgBytesPerSec, lForceAudioHz, pwfex->nSamplesPerSec);
			pwfex->nSamplesPerSec = lForceAudioHz;
			static_cast<AudioSourceAVI *>(&*as)->setRate(VDFraction(pwfex->nAvgBytesPerSec, pwfex->nBlockAlign));
		}
		*ppSrc = as.release();
		return true;
	}
	as = NULL;

	IAVIReadStream *pDVStream = pAVIFile->GetStream('svai', index);
	if (pDVStream) {
		as = new AudioSourceDV(this, pDVStream, fAutomated);
		if (static_cast<AudioSourceDV *>(&*as)->init()) {
			*ppSrc = as.release();
			return true;
		}
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

class VDAVIFileInfoDialog : public VDDialogFrameW32, public VDThread {
public:
	VDAVIFileInfoDialog(InputFileAVI *file);
	~VDAVIFileInfoDialog();

protected:
	virtual void ThreadRun();
	virtual bool OnLoaded();
	virtual void OnDestroy();
	virtual bool OnCommand(uint32 id, uint32 extcode);
	virtual bool OnTimer(uint32 id);

	enum {
		kUpdateTimerId = 100
	};

	vdrefptr<InputFileAVI> mpFile;
	vdrefptr<IVDVideoSource> mpVideo;
	vdrefptr<AudioSource> mpAudio;

	VDAtomicInt mbAbortScan;

	long	lVideoKFrames;
	long	lVideoKMinSize;
	sint64 i64VideoKTotalSize;
	long	lVideoKMaxSize;
	long	lVideoCFrames;
	long	lVideoCMinSize;
	sint64	i64VideoCTotalSize;
	long	lVideoCMaxSize;

	long	lAudioFrames;
	long	lAudioMinSize;
	sint64	i64AudioTotalSize;
	long	lAudioMaxSize;

	long	lAudioPreload;

	bool	bAudioFramesIndeterminate;

};

VDAVIFileInfoDialog::VDAVIFileInfoDialog(InputFileAVI *file)
	: VDDialogFrameW32(IDD_AVI_INFO)
	, mpFile(file)
	, mbAbortScan(false)
	, lVideoKFrames(0)
	, lVideoKMinSize(0)
	, i64VideoKTotalSize(0)
	, lVideoKMaxSize(0)
	, lVideoCFrames(0)
	, lVideoCMinSize(0)
	, i64VideoCTotalSize(0)
	, lVideoCMaxSize(0)
	, lAudioFrames(0)
	, lAudioMinSize(0)
	, i64AudioTotalSize(0)
	, lAudioMaxSize(0)
	, lAudioPreload(0)
	, bAudioFramesIndeterminate(false)
{
	file->GetVideoSource(0, ~mpVideo);
	file->GetAudioSource(0, ~mpAudio);
}

VDAVIFileInfoDialog::~VDAVIFileInfoDialog()
{
}

void VDAVIFileInfoDialog::ThreadRun() {
	VDPosition i;
	uint32 lActualBytes, lActualSamples;
	VideoSourceAVI *inputVideoAVI = static_cast<VideoSourceAVI *>(&*mpVideo);
	AudioSource *inputAudioAVI = mpAudio;

	lVideoCMinSize = 0x7FFFFFFF;
	lVideoKMinSize = 0x7FFFFFFF;

	const VDPosition videoFrameStart	= inputVideoAVI->getStart();
	const VDPosition videoFrameEnd		= inputVideoAVI->getEnd();

	for(i=videoFrameStart; i<videoFrameEnd; ++i) {
		if (inputVideoAVI->isKey(i)) {
			++lVideoKFrames;

			if (!inputVideoAVI->read(i, 1, NULL, 0, &lActualBytes, NULL)) {
				i64VideoKTotalSize += lActualBytes;
				if (lActualBytes < lVideoKMinSize) lVideoKMinSize = lActualBytes;
				if (lActualBytes > lVideoKMaxSize) lVideoKMaxSize = lActualBytes;
			}
		} else {
			++lVideoCFrames;

			if (!inputVideoAVI->read(i, 1, NULL, 0, &lActualBytes, NULL)) {
				i64VideoCTotalSize += lActualBytes;
				if (lActualBytes < lVideoCMinSize) lVideoCMinSize = lActualBytes;
				if (lActualBytes > lVideoCMaxSize) lVideoCMaxSize = lActualBytes;
			}
		}

		if (mbAbortScan)
			return;
	}

	if (inputAudioAVI) {
		const VDPosition audioFrameStart	= inputAudioAVI->getStart();
		const VDPosition audioFrameEnd		= inputAudioAVI->getEnd();

		lAudioMinSize = 0x7FFFFFFF;
		bAudioFramesIndeterminate = false;
		lAudioPreload = static_cast<VDAudioSourceAVISourced *>(inputAudioAVI)->GetPreloadSamples();

		i = audioFrameStart;
		while(i < audioFrameEnd) {
			if (inputAudioAVI->read(i, AVISTREAMREAD_CONVENIENT, NULL, 0, &lActualBytes, &lActualSamples))
				break;

			if (!lActualSamples) {
				bAudioFramesIndeterminate = true;
				break;
			}

			++lAudioFrames;
			i += lActualSamples;

			i64AudioTotalSize += lActualBytes;
			if (lActualBytes < lAudioMinSize) lAudioMinSize = lActualBytes;
			if (lActualBytes > lAudioMaxSize) lAudioMaxSize = lActualBytes;

			if (mbAbortScan)
				return;
		}
	}
}

bool VDAVIFileInfoDialog::OnLoaded() {
	VDStringW buf;

	if (mpVideo) {
		VideoSourceAVI *pvs = static_cast<VideoSourceAVI *>(&*mpVideo);

		SetControlTextF(IDC_VIDEO_FORMAT, L"%dx%d, %.3f fps (%ld \x00B5s)",
					pvs->getImageFormat()->biWidth,
					pvs->getImageFormat()->biHeight,
					pvs->getRate().asDouble(),
					VDRoundToLong(1000000.0 / pvs->getRate().asDouble()));

		const sint64 length = pvs->getLength();
		buf.sprintf(L"%lld frames (", length);
		DWORD ticks = VDRoundToInt(1000.0*length/pvs->getRate().asDouble());

		wchar_t tmp[128];
		ticks_to_str(tmp, std::size(tmp), ticks);
		tmp[127] = 0;
		buf += tmp;

		buf.append_sprintf(L".%02d)", (ticks/10)%100);
		SetControlText(IDC_VIDEO_NUMFRAMES, buf.c_str());

		buf = L"Unknown";

		if (const wchar_t *name = pvs->getDecompressorName()) {
			buf = name;

			if (buf.size() > 30) {
				buf.resize(27);
				buf += L"...";
			}

			buf += L" (";
			buf += printW_fourcc(pvs->getImageFormat()->biCompression);
			buf += ')';
		} else {
			const uint32 comp = pvs->getImageFormat()->biCompression;

			if (comp == '2YUY')
				buf = L"YCbCr 4:2:2 (YUY2)";
			else if (comp == 'YVYU')
				buf = L"YCbCr 4:2:2 (UYVY)";
			else if (comp == '024I')
				buf = L"YCbCr 4:2:0 planar (I420)";
			else if (comp == 'VUYI')
				buf = L"YCbCr 4:2:0 planar (IYUV)";
			else if (comp == '21VY')
				buf = L"YCbCr 4:2:0 planar (YV12)";
			else if (comp == '61VY')
				buf = L"YCbCr 4:2:2 planar (YV16)";
			else if (comp == '9UVY')
				buf = L"YCbCr 4:1:0 planar (YVU9)";
			else if (comp == '  8Y')
				buf = L"Monochrome (Y8)";
			else if (comp == '008Y')
				buf = L"Monochrome (Y800)";
			else
				buf.sprintf(L"Uncompressed RGB%d", pvs->getImageFormat()->biBitCount);
		}

		SetControlText(IDC_VIDEO_COMPRESSION, buf.c_str());
	}

	if (mpAudio) {
		AudioSourceAVI *pAS = static_cast<AudioSourceAVI *>(&*mpAudio);
		const VDWaveFormat *fmt = pAS->getWaveFormat();
		DWORD cbwfxTemp;
		WAVEFORMATEX *pwfxTemp;
		HACMSTREAM has;
		HACMDRIVERID hadid;
		ACMDRIVERDETAILSW add;
		bool fSuccessful = false;

		SetControlTextF(IDC_AUDIO_SAMPLINGRATE, L"%ldHz", fmt->mSamplingRate);

		if (fmt->mChannels == 8)
			buf = L"7.1";
		else if (fmt->mChannels == 6)
			buf = L"5.1";
		else if (fmt->mChannels > 2)
			buf.sprintf(L"%d", fmt->mChannels);
		else
			buf.sprintf(L"%d (%ls)", fmt->mChannels, fmt->mChannels>1 ? L"Stereo" : L"Mono");

		SetControlText(IDC_AUDIO_CHANNELS, buf.c_str());

		if (is_audio_float(fmt))
			SetControlTextF(IDC_AUDIO_PRECISION, L"%d-bit float", fmt->mSampleBits);
		else if (is_audio_pcm(fmt))
			SetControlTextF(IDC_AUDIO_PRECISION, L"%d-bit", fmt->mSampleBits);
		else
			SetControlText(IDC_AUDIO_PRECISION, L"N/A");

		sint64 len = pAS->getLength();

		buf.sprintf(L"%lld samples (", len);
		uint32 ticks = VDRoundToInt32(1000.0 * len * pAS->getRate().AsInverseDouble());

		wchar_t tmp[128];
		ticks_to_str(tmp, std::size(tmp), ticks);
		tmp[127] = 0;
		buf += tmp;

		buf.append_sprintf(L".%02d)", (ticks/10)%100);
		SetControlText(IDC_AUDIO_LENGTH, buf.c_str());

		////////// Attempt to detect audio compression //////////

		if (fmt->mTag == nsVDWinFormats::kWAVE_FORMAT_EXTENSIBLE) {
			const nsVDWinFormats::WaveFormatExtensible& wfe = *(const nsVDWinFormats::WaveFormatExtensible *)fmt;

			if (is_audio_float(fmt)) {
				SetControlTextF(IDC_AUDIO_COMPRESSION, L"PCM (chmask %x)", wfe.mChannelMask);
			} else if (is_audio_pcm(fmt)) {
				SetControlTextF(IDC_AUDIO_COMPRESSION, L"PCM (%d bits real, chmask %x)", wfe.mBitDepth, wfe.mChannelMask);
			} else {
				SetControlTextF(IDC_AUDIO_COMPRESSION, L"Unk.: {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}"
					, wfe.mGuid.mData1
					, wfe.mGuid.mData2
					, wfe.mGuid.mData3
					, wfe.mGuid.mData4[0]
					, wfe.mGuid.mData4[1]
					, wfe.mGuid.mData4[2]
					, wfe.mGuid.mData4[3]
					, wfe.mGuid.mData4[4]
					, wfe.mGuid.mData4[5]
					, wfe.mGuid.mData4[6]
					, wfe.mGuid.mData4[7]);
			}
		} else if (is_audio_pcm(fmt) || is_audio_float(fmt)) {
			SetControlText(IDC_AUDIO_COMPRESSION, L"PCM (Uncompressed)");
		} else {
			// Retrieve maximum format size.

			acmMetrics(NULL, ACM_METRIC_MAX_SIZE_FORMAT, (LPVOID)&cbwfxTemp);

			// Fill out a destination wave format (PCM).

			if (pwfxTemp = (WAVEFORMATEX *)allocmem(cbwfxTemp)) {
				pwfxTemp->wFormatTag	= WAVE_FORMAT_PCM;

				// Ask ACM to fill out the details.

				if (!acmFormatSuggest(NULL, (WAVEFORMATEX *)fmt, (WAVEFORMATEX *)pwfxTemp, cbwfxTemp, ACM_FORMATSUGGESTF_WFORMATTAG)) {
					if (!acmStreamOpen(&has, NULL, (WAVEFORMATEX *)fmt, pwfxTemp, NULL, NULL, NULL, ACM_STREAMOPENF_NONREALTIME)) {
						if (!acmDriverID((HACMOBJ)has, &hadid, 0)) {
							memset(&add, 0, sizeof add);

							add.cbStruct = sizeof add;

							if (!acmDriverDetailsW(hadid, &add, 0)) {
								buf = add.szLongName;
								if (buf.size() > 30) {
									buf.resize(27);
									buf += L"...";
								}

								buf.append_sprintf(L" (0x%04x)", fmt->mTag);
								SetControlText(IDC_AUDIO_COMPRESSION, buf.c_str());

								fSuccessful = true;
							}
						}

						acmStreamClose(has, 0);
					}
				}

				freemem(pwfxTemp);
			}

			if (!fSuccessful)
				SetControlTextF(IDC_AUDIO_COMPRESSION, L"Unknown (tag %04X)", fmt->mTag);
		}
	}

	ThreadStart();
	SetPeriodicTimer(kUpdateTimerId, 250);

	return VDDialogFrameW32::OnLoaded();
}

void VDAVIFileInfoDialog::OnDestroy() {
	ClearPeriodicTimer(kUpdateTimerId);

	mbAbortScan = true;
	ThreadWait();

	VDDialogFrameW32::OnDestroy();
}

bool VDAVIFileInfoDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_COPY) {
		static const uint32 kVideoIds[]={
			IDC_VIDEO_FORMAT,
			IDC_VIDEO_NUMFRAMES,
			IDC_VIDEO_COMPRESSION,
			IDC_VIDEO_NUMKEYFRAMES,
			IDC_VIDEO_KEYFRAMESIZES,
			IDC_VIDEO_NONKEYFRAMESIZES,
			IDC_VIDEO_DATARATE,
		};

		static const uint32 kAudioIds[]={
			IDC_AUDIO_SAMPLINGRATE,
			IDC_AUDIO_CHANNELS,
			IDC_AUDIO_PRECISION,
			IDC_AUDIO_COMPRESSION,
			IDC_AUDIO_LAYOUT,
			IDC_AUDIO_LENGTH,
			IDC_AUDIO_FRAMESIZES,
			IDC_AUDIO_DATARATE,
		};

		VDStringW buf;
		for(int pass=0; pass<2; ++pass) {
			const uint32 *ids = pass ? kAudioIds : kVideoIds;
			const uint32 idcount = pass ? std::size(kAudioIds) : std::size(kVideoIds);

			if (pass == 0) {
				buf = L"Video:\r\n";
			} else {
				buf += L"\r\nAudio:\r\n";
			}

			for(uint32 i=0; i<idcount; ++i) {
				HWND hwndItem = GetControl(ids[i]);

				if (hwndItem) {
					HWND hwndLabel = GetWindow(hwndItem, GW_HWNDPREV);

					if (hwndLabel) {
						buf += VDGetWindowTextW32(hwndLabel);
						buf += L" ";
						buf += VDGetWindowTextW32(hwndItem);
						buf += L"\r\n";
					}
				}
			}
		}

		VDCopyTextToClipboard(buf.c_str());

		return true;
	}

	return VDDialogFrameW32::OnCommand(id, extcode);
}

bool VDAVIFileInfoDialog::OnTimer(uint32 id) {
	if (id != kUpdateTimerId)
		return VDDialogFrameW32::OnTimer(id);

	SetControlTextF(IDC_VIDEO_NUMKEYFRAMES, L"%ld", lVideoKFrames);

	if (lVideoKFrames)
		SetControlTextF(IDC_VIDEO_KEYFRAMESIZES, L"%ld/%lld/%ld (%lldK)"
					,lVideoKMinSize
					,i64VideoKTotalSize/lVideoKFrames
					,lVideoKMaxSize
					,(i64VideoKTotalSize+1023)>>10);
	else
		SetControlText(IDC_VIDEO_KEYFRAMESIZES, L"(no key frames)");

	if (lVideoCFrames)
		SetControlTextF(IDC_VIDEO_NONKEYFRAMESIZES, L"%ld/%lld/%ld (%lldK)"
					,lVideoCMinSize
					,i64VideoCTotalSize/lVideoCFrames
					,lVideoCMaxSize
					,(i64VideoCTotalSize+1023)>>10);
	else
		SetControlText(IDC_VIDEO_NONKEYFRAMESIZES, L"(no delta frames)");

	if (mpAudio) {
		if (bAudioFramesIndeterminate) {
			SetControlText(IDC_AUDIO_NUMFRAMES, L"(indeterminate)");
			SetControlText(IDC_AUDIO_FRAMESIZES, L"(indeterminate)");
			SetControlText(IDC_AUDIO_PRELOAD, L"(indeterminate)");
		} else {

			if (lAudioFrames)
				SetControlTextF(IDC_AUDIO_FRAMESIZES, L"%ld/%I64d/%ld (%I64dK)"
						,lAudioMinSize
						,i64AudioTotalSize/lAudioFrames
						,lAudioMaxSize
						,(i64AudioTotalSize+1023)>>10);
			else
				SetControlText(IDC_AUDIO_FRAMESIZES, L"(no audio frames)");

			const VDWaveFormat *pWaveFormat = mpAudio->getWaveFormat();

			SetControlTextF(IDC_AUDIO_LAYOUT, L"%ld chunks (%.2fs preload)", lAudioFrames,
				(double)lAudioPreload * mpAudio->getRate().AsInverseDouble()
					);

			const double audioRate = (double)pWaveFormat->mDataRate * (1.0 / 125.0);
			const double rawOverhead = 24.0 * lAudioFrames;
			const double audioOverhead = 100.0 * rawOverhead / (rawOverhead + i64AudioTotalSize);
			SetControlTextF(IDC_AUDIO_DATARATE, L"%.0f kbps (%.2f%% overhead)", audioRate, audioOverhead);
		}
	}

	double totalVideoFrames = (double)lVideoKFrames + (sint64)lVideoCFrames;
	if (totalVideoFrames > 0) {
		VideoSourceAVI *pvs = static_cast<VideoSourceAVI *>(&*mpVideo);
		const double seconds = (double)pvs->getLength() / (double)pvs->getRate().asDouble();
		const double rawOverhead = (24.0 * totalVideoFrames);
		const double totalSize = (double)(i64VideoKTotalSize + i64VideoCTotalSize);
		const double videoRate = (1.0 / 125.0) * totalSize / seconds;
		const double videoOverhead = totalSize > 0 ? 100.0 * rawOverhead / (rawOverhead + totalSize) : 0;
		SetControlTextF(IDC_VIDEO_DATARATE, L"%.0f kbps (%.2f%% overhead)", videoRate, videoOverhead);
	}

	if (!isThreadActive())
		ClearPeriodicTimer(kUpdateTimerId);

	return true;
}

void InputFileAVI::InfoDialog(VDGUIHandle hwndParent) {
	VDAVIFileInfoDialog dlg(this);

	dlg.ShowDialog(hwndParent);
}

void InputFileAVI::Attach(VDAVIStreamSource *p) {
	mStreams.push_back(p);
}

void InputFileAVI::Detach(VDAVIStreamSource *p) {
	mStreams.erase(p);
}
