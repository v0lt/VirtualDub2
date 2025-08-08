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
#include <vd2/system/registry.h>
#include <vd2/Dita/resources.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDLib/Dialog.h>
#include "VideoSource.h"
#include "InputFile.h"
#include "resource.h"

extern const char g_szError[];
extern const char g_szRegKeyPersistence[];

///////////////////////////////////////////////////////////////////////////

namespace {
	enum {
		kVDST_RawVideoFormats = 11
	};
}

///////////////////////////////////////////////////////////////////////////

class VDInputFileRawVideoOptions : public InputFileOptions {
public:
	VDInputFileRawVideoOptions();

	void read(const char *buf, int buflen);
	int write(char *buf, int buflen) const;
	bool validate();

	VDFraction	mFrameRate;
	uint32	mWidth;
	uint32	mHeight;
	uint32	mAlignment;
	int		mFormat;
	bool	mbUpsideDown;
	bool	mbSwapChromaPlanes;
	uint32	mInitialPadding;
	uint32	mPostFramePadding;
};

VDInputFileRawVideoOptions::VDInputFileRawVideoOptions()
	: mFrameRate(15, 1)
	, mWidth(720)
	, mHeight(480)
	, mAlignment(4)
	, mFormat(nsVDPixmap::kPixFormat_RGB888)
	, mbUpsideDown(false)
	, mbSwapChromaPlanes(false)
	, mInitialPadding(0)
	, mPostFramePadding(0)
{
}

void VDInputFileRawVideoOptions::read(const char *buf, int buflen) {
	if (buflen < 40)
		goto invalid;

	if (VDReadUnalignedLEU32(buf + 0) != 40)
		goto invalid;

	mFrameRate = VDFraction(VDReadUnalignedLEU32(buf + 4), VDReadUnalignedLEU32(buf + 8));
	mWidth = VDReadUnalignedLEU32(buf + 12);
	mHeight = VDReadUnalignedLEU32(buf + 16);
	mAlignment = VDReadUnalignedLEU32(buf + 20);
	mFormat = VDReadUnalignedLEU32(buf + 24);
	mbUpsideDown = buf[28] != 0;
	mbSwapChromaPlanes = buf[29] != 0;
	mInitialPadding = VDReadUnalignedLEU32(buf + 32);
	mPostFramePadding = VDReadUnalignedLEU32(buf + 36);

	if (validate())
		return;

invalid:
	throw MyError("The options structure for raw video input is invalid.");
}

int VDInputFileRawVideoOptions::write(char *buf, int buflen) const {
	if (buflen >= 40) {
		VDWriteUnalignedLEU32(buf + 0, 40);
		VDWriteUnalignedLEU32(buf + 4, mFrameRate.getHi());
		VDWriteUnalignedLEU32(buf + 8, mFrameRate.getLo());
		VDWriteUnalignedLEU32(buf + 12, mWidth);
		VDWriteUnalignedLEU32(buf + 16, mHeight);
		VDWriteUnalignedLEU32(buf + 20, mAlignment);
		VDWriteUnalignedLEU32(buf + 24, mFormat);
		buf[28] = mbUpsideDown;
		buf[29] = mbSwapChromaPlanes;
		buf[30] = 0;
		buf[31] = 0;
		VDWriteUnalignedLEU32(buf + 32, mInitialPadding);
		VDWriteUnalignedLEU32(buf + 36, mPostFramePadding);
	}

	return 40;
}

bool VDInputFileRawVideoOptions::validate() {
	if (!mFrameRate.getLo())
		return false;

	if (mWidth == 0 || mWidth > 0x01000000)
		return false;

	if (mHeight == 0 || mHeight > 0x01000000)
		return false;

	if (mAlignment == 0 || (mAlignment & (mAlignment - 1)))
		return false;

	if (mFormat == 0 || mFormat <= nsVDPixmap::kPixFormat_Pal8 || mFormat >= nsVDPixmap::kPixFormat_Max_Standard)
		return false;

	return true;
}

///////////////////////////////////////////////////////////////////////////

class VDInputFileRawVideoOptionsDialog : public VDDialogFrameW32 {
public:
	VDInputFileRawVideoOptionsDialog(VDInputFileRawVideoOptions& options);

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);

	VDInputFileRawVideoOptions& mOptions;
	static const int kFormats[];
};

