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
#include <vd2/system/profile.h>
#include <vd2/Kasumi/region.h>
#include <vd2/Kasumi/text.h>
#include <vd2/Kasumi/triblt.h>
#include <vd2/Kasumi/pixel.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/blitter.h>

#include "VideoSource.h"
#include "InputFile.h"
#include "InputFileTestPhysics.h"
#include "gui.h"

extern const char g_szError[];

enum {
	kTest_cube = 0,
	kTest_cube_tff = 1,
	kTest_cube_bff = 2,
	kTest_sub_offset = 3,
	kTest_levels_rgba = 4,
	kTest_checker = 5,
	kTest_zone = 6,
	kTest_32_tff = 7,
	kTest_32_bff = 8,
	kTest_levels_lr = 9,
	kTest_levels_fr = 10,
	kTest_gradients = 11,
};

const char *const kModeNames[]={
	"RGB color cube",
	"RGB color cube (TFF)",
	"RGB color cube (BFF)",
	"Chroma subsampling offset",
	"Channel levels (RGBA)",
	"Checkerboard",
	"Zone plates",
	"3:2 pulldown (TFF)",
	"3:2 pulldown (BFF)",
	"Channel levels (YCbCr LR)",
	"Channel levels (YCbCr FR)",
	"Gradients",
};

class VDVideoSourceTest : public VideoSource {
public:
	VDVideoSourceTest(int mode, int scale);

	int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) override;
	bool _isKey(VDPosition samp) override { return true; }
	VDPosition nearestKey(VDPosition lSample) override	{ return lSample; }
	VDPosition prevKey(VDPosition lSample) override		{ return lSample>0 ? lSample-1 : -1; }
	VDPosition nextKey(VDPosition lSample) override		{ return lSample<mSampleLast ? lSample+1 : -1; }

	bool setTargetFormat(VDPixmapFormatEx format) override;

	void invalidateFrameBuffer() override	{ mCachedFrame = -1; }
	bool isFrameBufferValid() override		{ return mCachedFrame >= 0; }
	bool isStreaming() override				{ return false; }

	const void *streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition sample_num, VDPosition target_num) override;

	const void *getFrame(VDPosition frameNum) override;

	char getFrameTypeChar(VDPosition lFrameNum) override { return 'K'; }
	eDropType getDropType(VDPosition lFrameNum) override { return kIndependent; }
	bool isKeyframeOnly() override			{ return true; }
	bool isType1() override					{ return false; }
	bool isDecodable(VDPosition sample_num) override	{ return true; }

private:
	void DrawRotatingCubeFrame(VDPixmap& dst, bool interlaced, bool oddField, int frameIdx, bool isyuv, int variant=0);
	void DrawPhysFrame(VDPixmap& dst, bool oddField, int frame);

	VDPosition	mCachedFrame;
	const int	mMode;
	const int mScale;

	bool		mbUseTempBuffer;
	VDPixmapBuffer	mTempBuffer;
	VDPixmapBuffer	mRGB32Buffer;
	VDTestVidPhysVideo	mVideo;

	VDPixmapPathRasterizer	mTextRasterizer;
	VDPixmapRegion			mTextOutlineBrush;
	VDPixmapRegion			mTextRegion;
	VDPixmapRegion			mTextBorderRegion;
	VDPixmapRegion			mTempRegion;

	struct FormatInfo {
		int mFormat;
		bool mbIsYUV;
		const char *mpName;
	};

	const FormatInfo *mpFormatInfo;
	VDPixmapCachedBlitter mCachedBlitter;

	static const struct FormatInfo kFormatInfo[];
};

