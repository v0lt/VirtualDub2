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

#include "VideoSource.h"
#include "VBitmap.h"
#include "AVIStripeSystem.h"
#include "ProgressDialog.h"
#include "MJPEGDecoder.h"
#include "crash.h"

#include <vd2/system/bitmath.h>
#include <vd2/system/debug.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <vd2/system/log.h>
#include <vd2/system/protscope.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/Dita/resources.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Riza/bitmap.h>
#include <../dfsc/dfsc.h>
#include "misc.h"
#include "oshelper.h"
#include "helpfile.h"
#include "resource.h"
#include "VideoSourceAVI.h"

#if defined(_M_AMD64)
	#define VDPROT_PTR	"%p"
#else
	#define VDPROT_PTR	"%08x"
#endif

///////////////////////////

extern const char *LookupVideoCodec(uint32);
extern bool VDPreferencesIsDirectYCbCrInputEnabled();
extern bool VDPreferencesIsUseVideoFccHandlerEnabled();

///////////////////////////

namespace {
	enum { kVDST_VideoSource = 3 };

	enum {
		kVDM_ResumeFromConceal,
		kVDM_DecodingError,
		kVDM_FrameTooShort,
		kVDM_CodecMMXError,
		kVDM_FixingHugeVideoFormat,
		kVDM_CodecRenamingDetected,
		kVDM_CodecAcceptsBS
	};
}

namespace {
	enum { kVDST_VideoSequenceCompressor = 10 };

	enum {
		kVDM_CodecModifiesInput
	};
}

///////////////////////////////////////////////////////////////////////////

class VDVideoDecompressorMJPEG : public IVDVideoDecompressor {
public:
	VDVideoDecompressorMJPEG();
	~VDVideoDecompressorMJPEG();

	void Init(int w, int h);

	bool QueryTargetFormat(int format);
	bool QueryTargetFormat(const void *format);
	bool SetTargetFormat(int format);
	bool SetTargetFormat(const void *format);
	int GetTargetFormat() { return mFormat; }
	int GetTargetFormatVariant() { return 0; }
	const uint32 *GetTargetFormatPalette() { return NULL; }
	void Start();
	void Stop();
	void DecompressFrame(void *dst, const void *src, uint32 srcSize, bool keyframe, bool preroll);
	const void *GetRawCodecHandlePtr();
	const wchar_t *GetName();

protected:
	int		mWidth, mHeight;
	int		mFormat;

	vdautoptr<IMJPEGDecoder>	mpDecoder;
};

VDVideoDecompressorMJPEG::VDVideoDecompressorMJPEG()
	: mFormat(0)
{
}

VDVideoDecompressorMJPEG::~VDVideoDecompressorMJPEG() {
}

void VDVideoDecompressorMJPEG::Init(int w, int h) {
	mWidth = w;
	mHeight = h;

	mpDecoder = CreateMJPEGDecoder(w, h);
}

bool VDVideoDecompressorMJPEG::QueryTargetFormat(int format) {
	return format == nsVDPixmap::kPixFormat_XRGB1555
		|| format == nsVDPixmap::kPixFormat_XRGB8888
		|| format == nsVDPixmap::kPixFormat_YUV422_UYVY
		|| format == nsVDPixmap::kPixFormat_YUV422_YUYV;
}

bool VDVideoDecompressorMJPEG::QueryTargetFormat(const void *format) {
	const VDAVIBitmapInfoHeader& hdr = *(const VDAVIBitmapInfoHeader *)format;

	if (hdr.biWidth != mWidth || hdr.biHeight != mHeight)
		return false;

	int pxformat = VDBitmapFormatToPixmapFormat(hdr);

	return QueryTargetFormat(pxformat);
}

bool VDVideoDecompressorMJPEG::SetTargetFormat(int format) {
	if (!format)
		format = nsVDPixmap::kPixFormat_XRGB8888;

	if (QueryTargetFormat(format)) {
		mFormat = format;
		return true;
	}

	return false;
}

bool VDVideoDecompressorMJPEG::SetTargetFormat(const void *format) {
	const VDAVIBitmapInfoHeader& hdr = *(const VDAVIBitmapInfoHeader *)format;

	if (hdr.biWidth != mWidth || hdr.biHeight != mHeight)
		return false;

	int pxformat = VDBitmapFormatToPixmapFormat(hdr);

	if (QueryTargetFormat(pxformat)) {
		mFormat = pxformat;
		return true;
	}

	return false;
}

void VDVideoDecompressorMJPEG::Start() {
	if (!mFormat)
		throw MyError("Cannot find compatible target format for video decompression.");
}

void VDVideoDecompressorMJPEG::Stop() {
}

void VDVideoDecompressorMJPEG::DecompressFrame(void *dst, const void *src, uint32 srcSize, bool keyframe, bool preroll) {
	switch(mFormat) {
	case nsVDPixmap::kPixFormat_XRGB1555:
		mpDecoder->decodeFrameRGB15((uint32 *)dst, (uint8 *)src, srcSize);
		break;
	case nsVDPixmap::kPixFormat_XRGB8888:
		mpDecoder->decodeFrameRGB32((uint32 *)dst, (uint8 *)src, srcSize);
		break;
	case nsVDPixmap::kPixFormat_YUV422_UYVY:
		mpDecoder->decodeFrameUYVY((uint32 *)dst, (uint8 *)src, srcSize);
		break;
	case nsVDPixmap::kPixFormat_YUV422_YUYV:
		mpDecoder->decodeFrameYUY2((uint32 *)dst, (uint8 *)src, srcSize);
		break;
	default:
		throw MyError("Cannot find compatible target format for video decompression.");
	}
}

const void *VDVideoDecompressorMJPEG::GetRawCodecHandlePtr() {
	return NULL;
}

const wchar_t *VDVideoDecompressorMJPEG::GetName() {
	return L"Internal Motion JPEG decoder";
}

///////////////////////////////////////////////////////////////////////////

class VDVideoDecompressorDIBBitfields : public IVDVideoDecompressor {
public:
	VDVideoDecompressorDIBBitfields();
	~VDVideoDecompressorDIBBitfields();

	void Init(int w, int h, bool source32, const uint32 bitmasks[3]);

	bool QueryTargetFormat(int format);
	bool QueryTargetFormat(const void *format);
	bool SetTargetFormat(int format);
	bool SetTargetFormat(const void *format);
	int GetTargetFormat() { return mFormat; }
	int GetTargetFormatVariant() { return 0; }
	const uint32 *GetTargetFormatPalette() { return NULL; }
	void Start();
	void Stop();
	void DecompressFrame(void *dst, const void *src, uint32 srcSize, bool keyframe, bool preroll);
	const void *GetRawCodecHandlePtr();
	const wchar_t *GetName();

protected:
	void Decode16To16(void *dst0, const void *src0);
	void Decode16To24(void *dst0, const void *src0);
	void Decode16To32(void *dst0, const void *src0);
	void Decode32To16(void *dst0, const void *src0);
	void Decode32To24(void *dst0, const void *src0);
	void Decode32To32(void *dst0, const void *src0);

	int		mWidth, mHeight;
	int		mFormat;
	bool	mbSource32;
	ptrdiff_t	mSrcOffset;
	ptrdiff_t	mSrcModulo;
	ptrdiff_t	mDstModulo;
	uint32	mRedMask;
	uint32	mGreenMask;
	uint32	mBlueMask;
	int		mRedShift;
	int		mGreenShift;
	int		mBlueShift;
	int		mRedWidth;
	int		mGreenWidth;
	int		mBlueWidth;
	uint32	mRedRepeat;
	uint32	mGreenRepeat;
	uint32	mBlueRepeat;

	union {
		uint32	mDecodeTables32[4][256];
		uint16	mDecodeTables16[4][256];
	};
};

VDVideoDecompressorDIBBitfields::VDVideoDecompressorDIBBitfields()
	: mFormat(0)
{
}

VDVideoDecompressorDIBBitfields::~VDVideoDecompressorDIBBitfields() {
}

void VDVideoDecompressorDIBBitfields::Init(int w, int h, bool source32, const uint32 bitmasks[3]) {
	h = -h;
	mWidth = w;
	mHeight = abs(h);
	mbSource32 = source32;
	mSrcOffset = 0;
	mSrcModulo = 0;
	if (!source32 && (mWidth & 1))
		mSrcModulo = 2;

	if (h < 0) {
		ptrdiff_t pitch = mbSource32 ? (w << 2) : w + w + mSrcModulo;
		mSrcOffset += pitch * (mHeight - 1);
		mSrcModulo = -pitch*2 + mSrcModulo;
	}

	mRedMask = bitmasks[0];
	mGreenMask = bitmasks[1];
	mBlueMask = bitmasks[2];

	mRedRepeat = 0;
	mGreenRepeat = 0;
	mBlueRepeat = 0;

	mRedWidth = 0;
	mGreenWidth = 0;
	mBlueWidth = 0;

	if (mRedMask) {
		mRedShift = VDFindLowestSetBit(mRedMask);
		mRedWidth = VDCountBits(mRedMask);
		mRedRepeat = (0x7FFFFFFF / (mRedMask >> mRedShift)) << mRedWidth;
	}

	if (mGreenMask) {
		mGreenShift = VDFindLowestSetBit(mGreenMask);
		mGreenWidth = VDCountBits(mGreenMask);
		mGreenRepeat = (0x7FFFFFFF / (mGreenMask >> mGreenShift)) << mGreenWidth;
	}

	if (mBlueMask) {
		mBlueShift = VDFindLowestSetBit(mBlueMask);
		mBlueWidth = VDCountBits(mBlueMask);
		mBlueRepeat = (0x7FFFFFFF / (mBlueMask >> mBlueShift)) << mBlueWidth;
	}
}

bool VDVideoDecompressorDIBBitfields::QueryTargetFormat(int format) {
	return format == nsVDPixmap::kPixFormat_XRGB1555
		|| format == nsVDPixmap::kPixFormat_RGB565
		|| format == nsVDPixmap::kPixFormat_RGB888
		|| format == nsVDPixmap::kPixFormat_XRGB8888;
}

