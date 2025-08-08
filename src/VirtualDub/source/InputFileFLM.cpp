//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2003 Avery Lee
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

#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include "VideoSource.h"
#include "InputFile.h"

extern const char g_szError[];

///////////////////////////////////////////////////////////////////////////

namespace {
	uint16 VDFromBE16(uint16 v) {
		return (v<<8) + (v>>8);
	}

	uint32 VDFromBE32(uint32 v) {
		return (v>>24) + (v<<24) + ((v>>8)&0xff00) + ((v<<8)&0xff0000);
	}

	struct FilmstripHeader {
		sint32	signature;
		sint32	numFrames;
		sint16	packing;
		sint16	reserved;
		sint16	width;
		sint16	height;
		sint16	leading;
		sint16	framesPerSec;
		char	spare[16];

		void Read(const void *src) {
			const FilmstripHeader *src2 = (const FilmstripHeader *)src;

			signature		= VDFromBE32(src2->signature);
			numFrames		= VDFromBE32(src2->numFrames);
			packing			= VDFromBE16(src2->packing);
			reserved		= VDFromBE16(src2->reserved);
			width			= VDFromBE16(src2->width);
			height			= VDFromBE16(src2->height);
			leading			= VDFromBE16(src2->leading);
			framesPerSec	= VDFromBE16(src2->framesPerSec);
		}

		bool Validate() const {
			if (signature != 0x52616e64)
				return false;

			if (numFrames < 0)
				return false;

			if (packing < 0)
				return false;

			if (((uint16)width - 1) >= 0x1000)
				return false;

			if (((uint16)height - 1) >= 0x1000)
				return false;

			if (leading < 0)
				return false;

			if ((uint16)framesPerSec > 1000)
				return false;

			return true;
		}
	};
}

///////////////////////////////////////////////////////////////////////////

class IVDInputFileFLM : public IVDRefCount {
public:
	virtual uint32		GetFrameSize() = 0;
	virtual uint32		GetVisibleFrameSize() = 0;
	virtual uint32		GetFrameWidth() = 0;
	virtual uint32		GetFrameHeight() = 0;
	virtual VDPosition	GetFrameCount() = 0;
	virtual VDFraction	GetFrameRate() = 0;

	virtual void		ReadSpan(sint64 pos, void *data, uint32 len) = 0;
};

///////////////////////////////////////////////////////////////////////////

class VDVideoSourceFLM : public VideoSource {
public:
	VDVideoSourceFLM(IVDInputFileFLM *parent);

	int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) override;

	bool setTargetFormat(VDPixmapFormatEx format) override;

	void invalidateFrameBuffer() override		{ mCachedFrame = -1; }
	bool isFrameBufferValid() override			{ return mCachedFrame >= 0; }
	bool isStreaming() override					{ return false; }

	const void *getFrame(VDPosition lFrameDesired) override;
	const void *streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition sample_num, VDPosition target_sample) override;

	char getFrameTypeChar(VDPosition lFrameNum) override	{ return 'K'; }
	eDropType getDropType(VDPosition lFrameNum) override	{ return kIndependent; }
	bool isKeyframeOnly() override							{ return true; }
	bool isDecodable(VDPosition sample_num) override		{ return true; }

protected:
	VDPosition	mCachedFrame;
	uint32		mFrameSize;
	uint32		mVisibleFrameSize;
	uint32		mWidth;
	uint32		mHeight;

	vdrefptr<IVDInputFileFLM>	mpParent;
};