const struct VDVideoSourceTest::FormatInfo VDVideoSourceTest::kFormatInfo[]={
	{	nsVDPixmap::kPixFormat_XRGB1555,				false, "XRGB1555"	},
	{	nsVDPixmap::kPixFormat_RGB565,					false, "RGB565"	},
	{	nsVDPixmap::kPixFormat_RGB888,					false, "RGB24"	},
	{	nsVDPixmap::kPixFormat_XRGB8888,				false, "RGBA32"	},
	{	nsVDPixmap::kPixFormat_Y8,						false, "8-bit Y (16-235)"	},
	{	nsVDPixmap::kPixFormat_Y8_FR,					false, "8-bit Y (0-255)"	},
	{	nsVDPixmap::kPixFormat_Y16,						false, "16-bit Y"	},
	{	nsVDPixmap::kPixFormat_XRGB64,					false, "RGBA64"	},
	{	nsVDPixmap::kPixFormat_YUV422_UYVY,				true, "4:2:2 YCbCr (UYVY)"	},
	{	nsVDPixmap::kPixFormat_YUV422_YUYV,				true, "4:2:2 YCbCr (YUYV)"	},
	{	nsVDPixmap::kPixFormat_YUV444_Planar,			true, "4:4:4 YCbCr"	},
	{	nsVDPixmap::kPixFormat_YUV422_Planar,			true, "4:2:2 YCbCr"	},
	{	nsVDPixmap::kPixFormat_YUV420_Planar,			true, "4:2:0 YCbCr"	},
	{	nsVDPixmap::kPixFormat_YUV411_Planar,			true, "4:1:1 YCbCr"	},
	{	nsVDPixmap::kPixFormat_YUV410_Planar,			true, "4:1:0 YCbCr"	},
	{	nsVDPixmap::kPixFormat_YUV422_Planar_Centered,	true, "4:2:2 YCbCr (centered chroma)"	},
	{	nsVDPixmap::kPixFormat_YUV420_Planar_Centered,	true, "4:2:0 YCbCr (centered chroma)"	},
	{	nsVDPixmap::kPixFormat_YUV422_UYVY_709,			true, "4:2:2 YCbCr (UYVY, Rec. 709)"	},
	{	nsVDPixmap::kPixFormat_YUV420_NV12,				true, "4:2:0 YCbCr (NV12)"	},
	{	nsVDPixmap::kPixFormat_YUV444_V308,				true, "4:4:4 YCbCr (V308)"	},
	{	nsVDPixmap::kPixFormat_YUV422_YUYV_709,			true, "4:2:2 YCbCr (YUYV, Rec. 709)"	},
	{	nsVDPixmap::kPixFormat_YUV444_Planar_709,		true, "4:4:4 YCbCr (Rec. 709)"	},
	{	nsVDPixmap::kPixFormat_YUV422_Planar_709,		true, "4:2:2 YCbCr (Rec. 709)"	},
	{	nsVDPixmap::kPixFormat_YUV420_Planar_709,		true, "4:2:0 YCbCr (Rec. 709)"	},
	{	nsVDPixmap::kPixFormat_YUV411_Planar_709,		true, "4:1:1 YCbCr (Rec. 709)"	},
	{	nsVDPixmap::kPixFormat_YUV410_Planar_709,		true, "4:1:0 YCbCr (Rec. 709)"	},
	{	nsVDPixmap::kPixFormat_YUV422_UYVY_FR,			true, "4:2:2 YCbCr (UYVY, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV422_YUYV_FR,			true, "4:2:2 YCbCr (YUYV, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV444_Planar_FR,		true, "4:4:4 YCbCr (0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV422_Planar_FR,		true, "4:2:2 YCbCr (0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV420_Planar_FR,		true, "4:2:0 YCbCr (0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV411_Planar_FR,		true, "4:1:1 YCbCr (0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV410_Planar_FR,		true, "4:1:0 YCbCr (0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR,		true, "4:2:2 YCbCr (UYVY, Rec. 709, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR,		true, "4:2:2 YCbCr (YUYV, Rec. 709, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV444_Planar_709_FR,	true, "4:4:4 YCbCr (Rec. 709, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV422_Planar_709_FR,	true, "4:2:2 YCbCr (Rec. 709, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV420_Planar_709_FR,	true, "4:2:0 YCbCr (Rec. 709, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV411_Planar_709_FR,	true, "4:1:1 YCbCr (Rec. 709, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV410_Planar_709_FR,	true, "4:1:0 YCbCr (Rec. 709, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV420i_Planar,			true, "4:2:0 YCbCr (interlaced)"	},
	{	nsVDPixmap::kPixFormat_YUV420i_Planar_FR,		true, "4:2:0 YCbCr (interlaced, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV420i_Planar_709,		true, "4:2:0 YCbCr (interlaced, Rec. 709)"	},
	{	nsVDPixmap::kPixFormat_YUV420i_Planar_709_FR,	true, "4:2:0 YCbCr (interlaced, Rec. 709, 0-255)"	},
	{	nsVDPixmap::kPixFormat_YUV444_Planar16,			true, "16-bit 4:4:4 YCbCr"	},
	{	nsVDPixmap::kPixFormat_YUV444_Alpha_Planar16,	true, "16-bit 4:4:4 YCbCr+A"	},
	{	nsVDPixmap::kPixFormat_YUV444_Alpha_Planar,		true, "4:4:4 YCbCr+A"	},
};

///////////////////////////////////////////////////////////////////////////

VDVideoSourceTest::VDVideoSourceTest(int mode, int scale)
	: mMode(mode)
	, mScale(scale)
	, mpFormatInfo(NULL)
{
	mSampleFirst = 0;
	mSampleLast = 1000;

	//mpTargetFormatHeader.resize(sizeof(BITMAPINFOHEADER));
	BITMAPINFOHEADER *pFormat = (BITMAPINFOHEADER *)allocFormat(sizeof(BITMAPINFOHEADER));

	int w = 640*mScale;
	int h = 480*mScale;

	AllocFrameBuffer(w * h * 8);

	pFormat->biSize				= sizeof(BITMAPINFOHEADER);
	pFormat->biWidth			= w;
	pFormat->biHeight			= h;
	pFormat->biPlanes			= 1;
	pFormat->biCompression		= 0xFFFFFFFFUL;
	pFormat->biSizeImage	= 0;
	pFormat->biBitCount			= 0;
	pFormat->biXPelsPerMeter	= 0;
	pFormat->biYPelsPerMeter	= 0;
	pFormat->biClrUsed			= 0;
	pFormat->biClrImportant		= 0;

	invalidateFrameBuffer();

	// Fill out streamInfo

	streamInfo.fccType					= VDAVIStreamInfo::kTypeVideo;
	streamInfo.fccHandler				= NULL;
	streamInfo.dwFlags					= 0;
	streamInfo.dwCaps					= 0;
	streamInfo.wPriority				= 0;
	streamInfo.wLanguage				= 0;
	streamInfo.dwScale					= 1001;
	streamInfo.dwRate					= 30000;
	streamInfo.dwStart					= 0;
	streamInfo.dwLength					= VDClampToUint32(mSampleLast);
	streamInfo.dwInitialFrames			= 0;
	streamInfo.dwSuggestedBufferSize	= 0;
	streamInfo.dwQuality				= (DWORD)-1;
	streamInfo.dwSampleSize				= 0;
	streamInfo.rcFrameLeft				= 0;
	streamInfo.rcFrameTop				= 0;
	streamInfo.rcFrameRight				= (uint16)getImageFormat()->biWidth;
	streamInfo.rcFrameBottom			= (uint16)getImageFormat()->biHeight;

	if (mMode == kTest_32_tff || mMode == kTest_32_bff) {
		VDTestVidPhysSimulator sim;

		for(int i=0; i<800; ++i) {
			sim.EncodeFrame(mVideo);
			sim.Step(1001.0f / 24000.0f);
		}

		mRGB32Buffer.init(w, h, nsVDPixmap::kPixFormat_XRGB8888);
		mRGB32Buffer.info.alpha_type = FilterModPixmapInfo::kAlphaMask;
	}

	VDPixmapCreateRoundRegion(mTextOutlineBrush, 16.0f * mScale);
}

int VDVideoSourceTest::_read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *plBytesRead, uint32 *plSamplesRead) {
	if (plBytesRead)
		*plBytesRead = 0;

	if (plSamplesRead)
		*plSamplesRead = 0;

	if (!lpBuffer) {
		if (plBytesRead)
			*plBytesRead = sizeof(VDPosition);

		if (plSamplesRead)
			*plSamplesRead = 1;

		return 0;
	}

	if (sizeof(VDPosition) > cbBuffer) {
		if (plBytesRead)
			*plBytesRead = sizeof(VDPosition);

		return IVDStreamSource::kBufferTooSmall;
	}

	*(VDPosition *)lpBuffer = lStart;

	if (plBytesRead)
		*plBytesRead = sizeof(VDPosition);

	if (plSamplesRead)
		*plSamplesRead = 1;

	return 0;
}

bool VDVideoSourceTest::setTargetFormat(VDPixmapFormatEx format) {
	mbUseTempBuffer = true;

	if (!format)
		format = nsVDPixmap::kPixFormat_XRGB8888;

	BITMAPINFOHEADER *pFormat = (BITMAPINFOHEADER *)getFormat();
	int w = pFormat->biWidth;
	int h = pFormat->biHeight;

	const FormatInfo *finfo = NULL;

	for (const auto& formatInfo : kFormatInfo) {
		if (formatInfo.mFormat == format) {
			finfo = &formatInfo;
			break;
		}
	}

	if (!finfo) {
		setTargetFormat(0);
		return false;
	}

	VDPixmapFormatEx f1 = VDPixmapFormatNormalize(format);
	bool Is709 = f1.colorSpaceMode==vd2::kColorSpaceMode_709;
	bool IsFullRange = f1.colorRangeMode==vd2::kColorRangeMode_Full;

	if (format==nsVDPixmap::kPixFormat_XRGB64) {
		switch (mMode) {
		case kTest_checker:
		case kTest_zone:
			return false;
		}
	}

	if (format==nsVDPixmap::kPixFormat_Y8 || format==nsVDPixmap::kPixFormat_Y8_FR || format==nsVDPixmap::kPixFormat_Y16) {
		switch (mMode) {
		case kTest_checker:
		case kTest_zone:
		case kTest_32_tff:
		case kTest_32_bff:
			return false;
		}
	}

	if (format==nsVDPixmap::kPixFormat_YUV444_Planar16 || format==nsVDPixmap::kPixFormat_YUV444_Alpha_Planar16) {
		switch (mMode) {
		case kTest_levels_rgba:
		case kTest_checker:
		case kTest_zone:
		case kTest_32_tff:
		case kTest_32_bff:
		case kTest_levels_lr:
		case kTest_levels_fr:
			return false;
		}
	}

	// Channel level tests always use the temp buffer.
	if (mMode == kTest_levels_rgba) {
		switch(format) {
		case nsVDPixmap::kPixFormat_XRGB8888:
		case nsVDPixmap::kPixFormat_XRGB64:
			mTempBuffer.init(w, h, format);
			break;
		default:
			mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_XRGB8888);
		}

		mbUseTempBuffer = true;
	} else if (mMode == kTest_levels_lr) {
		if (finfo->mbIsYUV && Is709)
			mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV444_Planar_709);
		else
			mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV444_Planar);

		mbUseTempBuffer = true;
	} else if (mMode == kTest_levels_fr) {
		if (finfo->mbIsYUV && Is709)
			mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV444_Planar_709_FR);
		else
			mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV444_Planar_FR);

		mbUseTempBuffer = true;
	} else {
		switch(format) {
		case nsVDPixmap::kPixFormat_XRGB8888:
		case nsVDPixmap::kPixFormat_XRGB64:
		case nsVDPixmap::kPixFormat_Y8:
		case nsVDPixmap::kPixFormat_Y8_FR:
		case nsVDPixmap::kPixFormat_Y16:
		case nsVDPixmap::kPixFormat_YUV444_Planar:
		case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar16:
		case nsVDPixmap::kPixFormat_YUV444_Alpha_Planar16:
		case nsVDPixmap::kPixFormat_YUV444_Alpha_Planar:
			mbUseTempBuffer = false;
			break;
		default:
			if (!finfo->mbIsYUV)
				mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_XRGB8888);
			else if (Is709) {
				if (IsFullRange)
					mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV444_Planar_709_FR);
				else
					mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV444_Planar_709);
			} else {
				if (IsFullRange)
					mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV444_Planar_FR);
				else
					mTempBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV444_Planar);
			}
			break;
		}
	}

	// Block 4:2:0 and 4:1:0 progressive formats for interlaced tests.
	switch(format) {
		case nsVDPixmap::kPixFormat_YUV420_Planar:
		case nsVDPixmap::kPixFormat_YUV410_Planar:
		case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709:
		case nsVDPixmap::kPixFormat_YUV410_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
			switch(mMode) {
				case kTest_cube_tff:
				case kTest_cube_bff:
				case kTest_32_tff:
				case kTest_32_bff:
					return false;
			}
			break;
	}

	if (!VideoSource::setTargetFormat(format))
		return false;

	invalidateFrameBuffer();
	mpFormatInfo = finfo;
	mCachedBlitter.Invalidate();
	return true;
}