bool VDVideoDecompressorDIBBitfields::QueryTargetFormat(const void *format) {
	const VDAVIBitmapInfoHeader& hdr = *(const VDAVIBitmapInfoHeader *)format;

	if (hdr.biWidth != mWidth || hdr.biHeight != mHeight)
		return false;

	int pxformat = VDBitmapFormatToPixmapFormat(hdr);

	return QueryTargetFormat(pxformat);
}

bool VDVideoDecompressorDIBBitfields::SetTargetFormat(int format) {
	if (!format)
		format = nsVDPixmap::kPixFormat_XRGB8888;

	if (!QueryTargetFormat(format))
		return false;

	mFormat = format;

	uint32 rmask;
	uint32 gmask;
	uint32 bmask;
	int rshift;
	int gshift;
	int bshift;
	int rbits;
	int gbits;
	int bbits;
	bool dest32 = false;

	switch(format) {
		case nsVDPixmap::kPixFormat_XRGB1555:
			rmask = 0x7C00;
			gmask = 0x03E0;
			bmask = 0x001F;
			rshift = 17;
			gshift = 22;
			bshift = 27;
			rbits = 5;
			gbits = 5;
			bbits = 5;
			mDstModulo = 2*(mWidth & 1);
			break;
		case nsVDPixmap::kPixFormat_RGB565:
			rmask = 0xF800;
			gmask = 0x07E0;
			bmask = 0x001F;
			rshift = 16;
			gshift = 21;
			bshift = 27;
			rbits = 5;
			gbits = 6;
			bbits = 5;
			mDstModulo = 2*(mWidth & 1);
			break;
		case nsVDPixmap::kPixFormat_RGB888:
			rmask = 0x00FF0000;
			gmask = 0x0000FF00;
			bmask = 0x000000FF;
			rshift = 8;
			gshift = 16;
			bshift = 24;
			rbits = 8;
			gbits = 8;
			bbits = 8;
			mDstModulo = (mWidth & 3);
			dest32 = true;
			break;
		case nsVDPixmap::kPixFormat_XRGB8888:
			rmask = 0x00FF0000;
			gmask = 0x0000FF00;
			bmask = 0x000000FF;
			rshift = 8;
			gshift = 16;
			bshift = 24;
			rbits = 8;
			gbits = 8;
			bbits = 8;
			mDstModulo = 0;
			dest32 = true;
			break;
	}

	// construct conversion masks
	uint32	bitExpansions[32]={0};

	if (rbits > mRedWidth)
		rbits = mRedWidth;
	if (gbits > mGreenWidth)
		gbits = mGreenWidth;
	if (bbits > mBlueWidth)
		bbits = mBlueWidth;

	for(int i=0; i<rbits; ++i)
		bitExpansions[mRedShift + mRedWidth - 1 - i] = (mRedRepeat >> (rshift + i)) & rmask;

	for(int i=0; i<gbits; ++i)
		bitExpansions[mGreenShift + mGreenWidth - 1 - i] = (mGreenRepeat >> (gshift + i)) & gmask;

	for(int i=0; i<bbits; ++i)
		bitExpansions[mBlueShift + mBlueWidth - 1 - i] = (mBlueRepeat >> (bshift + i)) & bmask;

	if (dest32) {
		for(int i=0; i<4; ++i) {
			uint32 *dst = mDecodeTables32[i];
			const uint32 *VDRESTRICT bitexp = bitExpansions + i*8;
			for(int j=0; j<256; ++j) {
				uint32 v = 0;

				if (j & 0x01) v += bitexp[0];
				if (j & 0x02) v += bitexp[1];
				if (j & 0x04) v += bitexp[2];
				if (j & 0x08) v += bitexp[3];
				if (j & 0x10) v += bitexp[4];
				if (j & 0x20) v += bitexp[5];
				if (j & 0x40) v += bitexp[6];
				if (j & 0x80) v += bitexp[7];

				dst[j] = v;
			}
		}
	} else {
		for(int i=0; i<4; ++i) {
			uint16 *dst = mDecodeTables16[i];
			const uint32 *VDRESTRICT bitexp = bitExpansions + i*8;
			for(int j=0; j<256; ++j) {
				uint32 v = 0;

				if (j & 0x01) v += bitexp[0];
				if (j & 0x02) v += bitexp[1];
				if (j & 0x04) v += bitexp[2];
				if (j & 0x08) v += bitexp[3];
				if (j & 0x10) v += bitexp[4];
				if (j & 0x20) v += bitexp[5];
				if (j & 0x40) v += bitexp[6];
				if (j & 0x80) v += bitexp[7];

				dst[j] = (uint16)v;
			}
		}
	}

	return true;
}

bool VDVideoDecompressorDIBBitfields::SetTargetFormat(const void *format) {
	const VDAVIBitmapInfoHeader& hdr = *(const VDAVIBitmapInfoHeader *)format;

	if (hdr.biWidth != mWidth || hdr.biHeight != mHeight)
		return false;

	int pxformat = VDBitmapFormatToPixmapFormat(hdr);
	if (!pxformat)
		return false;

	return SetTargetFormat(pxformat);
}

void VDVideoDecompressorDIBBitfields::Start() {
	if (!mFormat)
		throw MyError("Cannot find compatible target format for video decompression.");
}

void VDVideoDecompressorDIBBitfields::Stop() {
}

void VDVideoDecompressorDIBBitfields::DecompressFrame(void *dst, const void *src, uint32 srcSize, bool keyframe, bool preroll) {
	if (mbSource32) {
		switch(mFormat) {
			case nsVDPixmap::kPixFormat_XRGB1555:
			case nsVDPixmap::kPixFormat_RGB565:
				Decode32To16(dst, src);
				break;
			case nsVDPixmap::kPixFormat_RGB888:
				Decode32To24(dst, src);
				break;
			case nsVDPixmap::kPixFormat_XRGB8888:
				Decode32To32(dst, src);
				break;
		}
	} else {
		switch(mFormat) {
			case nsVDPixmap::kPixFormat_XRGB1555:
			case nsVDPixmap::kPixFormat_RGB565:
				Decode16To16(dst, src);
				break;
			case nsVDPixmap::kPixFormat_RGB888:
				Decode16To24(dst, src);
				break;
			case nsVDPixmap::kPixFormat_XRGB8888:
				Decode16To32(dst, src);
				break;
		}
	}
}

const void *VDVideoDecompressorDIBBitfields::GetRawCodecHandlePtr() {
	return NULL;
}

const wchar_t *VDVideoDecompressorDIBBitfields::GetName() {
	return L"Internal DIB BI_BITFIELDS decoder";
}

void VDVideoDecompressorDIBBitfields::Decode16To16(void *dst0, const void *src0) {
	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)src0 + mSrcOffset;
	uint16 *dst = (uint16 *)dst0;

	for(uint32 y=0; y<h; ++y) {
		for(uint32 x=0; x<w; ++x) {
			*dst++ = mDecodeTables16[0][src[0]] + mDecodeTables16[1][src[1]];
			src += 2;
		}

		vdptrstep(src, mSrcModulo);
		vdptrstep(dst, mDstModulo);
	}
}

void VDVideoDecompressorDIBBitfields::Decode16To24(void *dst0, const void *src0) {
	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)src0 + mSrcOffset;
	uint8 *dst = (uint8 *)dst0;

	for(uint32 y=0; y<h; ++y) {
		for(uint32 x=0; x<w; ++x) {
			uint32 v = mDecodeTables32[0][src[0]] + mDecodeTables32[1][src[1]];
			*(uint16 *)dst = (uint16)v;
			*(uint8 *)(dst + 2) = (uint8)(v >> 16);
			dst += 3;
			src += 2;
		}

		vdptrstep(src, mSrcModulo);
		vdptrstep(dst, mDstModulo);
	}
}

void VDVideoDecompressorDIBBitfields::Decode16To32(void *dst0, const void *src0) {
	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)src0 + mSrcOffset;
	uint32 *dst = (uint32 *)dst0;

	for(uint32 y=0; y<h; ++y) {
		for(uint32 x=0; x<w; ++x) {
			*dst++ = mDecodeTables32[0][src[0]] + mDecodeTables32[1][src[1]];
			src += 2;
		}

		vdptrstep(src, mSrcModulo);
		vdptrstep(dst, mDstModulo);
	}
}

void VDVideoDecompressorDIBBitfields::Decode32To16(void *dst0, const void *src0) {
	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)src0 + mSrcOffset;
	uint16 *dst = (uint16 *)dst0;

	for(uint32 y=0; y<h; ++y) {
		for(uint32 x=0; x<w; ++x) {
			*dst++ = mDecodeTables16[0][src[0]] + mDecodeTables16[1][src[1]] + mDecodeTables16[2][src[2]] + mDecodeTables16[3][src[3]];
			src += 4;
		}

		vdptrstep(src, mSrcModulo);
		vdptrstep(dst, mDstModulo);
	}
}

void VDVideoDecompressorDIBBitfields::Decode32To24(void *dst0, const void *src0) {
	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)src0 + mSrcOffset;
	uint8 *dst = (uint8 *)dst0;

	for(uint32 y=0; y<h; ++y) {
		for(uint32 x=0; x<w; ++x) {
			uint32 v = mDecodeTables32[0][src[0]] + mDecodeTables32[1][src[1]] + mDecodeTables32[2][src[2]] + mDecodeTables32[3][src[3]];
			*(uint16 *)dst = (uint16)v;
			*(uint8 *)(dst + 2) = (uint8)(v >> 16);
			dst += 3;
			src += 4;
		}

		vdptrstep(src, mSrcModulo);
		vdptrstep(dst, mDstModulo);
	}
}