VDVideoSourceFLM::VDVideoSourceFLM(IVDInputFileFLM *parent)
	: mpParent(parent)
	, mFrameSize(parent->GetFrameSize())
	, mVisibleFrameSize(parent->GetVisibleFrameSize())
	, mWidth(parent->GetFrameWidth())
	, mHeight(parent->GetFrameHeight())
{
	mpTargetFormatHeader.resize(sizeof(BITMAPINFOHEADER));

	mSampleFirst	= 0;
	mSampleLast		= mpParent->GetFrameCount();

	memset(&streamInfo, 0, sizeof streamInfo);

	streamInfo.fccType		= VDAVIStreamInfo::kTypeVideo;
	streamInfo.dwLength		= (DWORD)mSampleLast;

	const VDFraction& rate = mpParent->GetFrameRate();
	streamInfo.dwRate		= rate.getHi();
	streamInfo.dwScale		= rate.getLo();
	streamInfo.rcFrameLeft		= 0;
	streamInfo.rcFrameTop		= 0;
	streamInfo.rcFrameRight		= (uint16)mWidth;
	streamInfo.rcFrameBottom	= (uint16)mHeight;

	BITMAPINFOHEADER& bih = *(BITMAPINFOHEADER *)allocFormat(sizeof(BITMAPINFOHEADER));

	bih.biSize			= sizeof(BITMAPINFOHEADER);
	bih.biWidth			= mpParent->GetFrameWidth();
	bih.biHeight		= mpParent->GetFrameHeight();
	bih.biPlanes		= 1;
	bih.biCompression	= (uint32)0xFFFFFFFF;
	bih.biBitCount		= 32;
	bih.biSizeImage		= mVisibleFrameSize;
	bih.biXPelsPerMeter	= 0;
	bih.biYPelsPerMeter	= 0;
	bih.biClrUsed		= 0;
	bih.biClrImportant	= 0;

	AllocFrameBuffer(mVisibleFrameSize);
}

int VDVideoSourceFLM::_read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
	if (lCount > 1)
		lCount = 1;

	int ret = 0;

	if (lCount > 0) {
		if (lpBuffer) {
			if (mVisibleFrameSize > cbBuffer)
				ret = IVDStreamSource::kBufferTooSmall;
			else {
				mpParent->ReadSpan((uint64)mFrameSize * lStart, lpBuffer, mVisibleFrameSize);

				// Swizzle the frame now. Easier this way. We need to go from RGBA to BGRA.
				uint8 *p = (uint8 *)lpBuffer;
				uint32 count = mVisibleFrameSize >> 2;

				do {
					uint8 r = p[0];
					uint8 b = p[2];

					p[0] = b;
					p[2] = r;
					p += 4;
				} while(--count);
			}
		}
	}

	if (lBytesRead)
		*lBytesRead = mVisibleFrameSize;
	if (lSamplesRead)
		*lSamplesRead = lCount;

	return ret;
}

const void *VDVideoSourceFLM::getFrame(VDPosition frameNum) {
	uint32 lBytes;
	const void *pFrame = NULL;

	if (mCachedFrame == frameNum)
		return mpFrameBuffer.get();

	if (!read(frameNum, 1, NULL, 0x7FFFFFFF, &lBytes, NULL) && lBytes) {
		vdblock<char> buffer(lBytes);
		uint32 lReadBytes;

		read(frameNum, 1, buffer.data(), lBytes, &lReadBytes, NULL);
		pFrame = streamGetFrame(buffer.data(), lReadBytes, FALSE, frameNum, frameNum);
	}

	return pFrame;
}

const void *VDVideoSourceFLM::streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num, VDPosition target_sample) {
	VDPixmap srcbm = {0};

	srcbm.data		= (void *)inputBuffer;
	srcbm.pitch		= mWidth * 4;
	srcbm.w			= mWidth;
	srcbm.h			= mHeight;
	srcbm.format	= nsVDPixmap::kPixFormat_XRGB8888;

	VDPixmapBlt(mTargetFormat, srcbm);

	mCachedFrame = frame_num;

	return getFrameBuffer();
}