///////////////////////////////////////////////////////////////////////////

namespace {
	void DrawZonePlateXRGB8888(VDPixmap& dst, int x0, int y0, int w, int h, uint32 multiplier) {
		uint32 *row = (uint32 *)((char *)dst.data + dst.pitch*y0) + x0;

		float iw = 1.0f / (float)w;
		float ih = 1.0f / (float)h;

		float tscale = nsVDMath::kfPi * 192;

		for(int y = 0; y < h; ++y) {
			float dy = (float)y * ih - 0.5f;
			float t2 = dy*dy;

			for(int x = 0; x < w; ++x) {
				float dx = (float)x * iw - 0.5f;
				float t = (t2 + dx*dx)*tscale;
				float v = cosf(t);

				row[x] = (uint32)VDClampedRoundFixedToUint8Fast(0.5f + 0.5f*v) * multiplier;
			}

			vdptrstep(row, dst.pitch);
		}
	}

	void DrawZonePlateA8(VDPixmap& dst, int plane, int x0, int y0, int w, int h, uint8 minval, uint8 maxval) {
		uint8 *row;
		ptrdiff_t pitch;

		switch(plane) {
		case 0:
		default:
			pitch = dst.pitch;
			row = (uint8 *)((char *)dst.data + dst.pitch*y0) + x0;
			break;
		case 1:
			pitch = dst.pitch2;
			row = (uint8 *)((char *)dst.data2 + dst.pitch2*y0) + x0;
			break;
		case 2:
			pitch = dst.pitch3;
			row = (uint8 *)((char *)dst.data3 + dst.pitch3*y0) + x0;
			break;
		}

		float bias = ((int)maxval + (int)minval) / 510.0f;
		float scale = ((int)maxval - (int)minval) / 510.0f;

		float iw = 1.0f / (float)w;
		float ih = 1.0f / (float)h;
		float tscale = nsVDMath::kfPi * 192;
		for(int y = 0; y < h; ++y) {
			float dy = (float)y * ih - 0.5f;
			float t2 = dy*dy;

			for(int x = 0; x < w; ++x) {
				float dx = (float)x * iw - 0.5f;
				float t = (t2 + dx*dx)*tscale;
				float v = cosf(t);

				row[x] = (uint8)VDClampedRoundFixedToUint8Fast(bias + scale*v);
			}

			vdptrstep(row, pitch);
		}
	}
}