void VDVideoDecompressorDIBBitfields::Decode32To32(void *dst0, const void *src0) {
	const uint32 w = mWidth;
	const uint32 h = mHeight;
	const uint8 *src = (const uint8 *)src0 + mSrcOffset;
	uint32 *dst = (uint32 *)dst0;

	for(uint32 y=0; y<h; ++y) {
		for(uint32 x=0; x<w; ++x) {
			*dst++ = mDecodeTables32[0][src[0]] + mDecodeTables32[1][src[1]] + mDecodeTables32[2][src[2]] + mDecodeTables32[3][src[3]];
			src += 4;
		}

		vdptrstep(src, mSrcModulo);
		vdptrstep(dst, mDstModulo);
	}
}

///////////////////////////////////////////////////////////////////////////

class VDVideoDecompressorDIB : public IVDVideoDecompressor {
public:
	VDVideoDecompressorDIB();
	~VDVideoDecompressorDIB();

	void Init(const VDPixmapLayout& srcLayout, int variant, const uint32 *palette, uint32 maxPaletteEntries);

	bool QueryTargetFormat(int format);
	bool QueryTargetFormat(const void *format);
	bool SetTargetFormat(int format);
	bool SetTargetFormat(const void *format);
	int GetTargetFormat() { return mDstFormat; }
	int GetTargetFormatVariant() { return mDstFormatVariant; }
	const uint32 *GetTargetFormatPalette() { return NULL; }
	void Start();
	void Stop();
	void DecompressFrame(void *dst, const void *src, uint32 srcSize, bool keyframe, bool preroll);
	const void *GetRawCodecHandlePtr();
	const wchar_t *GetName();
	bool GetAlpha() {
		return VDPixmapFormatHasAlpha(mSrcLayout.format);
	}

protected:
	int		mWidth, mHeight;
	uint32	mSrcLinSize;
	int		mDstFormat;
	int		mDstFormatVariant;
	int		mSrcFormatVariant;
	VDPixmapLayout	mSrcLayout;
	VDPixmapLayout	mDstLayout;

	uint32	mPalette[256];
};

VDVideoDecompressorDIB::VDVideoDecompressorDIB()
	: mWidth(0)
	, mHeight(0)
	, mSrcLinSize(0)
	, mDstFormat(0)
	, mDstFormatVariant(0)
	, mSrcFormatVariant(0)
	, mSrcLayout()
	, mDstLayout()
{
}

VDVideoDecompressorDIB::~VDVideoDecompressorDIB() {
}

void VDVideoDecompressorDIB::Init(const VDPixmapLayout& srcLayout, int variant, const uint32 *palette, uint32 maxPaletteEntries) {
	mWidth = srcLayout.w;
	mHeight = srcLayout.h;
	mSrcLayout = srcLayout;
	mSrcFormatVariant = variant;
	mSrcLinSize = VDPixmapLayoutGetMinSize(mSrcLayout);

	uint32 palEnts = VDPixmapGetInfo(srcLayout.format).palsize;

	if (palEnts) {
		if (palEnts > maxPaletteEntries)
			palEnts = maxPaletteEntries;

		uint32 toCopy = sizeof(uint32)*palEnts;
		memcpy(mPalette, palette, toCopy);
		memset(mPalette + palEnts, 0, sizeof mPalette - toCopy);

		mSrcLayout.palette = mPalette;
	}
}

bool VDVideoDecompressorDIB::QueryTargetFormat(int format) {
	return format >= nsVDPixmap::kPixFormat_XRGB1555;
}

bool VDVideoDecompressorDIB::QueryTargetFormat(const void *format) {
	const VDAVIBitmapInfoHeader& hdr = *(const VDAVIBitmapInfoHeader *)format;

	if (hdr.biWidth != mWidth || hdr.biHeight != mHeight)
		return false;

	int pxformat = VDBitmapFormatToPixmapFormat(hdr);

	return QueryTargetFormat(pxformat);
}

bool VDVideoDecompressorDIB::SetTargetFormat(int format) {
	bool enable_convert = false;

	if (mSrcFormatVariant!=1)
		enable_convert = false;

	switch(mSrcLayout.format) {
	case nsVDPixmap::kPixFormat_Y8:
	case nsVDPixmap::kPixFormat_XRGB64:
	case nsVDPixmap::kPixFormat_B64A:
	case nsVDPixmap::kPixFormat_YUV444_Planar16:
	case nsVDPixmap::kPixFormat_YUV422_Planar16:
	case nsVDPixmap::kPixFormat_YUV420_Planar16:
	case nsVDPixmap::kPixFormat_YUVA444_Y416:
	case nsVDPixmap::kPixFormat_YUV444_V410:
	case nsVDPixmap::kPixFormat_YUV444_Y410:
	case nsVDPixmap::kPixFormat_R210:
	case nsVDPixmap::kPixFormat_R10K:
	case nsVDPixmap::kPixFormat_B48R:
	case nsVDPixmap::kPixFormat_YUV444_V308:
	case nsVDPixmap::kPixFormat_YUV422_P210:
	case nsVDPixmap::kPixFormat_YUV420_P010:
	case nsVDPixmap::kPixFormat_YUV422_P216:
	case nsVDPixmap::kPixFormat_YUV420_P016:
		enable_convert = false;
		break;
	}

	if (enable_convert && format) {
		VDMakeBitmapCompatiblePixmapLayout(mDstLayout, mWidth, mHeight, format, 0);
		mDstFormat = format;
		mDstFormatVariant = 1;
	} else {
		mDstLayout = mSrcLayout;
		mDstFormat = mSrcLayout.format;
		mDstFormatVariant = mSrcFormatVariant;
	}

	return true;
}

bool VDVideoDecompressorDIB::SetTargetFormat(const void *format) {
	const VDAVIBitmapInfoHeader& hdr = *(const VDAVIBitmapInfoHeader *)format;

	if (hdr.biWidth != mWidth || hdr.biHeight != mHeight)
		return false;

	int variant;
	int pxformat = VDBitmapFormatToPixmapFormat(hdr, variant);
	if (!pxformat)
		return false;

	VDMakeBitmapCompatiblePixmapLayout(mDstLayout, mWidth, mHeight, pxformat, variant);
	mDstFormat = pxformat;
	mDstFormatVariant = variant;
	return true;
}

void VDVideoDecompressorDIB::Start() {
}

void VDVideoDecompressorDIB::Stop() {
}

void VDVideoDecompressorDIB::DecompressFrame(void *dst, const void *src, uint32 srcSize, bool keyframe, bool preroll) {
	if (srcSize < mSrcLinSize)
		throw MyError("Cannot decompress video frame: the video data is too short (%u bytes, should be %u).", srcSize, mSrcLinSize);

	VDPixmap pxdst(VDPixmapFromLayout(mDstLayout, dst));
	VDPixmap pxsrc(VDPixmapFromLayout(mSrcLayout, (void *)src));

	VDPixmapBlt(pxdst, pxsrc);
}

const void *VDVideoDecompressorDIB::GetRawCodecHandlePtr() {
	return NULL;
}

const wchar_t *VDVideoDecompressorDIB::GetName() {
	return L"Internal DIB decoder";
}

///////////////////////////////////////////////////////////////////////////

bool VDPixmapIsFCCFormat(int format) {
	switch(format) {
		case 0:
		case nsVDPixmap::kPixFormat_Pal1:
		case nsVDPixmap::kPixFormat_Pal2:
		case nsVDPixmap::kPixFormat_Pal4:
		case nsVDPixmap::kPixFormat_Pal8:
		case nsVDPixmap::kPixFormat_XRGB1555:
		case nsVDPixmap::kPixFormat_RGB565:
		case nsVDPixmap::kPixFormat_RGB888:
		case nsVDPixmap::kPixFormat_XRGB8888:
			return false;

		default:
			return true;
	}
}