bool VDVideoSourceFLM::setTargetFormat(VDPixmapFormatEx format) {
	if (!format)
		format = nsVDPixmap::kPixFormat_XRGB8888;

	switch(format) {
	case nsVDPixmap::kPixFormat_XRGB1555:
	case nsVDPixmap::kPixFormat_RGB888:
	case nsVDPixmap::kPixFormat_XRGB8888:
		if (!VideoSource::setTargetFormat(format))
			return false;

		invalidateFrameBuffer();
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

class VDInputFileFLM : public InputFile, public IVDInputFileFLM {
public:
	VDInputFileFLM();
	~VDInputFileFLM();

	int AddRef();
	int Release();

	void Init(const wchar_t *szFile);

	void setAutomated(bool fAuto);

	bool GetVideoSource(int index, IVDVideoSource **ppSrc);
	bool GetAudioSource(int index, AudioSource **ppSrc);

public:
	uint32		GetFrameSize() { return mFrameSize; }
	uint32		GetVisibleFrameSize() { return mVisibleFrameSize; }
	uint32		GetFrameWidth() { return mFrameWidth; }
	uint32		GetFrameHeight() { return mFrameHeight; }
	VDPosition	GetFrameCount() { return mFrameCount; }
	VDFraction	GetFrameRate() { return mFrameRate; }

	void		ReadSpan(sint64 pos, void *data, uint32 len);

protected:
	VDFile		mFile;
	uint32		mFrameSize;
	uint32		mVisibleFrameSize;
	uint32		mFrameWidth;
	uint32		mFrameHeight;
	uint32		mFrameCount;
	VDFraction	mFrameRate;
};

VDInputFileFLM::VDInputFileFLM()
{
}

VDInputFileFLM::~VDInputFileFLM() {
}

int VDInputFileFLM::AddRef() {
	return InputFile::AddRef();
}

int VDInputFileFLM::Release() {
	return InputFile::Release();
}

void VDInputFileFLM::Init(const wchar_t *filename) {
	bool valid = false;

	mFile.open(filename, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting);
	sint64 fsize = mFile.size();
	FilmstripHeader	mHeader;
	if (fsize >= 36) {
		mFile.seek(fsize - 36);

		char hdrbuf[36];
		mFile.read(hdrbuf, 36);

		mHeader.Read(hdrbuf);

		if (mHeader.Validate()) {
			mFrameSize	= (uint32)mHeader.width * ((uint32)mHeader.height + (uint32)mHeader.leading) * 4;

			if ((uint64)mFrameSize * (uint16)mHeader.numFrames + 36 <= fsize)
				valid = true;
		}
	}

	if (!valid)
		throw MyError("%ls does not appear to be a valid Adobe filmstrip file.", mFile.getFilenameForError());

	mVisibleFrameSize	= (uint32)mHeader.width * (uint32)mHeader.height * 4;
	mFrameWidth			= (uint32)mHeader.width;
	mFrameHeight		= (uint32)mHeader.height;
	mFrameCount			= (uint32)mHeader.numFrames;
	mFrameRate.Assign((uint32)mHeader.framesPerSec, 1);
}

void VDInputFileFLM::setAutomated(bool fAuto) {
}

bool VDInputFileFLM::GetVideoSource(int index, IVDVideoSource **ppSrc) {
	if (index)
		return false;

	*ppSrc = new VDVideoSourceFLM(this);
	if (!*ppSrc)
		return false;
	(*ppSrc)->AddRef();
	return true;
}

bool VDInputFileFLM::GetAudioSource(int index, AudioSource **ppSrc) {
	return false;
}

void VDInputFileFLM::ReadSpan(sint64 pos, void *data, uint32 len) {
	mFile.seek(pos);
	mFile.read(data, len);
}

///////////////////////////////////////////////////////////////////////////

class VDInputDriverFLM : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return L"Adobe filmstrip input driver (internal)"; }

	int GetDefaultPriority() {
		return 0;
	}

	uint32 GetFlags() { return kF_Video; }

	const wchar_t *GetFilenamePattern() {
		return L"Adobe filmstrip (*.flm)\0*.flm\0";
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		size_t l = wcslen(pszFilename);

		if (l>4 && !_wcsicmp(pszFilename + l - 4, L".flm"))
			return true;

		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		if (nFooterSize < 36) return kDC_Error_MoreData;
		if (nFooterSize >= 36) {
			const uint8 *buf = (const uint8 *)pFooter;
			FilmstripHeader hdr;

			hdr.Read(buf + nFooterSize - 36);
			if (hdr.Validate())
				return kDC_High;
		}

		return kDC_None;
	}

	InputFile *CreateInputFile(uint32 flags) {
		return new VDInputFileFLM;
	}
};

extern IVDInputDriver *VDCreateInputDriverFLM() { return new VDInputDriverFLM; }
