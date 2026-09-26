// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2015-2019 Anton Shekhovtsov
// Copyright (C) 2025-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include <stdafx.h>
#include <vd2/system/error.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstring.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/system/memory.h>

extern VDPixmapFormatInfo g_vdPixmapFormats[] = {
                                    // name         qchnk qw qh qwb qhb  qs ab aw ah as   ps
    /* Null */                      { "null",       false, 1, 1,  0,  0,  0, 0, 0, 0, 0,   0 },
    /* Pal1 */                      { "Pal1",        true, 8, 1,  3,  0,  1, 0, 0, 0, 0,   2 },
    /* Pal2 */                      { "Pal2",        true, 4, 1,  2,  0,  1, 0, 0, 0, 0,   4 },
    /* Pal4 */                      { "Pal4",        true, 2, 1,  1,  0,  1, 0, 0, 0, 0,  16 },
    /* Pal8 */                      { "Pal8",       false, 1, 1,  0,  0,  1, 0, 0, 0, 0, 256 },
    /* RGB16_555 */                 { "XRGB1555",   false, 1, 1,  0,  0,  2, 0, 0, 0, 0,   0 },
    /* RGB16_565 */                 { "RGB565",     false, 1, 1,  0,  0,  2, 0, 0, 0, 0,   0 },
    /* RGB24 */                     { "RGB24",      false, 1, 1,  0,  0,  3, 0, 0, 0, 0,   0 },
    /* RGB32 */                     { "RGBA32",     false, 1, 1,  0,  0,  4, 0, 0, 0, 0,   0 },
    /* Y8 */                        { "Y8",         false, 1, 1,  0,  0,  1, 0, 0, 0, 0,   0 },
    /* YUV422_UYVY */               { "UYVY",        true, 2, 1,  1,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV422_YUYV */               { "YUYV",        true, 2, 1,  1,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV444_XVYU */               { "XVYU",       false, 1, 1,  0,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV444_Planar */             { "YUV444",     false, 1, 1,  0,  0,  1, 2, 0, 0, 1,   0 },
    /* YUV422_Planar */             { "YUV422",     false, 1, 1,  0,  0,  1, 2, 1, 0, 1,   0 },
    /* YUV420_Planar */             { "YUV420",     false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV411_Planar */             { "YUV411",     false, 1, 1,  0,  0,  1, 2, 2, 0, 1,   0 },
    /* YUV410_Planar */             { "YUV410",     false, 1, 1,  0,  0,  1, 2, 2, 2, 1,   0 },
    /* YUV422_Planar_Centered */    { "YUV422C",    false, 1, 1,  0,  0,  1, 2, 1, 0, 1,   0 },
    /* YUV420_Planar_Centered */    { "YUV420C",    false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV422_Planar_16F */         { "YUV422_16F", false, 1, 1,  0,  0,  2, 2, 1, 0, 2,   0 },
    /* V210 */                      { "v210",        true,24, 1,  2,  0, 64, 0, 0, 0, 1,   0 },
    /* YUV422_UYVY_709 */           { "UYVY-709",    true, 2, 1,  1,  0,  4, 0, 0, 0, 0,   0 },
    /* NV12 */                      { "NV12",       false, 1, 1,  0,  0,  1, 1, 1, 1, 2,   0 },
    /* Y8-FR */                     { "Y8-FR",      false, 1, 1,  1,  0,  1, 0, 0, 0, 0,   0 },
    /* YUV422_YUYV_709 */           { "YUYV-709",    true, 2, 1,  1,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV444_Planar_709 */         { "YUV444-709", false, 1, 1,  0,  0,  1, 2, 0, 0, 1,   0 },
    /* YUV422_Planar_709 */         { "YUV422-709", false, 1, 1,  0,  0,  1, 2, 1, 0, 1,   0 },
    /* YUV420_Planar_709 */         { "YUV420-709", false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV411_Planar_709 */         { "YUV411-709", false, 1, 1,  0,  0,  1, 2, 2, 0, 1,   0 },
    /* YUV410_Planar_709 */         { "YUV410-709", false, 1, 1,  0,  0,  1, 2, 2, 2, 1,   0 },
    /* YUV422_UYVY_FR */            { "UYVY-FR",     true, 2, 1,  1,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV422_YUYV_FR */            { "YUYV-FR",     true, 2, 1,  1,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV444_Planar_FR */          { "YUV444-FR",  false, 1, 1,  0,  0,  1, 2, 0, 0, 1,   0 },
    /* YUV422_Planar_FR */          { "YUV422-FR",  false, 1, 1,  0,  0,  1, 2, 1, 0, 1,   0 },
    /* YUV420_Planar_FR */          { "YUV420-FR",  false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV411_Planar_FR */          { "YUV411-FR",  false, 1, 1,  0,  0,  1, 2, 2, 0, 1,   0 },
    /* YUV410_Planar_FR */          { "YUV410-FR",  false, 1, 1,  0,  0,  1, 2, 2, 2, 1,   0 },
                                    // name              qchnk qw qh qwb qhb  qs ab aw ah as   ps
    /* YUV422_UYVY_FR_709 */        { "UYVY-709-FR",     true, 2, 1,  1,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV422_YUYV_FR_709 */        { "YUYV-709-FR",     true, 2, 1,  1,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV444_Planar_FR_709 */      { "YUV444-709-FR",  false, 1, 1,  0,  0,  1, 2, 0, 0, 1,   0 },
    /* YUV422_Planar_FR_709 */      { "YUV422-709-FR",  false, 1, 1,  0,  0,  1, 2, 1, 0, 1,   0 },
    /* YUV420_Planar_FR_709 */      { "YUV420-709-FR",  false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV411_Planar_FR_709 */      { "YUV411-709-FR",  false, 1, 1,  0,  0,  1, 2, 2, 0, 1,   0 },
    /* YUV410_Planar_FR_709 */      { "YUV410-709-FR",  false, 1, 1,  0,  0,  1, 2, 2, 2, 1,   0 },
    /* YUV420i_Planar */            { "YUV420i",        false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420i_Planar_FR */         { "YUV420i-FR",     false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420i_Planar_709 */        { "YUV420i-709",    false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420i_Planar_709_FR */     { "YUV420i-709-FR", false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420it_Planar */           { "YUV420it",       false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420it_Planar_FR */        { "YUV420it-FR",    false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420it_Planar_709 */       { "YUV420it-709",   false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420it_Planar_709_FR */    { "YUV420it-709-FR",false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420ib_Planar */           { "YUV420ib",       false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420ib_Planar_FR */        { "YUV420ib-FR",    false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420ib_Planar_709 */       { "YUV420ib-709",   false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* YUV420ib_Planar_709_FR */    { "YUV420ib-709-FR",false, 1, 1,  0,  0,  1, 2, 1, 1, 1,   0 },
    /* RGB64 */                     { "RGBA64",         false, 1, 1,  0,  0,  8, 0, 0, 0, 0,   0 },
    /* YUV444_Planar16 */           { "YUV444P16",      false, 1, 1,  0,  0,  2, 2, 0, 0, 2,   0 },
    /* YUV422_Planar16 */           { "YUV422P16",      false, 1, 1,  0,  0,  2, 2, 1, 0, 2,   0 },
    /* YUV420_Planar16 */           { "YUV420P16",      false, 1, 1,  0,  0,  2, 2, 1, 1, 2,   0 },
    /* Y16 */                       { "Y16",            false, 1, 1,  1,  0,  2, 0, 0, 0, 0,   0 },
    /* YUVA444_Y416 */              { "Y416",           false, 1, 1,  0,  0,  8, 0, 0, 0, 0,   0 },
    /* YUV444_V410 */               { "v410",           false, 1, 1,  0,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV444_Y410 */               { "Y410",           false, 1, 1,  0,  0,  4, 0, 0, 0, 0,   0 },
    /* r210 */                      { "r210",           false, 1, 1,  0,  0,  4, 0, 0, 0, 0,   0 },
    /* r10k */                      { "R10k",           false, 1, 1,  0,  0,  4, 0, 0, 0, 0,   0 },
    /* YUV444_V308 */               { "v308",           false, 1, 1,  0,  0,  3, 0, 0, 0, 0,   0 },
    /* YUV422_P210 */               { "P210",           false, 1, 1,  0,  0,  2, 1, 1, 0, 4,   0 },
    /* YUV420_P010 */               { "P010",           false, 1, 1,  0,  0,  2, 1, 1, 1, 4,   0 },
    /* YUV422_P216 */               { "P216",           false, 1, 1,  0,  0,  2, 1, 1, 0, 4,   0 },
    /* YUV420_P016 */               { "P016",           false, 1, 1,  0,  0,  2, 1, 1, 1, 4,   0 },
                                    // name              qchnk qw qh qwb qhb  qs ab aw ah as   ps  as4
    /* YUV444_Alpha_Planar */       { "YUVA444",        false, 1, 1,  0,  0,  1, 3, 0, 0, 1,   0,  1 },
    /* YUV422_Alpha_Planar */       { "YUVA422",        false, 1, 1,  0,  0,  1, 3, 1, 0, 1,   0,  1 },
    /* YUV420_Alpha_Planar */       { "YUVA420",        false, 1, 1,  0,  0,  1, 3, 1, 1, 1,   0,  1 },
    /* YUV444_Alpha_Planar16 */     { "YUVA444P16",     false, 1, 1,  0,  0,  2, 3, 0, 0, 2,   0,  2 },
    /* YUV422_Alpha_Planar16 */     { "YUVA422P16",     false, 1, 1,  0,  0,  2, 3, 1, 0, 2,   0,  2 },
    /* YUV420_Alpha_Planar16 */     { "YUVA420P16",     false, 1, 1,  0,  0,  2, 3, 1, 1, 2,   0,  2 },
    /* YUV422_YU64 */               { "YU64",           true,  2, 1,  1,  0,  8, 0, 0, 0, 0,   0,  0 },
    /* B64A */                      { "b64a",           false, 1, 1,  0,  0,  8, 0, 0, 0, 0,   0,  0 },
    /* RGB_Planar */                { "RGB-P8",         false, 1, 1,  0,  0,  1, 2, 0, 0, 1,   0,  0 },
    /* RGB_Planar16 */              { "RGB-P16",        false, 1, 1,  0,  0,  2, 2, 0, 0, 2,   0,  0 },
    /* RGB_Planar32F */             { "RGB-Float",      false, 1, 1,  0,  0,  4, 2, 0, 0, 4,   0,  0 },
    /* RGBA_Planar */               { "RGBA-P8",        false, 1, 1,  0,  0,  1, 3, 0, 0, 1,   0,  1 },
    /* RGBA_Planar16 */             { "RGBA-P16",       false, 1, 1,  0,  0,  2, 3, 0, 0, 2,   0,  2 },
    /* RGBA_Planar32F */            { "RGBA-Float",     false, 1, 1,  0,  0,  4, 3, 0, 0, 4,   0,  4 },
    /* R_32F */                     { "R-Float",        false, 1, 1,  0,  0,  4, 0, 0, 0, 4,   0,  0 },
    /* B48R */                      { "b48r",           false, 1, 1,  0,  0,  6, 0, 0, 0, 0,   0,  0 },
};

bool VDPixmapFormatHasAlpha(sint32 format) {
	using namespace nsVDPixmap;
	switch (format) {
	case kPixFormat_XRGB8888:
	case kPixFormat_XRGB64:
	case kPixFormat_B64A:
	case kPixFormat_RGBA_Planar:
	case kPixFormat_RGBA_Planar16:
	case kPixFormat_RGBA_Planar32F:
	case kPixFormat_YUVA444_Y416:
	case kPixFormat_YUV444_Alpha_Planar:
	case kPixFormat_YUV422_Alpha_Planar:
	case kPixFormat_YUV420_Alpha_Planar:
	case kPixFormat_YUV444_Alpha_Planar16:
	case kPixFormat_YUV422_Alpha_Planar16:
	case kPixFormat_YUV420_Alpha_Planar16:
		return true;
	}
	return false;
}

bool VDPixmapFormatHasAlphaPlane(sint32 format) {
	using namespace nsVDPixmap;
	switch (format) {
	case kPixFormat_RGBA_Planar:
	case kPixFormat_RGBA_Planar16:
	case kPixFormat_RGBA_Planar32F:
	case kPixFormat_YUV444_Alpha_Planar:
	case kPixFormat_YUV422_Alpha_Planar:
	case kPixFormat_YUV420_Alpha_Planar:
	case kPixFormat_YUV444_Alpha_Planar16:
	case kPixFormat_YUV422_Alpha_Planar16:
	case kPixFormat_YUV420_Alpha_Planar16:
		return true;
	}
	return false;
}

bool VDPixmapFormatHasRGBPlane(sint32 format) {
	using namespace nsVDPixmap;
	switch (format) {
	case kPixFormat_RGB_Planar:
	case kPixFormat_RGB_Planar16:
	case kPixFormat_RGB_Planar32F:
	case kPixFormat_RGBA_Planar:
	case kPixFormat_RGBA_Planar16:
	case kPixFormat_RGBA_Planar32F:
		return true;
	}
	return false;
}

bool VDPixmapFormatHasYUV16(sint32 format) {
	using namespace nsVDPixmap;
	switch (format) {
	case kPixFormat_YUV444_Planar16:
	case kPixFormat_YUV422_Planar16:
	case kPixFormat_YUV420_Planar16:
	case kPixFormat_YUV444_Alpha_Planar16:
	case kPixFormat_YUV422_Alpha_Planar16:
	case kPixFormat_YUV420_Alpha_Planar16:
	case kPixFormat_YUVA444_Y416:
	case kPixFormat_YUV422_YU64:
	case kPixFormat_YUV422_P210:
	case kPixFormat_YUV422_P216:
	case kPixFormat_YUV420_P010:
	case kPixFormat_YUV420_P016:
		return true;
	}
	return false;
}

bool VDPixmapFormatGray(sint32 format) {
	using namespace nsVDPixmap;
	switch (format) {
	case kPixFormat_Y8:
	case kPixFormat_Y8_FR:
	case kPixFormat_Y16:
		return true;
	}
	return false;
}

VDPixmap VDPixmap::copy(const VDXPixmap& a) {
	VDPixmap b;
	b.data = a.data;
	b.palette = a.palette;
	b.w = a.w;
	b.h = a.h;
	b.pitch = a.pitch;
	b.format = a.format;
	b.data2 = a.data2;
	b.pitch2 = a.pitch2;
	b.data3 = a.data3;
	b.pitch3 = a.pitch3;

	if (VDPixmapFormatHasAlphaPlane(a.format)) {
		const VDXPixmapAlpha& aa = (const VDXPixmapAlpha&)a;
		b.data4 = aa.data4;
		b.pitch4 = aa.pitch4;
	}

	return b;
}

int VDPixmapFormatMatrixType(sint32 format) {
	using namespace nsVDPixmap;
	switch (VDPixmapFormatNormalize(format)) {
	case kPixFormat_Y16:
	case kPixFormat_YUV420_NV12:
	case kPixFormat_YUV422_V210:
	case kPixFormat_YUV444_V410:
	case kPixFormat_YUV444_Y410:
	case kPixFormat_YUV444_Planar16:
	case kPixFormat_YUV422_Planar16:
	case kPixFormat_YUV420_Planar16:
	case kPixFormat_YUVA444_Y416:
	case kPixFormat_YUV444_V308:
	case kPixFormat_YUV422_P216:
	case kPixFormat_YUV420_P016:
	case kPixFormat_YUV422_P210:
	case kPixFormat_YUV420_P010:
	case kPixFormat_YUV444_Alpha_Planar:
	case kPixFormat_YUV422_Alpha_Planar:
	case kPixFormat_YUV420_Alpha_Planar:
	case kPixFormat_YUV444_Alpha_Planar16:
	case kPixFormat_YUV422_Alpha_Planar16:
	case kPixFormat_YUV420_Alpha_Planar16:
	case kPixFormat_YUV422_YU64:
		return 1; // flexible

	case kPixFormat_Y8:
	case kPixFormat_YUV444_Planar:
	case kPixFormat_YUV422_Planar:
	case kPixFormat_YUV420_Planar:
	case kPixFormat_YUV410_Planar:
	case kPixFormat_YUV411_Planar:
	case kPixFormat_YUV422_YUYV:
	case kPixFormat_YUV422_UYVY:
		return 2; // combined
	}

	return 0;
}

int VDPixmapFormatGroup(int src) {
	using namespace nsVDPixmap;
	src = VDPixmapFormatNormalize(src);

	switch (src) {
	case kPixFormat_RGB565:
	case kPixFormat_XRGB1555:
	case kPixFormat_RGB888:
	case kPixFormat_RGB_Planar:
	case kPixFormat_RGBA_Planar:
		return kPixFormat_XRGB8888;

	case kPixFormat_R210:
	case kPixFormat_R10K:
	case kPixFormat_B64A:
	case kPixFormat_B48R:
	case kPixFormat_RGB_Planar16:
	case kPixFormat_RGBA_Planar16:
		return kPixFormat_XRGB64;

	case kPixFormat_YUV420_NV12:
		return kPixFormat_YUV420_Planar;

	case kPixFormat_YUV422_YUYV:
	case kPixFormat_YUV422_UYVY:
		return kPixFormat_YUV422_Planar;

	case kPixFormat_YUV444_V308:
		return kPixFormat_YUV444_Planar;

	case kPixFormat_YUV420_P016:
	case kPixFormat_YUV420_P010:
		return kPixFormat_YUV420_Planar16;

	case kPixFormat_YUV422_P216:
	case kPixFormat_YUV422_P210:
	case kPixFormat_YUV422_V210:
	case kPixFormat_YUV422_YU64:
		return kPixFormat_YUV422_Planar16;

	case kPixFormat_YUVA444_Y416:
	case kPixFormat_YUV444_V410:
	case kPixFormat_YUV444_Y410:
		return kPixFormat_YUV444_Planar16;
	}

	return src;
}

int VDPixmapFormatDifference(VDPixmapFormatEx src, VDPixmapFormatEx dst) {
	src = VDPixmapFormatNormalize(src);
	dst = VDPixmapFormatNormalize(dst);
	if (src.format==dst.format) return 0;

	src = VDPixmapFormatGroup(src);
	dst = VDPixmapFormatGroup(dst);

	if (src.format==dst.format) return 1;
	return 2;
}

// derive base format if possible,
// and expand mode flags
VDPixmapFormatEx VDPixmapFormatNormalize(VDPixmapFormatEx format) {
	using namespace nsVDPixmap;
	VDPixmapFormatEx r = format;

	switch (format) {
	case kPixFormat_Y8:
		r.format = kPixFormat_Y8;
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_None;
		break;

	case kPixFormat_Y8_FR:
		r.format = kPixFormat_Y8;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_None;
		break;

	case kPixFormat_YUV444_Planar_709_FR:
		r.format = kPixFormat_YUV444_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV444_Planar_FR:
		r.format = kPixFormat_YUV444_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV444_Planar_709:
		r.format = kPixFormat_YUV444_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV444_Planar:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV422_Planar_709_FR:
		r.format = kPixFormat_YUV422_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV422_Planar_FR:
		r.format = kPixFormat_YUV422_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV422_Planar_709:
		r.format = kPixFormat_YUV422_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV422_Planar:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV420_Planar_709_FR:
		r.format = kPixFormat_YUV420_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV420_Planar_FR:
		r.format = kPixFormat_YUV420_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV420_Planar_709:
		r.format = kPixFormat_YUV420_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV420_Planar:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV410_Planar_709_FR:
		r.format = kPixFormat_YUV410_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV410_Planar_FR:
		r.format = kPixFormat_YUV410_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV410_Planar_709:
		r.format = kPixFormat_YUV410_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV410_Planar:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV411_Planar_709_FR:
		r.format = kPixFormat_YUV411_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV411_Planar_FR:
		r.format = kPixFormat_YUV411_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV411_Planar_709:
		r.format = kPixFormat_YUV411_Planar;
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV411_Planar:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV422_YUYV_709_FR:
		r.format = kPixFormat_YUV422_YUYV;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV422_YUYV_FR:
		r.format = kPixFormat_YUV422_YUYV;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV422_YUYV_709:
		r.format = kPixFormat_YUV422_YUYV;
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV422_YUYV:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV422_UYVY_709_FR:
		r.format = kPixFormat_YUV422_UYVY;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV422_UYVY_FR:
		r.format = kPixFormat_YUV422_UYVY;
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV422_UYVY_709:
		r.format = kPixFormat_YUV422_UYVY;
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV422_UYVY:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV420i_Planar:
	case kPixFormat_YUV420it_Planar:
	case kPixFormat_YUV420ib_Planar:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV420i_Planar_FR:
	case kPixFormat_YUV420it_Planar_FR:
	case kPixFormat_YUV420ib_Planar_FR:
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_601;
		break;

	case kPixFormat_YUV420i_Planar_709:
	case kPixFormat_YUV420it_Planar_709:
	case kPixFormat_YUV420ib_Planar_709:
		r.colorRangeMode = vd2::kColorRangeMode_Limited;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;

	case kPixFormat_YUV420i_Planar_709_FR:
	case kPixFormat_YUV420it_Planar_709_FR:
	case kPixFormat_YUV420ib_Planar_709_FR:
		r.colorRangeMode = vd2::kColorRangeMode_Full;
		r.colorSpaceMode = vd2::kColorSpaceMode_709;
		break;
	}

	switch (format) {
	case kPixFormat_YUV420i_Planar_FR:
	case kPixFormat_YUV420i_Planar_709:
	case kPixFormat_YUV420i_Planar_709_FR:
		r.format = kPixFormat_YUV420i_Planar;
		break;

	case kPixFormat_YUV420it_Planar_FR:
	case kPixFormat_YUV420it_Planar_709:
	case kPixFormat_YUV420it_Planar_709_FR:
		r.format = kPixFormat_YUV420it_Planar;
		break;

	case kPixFormat_YUV420ib_Planar_FR:
	case kPixFormat_YUV420ib_Planar_709:
	case kPixFormat_YUV420ib_Planar_709_FR:
		r.format = kPixFormat_YUV420ib_Planar;
		break;
	}


	if (format.colorSpaceMode) r.colorSpaceMode = format.colorSpaceMode;
	if (format.colorRangeMode) r.colorRangeMode = format.colorRangeMode;

	return r;
}

// expand only special combined formats
VDPixmapFormatEx VDPixmapFormatNormalizeOpt(VDPixmapFormatEx format) {
	VDPixmapFormatEx r = VDPixmapFormatNormalize(format);
	if (r==format) return format;
	return r;
}

// apply matrix tweaks and combine
VDPixmapFormatEx VDPixmapFormatCombineOpt(VDPixmapFormatEx format, VDPixmapFormatEx opt) {
	int opt_type = VDPixmapFormatMatrixType(opt);
	if (opt.format==0 || opt_type==1 || opt_type==2) {
		VDPixmapFormatEx r = VDPixmapFormatNormalize(format);
		if (opt.colorSpaceMode) r.colorSpaceMode = opt.colorSpaceMode;
		if (opt.colorRangeMode) r.colorRangeMode = opt.colorRangeMode;
		return VDPixmapFormatCombine(r);
	} else {
		return VDPixmapFormatCombine(format);
	}
}

// replace with combined format if possible,
// and expand mode flags
VDPixmapFormatEx VDPixmapFormatCombine(VDPixmapFormatEx format) {
	using namespace nsVDPixmap;

	VDPixmapFormatEx r = VDPixmapFormatNormalize(format);
	int type = VDPixmapFormatMatrixType(format);

	if (type==0) {
		r.colorSpaceMode = vd2::kColorSpaceMode_None;
		r.colorRangeMode = vd2::kColorRangeMode_None;
	} else {
		if (!r.colorSpaceMode) r.colorSpaceMode = vd2::kColorSpaceMode_601;
		if (!r.colorRangeMode) r.colorRangeMode = vd2::kColorRangeMode_Limited;
	}

	if (type!=2) return r;

	switch (r.format) {
	case kPixFormat_Y8:
		r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_Y8_FR : kPixFormat_Y8;
		break;

	case kPixFormat_YUV444_Planar:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV444_Planar_709_FR : kPixFormat_YUV444_Planar_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV444_Planar_FR : kPixFormat_YUV444_Planar;
		break;

	case kPixFormat_YUV422_Planar:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV422_Planar_709_FR : kPixFormat_YUV422_Planar_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV422_Planar_FR : kPixFormat_YUV422_Planar;
		break;

	case kPixFormat_YUV420_Planar:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV420_Planar_709_FR : kPixFormat_YUV420_Planar_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV420_Planar_FR : kPixFormat_YUV420_Planar;
		break;

	case kPixFormat_YUV410_Planar:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV410_Planar_709_FR : kPixFormat_YUV410_Planar_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV410_Planar_FR : kPixFormat_YUV410_Planar;
		break;

	case kPixFormat_YUV411_Planar:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV411_Planar_709_FR : kPixFormat_YUV411_Planar_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV411_Planar_FR : kPixFormat_YUV411_Planar;
		break;

	case kPixFormat_YUV422_YUYV:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV422_YUYV_709_FR : kPixFormat_YUV422_YUYV_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV422_YUYV_FR : kPixFormat_YUV422_YUYV;
		break;

	case kPixFormat_YUV422_UYVY:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV422_UYVY_709_FR : kPixFormat_YUV422_UYVY_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV422_UYVY_FR : kPixFormat_YUV422_UYVY;
		break;

	case kPixFormat_YUV420i_Planar:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV420i_Planar_709_FR : kPixFormat_YUV420i_Planar_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV420i_Planar_FR : kPixFormat_YUV420i_Planar;
		break;

	case kPixFormat_YUV420it_Planar:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV420it_Planar_709_FR : kPixFormat_YUV420it_Planar_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV420it_Planar_FR : kPixFormat_YUV420it_Planar;
		break;

	case kPixFormat_YUV420ib_Planar:
		if (r.colorSpaceMode==vd2::kColorSpaceMode_709)
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV420ib_Planar_709_FR : kPixFormat_YUV420ib_Planar_709;
		else
			r.format = r.colorRangeMode==vd2::kColorRangeMode_Full ? kPixFormat_YUV420ib_Planar_FR : kPixFormat_YUV420ib_Planar;
		break;
	}

	return r;
}

VDStringA VDPixmapFormatPrintSpec(VDPixmapFormatEx format) {
	if (format==0) return VDStringA("(?)");

	int type = VDPixmapFormatMatrixType(format);

	format = VDPixmapFormatNormalize(format);
	const VDPixmapFormatInfo& info = VDPixmapGetInfo(format);
	VDStringA s(info.name);

	if (type==1 || type==2) {
		if (format.colorSpaceMode==vd2::kColorSpaceMode_709) s += "-709";
		if (format.colorRangeMode==vd2::kColorRangeMode_Full) s += "-FR";
	}

	return s;
}

VDStringA VDPixmapFormatPrintColor(VDPixmapFormatEx format) {
	int type = VDPixmapFormatMatrixType(format);

	format = VDPixmapFormatNormalize(format);
	VDStringA s;

	if (type==1 || type==2) {
		if (format.colorSpaceMode==vd2::kColorSpaceMode_709) s += "Rec. 709";
		if (format.colorRangeMode==vd2::kColorRangeMode_Full) { if(!s.empty()) s += ", "; s += "Full range"; }
	}

	return s;
}

// search logic (rules):
// 1. repack to universal formats
// 2. follow prerendered list organized in this order:
//   a. 16bit<->8bit
//   b. alpha->no alpha
//   c. upsample 420->422->444
//   d. yuv<->rgb
// 3. try some backup formats (drop colorspaces now)
// 4. try all legacy formats

void MatchFilterFormat::next1() {
	using namespace nsVDPixmap;

	formatMask.reset(format);

	if (base) {
		base = next_base();
		if (base) {
			format = VDPixmapFormatCombineOpt(base, original);
			return;
		}
	}

	if (follow_list) {
		while(1) {
			int i = follow_list[pos];
			if (!i) {
				follow_list = 0;
				break;
			}
			pos++;
			if (formatMask.test(i)) {
				format = VDPixmapFormatCombineOpt(i, original);
				return;
			}
		}
	}

	static const int kBackup[]={
		kPixFormat_YUV444_Planar,
		kPixFormat_YUV422_Planar,
		kPixFormat_YUV422_UYVY,
		kPixFormat_YUV422_YUYV,
		kPixFormat_YUV420_Planar,
		kPixFormat_YUV411_Planar,
		kPixFormat_YUV410_Planar,
		kPixFormat_XRGB8888,
		kPixFormat_XRGB64
	};

	while (backup < (int)std::size(kBackup)) {
		int i = kBackup[backup];
		backup++;
		if (formatMask.test(i)) {
			format = i;
			return;
		}
	}

	while(legacy<kPixFormat_XRGB64) {
		int i = legacy;
		legacy++;
		if (formatMask.test(i)) {
			format = i;
			return;
		}
	}

	format = 0;
}

void MatchFilterFormat::initBase() {
	using namespace nsVDPixmap;

	base = original;
	base1 = 0;
	follow_list = 0;
	pos = 0;

	switch(original) {
	case kPixFormat_Y8:
	case kPixFormat_Y8_FR:
		base1 = kPixFormat_Y8;
		{
			static int list[] = {
				kPixFormat_Y16,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_Y16:
		base1 = kPixFormat_Y16;
		{
			static int list[] = {
				kPixFormat_Y8,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV420_Planar:
	case kPixFormat_YUV410_Planar:
	case kPixFormat_YUV420_NV12:
		base1 = kPixFormat_YUV420_Planar;
		{
			static int list[] = {
				kPixFormat_YUV420_Planar16,
				kPixFormat_YUV422_Planar, kPixFormat_YUV422_Planar16,
				kPixFormat_YUV444_Planar, kPixFormat_YUV444_Planar16,
				kPixFormat_XRGB8888, kPixFormat_XRGB64,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV422_UYVY:
	case kPixFormat_YUV422_YUYV:
	case kPixFormat_YUV422_Planar:
	case kPixFormat_YUV411_Planar:
		base1 = kPixFormat_YUV422_Planar;
		{
			static int list[] = {
				kPixFormat_YUV422_Planar16,
				kPixFormat_YUV444_Planar, kPixFormat_YUV444_Planar16,
				kPixFormat_XRGB8888, kPixFormat_XRGB64,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV444_V308:
	case kPixFormat_YUV444_Planar:
		base1 = kPixFormat_YUV444_Planar;
		{
			static int list[] = {
				kPixFormat_YUV444_Planar16,
				kPixFormat_XRGB8888, kPixFormat_XRGB64,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV420_P010:
	case kPixFormat_YUV420_P016:
	case kPixFormat_YUV420_Planar16:
		base1 = kPixFormat_YUV420_Planar16;
		{
			static int list[] = {
				kPixFormat_YUV420_Planar, 
				kPixFormat_YUV422_Planar16, kPixFormat_YUV422_Planar, 
				kPixFormat_YUV444_Planar16, kPixFormat_YUV444_Planar,
				kPixFormat_XRGB64, kPixFormat_XRGB8888,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV422_V210:
	case kPixFormat_YUV422_P210:
	case kPixFormat_YUV422_P216:
	case kPixFormat_YUV422_YU64:
	case kPixFormat_YUV422_Planar16:
		base1 = kPixFormat_YUV422_Planar16;
		{
			static int list[] = {
				kPixFormat_YUV422_Planar,
				kPixFormat_YUV444_Planar16, kPixFormat_YUV444_Planar,
				kPixFormat_XRGB64, kPixFormat_XRGB8888,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV444_V410:
	case kPixFormat_YUV444_Y410:
	case kPixFormat_YUV444_Planar16:
		base1 = kPixFormat_YUV444_Planar16;
		{
			static int list[] = {
				kPixFormat_YUV444_Planar,
				kPixFormat_XRGB64, kPixFormat_XRGB8888,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUVA444_Y416:
	case kPixFormat_YUV444_Alpha_Planar16:
		base1 = kPixFormat_YUV444_Alpha_Planar16;
		{
			static int list[] = {
				kPixFormat_YUV444_Alpha_Planar,
				kPixFormat_YUV444_Planar16, kPixFormat_YUV444_Planar,
				kPixFormat_XRGB64, kPixFormat_XRGB8888,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV422_Alpha_Planar16:
		base1 = kPixFormat_YUV422_Alpha_Planar16;
		{
			static int list[] = {
				kPixFormat_YUV422_Alpha_Planar, 
				kPixFormat_YUV444_Alpha_Planar16, kPixFormat_YUV444_Alpha_Planar,
				kPixFormat_YUV422_Planar16, kPixFormat_YUV422_Planar, 
				kPixFormat_YUV444_Planar16, kPixFormat_YUV444_Planar,
				kPixFormat_XRGB64, kPixFormat_XRGB8888,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV420_Alpha_Planar16:
		base1 = kPixFormat_YUV420_Alpha_Planar16;
		{
			static int list[] = {
				kPixFormat_YUV420_Alpha_Planar, 
				kPixFormat_YUV422_Alpha_Planar16, kPixFormat_YUV422_Alpha_Planar, 
				kPixFormat_YUV444_Alpha_Planar16, kPixFormat_YUV444_Alpha_Planar,
				kPixFormat_YUV420_Planar16, kPixFormat_YUV420_Planar, 
				kPixFormat_YUV422_Planar16, kPixFormat_YUV422_Planar, 
				kPixFormat_YUV444_Planar16, kPixFormat_YUV444_Planar,
				kPixFormat_XRGB64, kPixFormat_XRGB8888,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV444_Alpha_Planar:
		base1 = kPixFormat_YUV444_Alpha_Planar;
		{
			static int list[] = {
				kPixFormat_YUV444_Alpha_Planar16,
				kPixFormat_YUV444_Planar, kPixFormat_YUV444_Planar16,
				kPixFormat_XRGB8888, kPixFormat_XRGB64,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV422_Alpha_Planar:
		base1 = kPixFormat_YUV422_Alpha_Planar;
		{
			static int list[] = {
				kPixFormat_YUV422_Alpha_Planar16,
				kPixFormat_YUV444_Alpha_Planar, kPixFormat_YUV444_Alpha_Planar16,
				kPixFormat_YUV422_Planar, kPixFormat_YUV422_Planar16,
				kPixFormat_YUV444_Planar, kPixFormat_YUV444_Planar16,
				kPixFormat_XRGB8888, kPixFormat_XRGB64,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_YUV420_Alpha_Planar:
		base1 = kPixFormat_YUV420_Alpha_Planar;
		{
			static int list[] = {
				kPixFormat_YUV420_Alpha_Planar16,
				kPixFormat_YUV422_Alpha_Planar, kPixFormat_YUV422_Alpha_Planar16,
				kPixFormat_YUV444_Alpha_Planar, kPixFormat_YUV444_Alpha_Planar16,
				kPixFormat_YUV420_Planar, kPixFormat_YUV420_Planar16,
				kPixFormat_YUV422_Planar, kPixFormat_YUV422_Planar16,
				kPixFormat_YUV444_Planar, kPixFormat_YUV444_Planar16,
				kPixFormat_XRGB8888, kPixFormat_XRGB64,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_R210:
	case kPixFormat_R10K:
	case kPixFormat_B48R:
	case kPixFormat_RGB_Planar16:
		base1 = kPixFormat_XRGB64;
		{
			static int list[] = {
				kPixFormat_RGB_Planar16,
				kPixFormat_RGB_Planar32F,
				kPixFormat_RGB_Planar,
				kPixFormat_XRGB8888,
				kPixFormat_YUV444_Planar16, kPixFormat_YUV444_Planar,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_B64A:
	case kPixFormat_XRGB64:
	case kPixFormat_RGBA_Planar16:
	case kPixFormat_RGBA_Planar32F:
		base1 = kPixFormat_XRGB64;
		{
			static int list[] = {
				kPixFormat_RGBA_Planar16,
				kPixFormat_RGBA_Planar32F,
				kPixFormat_XRGB8888,
				kPixFormat_RGBA_Planar,
				kPixFormat_YUV444_Planar16, kPixFormat_YUV444_Planar,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_RGB565:
	case kPixFormat_RGB888:
	case kPixFormat_RGB_Planar:
	case kPixFormat_RGB_Planar32F:
		base1 = kPixFormat_XRGB8888;
		{
			static int list[] = {
				kPixFormat_RGB_Planar,
				kPixFormat_XRGB64,
				kPixFormat_RGB_Planar16,
				kPixFormat_RGB_Planar32F,
				kPixFormat_YUV444_Planar, kPixFormat_YUV444_Planar16,
				0
			};
			follow_list = list;
		}
		break;

	case kPixFormat_XRGB1555:
	case kPixFormat_XRGB8888:
	case kPixFormat_RGBA_Planar:
		base1 = kPixFormat_XRGB8888;
		{
			static int list[] = {
				kPixFormat_RGBA_Planar,
				kPixFormat_XRGB64,
				kPixFormat_RGBA_Planar16,
				kPixFormat_RGBA_Planar32F,
				kPixFormat_YUV444_Planar, kPixFormat_YUV444_Planar16,
				0
			};
			follow_list = list;
		}
		break;
	}
}

int MatchFilterFormat::next_base() {
	using namespace nsVDPixmap;

	formatMask.reset(base);

	switch(base) {
	case kPixFormat_YUV422_UYVY:
		if (formatMask.test(kPixFormat_YUV422_YUYV))
			return kPixFormat_YUV422_YUYV;
		else
			return kPixFormat_YUV422_Planar;

	case kPixFormat_YUV422_YUYV:
		if (formatMask.test(kPixFormat_YUV422_UYVY))
			return kPixFormat_YUV422_UYVY;
		else
			return kPixFormat_YUV422_Planar;

	case kPixFormat_Y8:
	case kPixFormat_Y8_FR:
		return kPixFormat_YUV444_Planar;
	}

	if (formatMask.test(base1)) return base1;
	return 0;
}

void MatchFilterFormat::initMask() {
	using namespace nsVDPixmap;
	formatMask.set(kPixFormat_XRGB1555);
	formatMask.set(kPixFormat_RGB565);
	formatMask.set(kPixFormat_RGB888);
	formatMask.set(kPixFormat_XRGB8888);
	formatMask.set(kPixFormat_Y8);
	formatMask.set(kPixFormat_Y8_FR);
	formatMask.set(kPixFormat_YUV422_UYVY);
	formatMask.set(kPixFormat_YUV422_YUYV);
	formatMask.set(kPixFormat_YUV444_Planar);
	formatMask.set(kPixFormat_YUV422_Planar);
	formatMask.set(kPixFormat_YUV420_Planar);
	formatMask.set(kPixFormat_YUV420i_Planar);
	formatMask.set(kPixFormat_YUV420it_Planar);
	formatMask.set(kPixFormat_YUV420ib_Planar);
	formatMask.set(kPixFormat_YUV411_Planar);
	formatMask.set(kPixFormat_YUV410_Planar);
	formatMask.set(kPixFormat_YUV422_UYVY_709);
	formatMask.set(kPixFormat_YUV420_NV12);
	formatMask.set(kPixFormat_YUV422_YUYV_709);
	formatMask.set(kPixFormat_YUV444_Planar_709);
	formatMask.set(kPixFormat_YUV422_Planar_709);
	formatMask.set(kPixFormat_YUV420_Planar_709);
	formatMask.set(kPixFormat_YUV420i_Planar_709);
	formatMask.set(kPixFormat_YUV420it_Planar_709);
	formatMask.set(kPixFormat_YUV420ib_Planar_709);
	formatMask.set(kPixFormat_YUV411_Planar_709);
	formatMask.set(kPixFormat_YUV410_Planar_709);
	formatMask.set(kPixFormat_YUV422_UYVY_FR);
	formatMask.set(kPixFormat_YUV422_YUYV_FR);
	formatMask.set(kPixFormat_YUV444_Planar_FR);
	formatMask.set(kPixFormat_YUV422_Planar_FR);
	formatMask.set(kPixFormat_YUV420_Planar_FR);
	formatMask.set(kPixFormat_YUV420i_Planar_FR);
	formatMask.set(kPixFormat_YUV420it_Planar_FR);
	formatMask.set(kPixFormat_YUV420ib_Planar_FR);
	formatMask.set(kPixFormat_YUV411_Planar_FR);
	formatMask.set(kPixFormat_YUV410_Planar_FR);
	formatMask.set(kPixFormat_YUV422_UYVY_709_FR);
	formatMask.set(kPixFormat_YUV422_YUYV_709_FR);
	formatMask.set(kPixFormat_YUV444_Planar_709_FR);
	formatMask.set(kPixFormat_YUV422_Planar_709_FR);
	formatMask.set(kPixFormat_YUV420_Planar_709_FR);
	formatMask.set(kPixFormat_YUV420i_Planar_709_FR);
	formatMask.set(kPixFormat_YUV420it_Planar_709_FR);
	formatMask.set(kPixFormat_YUV420ib_Planar_709_FR);
	formatMask.set(kPixFormat_YUV411_Planar_709_FR);
	formatMask.set(kPixFormat_YUV410_Planar_709_FR);
	formatMask.set(kPixFormat_XRGB64);
	formatMask.set(kPixFormat_YUV444_Planar16);
	formatMask.set(kPixFormat_YUV422_Planar16);
	formatMask.set(kPixFormat_YUV420_Planar16);
	formatMask.set(kPixFormat_Y16);
	formatMask.set(kPixFormat_YUV444_Alpha_Planar);
	formatMask.set(kPixFormat_YUV422_Alpha_Planar);
	formatMask.set(kPixFormat_YUV420_Alpha_Planar);
	formatMask.set(kPixFormat_YUV444_Alpha_Planar16);
	formatMask.set(kPixFormat_YUV422_Alpha_Planar16);
	formatMask.set(kPixFormat_YUV420_Alpha_Planar16);
	formatMask.set(kPixFormat_RGB_Planar);
	formatMask.set(kPixFormat_RGB_Planar16);
	formatMask.set(kPixFormat_RGB_Planar32F);
	formatMask.set(kPixFormat_RGBA_Planar);
	formatMask.set(kPixFormat_RGBA_Planar16);
	formatMask.set(kPixFormat_RGBA_Planar32F);
}

namespace {
	void check() {
		VDASSERTCT(std::size(g_vdPixmapFormats) == nsVDPixmap::kPixFormat_Max_Standard);
	}
}

#ifdef _DEBUG
	bool VDIsValidPixmapPlane(const void *p, ptrdiff_t pitch, vdpixsize w, vdpixsize h) {
		bool isvalid;

		if (pitch < 0)
			isvalid = VDIsValidReadRegion((const char *)p + pitch*(h-1), (-pitch)*(h-1)+w);
		else
			isvalid = VDIsValidReadRegion(p, pitch*(h-1)+w);

		if (!isvalid) {
			VDDEBUG("Kasumi: Invalid pixmap plane detected.\n"
					"        Base=%p, pitch=%d, size=%dx%d (bytes)\n", p, (int)pitch, w, h);
		}

		return isvalid;
	}

	bool VDAssertValidPixmap(const VDPixmap& px) {
		const VDPixmapFormatInfo& info = VDPixmapGetInfo(px.format);

		if (px.format) {
			if (!VDIsValidPixmapPlane(px.data, px.pitch, -(-px.w / info.qw)*info.qsize, -(-px.h >> info.qhbits))) {
				VDDEBUG("Kasumi: Invalid primary plane detected in pixmap.\n"
						"        Pixmap info: format=%d (%s), dimensions=%dx%d\n", px.format, info.name, px.w, px.h);
				VDASSERT(!"Kasumi: Invalid primary plane detected in pixmap.\n");
				return false;
			}

			if (info.palsize)
				if (!VDIsValidReadRegion(px.palette, sizeof(uint32) * info.palsize)) {
					VDDEBUG("Kasumi: Invalid palette detected in pixmap.\n"
							"        Pixmap info: format=%d (%s), dimensions=%dx%d\n", px.format, info.name, px.w, px.h);
					VDASSERT(!"Kasumi: Invalid palette detected in pixmap.\n");
					return false;
				}

			if (info.auxbufs) {
				const vdpixsize auxw = -(-px.w >> info.auxwbits);
				const vdpixsize auxh = -(-px.h >> info.auxhbits);

				if (!VDIsValidPixmapPlane(px.data2, px.pitch2, auxw * info.auxsize, auxh)) {
					VDDEBUG("Kasumi: Invalid Cb plane detected in pixmap.\n"
							"        Pixmap info: format=%d (%s), dimensions=%dx%d\n", px.format, info.name, px.w, px.h);
					VDASSERT(!"Kasumi: Invalid Cb plane detected in pixmap.\n");
					return false;
				}

				if (info.auxbufs >= 2) {
					if (!VDIsValidPixmapPlane(px.data3, px.pitch3, auxw * info.auxsize, auxh)) {
						VDDEBUG("Kasumi: Invalid Cr plane detected in pixmap.\n"
								"        Pixmap info: format=%d, dimensions=%dx%d\n", px.format, px.w, px.h);
						VDASSERT(!"Kasumi: Invalid Cr plane detected in pixmap.\n");
						return false;
					}
				}

				if (info.auxbufs >= 3) {
					if (!VDIsValidPixmapPlane(px.data4, px.pitch4, px.w * info.aux4size, px.h)) {
						VDDEBUG("Kasumi: Invalid Alpha plane detected in pixmap.\n"
								"        Pixmap info: format=%d, dimensions=%dx%d\n", px.format, px.w, px.h);
						VDASSERT(!"Kasumi: Invalid Alpha plane detected in pixmap.\n");
						return false;
					}
				}
			}
		}

		return true;
	}

	bool VDAssertValidPixmapInfo(const VDPixmap& px) {
		switch(px.format){
		case nsVDPixmap::kPixFormat_XRGB64:
		case nsVDPixmap::kPixFormat_YUV444_Planar16:
		case nsVDPixmap::kPixFormat_YUV422_Planar16:
		case nsVDPixmap::kPixFormat_YUV420_Planar16:
		case nsVDPixmap::kPixFormat_YUVA444_Y416:
			if (px.info.ref_r<=0) {
				VDDEBUG("Kasumi: Invalid PixmapInfo detected in pixmap.\n"
						"        Pixmap info: format=%d, ref_r=%d\n", px.format, px.info.ref_r);
				VDASSERT(!"Kasumi: Invalid PixmapInfo detected in pixmap.\n");
				return false;
			}
		}
		return true;
	}

#endif

VDPixmap VDPixmapOffset(const VDPixmap& src, vdpixpos x, vdpixpos y) {
	VDPixmap temp(src);
	const VDPixmapFormatInfo& info = VDPixmapGetInfo(temp.format);

	if (info.qchunky) {
		x = (x + info.qw - 1) / info.qw;
		y >>= info.qhbits;
	}

	switch(info.auxbufs) {
	case 3:
		temp.data4 = (char *)temp.data4 + x*info.aux4size + y*temp.pitch4;
	case 2:
		temp.data3 = (char *)temp.data3 + (x >> info.auxwbits)*info.auxsize + (y >> info.auxhbits)*temp.pitch3;
	case 1:
		temp.data2 = (char *)temp.data2 + (x >> info.auxwbits)*info.auxsize + (y >> info.auxhbits)*temp.pitch2;
	case 0:
		temp.data = (char *)temp.data + x*info.qsize + y*temp.pitch;
	}

	return temp;
}

VDPixmapLayout VDPixmapLayoutOffset(const VDPixmapLayout& src, vdpixpos x, vdpixpos y) {
	VDPixmapLayout temp(src);
	const VDPixmapFormatInfo& info = VDPixmapGetInfo(temp.format);

	if (info.qchunky) {
		x = (x + info.qw - 1) / info.qw;
		y = -(-y >> info.qhbits);
	}

	switch(info.auxbufs) {
	case 3:
		temp.data4 += x*info.aux4size + y*temp.pitch4;
	case 2:
		temp.data3 += -(-x >> info.auxwbits)*info.auxsize + -(-y >> info.auxhbits)*temp.pitch3;
	case 1:
		temp.data2 += -(-x >> info.auxwbits)*info.auxsize + -(-y >> info.auxhbits)*temp.pitch2;
	case 0:
		temp.data += x*info.qsize + y*temp.pitch;
	}

	return temp;
}

uint32 VDPixmapCreateLinearLayout(VDPixmapLayout& layout, VDPixmapFormatEx format, vdpixsize w, vdpixsize h, int alignment) {
	const ptrdiff_t alignmask = alignment - 1;

	const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(format);
	sint32		qw			= (w + srcinfo.qw - 1) / srcinfo.qw;
	sint32		qh			= -(-h >> srcinfo.qhbits);
	sint32		subw		= -(-w >> srcinfo.auxwbits);
	sint32		subh		= -(-h >> srcinfo.auxhbits);
	sint32		auxsize		= srcinfo.auxsize;

	ptrdiff_t	mainpitch	= (srcinfo.qsize * qw + alignmask) & ~alignmask;
	size_t		mainsize	= mainpitch * qh;

	layout.data		= 0;
	layout.pitch	= mainpitch;
	layout.palette	= NULL;
	layout.data2	= 0;
	layout.pitch2	= 0;
	layout.data3	= 0;
	layout.pitch3	= 0;
	layout.data4	= 0;
	layout.pitch4	= 0;
	layout.w		= w;
	layout.h		= h;
	layout.format	= format;
	layout.formatEx	= format;

	if (srcinfo.auxbufs >= 1) {
		ptrdiff_t	subpitch	= (subw * auxsize + alignmask) & ~alignmask;
		size_t		subsize		= subpitch * subh;

		layout.data2	= mainsize;
		layout.pitch2	= subpitch;
		mainsize += subsize;

		if (srcinfo.auxbufs >= 2) {
			layout.data3	= mainsize;
			layout.pitch3	= subpitch;
			mainsize += subsize;
		}
	}

	if (srcinfo.auxbufs >= 3) {
		ptrdiff_t	apitch	= (srcinfo.aux4size * w + alignmask) & ~alignmask;
		size_t		asize	= apitch * h;

		layout.data4	= mainsize;
		layout.pitch4	= apitch;
		mainsize += asize;
	}

	return mainsize;
}

void VDPixmapFlipV(VDPixmap& px) {
	const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(px.format);
	sint32		w			= px.w;
	sint32		h			= px.h;
	sint32		qw			= (w + srcinfo.qw - 1) / srcinfo.qw;
	sint32		qh			= -(-h >> srcinfo.qhbits);
	sint32		subh		= -(-h >> srcinfo.auxhbits);

	vdptrstep(px.data, px.pitch * (qh - 1));
	px.pitch = -px.pitch;

	if (srcinfo.auxbufs >= 1) {
		vdptrstep(px.data2, px.pitch2 * (subh - 1));
		px.pitch2 = -px.pitch2;

		if (srcinfo.auxbufs >= 2) {
			vdptrstep(px.data3, px.pitch3 * (subh - 1));
			px.pitch3 = -px.pitch3;
		}
	}

	if (srcinfo.auxbufs >= 3) {
		vdptrstep(px.data4, px.pitch4 * (h - 1));
		px.pitch4 = -px.pitch4;
	}
}

void VDPixmapLayoutFlipV(VDPixmapLayout& layout) {
	const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(layout.format);
	sint32		w			= layout.w;
	sint32		h			= layout.h;
	sint32		qw			= (w + srcinfo.qw - 1) / srcinfo.qw;
	sint32		qh			= -(-h >> srcinfo.qhbits);
	sint32		subh		= -(-h >> srcinfo.auxhbits);

	layout.data += layout.pitch * (qh - 1);
	layout.pitch = -layout.pitch;

	if (srcinfo.auxbufs >= 1) {
		layout.data2 += layout.pitch2 * (subh - 1);
		layout.pitch2 = -layout.pitch2;

		if (srcinfo.auxbufs >= 2) {
			layout.data3 += layout.pitch3 * (subh - 1);
			layout.pitch3 = -layout.pitch3;
		}
	}

	if (srcinfo.auxbufs >= 3) {
		layout.data4 += layout.pitch4 * (h - 1);
		layout.pitch4 = -layout.pitch4;
	}
}

uint32 VDPixmapLayoutGetMinSize(const VDPixmapLayout& layout) {
	const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(layout.format);
	sint32		w			= layout.w;
	sint32		h			= layout.h;
	sint32		qw			= (w + srcinfo.qw - 1) / srcinfo.qw;
	sint32		qh			= -(-h >> srcinfo.qhbits);
	sint32		subh		= -(-h >> srcinfo.auxhbits);

	uint32 limit = layout.data;
	if (layout.pitch >= 0)
		limit += layout.pitch * qh;
	else
		limit -= layout.pitch;

	if (srcinfo.auxbufs >= 1) {
		uint32 limit2 = layout.data2;

		if (layout.pitch2 >= 0)
			limit2 += layout.pitch2 * subh;
		else
			limit2 -= layout.pitch2;

		if (limit < limit2)
			limit = limit2;

		if (srcinfo.auxbufs >= 2) {
			uint32 limit3 = layout.data3;

			if (layout.pitch3 >= 0)
				limit3 += layout.pitch3 * subh;
			else
				limit3 -= layout.pitch3;

			if (limit < limit3)
				limit = limit3;
		}
	}

	if (srcinfo.auxbufs >= 3) {
		uint32 limit4 = layout.data4;

		if (layout.pitch4 >= 0)
			limit4 += layout.pitch4 * h;
		else
			limit4 -= layout.pitch4;

		if (limit < limit4)
			limit = limit4;
	}

	return limit;
}

VDPixmap VDPixmapExtractField(const VDPixmap& src, bool field2) {
	VDPixmap px(src);

	if (field2) {
		const VDPixmapFormatInfo& info = VDPixmapGetInfo(px.format);

		if (px.data) {
			if (info.qh == 1)
				vdptrstep(px.data, px.pitch);

			if (!info.auxhbits ||
				src.format == nsVDPixmap::kPixFormat_YUV420i_Planar ||
				src.format == nsVDPixmap::kPixFormat_YUV420i_Planar_FR ||
				src.format == nsVDPixmap::kPixFormat_YUV420i_Planar_709 ||
				src.format == nsVDPixmap::kPixFormat_YUV420i_Planar_709_FR) {

				vdptrstep(px.data2, px.pitch2);
				vdptrstep(px.data3, px.pitch3);
			}
		}
	}

	switch(src.format) {
		case nsVDPixmap::kPixFormat_YUV420i_Planar:
			if (field2)
				px.format = nsVDPixmap::kPixFormat_YUV420ib_Planar;
			else
				px.format = nsVDPixmap::kPixFormat_YUV420it_Planar;
			break;

		case nsVDPixmap::kPixFormat_YUV420i_Planar_FR:
			if (field2)
				px.format = nsVDPixmap::kPixFormat_YUV420ib_Planar_FR;
			else
				px.format = nsVDPixmap::kPixFormat_YUV420it_Planar_FR;
			break;

		case nsVDPixmap::kPixFormat_YUV420i_Planar_709:
			if (field2)
				px.format = nsVDPixmap::kPixFormat_YUV420ib_Planar_709;
			else
				px.format = nsVDPixmap::kPixFormat_YUV420it_Planar_709;
			break;

		case nsVDPixmap::kPixFormat_YUV420i_Planar_709_FR:
			if (field2)
				px.format = nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR;
			else
				px.format = nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR;
			break;
	}

	px.h >>= 1;
	px.pitch += px.pitch;
	px.pitch2 += px.pitch2;
	px.pitch3 += px.pitch3;
	px.pitch4 += px.pitch4;
	return px;
}

///////////////////////////////////////////////////////////////////////////

VDPixmapBuffer::VDPixmapBuffer(const VDPixmap& src)
	: mpBuffer(NULL)
	, mLinearSize(0)
{
	assign(src);
}

VDPixmapBuffer::VDPixmapBuffer(const VDPixmapBuffer& src)
	: mpBuffer(NULL)
	, mLinearSize(0)
{
	assign(src);
}

VDPixmapBuffer::VDPixmapBuffer(const VDPixmapLayout& layout) {
	init(layout);
}

VDPixmapBuffer::~VDPixmapBuffer() {
#ifdef _DEBUG
	validate();
#endif

	delete[] mpBuffer;
}

void VDPixmapBuffer::init(sint32 width, sint32 height, int f) {
	const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(f);
	sint32		qw			= (width + srcinfo.qw - 1) / srcinfo.qw;
	sint32		qh			= -(-height >> srcinfo.qhbits);
	sint32		subw		= -(-width >> srcinfo.auxwbits);
	sint32		subh		= -(-height >> srcinfo.auxhbits);
	ptrdiff_t	mainpitch	= (srcinfo.qsize * qw + 15) & ~15;
	ptrdiff_t	subpitch	= (srcinfo.auxsize * subw + 15) & ~15;
	ptrdiff_t	apitch		= (srcinfo.aux4size * width + 15) & ~15;
	uint64		mainsize	= (uint64)mainpitch * qh;
	uint64		subsize		= (uint64)subpitch * subh;
	uint64		asize		= (uint64)apitch * height;
	uint64		totalsize64	= mainsize + 4 * srcinfo.palsize;

	switch (srcinfo.auxbufs) {
	case 3:
		totalsize64 += asize;
	case 2:
		totalsize64 += subsize;
	case 1:
		totalsize64 += subsize;
	}

#ifdef _DEBUG
	totalsize64 += 28;
#endif

	// reject huge allocations
	if (totalsize64 > (uint32)-1 - 4096) {
		throw MyMemoryError();
	}

	size_t totalsize = (uint32)totalsize64;

	if (mLinearSize != totalsize) {
		clear();
		mpBuffer = new_nothrow char[totalsize + 15];
		if (!mpBuffer)
			throw MyMemoryError(totalsize + 15);
		mLinearSize = totalsize;
	}

	char *p = mpBuffer + (-(int)(uintptr)mpBuffer & 15);

#ifdef _DEBUG
	*(uint32 *)p = totalsize;
	for(int i=0; i<12; ++i)
		p[4+i] = (char)(0xa0 + i);

	p += 16;
#endif

	data	= p;
	pitch	= mainpitch;
	p += mainsize;

	palette	= NULL;
	data2	= NULL;
	pitch2	= NULL;
	data3	= NULL;
	pitch3	= NULL;
	data4	= NULL;
	pitch4	= NULL;
	w		= width;
	h		= height;
	format	= f;
	info.clear();

	if (srcinfo.auxbufs >= 1) {
		data2	= p;
		pitch2	= subpitch;
		p += subsize;
	}

	if (srcinfo.auxbufs >= 2) {
		data3	= p;
		pitch3	= subpitch;
		p += subsize;
	}

	if (srcinfo.auxbufs >= 3) {
		data4	= p;
		pitch4	= apitch;
		p += asize;
	}

	if (srcinfo.palsize) {
		palette = (const uint32 *)p;
		p += srcinfo.palsize * 4;
	}

#ifdef _DEBUG
	for(int j=0; j<12; ++j)
		p[j] = (char)(0xb0 + j);
#endif
}

void VDPixmapBuffer::init(const VDPixmapLayout& layout, uint32 additionalPadding) {
	const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(layout.format);
	sint32		qw			= (layout.w + srcinfo.qw - 1) / srcinfo.qw;
	sint32		qh			= -(-layout.h >> srcinfo.qhbits);
	sint32		subw		= -(-layout.w >> srcinfo.auxwbits);
	sint32		subh		= -(-layout.h >> srcinfo.auxhbits);

	sint64 mino=0, maxo=0;

	if (layout.pitch < 0) {
		mino = std::min<sint64>(mino, layout.data + (sint64)layout.pitch * (qh-1));
		maxo = std::max<sint64>(maxo, layout.data - (sint64)layout.pitch);
	} else {
		mino = std::min<sint64>(mino, layout.data);
		maxo = std::max<sint64>(maxo, layout.data + (sint64)layout.pitch*qh);
	}

	if (srcinfo.auxbufs >= 1) {
		if (layout.pitch2 < 0) {
			mino = std::min<sint64>(mino, layout.data2 + (sint64)layout.pitch2 * (subh-1));
			maxo = std::max<sint64>(maxo, layout.data2 - (sint64)layout.pitch2);
		} else {
			mino = std::min<sint64>(mino, layout.data2);
			maxo = std::max<sint64>(maxo, layout.data2 + (sint64)layout.pitch2*subh);
		}

		if (srcinfo.auxbufs >= 2) {
			if (layout.pitch3 < 0) {
				mino = std::min<sint64>(mino, layout.data3 + (sint64)layout.pitch3 * (subh-1));
				maxo = std::max<sint64>(maxo, layout.data3 - (sint64)layout.pitch3);
			} else {
				mino = std::min<sint64>(mino, layout.data3);
				maxo = std::max<sint64>(maxo, layout.data3 + (sint64)layout.pitch3*subh);
			}
		}
	}

	if (srcinfo.auxbufs >= 3) {
		if (layout.pitch4 < 0) {
			mino = std::min<sint64>(mino, layout.data4 + (sint64)layout.pitch4 * (layout.h-1));
			maxo = std::max<sint64>(maxo, layout.data4 - (sint64)layout.pitch4);
		} else {
			mino = std::min<sint64>(mino, layout.data4);
			maxo = std::max<sint64>(maxo, layout.data4 + (sint64)layout.pitch4*layout.h);
		}
	}

	sint64 linsize64 = ((maxo - mino + 3) & ~(uint64)3);

	sint64 totalsize64 = linsize64 + 4*srcinfo.palsize + additionalPadding;

#ifdef _DEBUG
	totalsize64 += 28;
#endif

	// reject huge allocations
	if (totalsize64 > (uint32)-1 - 4096) {
		throw MyMemoryError();
	}

	size_t totalsize = (uint32)totalsize64;
	ptrdiff_t linsize = (uint32)linsize64;

	if (mLinearSize != totalsize) {
		clear();
		mpBuffer = new_nothrow char[totalsize + 15];
		if (!mpBuffer)
			throw MyMemoryError(totalsize + 15);
		mLinearSize = totalsize;
	}

	char *p = mpBuffer + (-(int)(uintptr)mpBuffer & 15);

#ifdef _DEBUG
	*(uint32 *)p = totalsize - 28;
	for(int i=0; i<12; ++i)
		p[4+i] = (char)(0xa0 + i);

	p += 16;
#endif

	w		= layout.w;
	h		= layout.h;
	format	= layout.format;
	info.clear();
	info.colorSpaceMode = layout.formatEx.colorSpaceMode;
	info.colorRangeMode = layout.formatEx.colorRangeMode;
	data	= p + layout.data - mino;
	data2	= p + layout.data2 - mino;
	data3	= p + layout.data3 - mino;
	data4	= p + layout.data4 - mino;
	pitch	= layout.pitch;
	pitch2	= layout.pitch2;
	pitch3	= layout.pitch3;
	pitch4	= layout.pitch4;
	palette	= NULL;

	if (srcinfo.palsize) {
		palette = (const uint32 *)(p + linsize);

		if (layout.palette)
			memcpy((void *)palette, layout.palette, 4*srcinfo.palsize);
	}

#ifdef _DEBUG
	for(int j=0; j<12; ++j)
		p[totalsize + j - 28] = (char)(0xb0 + j);
#endif

	VDAssertValidPixmap(*this);
}

void VDPixmapBuffer::assign(const VDPixmap& src) {
	if (!src.format) {
		delete[] mpBuffer;
		mpBuffer = NULL;
		data = NULL;
		format = 0;
	} else {
		init(src.w, src.h, src.format);
		info = src.info;

		const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(src.format);
		int qw = (src.w + srcinfo.qw - 1) / srcinfo.qw;
		int qh = -(-src.h >> srcinfo.qhbits);
		int subw = -(-src.w >> srcinfo.auxwbits);
		int subh = -(-src.h >> srcinfo.auxhbits);

		if (srcinfo.palsize)
			memcpy((void *)palette, src.palette, 4 * srcinfo.palsize);

		switch(srcinfo.auxbufs) {
		case 3:
			VDMemcpyRect(data4, pitch4, src.data4, src.pitch4, src.w * srcinfo.aux4size, src.h);
		case 2:
			VDMemcpyRect(data3, pitch3, src.data3, src.pitch3, subw, subh);
		case 1:
			VDMemcpyRect(data2, pitch2, src.data2, src.pitch2, subw, subh);
		case 0:
			VDMemcpyRect(data, pitch, src.data, src.pitch, qw * srcinfo.qsize, qh);
		}
	}
}

void VDPixmapBuffer::swap(VDPixmapBuffer& dst) {
	std::swap(mpBuffer, dst.mpBuffer);
	std::swap(mLinearSize, dst.mLinearSize);
	std::swap(static_cast<VDPixmap&>(*this), static_cast<VDPixmap&>(dst));
}

#ifdef _DEBUG
void VDPixmapBuffer::validate() {
	if (mpBuffer) {
		char *p = (char *)(((uintptr)mpBuffer + 15) & ~(uintptr)15);

		// verify head bytes
		for(int i=0; i<12; ++i)
			if (p[i+4] != (char)(0xa0 + i))
				VDASSERT(!"VDPixmapBuffer: Buffer underflow detected.\n");

		// verify tail bytes
		for(int j=0; j<12; ++j)
			if (p[mLinearSize - 12 + j] != (char)(0xb0 + j))
				VDASSERT(!"VDPixmapBuffer: Buffer overflow detected.\n");
	}
}
#endif