IVDVideoDecompressor *VDFindVideoDecompressorEx(uint32 fccHandler, const VDAVIBitmapInfoHeader *hdr, uint32 hdrlen, bool preferInternal) {
	IVDVideoDecompressor *dec;

	// get a decompressor
	//
	// 'DIB ' is the official value for uncompressed AVIs, but some stupid
	// programs also use (null) and 'RAW '
	//
	// NOTE: Don't handle RLE4/RLE8 here.  RLE is slightly different in AVI files!

	int variant;
	int format = VDBitmapFormatToPixmapFormat(*hdr, variant);

	if (!VDPreferencesIsDirectYCbCrInputEnabled()) {
		if (VDPixmapIsFCCFormat(format))
			format = 0;
	}

	bool is_dib = (format != 0);
	if (is_dib) {
		dec = new VDVideoDecompressorDIB;
		if (dec) {
			VDPixmapLayout layout;
			VDMakeBitmapCompatiblePixmapLayout(layout, hdr->biWidth, hdr->biHeight, format, variant, 0, hdr->biSizeImage);

			const uint32 *palette = (const uint32 *)(hdr + 1);
			static_cast<VDVideoDecompressorDIB *>(dec)->Init(layout, variant, palette, hdrlen >= sizeof(VDAVIBitmapInfoHeader) ? (hdrlen - sizeof(VDAVIBitmapInfoHeader)) >> 2 : 0);
			return dec;
		}
	}

	if (hdr->biCompression == VDAVIBitmapInfoHeader::kCompressionBitfields && (hdr->biBitCount == 16 || hdr->biBitCount == 32)) {
		dec = new VDVideoDecompressorDIBBitfields;
		if (dec) {
			static_cast<VDVideoDecompressorDIBBitfields *>(dec)->Init(hdr->biWidth, hdr->biHeight, hdr->biBitCount == 32, (const uint32 *)(hdr + 1));

			return dec;
		}

	}

	// If we aren't preferring internal decoders, look for an external decoder.
	if (!preferInternal) {
		dec = VDFindVideoDecompressor(fccHandler, hdr, hdrlen);
		if (dec)
			return dec;
	}

	const int w = hdr->biWidth;
	const int h = abs((int)hdr->biHeight);

	// If it's Motion JPEG, use the internal decoder.
	bool is_mjpeg	 = isEqualFOURCC(hdr->biCompression, 'GPJM')
					|| isEqualFOURCC(hdr->biCompression, '1bmd');
	if (is_mjpeg) {
		vdautoptr<VDVideoDecompressorMJPEG> pDecoder(new(std::nothrow) VDVideoDecompressorMJPEG);
		if (pDecoder) {
			pDecoder->Init(w, h);

			return pDecoder.release();
		}
	}

	// If it's DV, use the internal decoder.
	bool is_dv = isEqualFOURCC(hdr->biCompression, 'dsvd');
	if (is_dv && w==720 && (h == 480 || h == 576)) {
		dec = VDCreateVideoDecompressorDV(w, h);
		if (dec)
			return dec;
	}

	// If it's DV, use the internal decoder.
	bool is_huffyuv = isEqualFOURCC(hdr->biCompression, 'uyfh');
	if (is_huffyuv && !(w & 1)) {
		dec = VDCreateVideoDecompressorHuffyuv(w, h, hdr->biBitCount, (const uint8 *)(hdr + 1), hdrlen - sizeof(*hdr));
		if (dec)
			return dec;
	}

	// If it's Debugmode, use the internal decoder.
	bool is_dfsc = isEqualFOURCC(hdr->biCompression, 'CSFD');
	if (is_dfsc) {
		vdautoptr<VDVideoDecompressorDFSC> pDecoder(new(std::nothrow) VDVideoDecompressorDFSC);
		if (pDecoder) {
			pDecoder->Init(hdr, hdrlen);

			return pDecoder.release();
		}
	}

	// if we were asked to use an internal decoder and failed, try external decoders
	// now
	if (preferInternal) {
		dec = VDFindVideoDecompressor(fccHandler, hdr, hdrlen);
		if (dec)
			return dec;
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////

VideoSource::VideoSource() = default;

void *VideoSource::AllocFrameBuffer(uint32 size) {
	if (mFrameBufferSize != size) {

		mpFrameBuffer.reset(VDAlignedMalloc(size, 128));
		mFrameBufferSize = size;
	}

	return mpFrameBuffer.get();
}

const VDFraction VideoSource::getPixelAspectRatio() const {
	return VDFraction(0, 0);
}

bool VideoSource::setTargetFormat(VDPixmapFormatEx format) {
	return setTargetFormatVariant(format,0);
}

bool VideoSource::setTargetFormatVariant(VDPixmapFormatEx format, int variant) {
	using namespace nsVDPixmap;

	if (!format)
		format = kPixFormat_XRGB8888;
	mSourceFormat = format.format;

	const VDAVIBitmapInfoHeader *bih = getImageFormat();
	const sint32 w = bih->biWidth;
	const sint32 h = abs(bih->biHeight);			// we don't want inverted output....
	int bih_format = VDBitmapFormatToPixmapFormat(*bih);
	VDPixmapLayout layout;

	format = VDPixmapFormatCombine(format);
	VDMakeBitmapCompatiblePixmapLayout(layout, w, h, format, variant, 0, bih_format ? bih->biSizeImage:0);

	mTargetFormat = VDPixmapFromLayout(layout, mpFrameBuffer.get());
	mTargetFormatVariant = variant;
	mTargetFormat.info.colorRangeMode = format.colorRangeMode;
	mTargetFormat.info.colorSpaceMode = format.colorSpaceMode;

	if(format == nsVDPixmap::kPixFormat_Pal8) {
		int maxBytes = getFormatLen();
		int colors = bih->biClrUsed;
		if (!colors && (bih->biCompression == VDAVIBitmapInfoHeader::kCompressionRGB || bih->biCompression == VDAVIBitmapInfoHeader::kCompressionRLE4 || bih->biCompression == VDAVIBitmapInfoHeader::kCompressionRLE8) && bih->biBitCount <= 8)
			colors = 1 << bih->biBitCount;
		if (colors > 256)
			colors = 256;

		mpTargetFormatHeader.assign(getImageFormat(), sizeof(VDAVIBitmapInfoHeader) + sizeof(VDAVIRGBQuad) * colors);
		mpTargetFormatHeader->biSize			= sizeof(VDAVIBitmapInfoHeader);
		mpTargetFormatHeader->biPlanes			= 1;
		mpTargetFormatHeader->biBitCount		= 8;
		mpTargetFormatHeader->biCompression		= VDAVIBitmapInfoHeader::kCompressionRGB;
		mpTargetFormatHeader->biSizeImage		= ((w+3)&~3)*h;
		VDAVIRGBQuad *palette = (VDAVIRGBQuad *)(&*mpTargetFormatHeader + 1);

		if (colors > 0) {
			int maxColors = (maxBytes - bih->biSize) / sizeof(VDAVIRGBQuad);

			memset(palette, 0, sizeof(VDAVIRGBQuad)*colors);

			if (colors > maxColors)
				colors = maxColors;

			memcpy(palette, (char *)bih + bih->biSize, sizeof(VDAVIRGBQuad)*colors);
		}

		mTargetFormat.palette = mPalette;
	} else {
		const vdstructex<VDAVIBitmapInfoHeader> src(bih, getFormatLen());

		if (!VDMakeBitmapFormatFromPixmapFormat(mpTargetFormatHeader, src, format, variant)) {
			mpTargetFormatHeader.clear();
		}
	}

	invalidateFrameBuffer();

	return true;
}

bool VideoSource::setDecompressedFormat(int depth) {
	switch(depth) {
	case 8:		return setTargetFormat(nsVDPixmap::kPixFormat_Pal8);
	case 16:	return setTargetFormat(nsVDPixmap::kPixFormat_XRGB1555);
	case 24:	return setTargetFormat(nsVDPixmap::kPixFormat_RGB888);
	case 32:	return setTargetFormat(nsVDPixmap::kPixFormat_XRGB8888);
	}

	return false;
}

bool VideoSource::setDecompressedFormat(const VDAVIBitmapInfoHeader *pbih) {
	const VDAVIBitmapInfoHeader *bih = getImageFormat();

	if (pbih->biWidth == bih->biWidth && pbih->biHeight == bih->biHeight) {
		int variant;

		int format = VDBitmapFormatToPixmapFormat(*pbih, variant);

		if (format && variant <= 1)
			return setTargetFormat(format);
	}

	return false;
}

bool VideoSource::streamOwn(void *owner) {
	if (owner == mpStreamOwner)
		return true;

	if (mpStreamOwner)
		streamEnd();

	mpStreamOwner = owner;
	return false;
}

void VideoSource::streamDisown(void *owner) {
	if (mpStreamOwner == owner) {
		mpStreamOwner = NULL;
		streamEnd();
	}
}

void VideoSource::streamBegin(bool, bool) {
	streamRestart();
}

void VideoSource::streamRestart() {
	stream_current_frame	= -1;
}

void VideoSource::streamSetDesiredFrame(VDPosition frame_num) {
	VDPosition key;

	key = isKey(frame_num) ? frame_num : prevKey(frame_num);
	if (key<0)
		key = mSampleFirst;

	stream_desired_frame	= frame_num;

	if (stream_current_frame<key || stream_current_frame>frame_num)
		stream_current_frame	= key-1;

}

VDPosition VideoSource::streamGetNextRequiredFrame(bool& is_preroll) {
	if (stream_current_frame == stream_desired_frame) {
		is_preroll = false;

		return -1;
	}

	// skip zero-byte preroll frames
	do {
		is_preroll = (++stream_current_frame != stream_desired_frame);
	} while(is_preroll && stream_current_frame < stream_desired_frame && getDropType(stream_current_frame) == kDroppable);

	return stream_current_frame;
}

int VideoSource::streamGetRequiredCount(uint32 *totalsize) {

	VDPosition current = stream_current_frame + 1;
	uint32 size = 0;
	uint32 samp;
	uint32 fetch = 0;

	while(current <= stream_desired_frame) {
		uint32 onesize = 0;

		if (IVDStreamSource::kOK == read(current, 1, NULL, NULL, &onesize, &samp))
			size += onesize;

		if (onesize)
			++fetch;

		++current;
	}

	if (totalsize)
		*totalsize = size;

	if (!fetch)
		fetch = 1;

	return (int)fetch;
}

void VideoSource::invalidateFrameBuffer() {
}

bool VideoSource::isKey(VDPosition lSample) {
	if (lSample<mSampleFirst || lSample>=mSampleLast)
		return true;

	return _isKey(lSample);
}

bool VideoSource::_isKey(VDPosition lSample) {
	return true;
}

VDPosition VideoSource::nearestKey(VDPosition lSample) {
	return lSample;
}

VDPosition VideoSource::prevKey(VDPosition lSample) {
	return lSample <= mSampleFirst ? -1 : lSample-1;
}

VDPosition VideoSource::nextKey(VDPosition lSample) {
	return lSample+1 >= mSampleFirst ? -1 : lSample+1;
}

bool VideoSource::isKeyframeOnly() {
   return false;
}

bool VideoSource::isSyncDecode() {
   return false;
}

bool VideoSource::isType1() {
   return false;
}

///////////////////////////

VideoSourceAVI::VideoSourceAVI(InputFileAVI *pParent, IAVIReadHandler *pAVI, AVIStripeSystem *stripesys, IAVIReadHandler **stripe_files, bool use_internal, int mjpeg_mode, uint32 fccForceVideo, uint32 fccForceVideoHandler, const uint32 *key_flags)
	: VDAVIStreamSource(pParent)
	, mpKeyFlags(key_flags)
	, mErrorMode(kErrorModeReportAll)
	, mbMMXBrokenCodecDetected(false)
	, mbConcealingErrors(false)
	, mbDecodeStarted(false)
	, mbDecodeRealTime(false)
{
	pAVIFile	= pAVI;
	pAVIStream	= NULL;
	mjpeg_reorder_buffer = NULL;
	mjpeg_reorder_buffer_size = 0;
	mjpeg_splits = NULL;
	mjpeg_last = -1;
	this->fccForceVideo = fccForceVideo;
	this->fccForceVideoHandler = fccForceVideoHandler;
	bDirectDecompress = false;
	bInvertFrames = false;
	lLastFrame = -1;

	this->use_internal = use_internal;
	this->mjpeg_mode	= mjpeg_mode;
}

void VideoSourceAVI::_destruct() {
	mpDecompressor = NULL;

	if (pAVIStream) {
		delete pAVIStream;
		pAVIStream = NULL;
	}

	freemem(mjpeg_reorder_buffer);
	mjpeg_reorder_buffer = NULL;
	delete[] mjpeg_splits;
	mjpeg_splits = NULL;
}

VideoSourceAVI::~VideoSourceAVI() {
	_destruct();
}

bool VideoSourceAVI::Init(int streamIndex) {
	bool found;
	try {
		found = _construct(streamIndex);
	} catch(const MyError&) {
		_destruct();
		throw;
	}
	return found;
}

bool VideoSourceAVI::_construct(int streamIndex) {
	LONG format_len;
	VDAVIBitmapInfoHeader *bmih;

	// Look for a standard vids stream

	bIsType1 = false;
	pAVIStream = pAVIFile->GetStream(streamtypeVIDEO, streamIndex);
	if (!pAVIStream) {
		pAVIStream = pAVIFile->GetStream('svai', streamIndex);

		if (!pAVIStream)
			return false;

		bIsType1 = true;
	}

	if (pAVIStream->Info(&streamInfo))
		throw MyError("Error obtaining video stream info.");

	// Force the video type to be 'vids', in case it was type-1 coming in.

	streamInfo.fccType = 'sdiv';

	// Read video format.  If we're striping, the index stripe has a fake
	// format, so we have to grab the format from a video stripe.  If it's a
	// type-1 DV, we're going to have to fake it.

	if (bIsType1) {
		format_len = sizeof(VDAVIBitmapInfoHeader);

		if (!(bmih = (VDAVIBitmapInfoHeader *)allocFormat(format_len))) throw MyMemoryError();

		bmih->biSize			= sizeof(VDAVIBitmapInfoHeader);
		bmih->biWidth			= 720;

		if (streamInfo.dwRate > streamInfo.dwScale*26i64)
			bmih->biHeight			= 480;
		else
			bmih->biHeight			= 576;

		bmih->biPlanes			= 1;
		bmih->biBitCount		= 24;
		bmih->biCompression		= 'dsvd';
		bmih->biSizeImage		= streamInfo.dwSuggestedBufferSize;
		bmih->biXPelsPerMeter	= 0;
		bmih->biYPelsPerMeter	= 0;
		bmih->biClrUsed			= 0;
		bmih->biClrImportant	= 0;
	} else {
		pAVIStream->FormatSize(0, &format_len);

		vdfastvector<char> format(format_len);

		if (pAVIStream->ReadFormat(0, &format.front(), &format_len))
			throw MyError("Error obtaining video stream format.");

		// check for a very large BITMAPINFOHEADER -- structs as large as 153K
		// can be written out by some ePSXe video plugins

		const VDAVIBitmapInfoHeader *pFormat = (const VDAVIBitmapInfoHeader *)&format.front();

		if (format.size() >= 16384 && format.size() > pFormat->biSize) {
			int badsize = format.size();
			int realsize = pFormat->biSize;

			if (realsize >= sizeof(VDAVIBitmapInfoHeader)) {
				realsize = VDGetSizeOfBitmapHeaderW32((const BITMAPINFOHEADER *)pFormat);

				if (realsize < format.size()) {
					VDLogAppMessage(kVDLogWarning, kVDST_VideoSource, kVDM_FixingHugeVideoFormat, 2, &badsize, &realsize);

					format.resize(realsize);
					format_len = realsize;
				}
			}
		}

		// copy format to official video stream format

		if (!(bmih = (VDAVIBitmapInfoHeader *)allocFormat(format_len)))
			throw MyMemoryError();

		memcpy(getFormat(), pFormat, format.size());
	}

	mpTargetFormatHeader.resize(format_len);

	// initialize pixmap palette
	int palEnts = 0;

	if (bmih->biBitCount <= 8) {
		palEnts = bmih->biClrUsed;

		if (!palEnts)
			palEnts = 1 << bmih->biBitCount;
	}

	if (palEnts) {
		int maxColors = (format_len - bmih->biSize) / sizeof(VDAVIRGBQuad);

		if (palEnts > maxColors)
			palEnts = maxColors;

		memset(mPalette, 0, sizeof mPalette);
		memcpy(mPalette, (const uint32 *)((const char *)bmih + bmih->biSize), std::min<size_t>(256, palEnts) * 4);
	}


	// Some Dazzle drivers apparently do not set biSizeImage correctly.  Also,
	// zero is a valid value for BI_RGB, but it's annoying!

	if (bmih->biCompression == VDAVIBitmapInfoHeader::kCompressionRGB || bmih->biCompression == VDAVIBitmapInfoHeader::kCompressionBitfields) {
		// Check for an inverted DIB.  If so, silently flip it around.

		if ((long)bmih->biHeight < 0) {
			bmih->biHeight = abs((long)bmih->biHeight);
			bInvertFrames = true;
		}

		if (bmih->biPlanes == 1) {
			long nPitch = ((bmih->biWidth * bmih->biBitCount + 31) >> 5) * 4 * abs(bmih->biHeight);

			bmih->biSizeImage = nPitch;
		}
	}

	// Force the video format if necessary

	if (fccForceVideo)
		getImageFormat()->biCompression = fccForceVideo;

	if (fccForceVideoHandler)
		streamInfo.fccHandler = fccForceVideoHandler;

	// Check if we can handle the format directly; if so, convert bitmap format to Kasumi format
	mSourceLayout.data		= 0;
	mSourceLayout.data2		= 0;
	mSourceLayout.data3		= 0;
	mSourceLayout.pitch		= 0;
	mSourceLayout.pitch2	= 0;
	mSourceLayout.pitch3	= 0;
	mSourceLayout.palette	= 0;
	mSourceLayout.format	= 0;
	mSourceLayout.w			= bmih->biWidth;
	mSourceLayout.h			= abs(bmih->biHeight);
	mSourceVariant			= 1;

	mSourceLayout.format = VDBitmapFormatToPixmapFormat(*(const VDAVIBitmapInfoHeader *)bmih, mSourceVariant);

	if (!VDPreferencesIsDirectYCbCrInputEnabled()) {
		if (VDPixmapIsFCCFormat(mSourceLayout.format))
			mSourceLayout.format = 0;
	}

	if (mSourceLayout.format) {
		mSourceFrameSize = VDMakeBitmapCompatiblePixmapLayout(mSourceLayout, mSourceLayout.w, mSourceLayout.h, mSourceLayout.format, mSourceVariant, 0, bmih->biSizeImage);

		vdstructex<VDAVIBitmapInfoHeader> format;
		VDMakeBitmapFormatFromPixmapFormat(format, vdstructex<VDAVIBitmapInfoHeader>(getImageFormat(), getFormatLen()), mSourceLayout.format, mSourceVariant);
	}

	// init target format to something sane
	mTargetFormat = VDPixmapFromLayout(mSourceLayout, mpFrameBuffer.get());
	mpTargetFormatHeader.assign(getImageFormat(), sizeof(VDAVIBitmapInfoHeader));
	mpTargetFormatHeader->biSize			= sizeof(VDAVIBitmapInfoHeader);
	mpTargetFormatHeader->biPlanes			= 1;
	mpTargetFormatHeader->biBitCount		= 32;
	mpTargetFormatHeader->biCompression		= VDAVIBitmapInfoHeader::kCompressionRGB;
	mpTargetFormatHeader->biSizeImage		= mpTargetFormatHeader->biWidth*abs(mpTargetFormatHeader->biHeight)*4;

	// If this is MJPEG, check to see if we should modify the output format and/or stream info

	mSampleFirst = pAVIStream->Start();
	mSampleLast = pAVIStream->End();

	bool is_mjpeg	 = isEqualFOURCC(bmih->biCompression, 'GPJM')
					|| isEqualFOURCC(bmih->biCompression, '1bmd');

	if (is_mjpeg) {
		VDAVIBitmapInfoHeader *pbih = getImageFormat();

		if (mjpeg_mode && mjpeg_mode != IFMODE_SWAP && abs(pbih->biHeight) > 288) {
			pbih->biHeight /= 2;

			if (mjpeg_mode == IFMODE_SPLIT1 || mjpeg_mode == IFMODE_SPLIT2) {
				if (streamInfo.dwRate >= 0x7FFFFFFF)
					streamInfo.dwScale >>= 1;
				else
					streamInfo.dwRate *= 2;

				streamInfo.dwLength *= 2;
				mSampleLast = mSampleLast*2 - mSampleFirst;
			}
		}

		if (mjpeg_mode) {
			if (!(mjpeg_splits = new long[(size_t)(mSampleLast - mSampleFirst)]))
				throw MyMemoryError();

			for(int i=0; i<mSampleLast-mSampleFirst; i++)
				mjpeg_splits[i] = -1;
		}
	} else
		mjpeg_mode = 0;

	// Allocate framebuffer.
	//
	// Note that we have to accommodate V210 here, which has an average of 22 bits per pixel
	// but has a scanline alignment of 128 bytes (!).

  //! max decode is b64a / 64 bit

	uint32 rgbapitch = bmih->biWidth * 8;
	uint32 v210pitch = ((bmih->biWidth + 5) / 6 * 16 + 127) & ~127;

	if (!AllocFrameBuffer(std::max(rgbapitch, v210pitch) * abs((int)bmih->biHeight) + 4))
		throw MyMemoryError();

	uint32 fccHandlerSearch = streamInfo.fccHandler;

	if (!VDPreferencesIsUseVideoFccHandlerEnabled())
		fccHandlerSearch = 0;

	mpDecompressor = VDFindVideoDecompressorEx(fccHandlerSearch, bmih, format_len, use_internal);

	if (!mpDecompressor) {
		const char *s = LookupVideoCodec(bmih->biCompression);
		VDStringA fcc = print_fourcc(bmih->biCompression);

		throw MyError("Couldn't locate decompressor for format '%s' (%s)\n"
						"\n"
						"VirtualDub requires a Video for Windows (VFW) compatible codec to decompress "
						"video. DirectShow codecs, such as those used by Windows Media Player, are not "
						"suitable."
					,fcc.c_str()
					,s ? s : "unknown");
	}

	if (setTargetFormat(0)) {
		mDefaultFormat = getTargetFormat();
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////

void VideoSourceAVI::Reinit() {
	VDPosition nOldFrames, nNewFrames;

	nOldFrames = mSampleLast - mSampleFirst;
	nNewFrames = pAVIStream->End() - pAVIStream->Start();

	if (mjpeg_mode==IFMODE_SPLIT1 || mjpeg_mode==IFMODE_SPLIT2) {
		nOldFrames >>= 1;
	}

	if (nOldFrames != nNewFrames && mjpeg_mode) {
		// We have to resize the mjpeg_splits array.

		if (nNewFrames > (size_t)-1)
			throw MyMemoryError();

		long *pNewSplits = new long[(size_t)nNewFrames];

		if (!pNewSplits)
			throw MyMemoryError();

		VDPosition i;

		memcpy(pNewSplits, mjpeg_splits, sizeof(long)*(size_t)nOldFrames);

		for(i=nOldFrames; i<nNewFrames; i++)
			pNewSplits[i] = -1;

		delete[] mjpeg_splits;

		mjpeg_splits = pNewSplits;
	}

	if (pAVIStream->Info(&streamInfo))
		throw MyError("Error obtaining video stream info.");

	streamInfo.fccType = 'sdiv';


	mSampleFirst = pAVIStream->Start();

	if (mjpeg_mode==IFMODE_SPLIT1 || mjpeg_mode==IFMODE_SPLIT2) {
		if (streamInfo.dwRate >= 0x7FFFFFFF)
			streamInfo.dwScale >>= 1;
		else
			streamInfo.dwRate *= 2;

		mSampleLast = pAVIStream->End() * 2 - mSampleFirst;
	} else
		mSampleLast = pAVIStream->End();

	streamInfo.dwLength		= (DWORD)std::min<VDPosition>(0xFFFFFFFF, mSampleLast - mSampleFirst);
}

void VideoSourceAVI::redoKeyFlags(vdfastvector<uint32>& newFlags) {
	VDPosition lSample;
	long lMaxFrame=0;
	uint32 lActualBytes, lActualSamples;
	int err;
	char* lpInputBuffer = nullptr;
	bool fStreamBegun = false;
	long *pFrameSums;

	uint32 maskWords = (uint32)((mSampleLast-mSampleFirst+31) >> 5);
	newFlags.resize(maskWords, 0);

	// Find maximum frame

	lSample = mSampleFirst;
	while(lSample < mSampleLast) {
		err = _read(lSample, 1, NULL, 0, &lActualBytes, &lActualSamples);
		if (err == IVDStreamSource::kOK) {
			if (lActualBytes > lMaxFrame)
				lMaxFrame = lActualBytes;
		}

		++lSample;
	}

	if (!setDecompressedFormat(24))
		if (!setDecompressedFormat(32))
			if (!setDecompressedFormat(16))
				if (!setDecompressedFormat(8))
					throw MyError("Video decompressor is incapable of decompressing to an RGB format.");

	lpInputBuffer = new(std::nothrow) char[((lMaxFrame + 7) & -8) + lMaxFrame];
	if (!lpInputBuffer) {
		throw MyMemoryError();
	}

	pFrameSums = new(std::nothrow) long[(size_t)(mSampleLast - mSampleFirst)];
	if (!pFrameSums) {
		delete[] lpInputBuffer;
		throw MyMemoryError();
	}

	try {
		ProgressDialog pd(NULL, "AVI Import Filter", "Rekeying video stream", (long)(mSampleLast - mSampleFirst), true);
		pd.setValueFormat("Frame %ld of %ld");

		streamBegin(true, false);
		fStreamBegun = true;

		lSample = mSampleFirst;
		while(lSample < mSampleLast) {
			long lWhiteTotal=0;
			long x, y;
			const long lWidth	= (mpTargetFormatHeader->biWidth * mpTargetFormatHeader->biBitCount + 7)/8;
			const long lModulo	= (4-lWidth)&3;
			const long lHeight	= abs(mpTargetFormatHeader->biHeight);
			unsigned char *ptr;

			_RPT1(0,"Rekeying frame %ld\n", lSample);

			err = _read(lSample, 1, lpInputBuffer, lMaxFrame, &lActualBytes, &lActualSamples);
			if (err != IVDStreamSource::kOK)
				goto rekey_error;


			streamGetFrame(lpInputBuffer, lActualBytes, false, lSample, lSample);

			ptr = (unsigned char *)mpFrameBuffer.get();
			y = lHeight;
			do {
				x = lWidth;
				do {
					lWhiteTotal += (long)*ptr++;
					lWhiteTotal ^= 0xAAAAAAAA;
				} while(--x);

				ptr += lModulo;
			} while(--y);


			pFrameSums[lSample - mSampleFirst] = lWhiteTotal;


//			if (lBlackTotal == lWhiteTotal)
//				key_flags[(lSample - mSampleFirst)>>3] |= 1<<((lSample-mSampleFirst)&7);

rekey_error:
			++lSample;

			pd.advance((long)((lSample - mSampleFirst) >> 1));
			pd.check();
		}

		lSample = mSampleFirst;
		do {
			long lWhiteTotal=0;
			long x, y;
			const long lWidth	= (mpTargetFormatHeader->biWidth * mpTargetFormatHeader->biBitCount + 7)/8;
			const long lModulo	= (4-lWidth)&3;
			const long lHeight	= abs(mpTargetFormatHeader->biHeight);
			unsigned char *ptr;

			err = _read(lSample, 1, lpInputBuffer, lMaxFrame, &lActualBytes, &lActualSamples);
			if (err != IVDStreamSource::kOK)
				goto rekey_error2;

			streamGetFrame(lpInputBuffer, lActualBytes, false, lSample, lSample);

			ptr = (unsigned char *)mpFrameBuffer.get();
			y = lHeight;
			do {
				x = lWidth;
				do {
					lWhiteTotal += (long)*ptr++;
					lWhiteTotal ^= 0xAAAAAAAA;
				} while(--x);

				ptr += lModulo;
			} while(--y);


			if (lWhiteTotal == pFrameSums[lSample - mSampleFirst])
				newFlags[(size_t)((lSample - mSampleFirst)>>5)] |= 1<<(((uint32)lSample - (uint32)mSampleFirst)&31);

rekey_error2:
			if (lSample == mSampleFirst)
				lSample = mSampleLast-1;
			else
				--lSample;

			pd.advance((long)(mSampleLast - ((lSample+mSampleFirst) >> 1)));
			pd.check();
		} while(lSample >= mSampleFirst+1);

		streamEnd();
	} catch(...) {
		if (fStreamBegun) streamEnd();
		delete[] lpInputBuffer;
		delete[] pFrameSums;
		throw;
	}

	delete[] lpInputBuffer;
	delete[] pFrameSums;
}

int VideoSourceAVI::_read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
	IAVIReadStream *pSource = pAVIStream;
	bool phase = (lStart - mSampleFirst)&1;

	if (mjpeg_mode == IFMODE_SPLIT1 || mjpeg_mode == IFMODE_SPLIT2)
		lStart = mSampleFirst + (lStart - mSampleFirst)/2;

	// MJPEG modification mode?

	if (mjpeg_mode) {
		int res = IVDStreamSource::kBufferTooSmall;
		LONG lBytes, lSamples;
		long lOffset, lLength;

		// Did we just read in this sample!?

		if (lStart == mjpeg_last) {
			lBytes = mjpeg_last_size;
			res = IVDStreamSource::kOK;
		} else {

			// Read the sample into memory.  If we don't have a lpBuffer *and* already know
			// where the split is, just get the size.

			if (lpBuffer || mjpeg_splits[lStart - mSampleFirst]<0) {

				mjpeg_last = -1;

				if (mjpeg_reorder_buffer_size)
					res = pSource->Read(lStart, 1, mjpeg_reorder_buffer, mjpeg_reorder_buffer_size, &lBytes, &lSamples);

				if (IVDStreamSource::kBufferTooSmall == res) {
					void *new_buffer;
					int new_size;

					res = pSource->Read(lStart, 1, NULL, 0, &lBytes, &lSamples);

					if (res == IVDStreamSource::kOK) {

						VDASSERT(lBytes != 0);

						new_size = (lBytes + 4095) & -4096;
//						new_size = lBytes;
						new_buffer = reallocmem(mjpeg_reorder_buffer, new_size);

						if (!new_buffer)
							throw MyMemoryError();

						mjpeg_reorder_buffer = new_buffer;
						mjpeg_reorder_buffer_size = new_size;

						res = pSource->Read(lStart, 1, mjpeg_reorder_buffer, mjpeg_reorder_buffer_size, &lBytes, &lSamples);
					}
				}

				if (res == IVDStreamSource::kOK) {
					mjpeg_last = lStart;
					mjpeg_last_size = lBytes;
				}
			} else
				res = pSource->Read(lStart, 1, NULL, 0, &lBytes, &lSamples);
		}


		if (res != IVDStreamSource::kOK) {
			if (lBytesRead)
				*lBytesRead = 0;
			if (lSamplesRead)
				*lSamplesRead = 0;
			return res;
		} else if (!lBytes) {
			if (lBytesRead)
				*lBytesRead = 0;
			if (lSamplesRead)
				*lSamplesRead = 1;
			return IVDStreamSource::kOK;
		}

		// Attempt to find SOI tag in sample

		lOffset = 0;
		lLength = lBytes;

		{
			int i;

			// Do we already know where the split is?

			if (mjpeg_splits[lStart - mSampleFirst]<0) {
				for(i=2; i<lBytes-2; i++)
					if (((unsigned char *)mjpeg_reorder_buffer)[i] == (unsigned char)0xFF
							&& ((unsigned char *)mjpeg_reorder_buffer)[i+1] == (unsigned char)0xD8)
						break;

				mjpeg_splits[lStart - mSampleFirst] = i;
			} else {
				i = mjpeg_splits[lStart - mSampleFirst];
			}

			if (i<lBytes-2) {
				if (mjpeg_mode != IFMODE_SWAP) {
					switch(mjpeg_mode) {
					case IFMODE_SPLIT2:
						phase = !phase;
						break;
					case IFMODE_DISCARD1:
						phase = false;
						break;
					case IFMODE_DISCARD2:
						phase = true;
						break;
					}

					if (phase) {
						lOffset = i;
						lLength = lBytes - i;
					} else {
						lOffset = 0;
						lLength = i;
					}
				} else
					lOffset = i;
			}
		}

		if (lpBuffer) {
			if (lSamplesRead)
				*lSamplesRead = 1;
			if (lBytesRead)
				*lBytesRead = lLength;

			if (cbBuffer < lLength)
				return IVDStreamSource::kBufferTooSmall;

			if (mjpeg_mode == IFMODE_SWAP) {
				char *pp1 = (char *)lpBuffer;
				char *pp2 = (char *)lpBuffer + (lBytes - lOffset);

				memcpy(pp1, (char *)mjpeg_reorder_buffer+lOffset, lBytes - lOffset);
				if (lOffset)
					memcpy(pp2, mjpeg_reorder_buffer, lOffset);

				// patch phase on both MJPEG headers

				if (((short *)pp1)[1]==(short)0xE0FF)
					pp1[10] = 1;

				if (((short *)pp2)[1]==(short)0xE0FF)
					pp2[10] = 2;

			} else {

				memcpy(lpBuffer, (char *)mjpeg_reorder_buffer+lOffset, lLength);

				// patch phase on MJPEG header by looking for APP0 tag (xFFE0)

				// FFD8 FFE0 0010 'AVI1' polarity

				if (((short *)lpBuffer)[1]==(short)0xE0FF)
					((char *)lpBuffer)[10] = 0;
			}

			return IVDStreamSource::kOK;
		} else {
			if (lSamplesRead)
				*lSamplesRead = 1;
			if (lBytesRead)
				*lBytesRead = lLength;

			return IVDStreamSource::kOK;
		}

	} else {
		LONG samplesRead, bytesRead;

		int rv = pSource->Read(lStart, lCount, lpBuffer, cbBuffer, &bytesRead, &samplesRead);

		if (lSamplesRead)
			*lSamplesRead = samplesRead;
		if (lBytesRead)
			*lBytesRead = bytesRead;

		if (lpBuffer && bInvertFrames && !rv && samplesRead) {
			const VDAVIBitmapInfoHeader& hdr = *getImageFormat();
			const long dpr = ((hdr.biWidth * hdr.biBitCount + 31)>>5);

			if (bytesRead >= dpr * 4 * hdr.biHeight) {		// safety check
				const int h2 = hdr.biHeight >> 1;
				long *p0 = (long *)lpBuffer;
				long *p1 = (long *)lpBuffer + dpr * (hdr.biHeight - 1);

				for(int y=0; y<h2; ++y) {
					for(int x=0; x<dpr; ++x) {
						long t = p0[x];
						p0[x] = p1[x];
						p1[x] = t;
					}
					p0 += dpr;
					p1 -= dpr;
				}
			}
		}

		return rv;
	}
}

VDPosition VideoSourceAVI::getRealDisplayFrame(VDPosition display_num) {
	if (display_num >= mSampleLast) {
		display_num = mSampleLast - 1;
		if (display_num < mSampleFirst)
			display_num = mSampleFirst;
		return display_num;
	}

	while(display_num > mSampleFirst && getDropType(display_num) == kDroppable)
		--display_num;

	return display_num;
}

sint64 VideoSourceAVI::getSampleBytePosition(VDPosition pos) {
	IAVIReadStream *pSource = pAVIStream;

	if (mjpeg_mode == IFMODE_SPLIT1 || mjpeg_mode == IFMODE_SPLIT2)
		pos = mSampleFirst + (pos - mSampleFirst)/2;

	return pSource->getSampleBytePosition(pos);
}

bool VideoSourceAVI::_isKey(VDPosition samp) {
	if ((mjpeg_mode & -2) == IFMODE_SPLIT1)
		samp = mSampleFirst + (samp - mSampleFirst)/2;

	if (mpKeyFlags) {
		samp -= mSampleFirst;

		return !!(mpKeyFlags[samp>>5] & (1<<(samp&31)));
	} else
		return pAVIStream->IsKeyFrame(samp);
}

VDPosition VideoSourceAVI::nearestKey(VDPosition lSample) {
	if (mpKeyFlags) {
		if (lSample < mSampleFirst || lSample >= mSampleLast)
			return -1;

		if (_isKey(lSample)) return lSample;

		return prevKey(lSample);
	}

//	if (lNear == -1)
//		throw MyError("VideoSourceAVI: error getting previous key frame");

	if ((mjpeg_mode & -2) == IFMODE_SPLIT1) {
		return pAVIStream->NearestKeyFrame(mSampleFirst + (lSample-mSampleFirst)/2)*2-mSampleFirst;
	} else {
		return pAVIStream->NearestKeyFrame(lSample);
	}
}

VDPosition VideoSourceAVI::prevKey(VDPosition lSample) {
	if ((mjpeg_mode & -2) == IFMODE_SPLIT1) {
		lSample = mSampleFirst + (lSample - mSampleFirst)/2;

		if (mpKeyFlags) {
			if (lSample >= mSampleLast) return -1;

			while(--lSample >= mSampleFirst)
				if (_isKey(lSample)) return lSample*2-mSampleFirst;

			return -1;
		} else
			return pAVIStream->PrevKeyFrame(lSample)*2-mSampleFirst;
	} else {
		if (mpKeyFlags) {
			if (lSample >= mSampleLast) return -1;

			while(--lSample >= mSampleFirst)
				if (_isKey(lSample)) return lSample;

			return -1;
		} else
			return pAVIStream->PrevKeyFrame(lSample);
	}
}

VDPosition VideoSourceAVI::nextKey(VDPosition lSample) {
	if ((mjpeg_mode & -2) == IFMODE_SPLIT1) {
		lSample = mSampleFirst + (lSample - mSampleFirst)/2;

		if (mpKeyFlags) {
			if (lSample < mSampleFirst) return -1;

			while(++lSample < mSampleLast)
				if (_isKey(lSample)) return lSample*2 - mSampleFirst;

			return -1;
		} else
			return pAVIStream->NextKeyFrame(lSample)*2 - mSampleFirst;

	} else {

		if (mpKeyFlags) {
			if (lSample < mSampleFirst) return -1;

			while(++lSample < mSampleLast)
				if (_isKey(lSample)) return lSample;

			return -1;
		} else
			return pAVIStream->NextKeyFrame(lSample);

	}
}

bool VideoSourceAVI::setTargetFormat(VDPixmapFormatEx format) {
	using namespace nsVDPixmap;

	streamEnd();

	bDirectDecompress = false;

	if (format && format == mSourceLayout.format) {
		if (VideoSource::setTargetFormatVariant(format, mSourceVariant)) {
			if (format == kPixFormat_Pal8)
				mTargetFormat.palette = mPalette;
			bDirectDecompress = true;
			return true;
		}

		return false;
	}

	// attempt direct decompression
	if (format) {
		vdstructex<VDAVIBitmapInfoHeader> desiredFormat;
		vdstructex<VDAVIBitmapInfoHeader> srcFormat(getImageFormat(), getFormatLen());

		const int variants = VDGetPixmapToBitmapVariants(format);

		for(int variant=1; variant<=variants; ++variant) {
			if (VDMakeBitmapFormatFromPixmapFormat(desiredFormat, srcFormat, format, variant)) {
				if (srcFormat->biCompression == desiredFormat->biCompression
					&& srcFormat->biCompression != VDAVIBitmapInfoHeader::kCompressionBitfields
					&& srcFormat->biBitCount == desiredFormat->biBitCount
					&& srcFormat->biSizeImage == desiredFormat->biSizeImage
					&& srcFormat->biWidth == desiredFormat->biWidth
					&& srcFormat->biHeight == desiredFormat->biHeight
					&& srcFormat->biPlanes == desiredFormat->biPlanes) {

					mpTargetFormatHeader = srcFormat;

					VDVERIFY(VideoSource::setTargetFormatVariant(format, variant));

					if (format == kPixFormat_Pal8)
						mTargetFormat.palette = mPalette;

					bDirectDecompress = true;

					invalidateFrameBuffer();
					return true;
				}
			}
		}

		// no variants were an exact match
	}

	if (mpDecompressor->SetTargetFormat(format)) {
		VDPixmapFormatEx format2 = mpDecompressor->GetTargetFormat();
		format2 = VDPixmapFormatCombineOpt(format2, format);
		int variant2 = mpDecompressor->GetTargetFormatVariant();
		VDVERIFY(VideoSource::setTargetFormatVariant(format2, variant2));
		return true;
	}
	return false;
}

bool VideoSourceAVI::setDecompressedFormat(const VDAVIBitmapInfoHeader *pbih) {
	streamEnd();

	if (pbih->biCompression == VDAVIBitmapInfoHeader::kCompressionRGB && pbih->biBitCount > 8)
		return setDecompressedFormat(pbih->biBitCount);

	if (pbih->biCompression == getImageFormat()->biCompression) {
		const VDAVIBitmapInfoHeader *pbihSrc = getImageFormat();
		if (pbih->biBitCount == pbihSrc->biBitCount
			&& pbih->biSizeImage == pbihSrc->biSizeImage
			&& pbih->biWidth == pbihSrc->biWidth
			&& pbih->biHeight == pbihSrc->biHeight
			&& pbih->biPlanes == pbihSrc->biPlanes) {

			mpTargetFormatHeader.assign(pbih, sizeof(VDAVIBitmapInfoHeader));

			mTargetFormat.format = VDBitmapFormatToPixmapFormat(*mpTargetFormatHeader);
			mTargetFormat.palette = mPalette;

			bDirectDecompress = true;

			invalidateFrameBuffer();
			return true;
		}
	}

	mTargetFormat.format = 0;

	if (mpDecompressor->SetTargetFormat(pbih)) {
		int variant;
		int format = VDBitmapFormatToPixmapFormat(*pbih,variant);
		if (format)	VDVERIFY(VideoSource::setTargetFormatVariant(format, variant));
		mpTargetFormatHeader.assign(pbih, sizeof(VDAVIBitmapInfoHeader));

		invalidateFrameBuffer();
		bDirectDecompress = false;
		return true;
	}

	return false;
}

////////////////////////////////////////////////

void VideoSourceAVI::invalidateFrameBuffer() {
	if (lLastFrame != -1)
		mpDecompressor->Stop();

	lLastFrame = -1;
	mbConcealingErrors = false;
}

bool VideoSourceAVI::isFrameBufferValid() {
	return lLastFrame != -1;
}

void VideoSourceAVI::streamFillDecodePadding(void *inputBuffer, uint32 data_len) {
	if (data_len)
		memset((char *)inputBuffer + data_len, 0xA5, streamGetDecodePadding());
}

char VideoSourceAVI::getFrameTypeChar(VDPosition lFrameNum) {
	if (lFrameNum<mSampleFirst || lFrameNum >= mSampleLast)
		return ' ';

	if (_isKey(lFrameNum))
		return 'K';

	uint32 lBytes, lSamples;
	int err = _read(lFrameNum, 1, NULL, 0, &lBytes, &lSamples);

	if (err != IVDStreamSource::kOK)
		return ' ';

	return lBytes ? ' ' : 'D';
}

VideoSource::eDropType VideoSourceAVI::getDropType(VDPosition lFrameNum) {
	if (lFrameNum<mSampleFirst || lFrameNum >= mSampleLast)
		return kDroppable;

	if (_isKey(lFrameNum))
		return kIndependent;

	uint32 lBytes, lSamples;
	int err = _read(lFrameNum, 1, NULL, 0, &lBytes, &lSamples);

	if (err != IVDStreamSource::kOK)
		return kDependant;

	return lBytes ? kDependant : kDroppable;
}

bool VideoSourceAVI::isDecodable(VDPosition sample_num) {
	if (sample_num<mSampleFirst || sample_num >= mSampleLast)
		return false;

	return (isKey(sample_num) || sample_num == lLastFrame+1);
}

bool VideoSourceAVI::isStreaming() {
	return pAVIStream->isStreaming();
}

bool VideoSourceAVI::isKeyframeOnly() {
   return pAVIStream->isKeyframeOnly();
}

bool VideoSourceAVI::isType1() {
   return bIsType1;
}

void VideoSourceAVI::streamBegin(bool fRealTime, bool bForceReset) {
	if (bForceReset)
		stream_current_frame	= -1;

	if (mbDecodeStarted && fRealTime == mbDecodeRealTime)
		return;

	stream_current_frame	= -1;

	pAVIStream->BeginStreaming(mSampleFirst, mSampleLast, fRealTime ? 1000 : 2000);

	if (!bDirectDecompress)
		mpDecompressor->Start();

	mbDecodeStarted = true;
	mbDecodeRealTime = fRealTime;
}

const void *VideoSourceAVI::streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num, VDPosition target_sample) {
	if (isKey(frame_num)) {
		if (mbConcealingErrors) {
			const unsigned frame = (unsigned)frame_num;
			VDLogAppMessage(kVDLogWarning, kVDST_VideoSource, kVDM_ResumeFromConceal, 1, &frame);
		}
		mbConcealingErrors = false;
	}

	if (!data_len || mbConcealingErrors) return getFrameBuffer();

	if (bDirectDecompress) {
		// avoid passing runt uncompressed frames
		uint32 to_copy = data_len;
		if (mSourceLayout.format) {
			if (data_len < mSourceFrameSize) {
				if (mErrorMode != kErrorModeReportAll) {
					const unsigned actual = data_len;
					const unsigned expected = mSourceFrameSize;
					const unsigned frame = (unsigned)frame_num;
					VDLogAppMessage(kVDLogWarning, kVDST_VideoSource, kVDM_FrameTooShort, 3, &frame, &actual, &expected);
					to_copy = data_len;
				} else
					throw MyError("VideoSourceAVI: uncompressed frame %I64u is short (expected %d bytes, got %d)", frame_num, mSourceFrameSize, data_len);
			}
		}

		memcpy((void *)getFrameBuffer(), inputBuffer, to_copy);
	} else {
		// Asus ASV1 crashes with zero byte frames!!!

		if (data_len) {
			try {
				vdprotected2("using output buffer at "VDPROT_PTR"-"VDPROT_PTR, void *, mpFrameBuffer.get(), void*, (char*)mpFrameBuffer.get() + mFrameBufferSize - 1) {
					vdprotected2("using input buffer at "VDPROT_PTR"-"VDPROT_PTR, const void *, inputBuffer, const void *, (const char *)inputBuffer + data_len - 1) {
						vdprotected1("decompressing video frame %lu", unsigned long, (unsigned long)frame_num) {
							mpDecompressor->DecompressFrame(mpFrameBuffer.get(), inputBuffer, data_len, _isKey(frame_num), is_preroll);
						}
					}
				}
			} catch(const MyError& e) {
				if (mErrorMode == kErrorModeReportAll)
					throw MyError("Error decompressing video frame %u:\n\n%s", (unsigned)frame_num, e.gets());
				else {
					const unsigned frame = (unsigned)frame_num;
					VDLogAppMessage(kVDLogWarning, kVDST_VideoSource, kVDM_DecodingError, 1, &frame);

					if (mErrorMode == kErrorModeConceal)
						mbConcealingErrors = true;
				}
			}
		}
	}

	lLastFrame = frame_num;
	mTargetFormat.info.frame_num = frame_num;
	if(mpDecompressor->GetAlpha()) mTargetFormat.info.alpha_type = FilterModPixmapInfo::kAlphaMask;
	VDSetPixmapInfoForBitmap(mTargetFormat.info, mTargetFormat.format, mTargetFormatVariant);

	return getFrameBuffer();
}

void VideoSourceAVI::streamEnd() {

	if (!mbDecodeStarted)
		return;

	mbDecodeStarted = false;

	// If an error occurs, but no one is there to hear it, was
	// there ever really an error?

	if (!bDirectDecompress)
		mpDecompressor->Stop();

	pAVIStream->EndStreaming();

}

const void *VideoSourceAVI::getFrame(VDPosition lFrameDesired) {
	VDPosition lFrameKey, lFrameNum;
	int aviErr;

	// illegal frame number?

	if (lFrameDesired < mSampleFirst || lFrameDesired >= mSampleLast)
		throw MyError("VideoSourceAVI: bad frame # (%d not within [%u, %u])", lFrameDesired, (unsigned)mSampleFirst, (unsigned)(mSampleLast-1));

	// do we already have this frame?

	if (lLastFrame == lFrameDesired)
		return getFrameBuffer();

	// back us off to the last key frame if we need to

	lFrameNum = lFrameKey = nearestKey(lFrameDesired);

	if (lLastFrame > lFrameKey && lLastFrame < lFrameDesired)
		lFrameNum = lLastFrame+1;

	mpDecompressor->Start();

	lLastFrame = -1;		// In case we encounter an exception.
	stream_current_frame	= -1;	// invalidate streaming frame

	vdblock<char>	dataBuffer;
	uint32 decodePadding = streamGetDecodePadding();
	do {
		uint32 lBytesRead, lSamplesRead;

		for(;;) {
			if (dataBuffer.size() <= decodePadding)
				aviErr = IVDStreamSource::kBufferTooSmall;
			else
				aviErr = read(lFrameNum, 1, dataBuffer.data(), dataBuffer.size() - decodePadding, &lBytesRead, &lSamplesRead);

			if (aviErr == IVDStreamSource::kBufferTooSmall) {
				aviErr = read(lFrameNum, 1, NULL, 0, &lBytesRead, &lSamplesRead);

				if (aviErr)
					throw MyAVIError("VideoSourceAVI", aviErr);

				uint32 newSize = (lBytesRead + decodePadding + 65535) & -65535;
				if (!newSize)
					++newSize;

				dataBuffer.resize(newSize);
			} else if (aviErr) {
				throw MyAVIError("VideoSourceAVI", aviErr);
			} else {
				streamFillDecodePadding(dataBuffer.data(), lBytesRead);
				break;
			}
		};

		if (!lBytesRead)
			continue;

		streamGetFrame(dataBuffer.data(), lBytesRead, false, lFrameNum, lFrameNum);

	} while(++lFrameNum <= lFrameDesired);

	lLastFrame = lFrameDesired;

	return getFrameBuffer();
}

void VideoSourceAVI::setDecodeErrorMode(ErrorMode mode) {
	mErrorMode = mode;
}

bool VideoSourceAVI::isDecodeErrorModeSupported(ErrorMode mode) {
	return true;
}

///////////////////////////////////////////////////////////////////////////

class VDVideoCodecBugTrap : public IVDVideoCodecBugTrap {
public:
	void OnCodecRenamingDetected(const wchar_t *pName) {
		static bool sbBadCodecDetected = false;

		if (!sbBadCodecDetected) {
			VDLogAppMessage(kVDLogWarning, kVDST_VideoSource, kVDM_CodecRenamingDetected, 1, &pName);
			sbBadCodecDetected = true;
		}
	}

	void OnAcceptedBS(const wchar_t *pName) {
		static bool sbBSReported = false;	// Only report once per session.

		if (!sbBSReported) {
			VDLogAppMessage(kVDLogWarning, kVDST_VideoSource, kVDM_CodecAcceptsBS, 1, &pName);
			sbBSReported = true;
		}
	}

	void OnCodecModifiedInput(const wchar_t *pName) {
		static bool sbReported = false;	// Only report once per session.

		if (!sbReported) {
			VDLogAppMessage(kVDLogWarning, kVDST_VideoSequenceCompressor, kVDM_CodecModifiesInput, 1, &pName);
			sbReported = true;
		}
	}

} g_videoCodecBugTrap;

void VDInitVideoCodecBugTrap() {
	VDSetVideoCodecBugTrap(&g_videoCodecBugTrap);
}