const int VDInputFileRawVideoOptionsDialog::kFormats[] = {
	nsVDPixmap::kPixFormat_XRGB1555,
	nsVDPixmap::kPixFormat_RGB565,
	nsVDPixmap::kPixFormat_RGB888,
	nsVDPixmap::kPixFormat_XRGB8888,
	nsVDPixmap::kPixFormat_Y8,
	nsVDPixmap::kPixFormat_Y8_FR,
	nsVDPixmap::kPixFormat_Y16,
	nsVDPixmap::kPixFormat_YUV422_UYVY,
	nsVDPixmap::kPixFormat_YUV422_UYVY_709,
	nsVDPixmap::kPixFormat_YUV422_UYVY_FR,
	nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR,
	nsVDPixmap::kPixFormat_YUV422_YUYV,
	nsVDPixmap::kPixFormat_YUV422_YUYV_709,
	nsVDPixmap::kPixFormat_YUV422_YUYV_FR,
	nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR,
	nsVDPixmap::kPixFormat_YUV444_Planar,
	nsVDPixmap::kPixFormat_YUV444_Planar_FR,
	nsVDPixmap::kPixFormat_YUV444_Planar_709,
	nsVDPixmap::kPixFormat_YUV444_Planar_709_FR,
	nsVDPixmap::kPixFormat_YUV422_Planar,
	nsVDPixmap::kPixFormat_YUV422_Planar_FR,
	nsVDPixmap::kPixFormat_YUV422_Planar_709,
	nsVDPixmap::kPixFormat_YUV422_Planar_709_FR,
	nsVDPixmap::kPixFormat_YUV422_Planar_Centered,
	nsVDPixmap::kPixFormat_YUV420_Planar,
	nsVDPixmap::kPixFormat_YUV420_Planar_FR,
	nsVDPixmap::kPixFormat_YUV420_Planar_709,
	nsVDPixmap::kPixFormat_YUV420_Planar_709_FR,
	nsVDPixmap::kPixFormat_YUV420_Planar_Centered,
	nsVDPixmap::kPixFormat_YUV411_Planar,
	nsVDPixmap::kPixFormat_YUV411_Planar_FR,
	nsVDPixmap::kPixFormat_YUV411_Planar_709,
	nsVDPixmap::kPixFormat_YUV411_Planar_709_FR,
	nsVDPixmap::kPixFormat_YUV410_Planar,
	nsVDPixmap::kPixFormat_YUV410_Planar_FR,
	nsVDPixmap::kPixFormat_YUV410_Planar_709,
	nsVDPixmap::kPixFormat_YUV410_Planar_709_FR
};

VDInputFileRawVideoOptionsDialog::VDInputFileRawVideoOptionsDialog(VDInputFileRawVideoOptions& options)
	: VDDialogFrameW32(IDD_EXTOPENOPTS_RAWVIDEO)
	, mOptions(options)
{
}

bool VDInputFileRawVideoOptionsDialog::OnLoaded() {
	for (const auto& format : kFormats) {
		CBAddString(IDC_INPUT_FORMAT, VDLoadString(0, kVDST_RawVideoFormats, format));
	}

	OnDataExchange(false);

	SetFocusToControl(IDC_WIDTH);
	return true;
}