const void *VDVideoSourceTest::streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num, VDPosition target_sample) {
	// We may get a zero-byte frame if we already have the image.
	if (!data_len)
		return getFrameBuffer();

	using namespace nsVDPixmap;

	// clear framebuffer
	uint32 textcolor = 0xFF98FFFF;
	uint32 black = 0xFF000000;
	bool isyuv = false;

	VDPixmap *dst = &mTargetFormat;
	const int dstw = dst->w;
	const int dsth = dst->h;

	if (mbUseTempBuffer)
		dst = &mTempBuffer;

	VDPixmapFormatEx f1 = VDPixmapFormatNormalize(*dst);

	switch(dst->format) {
	case nsVDPixmap::kPixFormat_XRGB8888:
		dst->info.alpha_type = FilterModPixmapInfo::kAlphaMask;
		VDMemset32Rect(dst->data, dst->pitch, 0x00404040, dstw, dsth);
		break;
	case nsVDPixmap::kPixFormat_XRGB64:
		dst->info.ref_r = 0xFFFF;
		dst->info.ref_g = 0xFFFF;
		dst->info.ref_b = 0xFFFF;
		dst->info.ref_a = 0xFFFF;
		dst->info.alpha_type = FilterModPixmapInfo::kAlphaMask;
		VDMemset64Rect(dst->data, dst->pitch, 0x0000404040404040, dstw, dsth);
		break;
	case nsVDPixmap::kPixFormat_Y8:
	case nsVDPixmap::kPixFormat_Y8_FR:
	case nsVDPixmap::kPixFormat_Y16:
	case nsVDPixmap::kPixFormat_YUV444_Planar16:
	case nsVDPixmap::kPixFormat_YUV444_Planar:
	case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV444_Planar_709:
	case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
		isyuv = true;
		break;
	case nsVDPixmap::kPixFormat_YUV444_Alpha_Planar16:
		isyuv = true;
		dst->info.ref_a = 0xFFFF;
		dst->info.alpha_type = FilterModPixmapInfo::kAlphaMask;
		VDMemset16Rect(dst->data4, dst->pitch4, 0, dstw, dsth);
		break;
	case nsVDPixmap::kPixFormat_YUV444_Alpha_Planar:
		isyuv = true;
		dst->info.alpha_type = FilterModPixmapInfo::kAlphaMask;
		VDMemset8Rect(dst->data4, dst->pitch4, 0, dstw, dsth);
		break;
	}

	if (isyuv) {
		int gray;
		if (f1.colorRangeMode==vd2::kColorRangeMode_Limited) {
			gray = 0x47;
			if (f1.colorSpaceMode==vd2::kColorSpaceMode_709) {
				textcolor = VDConvertRGBToYCbCr(textcolor, true, false);
				black = 0xFF801080;
			} else {
				textcolor = VDConvertRGBToYCbCr(textcolor, false, false);
				black = 0xFF801080;
			}
		} else {
			gray = 0x40;
			if (f1.colorSpaceMode==vd2::kColorSpaceMode_709) {
				textcolor = VDConvertRGBToYCbCr(textcolor, true, true);
				black = 0xFF800080;
			} else {
				textcolor = VDConvertRGBToYCbCr(textcolor, false, true);
				black = 0xFF800080;
			}
		}

		if (dst->format==nsVDPixmap::kPixFormat_Y8 || dst->format==nsVDPixmap::kPixFormat_Y8_FR) {
			VDMemset8Rect(dst->data, dst->pitch, uint8(gray), dstw, dsth);
		} else if (dst->format==nsVDPixmap::kPixFormat_Y16) {
			dst->info.ref_r = 0xFFFF;
			gray = (gray<<8) | gray;
			VDMemset16Rect(dst->data, dst->pitch, uint16(gray), dstw, dsth);
		} else if (dst->format==nsVDPixmap::kPixFormat_YUV444_Planar16 || dst->format==nsVDPixmap::kPixFormat_YUV444_Alpha_Planar16) {
			dst->info.ref_r = 0xFFFF;
			gray = (gray<<8) | gray;
			VDMemset16Rect(dst->data, dst->pitch, uint16(gray), dstw, dsth);
			VDMemset16Rect(dst->data2, dst->pitch2, 0x8000, dstw, dsth);
			VDMemset16Rect(dst->data3, dst->pitch3, 0x8000, dstw, dsth);
		} else {
			VDMemset8Rect(dst->data, dst->pitch, uint8(gray), dstw, dsth);
			VDMemset8Rect(dst->data2, dst->pitch2, 0x80, dstw, dsth);
			VDMemset8Rect(dst->data3, dst->pitch3, 0x80, dstw, dsth);
		}
	}

	vdfloat4x4 ortho;
	ortho.m[0].set(2.0f/640.0f, 0.0f, 0.0f, -1.0f);
	ortho.m[1].set(0.0f, 2.0f/480.0f, 0.0f, -1.0f);
	ortho.m[2].set(0.0f, 0.0f, 0.0f, 0.0f);
	ortho.m[3].set(0.0f, 0.0f, 0.0f, 1.0f);

	if (mMode == kTest_cube) {
		DrawRotatingCubeFrame(*dst, false, false, (int)frame_num, isyuv);
	} else if (mMode == kTest_gradients) {
		DrawRotatingCubeFrame(*dst, false, false, (int)frame_num, isyuv, kTest_gradients);
	} else if (mMode == kTest_cube_tff) {
		int frameBase = (int)frame_num << 1;
		DrawRotatingCubeFrame(*dst, true, false, frameBase + 0, isyuv);
		DrawRotatingCubeFrame(*dst, true, true,  frameBase + 1, isyuv);
	} else if (mMode == kTest_cube_bff) {
		int frameBase = (int)frame_num << 1;
		DrawRotatingCubeFrame(*dst, true, false, frameBase + 1, isyuv);
		DrawRotatingCubeFrame(*dst, true, true,  frameBase + 0, isyuv);
	} else if (mMode == kTest_sub_offset) {
		static const int kIndices[6]={0,2,1,3,4,5};
		VDTriColorVertex triv[6];

		triv[0].x = 320.0f;
		triv[0].y = 240.0f;
		triv[0].z = 0.0f;
		if (isyuv) {
			triv[0].r = 128.0f / 255.0f;
			triv[0].g = 235.0f / 255.0f;
			triv[0].b = 128.0f / 255.0f;
			triv[0].a = 1.0f;
		} else {
			triv[0].r = 1.0f;
			triv[0].g = 1.0f;
			triv[0].b = 1.0f;
			triv[0].a = 1.0f;
		}

		triv[1] = triv[0];
		triv[1].x = 380.0f;
		triv[1].y = 140.0f;

		triv[2] = triv[1];
		triv[2].x = 260.0f;

		triv[3].x = 320.0f;
		triv[3].y = 240.0f;
		triv[3].z = 0.0f;
		if (isyuv) {
			triv[3].r = 240.0f / 255.0f;
			triv[3].g = 128.0f / 255.0f;
			triv[3].b = 240.0f / 255.0f;
			triv[3].a = 1.0f;
		} else {
			triv[3].r = 1.0f;
			triv[3].g = 0.0f;
			triv[3].b = 1.0f;
			triv[3].a = 1.0f;
		}

		triv[4] = triv[3];
		triv[4].x = 380.0f;
		triv[4].y = 340.0f;

		triv[5] = triv[4];
		triv[5].x = 260.0f;

		VDPixmapTriFill(*dst, triv, 6, kIndices, 6, &ortho[0][0]);
	} else if (mMode == kTest_levels_rgba || mMode == kTest_levels_lr || mMode == kTest_levels_fr) {
		static const int kIndices[6]={0,1,2,2,1,3};
		VDTriColorVertex triv[4];

		static const char *const kNames[2][4]={
			{ "Red", "Green", "Blue", "Alpha" },
			{ "Cr", "Y", "Cb" },
		};

		int n = 3;
		float ch = 120.0f;
		int names = 1;
		if (mMode==kTest_levels_rgba) { n = 4; ch = 100.0f; names = 0; }

		for(int channel = 0; channel < n; ++channel) {
			for(int v=0; v<4; ++v) {
				triv[v].z = 0;
				triv[v].a = 1.0f;

				if (mMode == kTest_levels_lr || mMode == kTest_levels_fr) {
					triv[v].x = 90.0f + ((v&1) ? 512.0f : 0.0f);
					triv[v].y = 40.0f + ch * channel + ((v&2) ? 80.0f : 0.0f);
					triv[v].r = 128.0f / 255.0f;
					triv[v].g = 128.0f / 255.0f;
					triv[v].b = 128.0f / 255.0f;

					switch(channel) {
					case 0:
						triv[v].r = v&1 ? 1.0f : 0.0f;
						break;
					case 1:
						triv[v].g = v&1 ? 1.0f : 0.0f;
						triv[v].y = 40.0f + ch * channel + ((v&2) ? 80.0f : 60.0f);
						break;
					case 2:
						triv[v].b = v&1 ? 1.0f : 0.0f;
						break;
					}
				} else {
					triv[v].x = 90.0f + ((v&1) ? 528.0f : 0.0f);
					triv[v].y = 40.0f + ch * channel + ((v&2) ? 60.0f : 0.0f);
					float c0 = -4.0/255;
					float c1 = 259.0/255;
					triv[v].r = c0;
					triv[v].g = c0;
					triv[v].b = c0;

					switch(channel) {
					case 0:
						triv[v].r = v&1 ? c1 : c0;
						break;
					case 1:
						triv[v].g = v&1 ? c1 : c0;
						break;
					case 2:
						triv[v].b = v&1 ? c1 : c0;
						break;
					case 3:
						triv[v].r = v&1 ? c1 : c0;
						triv[v].g = v&1 ? c1 : c0;
						triv[v].b = v&1 ? c1 : c0;
						triv[v].a = v&1 ? c1 : c0;
						break;
					}
				}
			}

			VDPixmapTriFill(*dst, triv, 4, kIndices, 6, &ortho[0][0]);

			VDPixmapPathRasterizer rast;
			VDPixmapConvertTextToPath(rast, NULL, 24.0f * 64.0f * mScale, 10.0f * 64.0f * mScale, (60.0f + ch * channel) * 64.0f * mScale, kNames[names][channel]);

			VDPixmapRegion region;
			VDPixmapRegion border;
			VDPixmapRegion brush;
			rast.ScanConvert(region);

			VDPixmapCreateRoundRegion(brush, 16.0f * mScale);
			VDPixmapConvolveRegion(border, region, brush);

			VDPixmapFillPixmapAntialiased8x(*dst, border, 0, 0, black);
			VDPixmapFillPixmapAntialiased8x(*dst, region, 0, 0, textcolor);

			if ((mMode == kTest_levels_lr || mMode == kTest_levels_fr) && channel == 1) {
				// Draw double limited range ramp
				triv[0].y = triv[1].y = 160.0f;
				triv[2].y = triv[3].y = 180.0f;

				// draw 0-16 ramp
				triv[0].x = triv[2].x = 90.0f;
				triv[1].x = triv[3].x = 90.0f + (16.0f / 255.0f * 219.0f / 255.0f + 16.0f / 255.0f) * 512.0f;
				triv[0].g = triv[2].g = 16.0f / 255.0f * 219.0f / 255.0f + 16.0f / 255.0f;
				triv[1].g = triv[3].g = triv[0].g;
				VDPixmapTriFill(*dst, triv, 4, kIndices, 6, &ortho[0][0]);

				// draw 16-235 ramp
				triv[0].x = triv[1].x;
				triv[2].x = triv[3].x;
				triv[1].x = triv[3].x = 90.0f + (235.0f / 255.0f * 219.0f / 255.0f + 16.0f / 255.0f) * 512.0f;
				triv[1].g = triv[3].g = 235.0f / 255.0f * 219.0f / 255.0f + 16.0f / 255.0f;
				VDPixmapTriFill(*dst, triv, 4, kIndices, 6, &ortho[0][0]);

				// draw 235-255 clamp
				triv[0].x = triv[1].x;
				triv[2].x = triv[3].x;
				triv[1].x = triv[3].x = 90.0f + 512.0f;
				triv[0].g = triv[2].g = triv[1].g;
				VDPixmapTriFill(*dst, triv, 4, kIndices, 6, &ortho[0][0]);

				// draw 16-235 clamped ramp
				triv[0].y = triv[1].y = 180.0f;
				triv[2].y = triv[3].y = 220.0f;

				// draw 0-16 clamp
				triv[0].x = triv[2].x = 90.0f;
				triv[1].x = triv[3].x = 90.0f + (16.0f / 255.0f) * 512.0f;
				triv[0].g = 16.0f / 255.0f;
				triv[1].g = 16.0f / 255.0f;
				triv[2].g = 16.0f / 255.0f;
				triv[3].g = 16.0f / 255.0f;
				VDPixmapTriFill(*dst, triv, 4, kIndices, 6, &ortho[0][0]);

				// draw 16-235 ramp
				triv[0].x = triv[1].x;
				triv[2].x = triv[3].x;
				triv[1].x = 90.0f + (235.0f / 255.0f) * 512.0f;
				triv[3].x = triv[1].x;
				triv[1].g = 235.0f / 255.0f;
				triv[3].g = 235.0f / 255.0f;
				VDPixmapTriFill(*dst, triv, 4, kIndices, 6, &ortho[0][0]);

				// draw 235-255 clamp
				triv[0].x = triv[1].x;
				triv[2].x = triv[3].x;
				triv[1].x = 90.0f + 512.0f;
				triv[3].x = triv[1].x;
				triv[0].g = 235.0f / 255.0f;
				triv[2].g = 235.0f / 255.0f;
				VDPixmapTriFill(*dst, triv, 4, kIndices, 6, &ortho[0][0]);

				static const char *const kSubNames[]={
					"30-217",
					"16-235",
					"0-255"
				};

				for(int subChannel = 0; subChannel < 3; ++subChannel) {
					VDPixmapConvertTextToPath(rast, NULL, 14.0f * 64.0f * mScale, 30.0f * 64.0f * mScale, (170.0f + 100.0f / 3.0f * subChannel) * 64.0f * mScale, kSubNames[subChannel]);

					rast.ScanConvert(region);

					VDPixmapCreateRoundRegion(brush, 16.0f * mScale);
					VDPixmapConvolveRegion(border, region, brush);

					VDPixmapFillPixmapAntialiased8x(*dst, border, 0, 0, black);
					VDPixmapFillPixmapAntialiased8x(*dst, region, 0, 0, textcolor);
				}
			}
		}
	} else if (mMode == kTest_checker) {
		uint8 *dstrow = (uint8 *)dst->data;
		for(int y=0; y<dst->h; ++y) {
			uint8 *dstp = dstrow;

			switch(dst->format) {
			case nsVDPixmap::kPixFormat_XRGB8888:
				for(int x=0; x<dst->w; ++x) {
					((uint32 *)dstp)[x] = (x^y)&1 ? 0xFFFFFFFF : 0;
				}
				break;
			case nsVDPixmap::kPixFormat_YUV444_Planar:
			case nsVDPixmap::kPixFormat_YUV444_Planar_709:
				VDMemset32(dstrow, y&1 ? 0x10EB10EB : 0xEB10EB10, 160);
				break;
			case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
				VDMemset32(dstrow, y&1 ? 0x00FF00FF : 0xFF00FF00, 160);
				break;
			}

			vdptrstep(dstrow, dst->pitch);
		}
	} else if (mMode == kTest_zone) {
		switch(dst->format) {
		case nsVDPixmap::kPixFormat_XRGB8888:
			DrawZonePlateXRGB8888(*dst, 112*mScale,  16*mScale, 192*mScale, 192*mScale, 0x01010000);
			DrawZonePlateXRGB8888(*dst, 336*mScale,  16*mScale, 192*mScale, 192*mScale, 0x01000100);
			DrawZonePlateXRGB8888(*dst, 112*mScale, 224*mScale, 192*mScale, 192*mScale, 0x01000001);
			DrawZonePlateXRGB8888(*dst, 336*mScale, 224*mScale, 192*mScale, 192*mScale, 0x01010101);
			break;
		case nsVDPixmap::kPixFormat_YUV444_Planar:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709:
			DrawZonePlateA8(*dst, 0, 224*mScale,  16*mScale, 192*mScale, 192*mScale, 16, 235);
			DrawZonePlateA8(*dst, 1, 112*mScale, 224*mScale, 192*mScale, 192*mScale, 16, 240);
			DrawZonePlateA8(*dst, 2, 336*mScale, 224*mScale, 192*mScale, 192*mScale, 16, 240);
			break;
		case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
			DrawZonePlateA8(*dst, 0, 224*mScale,  16*mScale, 192*mScale, 192*mScale, 0, 255);
			DrawZonePlateA8(*dst, 1, 112*mScale, 224*mScale, 192*mScale, 192*mScale, 0, 255);
			DrawZonePlateA8(*dst, 2, 336*mScale, 224*mScale, 192*mScale, 192*mScale, 0, 255);
			break;
		}
	} else if (mMode == kTest_32_tff) {
		// A1 A1 B1 C1 D1
		// A2 B2 C2 C2 D2
		int frame32 = (int)frame_num;
		int frameBase = frame32 / 5 * 4;

		VDPixmap pxfield0(VDPixmapExtractField(mRGB32Buffer, false));
		VDPixmap pxfield1(VDPixmapExtractField(mRGB32Buffer, true));

		switch(frame32 % 5) {
			case 0:
				DrawPhysFrame(pxfield0, false, frameBase+0);
				DrawPhysFrame(pxfield1, true, frameBase+0);
				break;
			case 1:
				DrawPhysFrame(pxfield0, false, frameBase+0);
				DrawPhysFrame(pxfield1, true, frameBase+1);
				break;
			case 2:
				DrawPhysFrame(pxfield0, false, frameBase+1);
				DrawPhysFrame(pxfield1, true, frameBase+2);
				break;
			case 3:
				DrawPhysFrame(pxfield0, false, frameBase+2);
				DrawPhysFrame(pxfield1, true, frameBase+2);
				break;
			case 4:
				DrawPhysFrame(pxfield0, false, frameBase+3);
				DrawPhysFrame(pxfield1, true, frameBase+3);
				break;
		}

		VDPixmapBlt(*dst, mRGB32Buffer);
	} else if (mMode == kTest_32_bff) {
		// A1 B1 C1 C1 D1
		// A2 A2 B2 C2 D2
		int frame32 = (int)frame_num;
		int frameBase = frame32 / 5 * 4;

		VDPixmap pxfield0(VDPixmapExtractField(mRGB32Buffer, false));
		VDPixmap pxfield1(VDPixmapExtractField(mRGB32Buffer, true));

		switch(frame32 % 5) {
			case 0:
				DrawPhysFrame(pxfield0, false, frameBase+0);
				DrawPhysFrame(pxfield1, true, frameBase+0);
				break;
			case 1:
				DrawPhysFrame(pxfield0, false, frameBase+1);
				DrawPhysFrame(pxfield1, true, frameBase+0);
				break;
			case 2:
				DrawPhysFrame(pxfield0, false, frameBase+2);
				DrawPhysFrame(pxfield1, true, frameBase+1);
				break;
			case 3:
				DrawPhysFrame(pxfield0, false, frameBase+2);
				DrawPhysFrame(pxfield1, true, frameBase+2);
				break;
			case 4:
				DrawPhysFrame(pxfield0, false, frameBase+3);
				DrawPhysFrame(pxfield1, true, frameBase+3);
				break;
		}

		VDPixmapBlt(*dst, mRGB32Buffer);
	}

	// draw text
	const char* mode_name = mMode < (int)std::size(kModeNames) ? kModeNames[mMode] : "Unknown mode";

	VDStringA buf;
	buf.sprintf("%s - frame %d (%s)", mode_name, (int)frame_num, mpFormatInfo->mpName);

	if (VDPixmapFormatMatrixType(mTargetFormat.format)==1) {
		buf += " ";
		buf += VDPixmapFormatPrintColor(f1);
	}

	mTextRasterizer.Clear();
	VDPixmapConvertTextToPath(mTextRasterizer, NULL, 24.0f * 64.0f * mScale, 20.0f * 64.0f * mScale, 460.0f * 64.0f * mScale, buf.c_str());

	mTextRasterizer.ScanConvert(mTextRegion);

	VDPixmapConvolveRegion(mTextBorderRegion, mTextRegion, mTextOutlineBrush, &mTempRegion);

	VDPixmapFillPixmapAntialiased8x(*dst, mTextBorderRegion, 0, 0, black);
	VDPixmapFillPixmapAntialiased8x(*dst, mTextRegion, 0, 0, textcolor);

	if (&mTargetFormat != dst) {
		mCachedBlitter.Update(mTargetFormat, *dst);
		VDPROFILEBEGINEX3("V-BlitSource",(int)frame_num,0,mCachedBlitter.profiler_comment.c_str());
		mCachedBlitter.Blit(mTargetFormat, *dst);
		VDPROFILEEND();
	}

	mCachedFrame = frame_num;
	return mpFrameBuffer.get();
}