void VDInputFileRawVideoOptionsDialog::OnDataExchange(bool write) {
	if (write) {
		const VDStringW s(GetControlValueString(IDC_FRAMERATE));
		VDFraction fr;
		unsigned hi, lo;
		bool failed = false;
		if (2 == swscanf(s.c_str(), L"%u / %u", &hi, &lo)) {
			if (!lo)
				failed = true;
			else
				fr = VDFraction(hi, lo);
		} else if (!fr.Parse(VDTextWToA(s).c_str()) || fr.asDouble() >= 1000000.0) {
			failed = true;
		}

		if (fr.getHi() == 0)
			failed = true;

		if (failed) {
			FailValidation(IDC_FRAMERATE);
			return;
		}

		mOptions.mFrameRate = fr;
		mOptions.mWidth = GetControlValueUint32(IDC_WIDTH);
		mOptions.mHeight = GetControlValueUint32(IDC_HEIGHT);
		mOptions.mAlignment = GetControlValueUint32(IDC_ALIGNMENT);
		if (!mOptions.mAlignment || (mOptions.mAlignment & (mOptions.mAlignment - 1)) || mOptions.mAlignment > 65536)
			FailValidation(IDC_ALIGNMENT);

		int idx = CBGetSelectedIndex(IDC_INPUT_FORMAT);
		if ((unsigned)idx >= std::size(kFormats)) {
			FailValidation(IDC_INPUT_FORMAT);
			return;
		}

		mOptions.mFormat = kFormats[idx];

		mOptions.mInitialPadding = GetControlValueUint32(IDC_PADDING_INITIAL);
		mOptions.mPostFramePadding = GetControlValueUint32(IDC_PADDING_FRAME);

		mOptions.mbUpsideDown = IsButtonChecked(IDC_VORIENT_BOTTOMUP);
		mOptions.mbSwapChromaPlanes = IsButtonChecked(IDC_PLANEORDER_CRCB);
	} else {
		for (size_t i = 0; i < std::size(kFormats); ++i) {
			const int format = kFormats[i];
			if (format == mOptions.mFormat) {
				CBSetSelectedIndex(IDC_INPUT_FORMAT, i);
				break;
			}
		}

		VDStringA s;
		s.sprintf("%.4f", mOptions.mFrameRate.asDouble());

		VDFraction fr2(mOptions.mFrameRate);
		VDVERIFY(fr2.Parse(s.c_str()));

		if (fr2 != mOptions.mFrameRate)
			s.sprintf("%u/%u (~%.7f)", mOptions.mFrameRate.getHi(), mOptions.mFrameRate.getLo(), mOptions.mFrameRate.asDouble());

		SetControlText(IDC_FRAMERATE, VDTextAToW(s).c_str());

		SetControlTextF(IDC_WIDTH, L"%u", mOptions.mWidth);
		SetControlTextF(IDC_HEIGHT, L"%u", mOptions.mHeight);
		SetControlTextF(IDC_ALIGNMENT, L"%u", mOptions.mAlignment);
		SetControlTextF(IDC_PADDING_INITIAL, L"%u", mOptions.mInitialPadding);
		SetControlTextF(IDC_PADDING_FRAME, L"%u", mOptions.mPostFramePadding);
		CheckButton(IDC_VORIENT_TOPDOWN, !mOptions.mbUpsideDown);
		CheckButton(IDC_VORIENT_BOTTOMUP, mOptions.mbUpsideDown);
		CheckButton(IDC_PLANEORDER_CBCR, !mOptions.mbSwapChromaPlanes);
		CheckButton(IDC_PLANEORDER_CRCB, mOptions.mbSwapChromaPlanes);
	}
}

///////////////////////////////////////////////////////////////////////////

class IVDInputFileRawVideo : public IVDRefCount {
public:
	virtual sint64 GetFileSize() = 0;
	virtual void ReadSpan(sint64 pos, void *data, uint32 len) = 0;
};

///////////////////////////////////////////////////////////////////////////

class VDVideoSourceRawVideo : public VideoSource {
public:
	VDVideoSourceRawVideo(IVDInputFileRawVideo *parent, const VDInputFileRawVideoOptions& options);

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
	sint64		mFrameOffset;
	sint64		mFrameStride;

	VDPixmapLayout	mLayout;

	vdrefptr<IVDInputFileRawVideo>	mpParent;
};

VDVideoSourceRawVideo::VDVideoSourceRawVideo(IVDInputFileRawVideo *parent, const VDInputFileRawVideoOptions& options)
	: mpParent(parent)
{
	mpTargetFormatHeader.resize(sizeof(VDAVIBitmapInfoHeader));

	mFrameSize = VDPixmapCreateLinearLayout(mLayout, options.mFormat, options.mWidth, options.mHeight, options.mAlignment);

	if (options.mbSwapChromaPlanes) {
		std::swap(mLayout.data2, mLayout.data3);
		std::swap(mLayout.pitch2, mLayout.pitch3);
	}

	if (options.mbUpsideDown)
		VDPixmapLayoutFlipV(mLayout);

	mSampleFirst	= 0;
	mSampleLast		= 0;

	sint64 fileSize = parent->GetFileSize();

	mFrameOffset = options.mInitialPadding;
	mFrameStride = (sint64)mFrameSize + options.mPostFramePadding;

	if (fileSize > options.mInitialPadding)
		mSampleLast = (fileSize - options.mInitialPadding + options.mPostFramePadding) / mFrameStride;

	memset(&streamInfo, 0, sizeof streamInfo);

	streamInfo.fccType		= VDAVIStreamInfo::kTypeVideo;
	streamInfo.dwLength		= (uint32)mSampleLast;

	const VDFraction& rate = options.mFrameRate;
	streamInfo.dwRate		= rate.getHi();
	streamInfo.dwScale		= rate.getLo();
	streamInfo.rcFrameLeft		= 0;
	streamInfo.rcFrameTop		= 0;
	streamInfo.rcFrameRight		= (uint16)mLayout.w;
	streamInfo.rcFrameBottom	= (uint16)mLayout.h;

	VDAVIBitmapInfoHeader& bih = *(VDAVIBitmapInfoHeader *)allocFormat(sizeof(VDAVIBitmapInfoHeader));

	bih.biSize			= sizeof(VDAVIBitmapInfoHeader);
	bih.biWidth			= mLayout.w;
	bih.biHeight		= mLayout.h;
	bih.biPlanes		= 1;
	bih.biCompression	= (uint32)0xFFFFFFFF;
	bih.biBitCount		= 32;
	bih.biSizeImage		= mFrameSize;
	bih.biXPelsPerMeter	= 0;
	bih.biYPelsPerMeter	= 0;
	bih.biClrUsed		= 0;
	bih.biClrImportant	= 0;

	AllocFrameBuffer(mLayout.w * mLayout.h * 4);
}