const void *VDVideoSourceTest::getFrame(VDPosition frameNum) {
	if (mCachedFrame == frameNum)
		return mpFrameBuffer.get();

	return streamGetFrame(&frameNum, sizeof(VDPosition), FALSE, frameNum, frameNum);
}

void VDVideoSourceTest::DrawRotatingCubeFrame(VDPixmap& dst, bool interlaced, bool oddField, int frame_num, bool isyuv, int variant) {
	VDPixmapFormatEx f1 = VDPixmapFormatNormalize(dst);

	// draw cube
	static const int kIndices[] = {
		0, 1, 4, 4, 1, 5,
		2, 6, 3, 3, 6, 7,
		0, 4, 2, 2, 4, 6,
		1, 3, 5, 5, 3, 7,
		0, 2, 1, 1, 2, 3,
		4, 5, 6, 6, 5, 7,
	};

	VDTriColorVertex vertices[8];

	for(int i=0; i<8; ++i) {
		vertices[i].x = i&1 ? 1.0f : -1.0f;
		vertices[i].y = i&2 ? 1.0f : -1.0f;
		vertices[i].z = i&4 ? 1.0f : -1.0f;

		float r = (i&1) ? 1.0f : 0.0f;
		float g = (i&2) ? 1.0f : 0.0f;
		float b = (i&4) ? 1.0f : 0.0f;
		if (variant==kTest_gradients) {
			r = (i&1) ? 0.6f : 0.4f;
			g = (i&2) ? 0.6f : 0.4f;
			b = (i&4) ? 0.6f : 0.4f;
		}

		if (isyuv) {
			float y,cb,cr;
			VDConvertRGBToYCbCr(r,g,b,y,cb,cr,f1.colorSpaceMode==vd2::kColorSpaceMode_709, f1.colorRangeMode==vd2::kColorRangeMode_Full);
			vertices[i].r = cr;
			vertices[i].g = y;
			vertices[i].b = cb;
		} else {
			vertices[i].r = r;
			vertices[i].g = g;
			vertices[i].b = b;
		}

		vertices[i].a = 1.0f;
	}

	float rot = (float)frame_num * 0.02f;

	if (interlaced)
		rot *= 0.5f;

	const float kNear = 0.1f;
	const float kFar = 500.0f;
	const float w = 640.0f / 5000.0f;
	const float h = 480.0f / 5000.0f;

	vdfloat4x4 proj;

	proj.m[0].set(2.0f*kNear/w, 0.0f, 0.0f, 0.0f);
	proj.m[1].set(0.0f, 2.0f*kNear/h, 0.0f, 0.0f);
	proj.m[2].set(0.0f, 0.0f, -(kFar+kNear)/(kFar-kNear), -2.0f*kNear*kFar/(kFar-kNear));
	proj.m[3].set(0.0f, 0.0f, -1.0f, 0.0f);

	vdfloat4x4 objpos;

	float size = 10.0f;
	float pos = -50.0f;
	if (variant==kTest_gradients) {
		size = -10.0f;
		pos = 0.0f;
		vertices[1].a = 0.0f;
		vertices[2].a = 0.0f;
		vertices[5].a = 0.0f;
		vertices[6].a = 0.0f;
	}

	objpos.m[0].set(size, 0.0f, 0.0f, 0.0f);
	objpos.m[1].set(0.0f, size, 0.0f, 0.0f);
	objpos.m[2].set(0.0f, 0.0f, size, pos);
	objpos.m[3].set(0.0f, 0.0f, 0.0f, 1.0f);

	vdfloat4x4 xf = proj * objpos * vdfloat4x4(vdfloat4x4::rotation_x, rot*1.0f) * vdfloat4x4(vdfloat4x4::rotation_y, rot*1.7f) * vdfloat4x4(vdfloat4x4::rotation_z, rot*2.3f);

	if (interlaced) {
		VDPixmap field(VDPixmapExtractField(dst, oddField));
		float chroma_yoffset = 0;

		if (oddField)
			proj.m[2].z += proj.m[2].w / 480.0f;
		else
			proj.m[2].z -= proj.m[2].w / 480.0f;

		switch(field.format) {
			case nsVDPixmap::kPixFormat_YUV420it_Planar:
			case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
			case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
				chroma_yoffset = -0.25f / 120.0f;
				field.format = nsVDPixmap::kPixFormat_YUV420_Planar;
				break;

			case nsVDPixmap::kPixFormat_YUV420ib_Planar:
			case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
			case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
				chroma_yoffset = +0.25f / 120.0f;
				field.format = nsVDPixmap::kPixFormat_YUV420_Planar;
				break;
		}

		VDPixmapTriFill(field, vertices, 8, kIndices, 36, &xf[0][0], &chroma_yoffset);
	} else {
		VDPixmapTriFill(dst, vertices, 8, kIndices, 36, &xf[0][0]);
	}
}