int VDVideoSourceRawVideo::_read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
	if (lCount > 1)
		lCount = 1;

	int ret = 0;

	if (lCount > 0) {
		if (lpBuffer) {
			if (mFrameSize > cbBuffer)
				ret = IVDStreamSource::kBufferTooSmall;
			else {
				mpParent->ReadSpan(mFrameOffset + mFrameStride * lStart, lpBuffer, mFrameSize);
			}
		}
	}

	if (lBytesRead)
		*lBytesRead = mFrameSize;
	if (lSamplesRead)
		*lSamplesRead = lCount;

	return ret;
}

const void *VDVideoSourceRawVideo::getFrame(VDPosition frameNum) {
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

const void *VDVideoSourceRawVideo::streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num, VDPosition target_sample) {
	VDPixmap srcbm = VDPixmapFromLayout(mLayout, (void *)inputBuffer);
	switch (srcbm.format) {
	case nsVDPixmap::kPixFormat_Y16:
		srcbm.info.ref_r = 0xFFFF;
		break;
	}

	VDPixmapBlt(mTargetFormat, srcbm);

	mCachedFrame = frame_num;

	return getFrameBuffer();
}

bool VDVideoSourceRawVideo::setTargetFormat(VDPixmapFormatEx format) {
	if (!format)
		format = mLayout.format;
	else
		return false;

	if (!VideoSource::setTargetFormat(format))
		return false;

	invalidateFrameBuffer();
	return true;
}

///////////////////////////////////////////////////////////////////////////

class VDInputFileRawVideo : public InputFile, public IVDInputFileRawVideo {
public:
	VDInputFileRawVideo();
	~VDInputFileRawVideo();

	int AddRef();
	int Release();

	void Init(const wchar_t *szFile);

	void setOptions(InputFileOptions *);
	InputFileOptions *promptForOptions(VDGUIHandle hwndParent);
	InputFileOptions *createOptions(const void *buf, uint32 len);

	void setAutomated(bool fAuto);

	bool GetVideoSource(int index, IVDVideoSource **ppSrc);
	bool GetAudioSource(int index, AudioSource **ppSrc);

public:
	sint64 GetFileSize() { return mFileSize; }
	void ReadSpan(sint64 pos, void *data, uint32 len);

protected:
	VDFile		mFile;
	sint64		mFileSize;

	VDInputFileRawVideoOptions mOptions;
};

VDInputFileRawVideo::VDInputFileRawVideo() {
}

VDInputFileRawVideo::~VDInputFileRawVideo() {
}

int VDInputFileRawVideo::AddRef() {
	return InputFile::AddRef();
}

int VDInputFileRawVideo::Release() {
	return InputFile::Release();
}

void VDInputFileRawVideo::Init(const wchar_t *filename) {
	mFile.open(filename, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting);
	mFileSize = mFile.size();
}

void VDInputFileRawVideo::setOptions(InputFileOptions *options) {
	mOptions = *static_cast<VDInputFileRawVideoOptions *>(options);
}

InputFileOptions *VDInputFileRawVideo::promptForOptions(VDGUIHandle hwndParent) {
	vdautoptr<VDInputFileRawVideoOptions> options(new VDInputFileRawVideoOptions);

	// restore from registry
	VDRegistryAppKey key(g_szRegKeyPersistence);

	VDInputFileRawVideoOptions regopts;
	regopts.mFrameRate = VDFraction((unsigned)key.getInt("Raw Video Input: Frame rate low", regopts.mFrameRate.getHi()),
		(unsigned)key.getInt("Raw Video Input: Frame rate high", regopts.mFrameRate.getLo()));
	regopts.mWidth = key.getInt("Raw Video Input: Width", regopts.mWidth);
	regopts.mHeight = key.getInt("Raw Video Input: Height", regopts.mHeight);
	regopts.mAlignment = key.getInt("Raw Video Input: Alignment", regopts.mAlignment);
	regopts.mFormat = key.getInt("Raw Video Input: Format", regopts.mFormat);
	regopts.mbUpsideDown = key.getBool("Raw Video Input: Upside-down", regopts.mbUpsideDown);
	regopts.mbSwapChromaPlanes = key.getBool("Raw Video Input: Swap chroma planes", regopts.mbSwapChromaPlanes);
	regopts.mInitialPadding = key.getInt("Raw Video Input: Initial padding", regopts.mInitialPadding);
	regopts.mPostFramePadding = key.getInt("Raw Video Input: Post-frame padding", regopts.mPostFramePadding);

	if (regopts.validate())
		*options = regopts;

	VDInputFileRawVideoOptionsDialog dlg(*options);
	if (!dlg.ShowDialog(hwndParent))
		return NULL;

	key.setInt("Raw Video Input: Frame rate low", options->mFrameRate.getHi());
	key.setInt("Raw Video Input: Frame rate high", options->mFrameRate.getLo());
	key.setInt("Raw Video Input: Width", options->mWidth);
	key.setInt("Raw Video Input: Height", options->mHeight);
	key.setInt("Raw Video Input: Alignment", options->mAlignment);
	key.setInt("Raw Video Input: Format", options->mFormat);
	key.setBool("Raw Video Input: Upside-down", options->mbUpsideDown);
	key.setBool("Raw Video Input: Swap chroma planes", options->mbSwapChromaPlanes);
	key.setInt("Raw Video Input: Initial padding", options->mInitialPadding);
	key.setInt("Raw Video Input: Post-frame padding", options->mPostFramePadding);

	return options.release();
}

InputFileOptions *VDInputFileRawVideo::createOptions(const void *buf, uint32 len) {
	vdautoptr<VDInputFileRawVideoOptions> options(new VDInputFileRawVideoOptions);

	options->read((const char *)buf, len);
	return options.release();
}

void VDInputFileRawVideo::setAutomated(bool fAuto) {
}

bool VDInputFileRawVideo::GetVideoSource(int index, IVDVideoSource **ppSrc) {
	if (index)
		return false;

	*ppSrc = new VDVideoSourceRawVideo(this, mOptions);
	if (!*ppSrc)
		return false;
	(*ppSrc)->AddRef();
	return true;
}

bool VDInputFileRawVideo::GetAudioSource(int index, AudioSource **ppSrc) {
	return false;
}

void VDInputFileRawVideo::ReadSpan(sint64 pos, void *data, uint32 len) {
	mFile.seek(pos);
	mFile.read(data, len);
}

///////////////////////////////////////////////////////////////////////////

class VDInputDriverRawVideo : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return L"Raw video input driver (internal)"; }

	int GetDefaultPriority() {
		return 0;
	}

	uint32 GetFlags() { return kF_Video | kF_PromptForOpts | kF_SupportsOpts; }

	const wchar_t *GetFilenamePattern() {
		return L"Raw video (*.yuv,*.rgb,*.bin)\0*.yuv;*.rgb;*.bin\0";
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		size_t l = wcslen(pszFilename);

		if (l>4 && !_wcsicmp(pszFilename + l - 4, L".yuv"))
			return true;

		if (l>4 && !_wcsicmp(pszFilename + l - 4, L".rgb"))
			return true;

		if (l>4 && !_wcsicmp(pszFilename + l - 4, L".bin"))
			return true;

		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		return kDC_None;
	}

	InputFile *CreateInputFile(uint32 flags) {
		return new VDInputFileRawVideo;
	}
};

extern IVDInputDriver *VDCreateInputDriverRawVideo() { return new VDInputDriverRawVideo; }