void VDVideoSourceTest::DrawPhysFrame(VDPixmap& dst, bool oddField, int frameIdx) {
	VDMemset32Rect(dst.data, dst.pitch, 0x00, dst.w, dst.h);

	VDTriColorVertex vx[10];

	const VDTestVidPhysFrame& frame = mVideo.mFrames[frameIdx];
	const VDTestVidPhysPartPos *parts = mVideo.mParticles.data() + frame.mFirstParticle;

	for(int i=0; i<3; ++i) {
		float t = (float)i * 6.28318f / 3.0f + frame.mTriRotation;
		vx[i].x = 320.0f + 100.0f*cosf(t);
		vx[i].y = 240.0f - 100.0f*sinf(t);
		vx[i].z = 0.0f;
		vx[i].r = (i == 0) ? 1.0f : 0.0f;
		vx[i].g = (i == 1) ? 1.0f : 0.0f;
		vx[i].b = (i == 2) ? 1.0f : 0.0f;
		vx[i].a = 255;
	}

	const float xf[16]={
		2.0f / 640.0f, 0.0f, 0.0f, -1.0f,
		0.0f, -2.0f / 480.0f, 0.0f, 1.0f + (oddField ? -1.0f / 480.0f : 1.0f / 480.0f),
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	static const int indices[27]={0,1,2,0,2,3,0,3,4,0,4,5,0,5,6,0,6,7,0,7,8,0,8,9,0,9,1};
	VDPixmapTriFill(dst, vx, 3, indices, 3, xf);

	for(int i=0; i<frame.mParticleCount; ++i) {
		const VDTestVidPhysPartPos& par = parts[i];

		for(int j=0; j<10; ++j) {
			float t = (float)j * 6.28318f / 10.0f;
			vx[j].x = par.mX * (1.0f/16.0f) + cosf(t)*5.0f;
			vx[j].y = par.mY * (1.0f/16.0f) - sinf(t)*5.0f;
			vx[j].z = 0.0f;
			vx[j].r = 1.0f;
			vx[j].g = 1.0f;
			vx[j].b = 1.0f;
			vx[j].a = 1.0f;
		}

		VDPixmapTriFill(dst, vx, 10, indices, 27, xf);
	}
}

///////////////////////////////////////////////////////////////////////////////
class VDInputFileTestOptions : public InputFileOptions {
public:
	VDInputFileTestOptions();
	~VDInputFileTestOptions();
	bool read(const char *buf);
	int write(char *buf, int buflen) const;

public:
	int mMode;
	int mScale;
};

VDInputFileTestOptions::VDInputFileTestOptions()
	: mMode(0), mScale(1)
{
}

VDInputFileTestOptions::~VDInputFileTestOptions() {
}

bool VDInputFileTestOptions::read(const char *buf) {
	if (buf[0] == 1) {
		mMode = buf[1];
		mScale = 1;
		return true;
	}
	if (buf[0] == 2) {
		mMode = buf[1];
		mScale = buf[2];
		return true;
	}

	return false;
}

int VDInputFileTestOptions::write(char *buf, int buflen) const {
	int req_size = 2;
	if (mScale!=1)
		req_size = 3;

	if (!buf)
		return req_size;

	if (buflen < req_size)
		return 0;

	if (mScale==1) {
		buf[0] = 1;
		buf[1] = (char)mMode;
	} else {
		buf[0] = 2;
		buf[1] = (char)mMode;
		buf[2] = (char)mScale;
	}

	return req_size;
}

class VDInputFileTestOptionsDialog : public VDDialogBase {
	VDInputFileTestOptions& mOpts;

public:
	VDInputFileTestOptionsDialog(VDInputFileTestOptions& opts) : mOpts(opts) {}

	bool HandleUIEvent(IVDUIBase *pBase, IVDUIWindow *pWin, uint32 id, eEventType type, int item) {
		if (type == kEventAttach) {
			mpBase = pBase;
			SetValue(100, mOpts.mMode);
			SetValue(200, mOpts.mScale-1);
			pBase->ExecuteAllLinks();
		} else if (type == kEventSelect) {
			if (id == 10) {
				mOpts.mMode = GetValue(100);
				mOpts.mScale = GetValue(200)+1;
				pBase->EndModal(true);
				return true;
			} else if (id == 11) {
				pBase->EndModal(false);
				return true;
			}
		}
		return false;
	}
};

///////////////////////////////////////////////////////////////////////////////

class VDInputFileTest : public InputFile {
public:
	VDInputFileTest();
	~VDInputFileTest();

	void Init(const wchar_t *szFile);

	void setOptions(InputFileOptions *_ifo);
	InputFileOptions *createOptions(const void *buf, uint32 len);
	InputFileOptions *promptForOptions(VDGUIHandle hwnd);

	void setAutomated(bool fAuto);

	bool GetVideoSource(int index, IVDVideoSource **ppSrc);
	bool GetAudioSource(int index, AudioSource **ppSrc);

protected:
	VDInputFileTestOptions	mOptions;
};

VDInputFileTest::VDInputFileTest() {
}

VDInputFileTest::~VDInputFileTest() {
}

void VDInputFileTest::Init(const wchar_t *) {
}

void VDInputFileTest::setOptions(InputFileOptions *_ifo) {
	mOptions = *static_cast<VDInputFileTestOptions *>(_ifo);
}

InputFileOptions *VDInputFileTest::createOptions(const void *buf, uint32 len) {
	VDInputFileTestOptions *opts = new_nothrow VDInputFileTestOptions;

	if (opts) {
		if (opts->read((const char *)buf))
			return opts;

		delete opts;
	}

	return NULL;
}

InputFileOptions *VDInputFileTest::promptForOptions(VDGUIHandle hwnd) {
	vdautoptr<VDInputFileTestOptions> testopts(new_nothrow VDInputFileTestOptions);

	if (!testopts)
		return NULL;

	vdautoptr<IVDUIWindow> peer(VDUICreatePeer((VDGUIHandle)hwnd));

	IVDUIWindow *pWin = VDCreateDialogFromResource(3000, peer);
	VDInputFileTestOptionsDialog dlg(*testopts);

	IVDUIBase *pBase = vdpoly_cast<IVDUIBase *>(pWin);

	pBase->SetCallback(&dlg, false);
	int result = pBase->DoModal();

	peer->Shutdown();

	if (!result)
		throw MyUserAbortError();

	return testopts.release();
}

void VDInputFileTest::setAutomated(bool fAuto) {
}

bool VDInputFileTest::GetVideoSource(int index, IVDVideoSource **ppSrc) {
	if (index)
		return false;

	IVDVideoSource *videoSrc = new VDVideoSourceTest(mOptions.mMode, mOptions.mScale);
	*ppSrc = videoSrc;
	videoSrc->AddRef();
	return true;
}

bool VDInputFileTest::GetAudioSource(int index, AudioSource **ppSrc) {
	return false;
}

///////////////////////////////////////////////////////////////////////////

class VDInputDriverTest : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return L"Test video input driver (internal)"; }

	int GetDefaultPriority() {
		return -1000;
	}

	uint32 GetFlags() { return kF_Video | kF_SupportsOpts; }

	const wchar_t *GetFilenamePattern() {
		// This input driver is never meant to be used for a file.
		return NULL;
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		return kDC_Error_NotImpl;
	}

	InputFile *CreateInputFile(uint32 flags) {
		return new VDInputFileTest;
	}
};

extern IVDInputDriver *VDCreateInputDriverTest() { return new VDInputDriverTest; }
