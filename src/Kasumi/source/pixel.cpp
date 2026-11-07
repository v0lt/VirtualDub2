// VirtualDub - Video processing and capture application
// Graphics support library
//
// Copyright (C) 1998-2009 Avery Lee
// Copyright (C) 2015-2018 Anton Shekhovtsov
// Copyright (C) 2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include <stdafx.h>
#include <vd2/system/math.h>
#include <vd2/system/halffloat.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixel.h>
#include <vd2/Kasumi/pixmapUtils.h>

uint32 VDPixmapInterpolateSample8x2To24(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256);
uint32 VDPixmapInterpolateSample8x4To24(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256);
uint32 VDPixmapInterpolateSample8To24(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256);

void VDPixmapSample(const VDPixmap& px, sint32 x, sint32 y, VDSample& ps) {
	if (x >= px.w)
		x = px.w - 1;
	if (y >= px.h)
		y = px.h - 1;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	ps.format = px.format;
	ps.sr = -1;
	ps.sg = -1;
	ps.sb = -1;
	ps.sa = -1;
	ps.r = 0;
	ps.g = 0;
	ps.b = 0;
	ps.a = 0;

	bool valid_rgb = false;
	bool valid_yuv = false;
	bool valid_y = false;
	int yuv_ref = 255;

	switch(px.format) {
	case nsVDPixmap::kPixFormat_RGBA_Planar:
	case nsVDPixmap::kPixFormat_YUV444_Alpha_Planar:
	case nsVDPixmap::kPixFormat_YUV422_Alpha_Planar:
	case nsVDPixmap::kPixFormat_YUV420_Alpha_Planar:
		{
			uint8 a = VDPixmapSample8(px.data4, px.pitch4, x, y);
			ps.sa = a;
			ps.a = float(a);
		}
		break;
	case nsVDPixmap::kPixFormat_RGBA_Planar16:
	case nsVDPixmap::kPixFormat_YUV444_Alpha_Planar16:
	case nsVDPixmap::kPixFormat_YUV422_Alpha_Planar16:
	case nsVDPixmap::kPixFormat_YUV420_Alpha_Planar16:
		{
			uint16 a = VDPixmapSample16U(px.data4, px.pitch4, x, y);
			ps.sa = a;
			if(a>=px.info.ref_a) ps.a=255; else ps.a=float(a*255.0/px.info.ref_a);
		}
		break;
	case nsVDPixmap::kPixFormat_RGBA_Planar32F:
		{
			const float *src3 = (const float*)(size_t(px.data4) + px.pitch4*y + x*4);
			ps.sa = *(int*)src3;
			ps.a = *src3*255;
		}
		break;
	}

	switch(px.format) {
	case nsVDPixmap::kPixFormat_Pal1:
	case nsVDPixmap::kPixFormat_Pal2:
	case nsVDPixmap::kPixFormat_Pal4:
	case nsVDPixmap::kPixFormat_Pal8:
	case nsVDPixmap::kPixFormat_XRGB1555:
	case nsVDPixmap::kPixFormat_RGB565:
		{
			uint32 m = VDPixmapSamplePalRGB(px,x,y);
			ps.sr = (m >> 16) & 0xff;
			ps.sg = (m >> 8) & 0xff;
			ps.sb = m & 0xff;
			ps.sa = m >> 24;
			ps.r = float(ps.sr);
			ps.g = float(ps.sg);
			ps.b = float(ps.sb);
			ps.a = float(ps.sa);
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_RGB888:
		{
			const uint8 *src = (const uint8 *)px.data + px.pitch*y + 3*x;
			ps.sb = src[0];
			ps.sg = src[1];
			ps.sr = src[2];
			ps.r = float(ps.sr);
			ps.g = float(ps.sg);
			ps.b = float(ps.sb);
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_XRGB8888:
		{
			const uint8 *src = (const uint8 *)px.data + px.pitch*y + 4*x;
			ps.sb = src[0];
			ps.sg = src[1];
			ps.sr = src[2];
			ps.sa = src[3];
			ps.r = float(ps.sr);
			ps.g = float(ps.sg);
			ps.b = float(ps.sb);
			ps.a = float(ps.sa);
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_RGBA_Planar:
	case nsVDPixmap::kPixFormat_RGB_Planar:
		{
			const uint8 *src0 = (const uint8 *)px.data  + px.pitch*y  + x;
			const uint8 *src1 = (const uint8 *)px.data2 + px.pitch2*y + x;
			const uint8 *src2 = (const uint8 *)px.data3 + px.pitch3*y + x;
			ps.sr = *src0;
			ps.sg = *src1;
			ps.sb = *src2;
			ps.r = float(ps.sr);
			ps.g = float(ps.sg);
			ps.b = float(ps.sb);
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_RGBA_Planar16:
	case nsVDPixmap::kPixFormat_RGB_Planar16:
		{
			const uint16 *src0 = (const uint16*)(size_t(px.data)  + px.pitch*y  + x*2);
			const uint16 *src1 = (const uint16*)(size_t(px.data2) + px.pitch2*y + x*2);
			const uint16 *src2 = (const uint16*)(size_t(px.data3) + px.pitch3*y + x*2);
			uint32 r = *src0;
			uint32 g = *src1;
			uint32 b = *src2;
			ps.sr = r;
			ps.sg = g;
			ps.sb = b;
			if(r>=px.info.ref_r) ps.r=255; else ps.r=float(r*255.0/px.info.ref_r);
			if(g>=px.info.ref_g) ps.g=255; else ps.g=float(g*255.0/px.info.ref_g);
			if(b>=px.info.ref_b) ps.b=255; else ps.b=float(b*255.0/px.info.ref_b);
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_RGBA_Planar32F:
	case nsVDPixmap::kPixFormat_RGB_Planar32F:
		{
			const float *src0 = (const float*)(size_t(px.data)  + px.pitch*y  + x*4);
			const float *src1 = (const float*)(size_t(px.data2) + px.pitch2*y + x*4);
			const float *src2 = (const float*)(size_t(px.data3) + px.pitch3*y + x*4);
			ps.sr = *(int*)src0;
			ps.sg = *(int*)src1;
			ps.sb = *(int*)src2;
			ps.r = *src0*255;
			ps.g = *src1*255;
			ps.b = *src2*255;
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_XRGB64:
		{
			const uint16* s = (const uint16*)(size_t(px.data) + px.pitch*y + x*8);
			uint32 r = s[2];
			uint32 g = s[1];
			uint32 b = s[0];
			uint32 a = s[3];
			ps.sr = r;
			ps.sg = g;
			ps.sb = b;
			ps.sa = a;
			if(r>=px.info.ref_r) ps.r=255; else ps.r=float(r*255.0/px.info.ref_r);
			if(g>=px.info.ref_g) ps.g=255; else ps.g=float(g*255.0/px.info.ref_g);
			if(b>=px.info.ref_b) ps.b=255; else ps.b=float(b*255.0/px.info.ref_b);
			if(a>=px.info.ref_a) ps.a=255; else ps.a=float(a*255.0/px.info.ref_a);
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_R210:
		{
			uint32 c = *(const uint32*)(size_t(px.data) + px.pitch*y + x*4);
			c = _byteswap_ulong(c);
			ps.sr = (c>>20) & 0x3FF;
			ps.sg = (c>>10) & 0x3FF;
			ps.sb = c & 0x3FF;
			ps.r = float(ps.sr*255.0/1023.0);
			ps.g = float(ps.sg*255.0/1023.0);
			ps.b = float(ps.sb*255.0/1023.0);
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_R10K:
		{
			uint32 c = *(const uint32*)(size_t(px.data) + px.pitch*y + x*4);
			c = _byteswap_ulong(c);
			ps.sr = (c>>22) & 0x3FF;
			ps.sg = (c>>12) & 0x3FF;
			ps.sb = (c>>2) & 0x3FF;
			ps.r = float(ps.sr*255.0/1023.0);
			ps.g = float(ps.sg*255.0/1023.0);
			ps.b = float(ps.sb*255.0/1023.0);
			valid_rgb = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUVA444_Y416:
		{
			yuv_ref = px.info.ref_r;
			const uint16* s = (const uint16*)(size_t(px.data) + px.pitch*y + x*8);
			uint16 a = s[3];
			ps.sy = s[1];
			ps.scb = s[0];
			ps.scr = s[2];
			valid_yuv = true;
			ps.sa = a;
			if(a>=px.info.ref_a) ps.a=255; else ps.a=float(a*255.0/px.info.ref_a);
		}
		break;

	case nsVDPixmap::kPixFormat_YUV444_V410:
		{
			yuv_ref = px.info.colorRangeMode==vd2::kColorRangeMode_Full ? 0x3FF:0x3FC;
			const uint32* s = (const uint32*)(size_t(px.data) + px.pitch*y + x*4);
			ps.scb = (s[0] >> 2)  & 0x3ff;
			ps.sy =  (s[0] >> 12) & 0x3ff;
			ps.scr = (s[0] >> 22) & 0x3ff;
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV444_Y410:
		{
			yuv_ref = px.info.colorRangeMode==vd2::kColorRangeMode_Full ? 0x3FF:0x3FC;
			const uint32* s = (const uint32*)(size_t(px.data) + px.pitch*y + x*4);
			ps.scb = s[0]         & 0x3ff;
			ps.sy =  (s[0] >> 10) & 0x3ff;
			ps.scr = (s[0] >> 20) & 0x3ff;
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV444_V308:
		{
			const uint8* s = (const uint8*)(size_t(px.data) + px.pitch*y + x*3);
			ps.sy = s[1];
			ps.scb = s[2];
			ps.scr = s[0];
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV444_Alpha_Planar16:
	case nsVDPixmap::kPixFormat_YUV444_Planar16:
		{
			yuv_ref = px.info.ref_r;
			ps.sy = VDPixmapSample16U(px.data, px.pitch, x, y);
			ps.scb = VDPixmapSample16U(px.data2, px.pitch2, x, y);
			ps.scr = VDPixmapSample16U(px.data3, px.pitch3, x, y);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV422_Alpha_Planar16:
	case nsVDPixmap::kPixFormat_YUV422_Planar16:
		{
			sint32 u = (x << 7) + 128;
			sint32 v = (y << 8);
			uint32 w2 = px.w >> 1;
			uint32 h2 = px.h;

			yuv_ref = px.info.ref_r;
			ps.sy = VDPixmapSample16U(px.data, px.pitch, x, y);
			ps.scb = VDPixmapInterpolateSample16U(px.data2, px.pitch2, w2, h2, u, v, yuv_ref);
			ps.scr = VDPixmapInterpolateSample16U(px.data3, px.pitch3, w2, h2, u, v, yuv_ref);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV420_Alpha_Planar16:
	case nsVDPixmap::kPixFormat_YUV420_Planar16:
		{
			sint32 u = (x << 7) + 128;
			sint32 v = (y << 7);
			uint32 w2 = px.w >> 1;
			uint32 h2 = px.h >> 1;

			yuv_ref = px.info.ref_r;
			ps.sy = VDPixmapSample16U(px.data, px.pitch, x, y);
			ps.scb = VDPixmapInterpolateSample16U(px.data2, px.pitch2, w2, h2, u, v, yuv_ref);
			ps.scr = VDPixmapInterpolateSample16U(px.data3, px.pitch3, w2, h2, u, v, yuv_ref);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV444_Alpha_Planar:
	case nsVDPixmap::kPixFormat_YUV444_Planar:
	case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV444_Planar_709:
	case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
		{
			ps.sy = VDPixmapSample8(px.data, px.pitch, x, y);
			ps.scb = VDPixmapSample8(px.data2, px.pitch2, x, y);
			ps.scr = VDPixmapSample8(px.data3, px.pitch3, x, y);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV422_Alpha_Planar:
	case nsVDPixmap::kPixFormat_YUV422_Planar:
	case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV422_Planar_709:
	case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
		{
			sint32 u = (x << 7) + 128;
			sint32 v = (y << 8);
			uint32 w2 = px.w >> 1;
			uint32 h2 = px.h;

			ps.sy =	VDPixmapSample8(px.data, px.pitch, x, y);
			ps.scb = VDPixmapInterpolateSample8(px.data2, px.pitch2, w2, h2, u, v);
			ps.scr = VDPixmapInterpolateSample8(px.data3, px.pitch3, w2, h2, u, v);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV420_Alpha_Planar:
	case nsVDPixmap::kPixFormat_YUV420_Planar:
	case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV420_Planar_709:
	case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
		{
			sint32 u = (x << 7) + 128;
			sint32 v = (y << 7);
			uint32 w2 = px.w >> 1;
			uint32 h2 = px.h >> 1;

			ps.sy = VDPixmapSample8(px.data, px.pitch, x, y);
			ps.scb = VDPixmapInterpolateSample8(px.data2, px.pitch2, w2, h2, u, v);
			ps.scr = VDPixmapInterpolateSample8(px.data3, px.pitch3, w2, h2, u, v);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV411_Planar:
	case nsVDPixmap::kPixFormat_YUV411_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV411_Planar_709:
	case nsVDPixmap::kPixFormat_YUV411_Planar_709_FR:
		{
			sint32 u = (x << 6) + 128;
			sint32 v = (y << 8);
			uint32 w2 = px.w >> 2;
			uint32 h2 = px.h;

			ps.sy = VDPixmapSample8(px.data, px.pitch, x, y);
			ps.scb = VDPixmapInterpolateSample8(px.data2, px.pitch2, w2, h2, u, v);
			ps.scr = VDPixmapInterpolateSample8(px.data3, px.pitch3, w2, h2, u, v);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV410_Planar:
	case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
	case nsVDPixmap::kPixFormat_YUV410_Planar_709:
	case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
		{
			sint32 u = (x << 6) + 128;
			sint32 v = (y << 6);
			uint32 w2 = px.w >> 2;
			uint32 h2 = px.h >> 2;

			ps.sy = VDPixmapSample8(px.data, px.pitch, x, y);
			ps.scb = VDPixmapInterpolateSample8(px.data2, px.pitch2, w2, h2, u, v);
			ps.scr = VDPixmapInterpolateSample8(px.data3, px.pitch3, w2, h2, u, v);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV422_UYVY:
	case nsVDPixmap::kPixFormat_YUV422_UYVY_FR:
	case nsVDPixmap::kPixFormat_YUV422_UYVY_709:
	case nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR:
		{
			sint32 x_256 = (x << 8) + 128;
			sint32 y_256 = (y << 8) + 128;
			ps.sy = VDPixmapInterpolateSample8x2To24((const char *)px.data + 1, px.pitch, px.w, px.h, x_256, y_256) >> 16;
			ps.scb = VDPixmapInterpolateSample8x4To24((const char *)px.data + 0, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256) >> 16;
			ps.scr = VDPixmapInterpolateSample8x4To24((const char *)px.data + 2, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256) >> 16;
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV422_YUYV:
	case nsVDPixmap::kPixFormat_YUV422_YUYV_FR:
	case nsVDPixmap::kPixFormat_YUV422_YUYV_709:
	case nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR:
		{
			sint32 x_256 = (x << 8) + 128;
			sint32 y_256 = (y << 8) + 128;
			ps.sy = VDPixmapInterpolateSample8x2To24((const char *)px.data + 0, px.pitch, px.w, px.h, x_256, y_256) >> 16;
			ps.scb = VDPixmapInterpolateSample8x4To24((const char *)px.data + 1, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256) >> 16;
			ps.scr = VDPixmapInterpolateSample8x4To24((const char *)px.data + 3, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256) >> 16;
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV444_XVYU:
		{
			sint32 x_256 = (x << 8) + 128;
			sint32 y_256 = (y << 8) + 128;
			ps.sy = VDPixmapInterpolateSample8x4To24((const char *)px.data + 1, px.pitch, px.w, px.h, x_256, y_256) >> 16;
			ps.scb = VDPixmapInterpolateSample8x4To24((const char *)px.data + 0, px.pitch, px.w, px.h, x_256, y_256) >> 16;
			ps.scr = VDPixmapInterpolateSample8x4To24((const char *)px.data + 2, px.pitch, px.w, px.h, x_256, y_256) >> 16;
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV420_NV12:
		{
			sint32 x_256 = (x << 8) + 128;
			sint32 y_256 = (y << 8) + 128;
			ps.sy = VDPixmapInterpolateSample8To24(px.data, px.pitch, px.w, px.h, x_256, y_256) >> 16;
			ps.scb = VDPixmapInterpolateSample8x2To24((const char *)px.data2 + 0, px.pitch2, (px.w + 1) >> 1, (px.h + 1) >> 1, (x_256 >> 1) + 128, y_256 >> 1) >> 16;
			ps.scr = VDPixmapInterpolateSample8x2To24((const char *)px.data2 + 1, px.pitch2, (px.w + 1) >> 1, (px.h + 1) >> 1, (x_256 >> 1) + 128, y_256 >> 1) >> 16;
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV422_P216:
	case nsVDPixmap::kPixFormat_YUV422_P210:
		{
			sint32 u = (x << 7) + 128;
			sint32 v = (y << 8);
			uint32 w2 = px.w >> 1;
			uint32 h2 = px.h;

			yuv_ref = px.info.ref_r;
			ps.sy = VDPixmapSample16U(px.data, px.pitch, x, y);
			ps.scb = VDPixmapInterpolateSample2x16U(px.data2, px.pitch2, w2, h2, u, v, yuv_ref);
			ps.scr = VDPixmapInterpolateSample2x16U(((char*)px.data2)+2, px.pitch2, w2, h2, u, v, yuv_ref);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_YUV420_P016:
	case nsVDPixmap::kPixFormat_YUV420_P010:
		{
			sint32 u = (x << 7) + 128;
			sint32 v = (y << 7);
			uint32 w2 = px.w >> 1;
			uint32 h2 = px.h >> 1;

			yuv_ref = px.info.ref_r;
			ps.sy = VDPixmapSample16U(px.data, px.pitch, x, y);
			ps.scb = VDPixmapInterpolateSample2x16U(px.data2, px.pitch2, w2, h2, u, v, yuv_ref);
			ps.scr = VDPixmapInterpolateSample2x16U(((char*)px.data2)+2, px.pitch2, w2, h2, u, v, yuv_ref);
			valid_yuv = true;
		}
		break;

	case nsVDPixmap::kPixFormat_Y16:
		{
			yuv_ref = px.info.ref_r;
			ps.sy = VDPixmapSample16U(px.data, px.pitch, x, y);
			int n0 = vd2::chroma_neutral(yuv_ref);
			ps.scb = n0;
			ps.scr = n0;
			valid_y = true;
		}
		break;

	case nsVDPixmap::kPixFormat_Y8:
	case nsVDPixmap::kPixFormat_Y8_FR:
		{
			uint8 luma = ((const uint8 *)px.data + px.pitch*y)[x];
			ps.sy = luma;
			ps.scb = 0x80;
			ps.scr = 0x80;
			valid_y = true;
		}
		break;

	default:
		{
			uint32 m = VDPixmapInterpolateSampleRGB24(px, (x << 8) + 128, (y << 8) + 128);
			int r = (m >> 16) & 0xff;
			int g = (m >> 8) & 0xff;
			int b = m & 0xff;
			int a = m >> 24;
			ps.r = float(r);
			ps.g = float(g);
			ps.b = float(b);
			ps.a = float(a);
		}
	}

	if (valid_yuv || valid_y) {
		VDPixmapFormatEx f = VDPixmapFormatCombine(px);
		VDConvertYCbCrToRGB(ps.sy,ps.scb,ps.scr,yuv_ref,f,ps.r,ps.g,ps.b);
		ps.r*=255; ps.g*=255; ps.b*=255;
	}

	if(!px.info.alpha_type){ ps.sa=-1; ps.a=0; }
	if(!valid_rgb){ ps.sr=-1; ps.sg=-1; ps.sb=-1; }
	if(!valid_yuv){ ps.scb=-1; ps.scr=-1; }
	if(!valid_y && !valid_yuv) ps.sy=-1;
}

uint32 VDPixmapSamplePalRGB(const VDPixmap& px, sint32 x, sint32 y) {
	switch(px.format) {
	case nsVDPixmap::kPixFormat_Pal1:
		{
			uint8 idx = ((const uint8 *)px.data + px.pitch*y)[x >> 3];

			return px.palette[(idx >> (7 - (x & 7))) & 1];
		}

	case nsVDPixmap::kPixFormat_Pal2:
		{
			uint8 idx = ((const uint8 *)px.data + px.pitch*y)[x >> 2];

			return px.palette[(idx >> (6 - (x & 3)*2)) & 3];
		}

	case nsVDPixmap::kPixFormat_Pal4:
		{
			uint8 idx = ((const uint8 *)px.data + px.pitch*y)[x >> 1];

			if (!(x & 1))
				idx >>= 4;

			return px.palette[idx & 15];
		}

	case nsVDPixmap::kPixFormat_Pal8:
		{
			uint8 idx = ((const uint8 *)px.data + px.pitch*y)[x];

			return px.palette[idx];
		}

	case nsVDPixmap::kPixFormat_XRGB1555:
		{
			uint16 c = ((const uint16 *)((const uint8 *)px.data + px.pitch*y))[x];
			uint32 r = c & 0x7c00;
			uint32 g = c & 0x03e0;
			uint32 b = c & 0x001f;
			uint32 rgb = (r << 9) + (g << 6) + (b << 3);

			return rgb + ((rgb >> 5) & 0x070707);
		}
		break;

	case nsVDPixmap::kPixFormat_RGB565:
		{
			uint16 c = ((const uint16 *)((const uint8 *)px.data + px.pitch*y))[x];
			uint32 r = c & 0xf800;
			uint32 g = c & 0x07e0;
			uint32 b = c & 0x001f;
			uint32 rb = (r << 8) + (b << 3);

			return rb + ((rb >> 5) & 0x070007) + (g << 5) + ((g >> 1) & 0x0300);
		}
		break;
	}

	return 0;
}

uint32 VDPixmapSample(const VDPixmap& px, sint32 x, sint32 y) {
  VDSample ps;
  VDPixmapSample(px,x,y,ps);
	return VDPackRGBA(ps.r/255, ps.g/255, ps.b/255, ps.a/255);
}


uint8 VDPixmapInterpolateSample8(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256) {
	// bias coordinates to integer
	x_256 -= 128;
	y_256 -= 128;

	// clamp coordinates
	x_256 &= ~(x_256 >> 31);
	y_256 &= ~(y_256 >> 31);

	uint32 w_256 = (w - 1) << 8;
	uint32 h_256 = (h - 1) << 8;
	x_256 ^= (x_256 ^ w_256) & ((x_256 - w_256) >> 31);
	y_256 ^= (y_256 ^ h_256) & ((y_256 - h_256) >> 31);

	const uint8 *row0 = (const uint8 *)data + pitch * (y_256 >> 8) + (x_256 >> 8);
	const uint8 *row1 = row0;

	if ((uint32)y_256 < h_256)
		row1 += pitch;

	ptrdiff_t xstep = (uint32)x_256 < w_256 ? 1 : 0;
	sint32 xoffset = x_256 & 255;
	sint32 yoffset = y_256 & 255;
	sint32 p00 = row0[0];
	sint32 p10 = row0[xstep];
	sint32 p01 = row1[0];
	sint32 p11 = row1[xstep];
	sint32 p0 = (p00 << 8) + (p10 - p00)*xoffset;
	sint32 p1 = (p01 << 8) + (p11 - p01)*xoffset;
	sint32 p = ((p0 << 8) + (p1 - p0)*yoffset + 0x8000) >> 16;

	return (uint8)p;
}

uint16 VDPixmapInterpolateSample16U(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256, uint32 ref, int xx) {
	// bias coordinates to integer
	x_256 -= 128;
	y_256 -= 128;

	// clamp coordinates
	x_256 &= ~(x_256 >> 31);
	y_256 &= ~(y_256 >> 31);

	uint32 w_256 = (w - 1) << 8;
	uint32 h_256 = (h - 1) << 8;
	x_256 ^= (x_256 ^ w_256) & ((x_256 - w_256) >> 31);
	y_256 ^= (y_256 ^ h_256) & ((y_256 - h_256) >> 31);

	const uint16 *row0 = (const uint16 *)data + pitch/2 * (y_256 >> 8) + (x_256 >> 8) * xx;
	const uint16 *row1 = row0;

	if ((uint32)y_256 < h_256)
		row1 += pitch/2;

	ptrdiff_t xstep = (uint32)x_256 < w_256 ? xx : 0;
	sint32 xoffset = x_256 & 255;
	sint32 yoffset = y_256 & 255;
	sint32 p00 = row0[0];
	sint32 p10 = row0[xstep];
	sint32 p01 = row1[0];
	sint32 p11 = row1[xstep];
	sint32 p0 = ((p00 << 8) + (p10 - p00)*xoffset) >> 8;
	sint32 p1 = ((p01 << 8) + (p11 - p01)*xoffset) >> 8;
	sint32 p = ((p0 << 8) + (p1 - p0)*yoffset + (ref+1)/0x200) >> 8;

	return p;
}

uint16 VDPixmapInterpolateSample2x16U(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256, uint32 ref) {
  return VDPixmapInterpolateSample16U(data,pitch,w,h,x_256,y_256,ref,2);
}

uint8 VDPixmapInterpolateSample16(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256, uint32 ref) {
	uint16 v = VDPixmapInterpolateSample16U(data,pitch,w,h,x_256,y_256,ref);
	if (v>ref) return 255;
	return (uint8)((v*0xFF0/ref+8)>>4);
}

uint8 VDPixmapInterpolateSample2x16(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256, uint32 ref) {
	uint16 v = VDPixmapInterpolateSample16U(data,pitch,w,h,x_256,y_256,ref,2);
	if (v>ref) return 255;
	return (uint8)((v*0xFF0/ref+8)>>4);
}

uint32 VDPixmapInterpolateSample8To24(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256) {
	// bias coordinates to integer
	x_256 -= 128;
	y_256 -= 128;

	// clamp coordinates
	x_256 &= ~(x_256 >> 31);
	y_256 &= ~(y_256 >> 31);

	sint32 w_256 = (w - 1) << 8;
	sint32 h_256 = (h - 1) << 8;
	x_256 += (w_256 - x_256) & ((w_256 - x_256) >> 31);
	y_256 += (h_256 - y_256) & ((h_256 - y_256) >> 31);

	const uint8 *row0 = (const uint8 *)data + pitch * (y_256 >> 8) + (x_256 >> 8);
	const uint8 *row1 = row0;

	if (y_256 < h_256)
		row1 += pitch;

	ptrdiff_t xstep = x_256 < w_256 ? 1 : 0;
	sint32 xoffset = x_256 & 255;
	sint32 yoffset = y_256 & 255;
	sint32 p00 = row0[0];
	sint32 p10 = row0[xstep];
	sint32 p01 = row1[0];
	sint32 p11 = row1[xstep];
	sint32 p0 = (p00 << 8) + (p10 - p00)*xoffset;
	sint32 p1 = (p01 << 8) + (p11 - p01)*xoffset;
	sint32 p = (p0 << 8) + (p1 - p0)*yoffset;

	return p;
}

uint32 VDPixmapInterpolateSample8x2To24(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256) {
	// bias coordinates to integer
	x_256 -= 128;
	y_256 -= 128;

	// clamp coordinates
	x_256 &= ~(x_256 >> 31);
	y_256 &= ~(y_256 >> 31);

	uint32 w_256 = (w - 1) << 8;
	uint32 h_256 = (h - 1) << 8;
	x_256 ^= (x_256 ^ w_256) & ((x_256 - w_256) >> 31);
	y_256 ^= (y_256 ^ h_256) & ((y_256 - h_256) >> 31);

	const uint8 *row0 = (const uint8 *)data + pitch * (y_256 >> 8) + (x_256 >> 8)*2;
	const uint8 *row1 = row0;

	if ((uint32)y_256 < h_256)
		row1 += pitch;

	ptrdiff_t xstep = (uint32)x_256 < w_256 ? 2 : 0;
	sint32 xoffset = x_256 & 255;
	sint32 yoffset = y_256 & 255;
	sint32 p00 = row0[0];
	sint32 p10 = row0[xstep];
	sint32 p01 = row1[0];
	sint32 p11 = row1[xstep];
	sint32 p0 = (p00 << 8) + (p10 - p00)*xoffset;
	sint32 p1 = (p01 << 8) + (p11 - p01)*xoffset;
	sint32 p = (p0 << 8) + (p1 - p0)*yoffset;

	return p;
}

uint32 VDPixmapInterpolateSample8x4To24(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256) {
	// bias coordinates to integer
	x_256 -= 128;
	y_256 -= 128;

	// clamp coordinates
	x_256 &= ~(x_256 >> 31);
	y_256 &= ~(y_256 >> 31);

	uint32 w_256 = (w - 1) << 8;
	uint32 h_256 = (h - 1) << 8;
	x_256 ^= (x_256 ^ w_256) & ((x_256 - w_256) >> 31);
	y_256 ^= (y_256 ^ h_256) & ((y_256 - h_256) >> 31);

	const uint8 *row0 = (const uint8 *)data + pitch * (y_256 >> 8) + (x_256 >> 8)*4;
	const uint8 *row1 = row0;

	if ((uint32)y_256 < h_256)
		row1 += pitch;

	ptrdiff_t xstep = (uint32)x_256 < w_256 ? 4 : 0;
	sint32 xoffset = x_256 & 255;
	sint32 yoffset = y_256 & 255;
	sint32 p00 = row0[0];
	sint32 p10 = row0[xstep];
	sint32 p01 = row1[0];
	sint32 p11 = row1[xstep];
	sint32 p0 = (p00 << 8) + (p10 - p00)*xoffset;
	sint32 p1 = (p01 << 8) + (p11 - p01)*xoffset;
	sint32 p = (p0 << 8) + (p1 - p0)*yoffset;

	return p;
}

float VDPixmapInterpolateSample16F(const void *data, ptrdiff_t pitch, uint32 w, uint32 h, sint32 x_256, sint32 y_256) {
	// bias coordinates to integer
	x_256 -= 128;
	y_256 -= 128;

	// clamp coordinates
	x_256 &= ~(x_256 >> 31);
	y_256 &= ~(y_256 >> 31);

	uint32 w_256 = (w - 1) << 8;
	uint32 h_256 = (h - 1) << 8;
	x_256 ^= (x_256 ^ w_256) & ((x_256 - w_256) >> 31);
	y_256 ^= (y_256 ^ h_256) & ((y_256 - h_256) >> 31);

	const uint16 *row0 = (const uint16 *)((const uint8 *)data + pitch * (y_256 >> 8) + (x_256 >> 8)*2);
	const uint16 *row1 = row0;

	if ((uint32)y_256 < h_256)
		row1 = (const uint16 *)((const char *)row1 + pitch);

	ptrdiff_t xstep = (uint32)x_256 < w_256 ? 1 : 0;
	float xoffset = (float)(x_256 & 255) * (1.0f / 255.0f);
	float yoffset = (float)(y_256 & 255) * (1.0f / 255.0f);

	float p00;
	float p10;
	float p01;
	float p11;
	VDConvertHalfToFloat(row0[0], &p00);
	VDConvertHalfToFloat(row0[xstep], &p10);
	VDConvertHalfToFloat(row1[0], &p01);
	VDConvertHalfToFloat(row1[xstep], &p11);

	float p0 = p00 + (p10 - p00)*xoffset;
	float p1 = p01 + (p11 - p01)*xoffset;

	return p0 + (p1 - p0)*yoffset;
}

namespace {
	uint32 Lerp8888(uint32 p0, uint32 p1, uint32 p2, uint32 p3, uint32 xf, uint32 yf) {
		uint32 rb0 = p0 & 0x00ff00ff;
		uint32 ag0 = p0 & 0xff00ff00;
		uint32 rb1 = p1 & 0x00ff00ff;
		uint32 ag1 = p1 & 0xff00ff00;
		uint32 rb2 = p2 & 0x00ff00ff;
		uint32 ag2 = p2 & 0xff00ff00;
		uint32 rb3 = p3 & 0x00ff00ff;
		uint32 ag3 = p3 & 0xff00ff00;

		uint32 rbt = (rb0 + (((       rb1 - rb0       )*xf + 0x00800080) >> 8)) & 0x00ff00ff;
		uint32 agt = (ag0 + ((((ag1 >> 8) - (ag0 >> 8))*xf + 0x00800080)     )) & 0xff00ff00;
		uint32 rbb = (rb2 + (((       rb3 - rb2       )*xf + 0x00800080) >> 8)) & 0x00ff00ff;
		uint32 agb = (ag2 + ((((ag3 >> 8) - (ag2 >> 8))*xf + 0x00800080)     )) & 0xff00ff00;
		uint32 rb  = (rbt + (((       rbb - rbt       )*yf + 0x00800080) >> 8)) & 0x00ff00ff;
		uint32 ag  = (agt + ((((agb >> 8) - (agt >> 8))*yf + 0x00800080)     )) & 0xff00ff00;

		return rb + ag;
	}

	uint32 InterpPlanarY8(const VDPixmap& px, sint32 x1, sint32 y1) {
		sint32 y = VDPixmapInterpolateSample8To24(px.data, px.pitch, px.w, px.h, x1, y1);

		return VDClampedRoundFixedToUint8Fast((float)(y-0x100000) * (1.1643836f/65536.0f/255.0f))*0x010101;
	}

	uint32 ConvertYCC72ToRGB24(sint32 iy, sint32 icb, sint32 icr) {
		float y  = (float)iy;
		float cb = (float)icb;
		float cr = (float)icr;

		//	!   1.1643836  - 5.599D-17    1.5960268  - 222.92157 !
		//	!   1.1643836  - 0.3917623  - 0.8129676    135.57529 !
		//	!   1.1643836    2.0172321  - 1.110D-16  - 276.83585 !
		uint32 ir = VDClampedRoundFixedToUint8Fast((1.1643836f/65536.0f/255.0f)*y + (1.5960268f/65536.0f/255.0f)*cr - (222.92157f / 255.0f));
		uint32 ig = VDClampedRoundFixedToUint8Fast((1.1643836f/65536.0f/255.0f)*y - (0.3917623f/65536.0f/255.0f)*cb - (0.8129676f/65536.0f/255.0f)*cr + (135.57529f / 255.0f));
		uint32 ib = VDClampedRoundFixedToUint8Fast((1.1643836f/65536.0f/255.0f)*y + (2.0172321f/65536.0f/255.0f)*cb - (276.83585f / 255.0f));

		return (ir << 16) + (ig << 8) + ib;
	}

	void ConvertYCC72ToRGB(float y, float cb, float cr, float& r, float& g, float& b) {
		//	!   1.1643836  - 5.599D-17    1.5960268  - 222.92157 !
		//	!   1.1643836  - 0.3917623  - 0.8129676    135.57529 !
		//	!   1.1643836    2.0172321  - 1.110D-16  - 276.83585 !
		r = 1.1643836f*y + 1.5960268f*cr - (222.92157f / 255.0f);
		g = 1.1643836f*y - 0.3917623f*cb - 0.8129676f*cr + (135.57529f / 255.0f);
		b = 1.1643836f*y + 2.0172321f*cb - (276.83585f / 255.0f);

		/*
		const float kCoeffCr[3] = { 1.596f, -0.813f, 0 };
		const float kCoeffCb[3] = { 0, -0.391f, 2.018f };
		const float kCoeffY = 1.164f;
		const float kBiasY = -16.0f / 255.0f;
		const float kBiasC = -128.0f / 255.0f;

		float y2 = y * kCoeffY;
		float r2 = y2 + kCoeffCr[0]*cr + kCoeffCb[0]*cb + kCoeffY * kBiasY + (kCoeffCr[0] + kCoeffCb[0]) * kBiasC;
		float g2 = y2 + kCoeffCr[1]*cr + kCoeffCb[1]*cb + kCoeffY * kBiasY + (kCoeffCr[1] + kCoeffCb[1]) * kBiasC;
		float b2 = y2 + kCoeffCr[2]*cr + kCoeffCb[2]*cb + kCoeffY * kBiasY + (kCoeffCr[2] + kCoeffCb[2]) * kBiasC;
		*/
	}

	uint32 ConvertYCC72ToRGB24_FR(sint32 iy, sint32 icb, sint32 icr) {
		float y  = (float)iy;
		float cb = (float)icb;
		float cr = (float)icr;

		//	1.    0.           1.402      - 179.456    
		//	1.  - 0.3441363  - 0.7141363    135.45889 
		//	1.    1.772      - 2.220D-16  - 226.816    
		uint32 ir = VDClampedRoundFixedToUint8Fast((1.0f/65536.0f/255.0f)*y + (1.4020000f/65536.0f/255.0f)*cr - (179.456f / 255.0f));
		uint32 ig = VDClampedRoundFixedToUint8Fast((1.0f/65536.0f/255.0f)*y - (0.3441363f/65536.0f/255.0f)*cb - (0.7141363f/65536.0f/255.0f)*cr + (135.45889f / 255.0f));
		uint32 ib = VDClampedRoundFixedToUint8Fast((1.0f/65536.0f/255.0f)*y + (1.7720000f/65536.0f/255.0f)*cb - (226.816f / 255.0f));

		return (ir << 16) + (ig << 8) + ib;
	}

	void ConvertYCC72ToRGB_FR(float y, float cb, float cr, float& r, float& g, float& b) {
		//	1.    0.           1.402      - 179.456    
		//	1.  - 0.3441363  - 0.7141363    135.45889 
		//	1.    1.772      - 2.220D-16  - 226.816    
		r = y + 1.4020000f*cr - (179.456f / 255.0f);
		g = y - 0.3441363f*cb - 0.7141363f*cr + (135.45889f / 255.0f);
		b = y + 1.7720000f*cb - (226.816f / 255.0f);

		/*
		const float kCoeffCr[3] = { 1.402f, -0.7141363f, 0 };
		const float kCoeffCb[3] = { 0, -0.3441363f, 1.772f };
		const float kCoeffY = 1.0f;
		const float kBiasY = 0.0f;
		const float kBiasC = -128.0f / 255.0f;

		float y2 = y * kCoeffY;
		float r2 = y2 + kCoeffCr[0]*cr + kCoeffCb[0]*cb + kCoeffY * kBiasY + (kCoeffCr[0] + kCoeffCb[0]) * kBiasC;
		float g2 = y2 + kCoeffCr[1]*cr + kCoeffCb[1]*cb + kCoeffY * kBiasY + (kCoeffCr[1] + kCoeffCb[1]) * kBiasC;
		float b2 = y2 + kCoeffCr[2]*cr + kCoeffCb[2]*cb + kCoeffY * kBiasY + (kCoeffCr[2] + kCoeffCb[2]) * kBiasC;
		*/
	}

	uint32 ConvertYCC72ToRGB24_709(sint32 iy, sint32 icb, sint32 icr) {
		float y  = (float)iy;
		float cb = (float)icb;
		float cr = (float)icr;

		//	!   1.1643836  - 2.932D-17    1.7927411  - 248.10099 !
		//	!   1.1643836  - 0.2132486  - 0.5329093    76.87808  !
		//	!   1.1643836    2.1124018  - 5.551D-17  - 289.01757 !
		uint32 ir = VDClampedRoundFixedToUint8Fast((1.1643836f/65536.0f/255.0f)*y + (1.7927411f/65536.0f/255.0f)*cr - (248.10099f / 255.0f));
		uint32 ig = VDClampedRoundFixedToUint8Fast((1.1643836f/65536.0f/255.0f)*y - (0.2132486f/65536.0f/255.0f)*cb - (0.5329093f/65536.0f/255.0f)*cr + (76.87808f / 255.0f));
		uint32 ib = VDClampedRoundFixedToUint8Fast((1.1643836f/65536.0f/255.0f)*y + (2.1124018f/65536.0f/255.0f)*cb - (289.01757f / 255.0f));

		return (ir << 16) + (ig << 8) + ib;
	}

	void ConvertYCC72ToRGB_709(float y, float cb, float cr, float& r, float& g, float& b) {
		//	!   1.1643836  - 2.932D-17    1.7927411  - 248.10099 !
		//	!   1.1643836  - 0.2132486  - 0.5329093    76.87808  !
		//	!   1.1643836    2.1124018  - 5.551D-17  - 289.01757 !
		r = 1.1643836f*y + 1.7927411f*cr - (248.10099f / 255.0f);
		g = 1.1643836f*y - 0.2132486f*cb - 0.5329093f*cr + (76.87808f / 255.0f);
		b = 1.1643836f*y + 2.1124018f*cb - (289.01757f / 255.0f);
	}

	uint32 ConvertYCC72ToRGB24_709_FR(sint32 iy, sint32 icb, sint32 icr) {
		float y  = (float)iy;
		float cb = (float)icb;
		float cr = (float)icr;

		//	    1.    0.           1.5748     - 201.5744   
		//	    1.  - 0.1873243  - 0.4681243    83.897414  
		//	    1.    1.8556       0.         - 237.5168   
		uint32 ir = VDClampedRoundFixedToUint8Fast((1.0f/65536.0f/255.0f)*y + (1.5748f/65536.0f/255.0f)*cr - (201.5744f / 255.0f));
		uint32 ig = VDClampedRoundFixedToUint8Fast((1.0f/65536.0f/255.0f)*y - (0.1873243f/65536.0f/255.0f)*cb - (0.4681243f/65536.0f/255.0f)*cr + (83.897414f / 255.0f));
		uint32 ib = VDClampedRoundFixedToUint8Fast((1.0f/65536.0f/255.0f)*y + (1.8556f/65536.0f/255.0f)*cb - (237.5168f / 255.0f));

		return (ir << 16) + (ig << 8) + ib;
	}

	void ConvertYCC72ToRGB_709_FR(float y, float cb, float cr, float& r, float& g, float& b) {
		//	    1.    0.           1.5748     - 201.5744   
		//	    1.  - 0.1873243  - 0.4681243    83.897414  
		//	    1.    1.8556       0.         - 237.5168   
		r = 1.0f*y + 1.5748f*cr - (201.5744f / 255.0f);
		g = 1.0f*y - 0.1873243f*cb - 0.4681243f*cr + (83.897414f / 255.0f);
		b = 1.0f*y + 1.8556f*cb - (237.5168f / 255.0f);
	}

	uint32 InterpPlanarYCC888(const VDPixmap& px, sint32 x1, sint32 y1, sint32 x23, sint32 y23, uint32 w23, uint32 h23) {
		sint32 y  = VDPixmapInterpolateSample8To24(px.data, px.pitch, px.w, px.h, x1, y1);
		sint32 cb = VDPixmapInterpolateSample8To24(px.data2, px.pitch2, w23, h23, x23, y23);
		sint32 cr = VDPixmapInterpolateSample8To24(px.data3, px.pitch3, w23, h23, x23, y23);

		return ConvertYCC72ToRGB24(y, cb, cr);
	}

	uint32 InterpPlanarYCC888_709(const VDPixmap& px, sint32 x1, sint32 y1, sint32 x23, sint32 y23, uint32 w23, uint32 h23) {
		sint32 y  = VDPixmapInterpolateSample8To24(px.data, px.pitch, px.w, px.h, x1, y1);
		sint32 cb = VDPixmapInterpolateSample8To24(px.data2, px.pitch2, w23, h23, x23, y23);
		sint32 cr = VDPixmapInterpolateSample8To24(px.data3, px.pitch3, w23, h23, x23, y23);

		return ConvertYCC72ToRGB24_709(y, cb, cr);
	}

	uint32 InterpPlanarYCC888_FR(const VDPixmap& px, sint32 x1, sint32 y1, sint32 x23, sint32 y23, uint32 w23, uint32 h23) {
		sint32 y  = VDPixmapInterpolateSample8To24(px.data, px.pitch, px.w, px.h, x1, y1);
		sint32 cb = VDPixmapInterpolateSample8To24(px.data2, px.pitch2, w23, h23, x23, y23);
		sint32 cr = VDPixmapInterpolateSample8To24(px.data3, px.pitch3, w23, h23, x23, y23);

		return ConvertYCC72ToRGB24_FR(y, cb, cr);
	}

	uint32 InterpPlanarYCC888_709_FR(const VDPixmap& px, sint32 x1, sint32 y1, sint32 x23, sint32 y23, uint32 w23, uint32 h23) {
		sint32 y  = VDPixmapInterpolateSample8To24(px.data, px.pitch, px.w, px.h, x1, y1);
		sint32 cb = VDPixmapInterpolateSample8To24(px.data2, px.pitch2, w23, h23, x23, y23);
		sint32 cr = VDPixmapInterpolateSample8To24(px.data3, px.pitch3, w23, h23, x23, y23);

		return ConvertYCC72ToRGB24_709_FR(y, cb, cr);
	}

	template<uint32 (*ConvFn)(sint32, sint32, sint32)>
	uint32 InterpPlanarYCC888_420i(const VDPixmap& px, sint32 x1, sint32 y1) {
		sint32 y  = VDPixmapInterpolateSample8To24(px.data, px.pitch, px.w, px.h, x1, y1);
		sint32 cb;
		sint32 cr;

		const uint8 *src2 = (const uint8 *)px.data2;
		const uint8 *src3 = (const uint8 *)px.data3;
		const ptrdiff_t pitch2 = px.pitch2 + px.pitch2;
		const ptrdiff_t pitch3 = px.pitch3 + px.pitch3;
		const uint32 w23 = (px.w + 1) >> 1;
		const uint32 h23 = (px.h + 1) >> 1;
		const sint32 xc = (x1 >> 1) + 64;
		sint32 yc = (y1 >> 1) + 64;

		if (y1 & 1) {
			yc -= 256;
			cb = VDPixmapInterpolateSample8To24(src2, pitch2, w23, h23 >> 1, xc, yc);
			cr = VDPixmapInterpolateSample8To24(src3, pitch3, w23, h23 >> 1, xc, yc);
		} else {
			cb = VDPixmapInterpolateSample8To24(src2 + px.pitch2, pitch2, w23, (h23 + 1) >> 1, xc, yc);
			cr = VDPixmapInterpolateSample8To24(src3 + px.pitch3, pitch3, w23, (h23 + 1) >> 1, xc, yc);
		}

		return ConvFn(y, cb, cr);
	}

	uint32 SampleV210_Y(const void *src, ptrdiff_t srcpitch, sint32 x, sint32 y, uint32 w, uint32 h) {
		if (x < 0)
			x = 0;
		if ((uint32)x >= w)
			x = w - 1;
		if (y < 0)
			y = 0;
		if ((uint32)y >= h)
			y = h - 1;

		const uint32 *p = (const uint32 *)((const char *)src + srcpitch*y) + (x / 6)*4;

		switch((uint32)x % 6) {
			default:
			case 0:	return (p[0] >> 10) & 0x3ff;
			case 1:	return (p[1] >>  0) & 0x3ff;
			case 2:	return (p[1] >> 20) & 0x3ff;
			case 3:	return (p[2] >> 10) & 0x3ff;
			case 4:	return (p[3] >>  0) & 0x3ff;
			case 5:	return (p[3] >> 20) & 0x3ff;
		}
	}

	uint32 SampleV210_Cb(const void *src, ptrdiff_t srcpitch, sint32 x, sint32 y, uint32 w, uint32 h) {
		if (x < 0)
			x = 0;
		if ((uint32)x >= w)
			x = w - 1;
		if (y < 0)
			y = 0;
		if ((uint32)y >= h)
			y = h - 1;

		const uint32 *p = (const uint32 *)((const char *)src + srcpitch*y) + (x / 3)*4;

		switch((uint32)x % 3) {
			default:
			case 0:	return (p[0] >>  0) & 0x3ff;
			case 1:	return (p[1] >> 10) & 0x3ff;
			case 2:	return (p[2] >> 20) & 0x3ff;
		}
	}

	uint32 SampleV210_Cr(const void *src, ptrdiff_t srcpitch, sint32 x, sint32 y, uint32 w, uint32 h) {
		if (x < 0)
			x = 0;
		if ((uint32)x >= w)
			x = w - 1;
		if (y < 0)
			y = 0;
		if ((uint32)y >= h)
			y = h - 1;

		const uint32 *p = (const uint32 *)((const char *)src + srcpitch*y) + (x / 3)*4;

		switch((uint32)x % 3) {
			default:
			case 0:	return (p[0] >> 20) & 0x3ff;
			case 1:	return (p[2] >>  0) & 0x3ff;
			case 2:	return (p[3] >> 10) & 0x3ff;
		}
	}
}

uint32 VDPixmapInterpolateSampleRGB24(const VDPixmap& px, sint32 x_256, sint32 y_256) {
	switch(px.format) {
		case nsVDPixmap::kPixFormat_Pal1:
		case nsVDPixmap::kPixFormat_Pal2:
		case nsVDPixmap::kPixFormat_Pal4:
		case nsVDPixmap::kPixFormat_Pal8:
		case nsVDPixmap::kPixFormat_RGB565:
		case nsVDPixmap::kPixFormat_RGB888:
		case nsVDPixmap::kPixFormat_XRGB1555:
		case nsVDPixmap::kPixFormat_XRGB8888:
			{
				x_256 -= 128;
				y_256 -= 128;
				int ix = x_256 >> 8;
				int iy = y_256 >> 8;
				uint32 p0 = VDPixmapSample(px, ix, iy);
				uint32 p1 = VDPixmapSample(px, ix+1, iy);
				uint32 p2 = VDPixmapSample(px, ix, iy+1);
				uint32 p3 = VDPixmapSample(px, ix+1, iy+1);

				return Lerp8888(p0, p1, p2, p3, x_256 & 255, y_256 & 255);
			}
			break;

		case nsVDPixmap::kPixFormat_Y8:
			return InterpPlanarY8(px, x_256, y_256); 

		case nsVDPixmap::kPixFormat_YUV422_UYVY:
			return ConvertYCC72ToRGB24(
					VDPixmapInterpolateSample8x2To24((const char *)px.data + 1, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 0, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 2, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV422_YUYV:
			return ConvertYCC72ToRGB24(
					VDPixmapInterpolateSample8x2To24((const char *)px.data + 0, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 1, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 3, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV422_UYVY_FR:
			return ConvertYCC72ToRGB24_FR(
					VDPixmapInterpolateSample8x2To24((const char *)px.data + 1, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 0, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 2, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV422_YUYV_FR:
			return ConvertYCC72ToRGB24_FR(
					VDPixmapInterpolateSample8x2To24((const char *)px.data + 0, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 1, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 3, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV444_XVYU:
			return ConvertYCC72ToRGB24(
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 1, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 0, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 2, px.pitch, px.w, px.h, x_256, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV422_UYVY_709:
			return ConvertYCC72ToRGB24_709(
					VDPixmapInterpolateSample8x2To24((const char *)px.data + 1, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 0, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 2, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV422_YUYV_709:
			return ConvertYCC72ToRGB24_709(
					VDPixmapInterpolateSample8x2To24((const char *)px.data + 0, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 1, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 3, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR:
			return ConvertYCC72ToRGB24_709_FR(
					VDPixmapInterpolateSample8x2To24((const char *)px.data + 1, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 0, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 2, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR:
			return ConvertYCC72ToRGB24_709_FR(
					VDPixmapInterpolateSample8x2To24((const char *)px.data + 0, px.pitch, px.w, px.h, x_256, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 1, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256),
					VDPixmapInterpolateSample8x4To24((const char *)px.data + 3, px.pitch, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256)
				);

		case nsVDPixmap::kPixFormat_YUV444_Planar:
			return InterpPlanarYCC888(px, x_256, y_256, x_256, y_256, px.w, px.h);

		case nsVDPixmap::kPixFormat_YUV422_Planar:
			return InterpPlanarYCC888(px, x_256, y_256, (x_256 >> 1) + 64, y_256, (px.w + 1) >> 1, px.h);

		case nsVDPixmap::kPixFormat_YUV411_Planar:
			return InterpPlanarYCC888(px, x_256, y_256, (x_256 >> 2) + 96, y_256, (px.w + 3) >> 2, px.h);

		case nsVDPixmap::kPixFormat_YUV420_Planar:
			return InterpPlanarYCC888(px, x_256, y_256, (x_256 >> 1) + 64, y_256 >> 1, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV420it_Planar:
			return InterpPlanarYCC888(px, x_256, y_256, (x_256 >> 1) + 64, (y_256 >> 1) + 32, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV420ib_Planar:
			return InterpPlanarYCC888(px, x_256, y_256, (x_256 >> 1) + 64, (y_256 >> 1) - 32, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV410_Planar:
			return InterpPlanarYCC888(px, x_256, y_256, (x_256 >> 2) + 96, y_256 >> 2, (px.w + 3) >> 2, (px.h + 3) >> 2);

		case nsVDPixmap::kPixFormat_YUV420_Planar_Centered:
			return InterpPlanarYCC888(px, x_256, y_256, x_256 >> 1, y_256 >> 1, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV422_Planar_Centered:
			return InterpPlanarYCC888(px, x_256, y_256, x_256 >> 1, y_256, (px.w + 1) >> 1, px.h);

		case nsVDPixmap::kPixFormat_YUV444_Planar_709:
			return InterpPlanarYCC888_709(px, x_256, y_256, x_256, y_256, px.w, px.h);

		case nsVDPixmap::kPixFormat_YUV422_Planar_709:
			return InterpPlanarYCC888_709(px, x_256, y_256, (x_256 >> 1) + 64, y_256, (px.w + 1) >> 1, px.h);

		case nsVDPixmap::kPixFormat_YUV411_Planar_709:
			return InterpPlanarYCC888_709(px, x_256, y_256, (x_256 >> 2) + 96, y_256, (px.w + 3) >> 2, px.h);

		case nsVDPixmap::kPixFormat_YUV420_Planar_709:
			return InterpPlanarYCC888_709(px, x_256, y_256, (x_256 >> 1) + 64, y_256 >> 1, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
			return InterpPlanarYCC888_709(px, x_256, y_256, (x_256 >> 1) + 64, (y_256 >> 1) + 32, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
			return InterpPlanarYCC888_709(px, x_256, y_256, (x_256 >> 1) + 64, (y_256 >> 1) - 32, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV410_Planar_709:
			return InterpPlanarYCC888_709(px, x_256, y_256, (x_256 >> 2) + 96, y_256 >> 2, (px.w + 3) >> 2, (px.h + 3) >> 2);

		case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
			return InterpPlanarYCC888_FR(px, x_256, y_256, x_256, y_256, px.w, px.h);

		case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
			return InterpPlanarYCC888_FR(px, x_256, y_256, (x_256 >> 1) + 64, y_256, (px.w + 1) >> 1, px.h);

		case nsVDPixmap::kPixFormat_YUV411_Planar_FR:
			return InterpPlanarYCC888_FR(px, x_256, y_256, (x_256 >> 2) + 96, y_256, (px.w + 3) >> 2, px.h);

		case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
			return InterpPlanarYCC888_FR(px, x_256, y_256, (x_256 >> 1) + 64, y_256 >> 1, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
			return InterpPlanarYCC888_FR(px, x_256, y_256, (x_256 >> 1) + 64, (y_256 >> 1) + 32, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
			return InterpPlanarYCC888_FR(px, x_256, y_256, (x_256 >> 1) + 64, (y_256 >> 1) - 32, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
			return InterpPlanarYCC888_FR(px, x_256, y_256, (x_256 >> 2) + 96, y_256 >> 2, (px.w + 3) >> 2, (px.h + 3) >> 2);

		case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
			return InterpPlanarYCC888_709_FR(px, x_256, y_256, x_256, y_256, px.w, px.h);

		case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
			return InterpPlanarYCC888_709_FR(px, x_256, y_256, (x_256 >> 1) + 64, y_256, (px.w + 1) >> 1, px.h);

		case nsVDPixmap::kPixFormat_YUV411_Planar_709_FR:
			return InterpPlanarYCC888_709_FR(px, x_256, y_256, (x_256 >> 2) + 96, y_256, (px.w + 3) >> 2, px.h);

		case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
			return InterpPlanarYCC888_709_FR(px, x_256, y_256, (x_256 >> 1) + 64, y_256 >> 1, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
			return InterpPlanarYCC888_709_FR(px, x_256, y_256, (x_256 >> 1) + 64, (y_256 >> 1) + 32, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
			return InterpPlanarYCC888_709_FR(px, x_256, y_256, (x_256 >> 1) + 64, (y_256 >> 1) - 32, (px.w + 1) >> 1, (px.h + 1) >> 1);

		case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
			return InterpPlanarYCC888_709_FR(px, x_256, y_256, (x_256 >> 2) + 96, y_256 >> 2, (px.w + 3) >> 2, (px.h + 3) >> 2);

		case nsVDPixmap::kPixFormat_YUV420i_Planar:
			return InterpPlanarYCC888_420i<ConvertYCC72ToRGB24       >(px, x_256, y_256);

		case nsVDPixmap::kPixFormat_YUV420i_Planar_FR:
			return InterpPlanarYCC888_420i<ConvertYCC72ToRGB24_FR    >(px, x_256, y_256);

		case nsVDPixmap::kPixFormat_YUV420i_Planar_709:
			return InterpPlanarYCC888_420i<ConvertYCC72ToRGB24_709   >(px, x_256, y_256);

		case nsVDPixmap::kPixFormat_YUV420i_Planar_709_FR:
			return InterpPlanarYCC888_420i<ConvertYCC72ToRGB24_709_FR>(px, x_256, y_256);

		case nsVDPixmap::kPixFormat_YUV422_Planar_16F:
			{
				float y  = VDPixmapInterpolateSample16F(px.data, px.pitch, px.w, px.h, x_256, y_256);
				float cb = VDPixmapInterpolateSample16F(px.data2, px.pitch2, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256);
				float cr = VDPixmapInterpolateSample16F(px.data3, px.pitch3, (px.w + 1) >> 1, px.h, (x_256 >> 1) + 128, y_256);

				uint32 ir = VDClampedRoundFixedToUint8Fast(1.1643836f*y + 1.5960268f*cr - (222.92157f / 255.0f));
				uint32 ig = VDClampedRoundFixedToUint8Fast(1.1643836f*y - 0.3917623f*cb - 0.8129676f*cr + (135.57529f / 255.0f));
				uint32 ib = VDClampedRoundFixedToUint8Fast(1.1643836f*y + 2.0172321f*cb - (276.83585f / 255.0f));

				return (ir << 16) + (ig << 8) + ib;
			}

		case nsVDPixmap::kPixFormat_YUV422_V210:
			{
				sint32 luma_x = x_256 - 128;
				sint32 luma_y = y_256 - 128;

				if (luma_x < 0)
					luma_x = 0;

				if (luma_y < 0)
					luma_y = 0;

				if (luma_x > (sint32)((px.w - 1) << 8))
					luma_x = (sint32)((px.w - 1) << 8);

				if (luma_y > (sint32)((px.h - 1) << 8))
					luma_y = (sint32)((px.h - 1) << 8);

				sint32 luma_ix = luma_x >> 8;
				sint32 luma_iy = luma_y >> 8;
				float luma_fx = (float)(luma_x & 255) * (1.0f / 255.0f);
				float luma_fy = (float)(luma_y & 255) * (1.0f / 255.0f);

				float y0 = (float)SampleV210_Y(px.data, px.pitch, luma_ix+0, luma_iy+0, px.w, px.h);
				float y1 = (float)SampleV210_Y(px.data, px.pitch, luma_ix+1, luma_iy+0, px.w, px.h);
				float y2 = (float)SampleV210_Y(px.data, px.pitch, luma_ix+0, luma_iy+1, px.w, px.h);
				float y3 = (float)SampleV210_Y(px.data, px.pitch, luma_ix+1, luma_iy+1, px.w, px.h);
				float yt = y0 + (y1 - y0)*luma_fx;
				float yb = y2 + (y3 - y2)*luma_fx;
				float yr = yt + (yb - yt)*luma_fy;

				uint32 chroma_w = (px.w + 1) >> 1;
				uint32 chroma_h = px.h;
				sint32 chroma_x = x_256 >> 1;
				sint32 chroma_y = y_256 - 128;

				if (chroma_x < 0)
					chroma_x = 0;

				if (chroma_y < 0)
					chroma_y = 0;

				if (chroma_x > (sint32)((chroma_w - 1) << 8))
					chroma_x = (sint32)((chroma_w - 1) << 8);

				if (chroma_y > (sint32)((chroma_h - 1) << 8))
					chroma_y = (sint32)((chroma_h - 1) << 8);

				sint32 chroma_ix = chroma_x >> 8;
				sint32 chroma_iy = chroma_y >> 8;
				float chroma_fx = (float)(chroma_x & 255) * (1.0f / 255.0f);
				float chroma_fy = (float)(chroma_y & 255) * (1.0f / 255.0f);

				float cb0 = (float)SampleV210_Cb(px.data, px.pitch, chroma_ix+0, chroma_iy+0, chroma_w, chroma_h);
				float cb1 = (float)SampleV210_Cb(px.data, px.pitch, chroma_ix+1, chroma_iy+0, chroma_w, chroma_h);
				float cb2 = (float)SampleV210_Cb(px.data, px.pitch, chroma_ix+0, chroma_iy+1, chroma_w, chroma_h);
				float cb3 = (float)SampleV210_Cb(px.data, px.pitch, chroma_ix+1, chroma_iy+1, chroma_w, chroma_h);
				float cbt = cb0 + (cb1 - cb0)*chroma_fx;
				float cbb = cb2 + (cb3 - cb2)*chroma_fx;
				float cbr = cbt + (cbb - cbt)*chroma_fy;

				float cr0 = (float)SampleV210_Cr(px.data, px.pitch, chroma_ix+0, chroma_iy+0, chroma_w, chroma_h);
				float cr1 = (float)SampleV210_Cr(px.data, px.pitch, chroma_ix+1, chroma_iy+0, chroma_w, chroma_h);
				float cr2 = (float)SampleV210_Cr(px.data, px.pitch, chroma_ix+0, chroma_iy+1, chroma_w, chroma_h);
				float cr3 = (float)SampleV210_Cr(px.data, px.pitch, chroma_ix+1, chroma_iy+1, chroma_w, chroma_h);
				float crt = cr0 + (cr1 - cr0)*chroma_fx;
				float crb = cr2 + (cr3 - cr2)*chroma_fx;
				float crr = crt + (crb - crt)*chroma_fy;

				int ref = px.info.colorRangeMode==vd2::kColorRangeMode_Full ? 0x3FF:0x3FC;
				VDPixmapFormatEx f = VDPixmapFormatCombine(px);
				float r,g,b;
				VDConvertYCbCrToRGB(int(yr),int(cbr),int(crr),ref,f,r,g,b);
				return VDPackRGB(r,g,b);
			}
			break;

		default:
			return 0;
	}
}

uint32 VDConvertYCbCrToRGB(uint8 y0, uint8 cb0, uint8 cr0, bool use709, bool useFullRange) {
	//! this code is not fully used and seems wrong
	sint32  y =  y0;
	sint32 cb = cb0 - 128;
	sint32 cr = cr0 - 128;
	sint32 r;
	sint32 g;
	sint32 b;

	if (use709) {
		if (useFullRange) {
			sint32 y2 = (y << 16) + 0x8000;
			r = y2 + cr * 103206;
			g = y2 + cr * -30679 + cb * -12276;
			b = y2 + cb * 121609;
		} else {
			sint32 y2 = (y - 16) * 76309 + 0x8000;
			r = y2 + cr * 117489;
			g = y2 + cr * -34925 + cb * -13975;
			b = y2 + cb * 138438;
		}
	} else {
		if (useFullRange) {
			sint32 y2 = (y << 16) + 0x8000;
			r = y2 + cr * 91181;
			g = y2 + cr * -46802 + cb * -22554;
			b = y2 + cb * 166130;
		} else {
			sint32 y2 = (y - 16) * 76309 + 0x8000;
			r = y2 + cr * 104597;
			g = y2 + cr * -53279 + cb * -25674;
			b = y2 + cb * 132201;
		}
	}

	r &= ~(r >> 31);
	g &= ~(g >> 31);
	b &= ~(b >> 31);
	r += (0xffffff - r) & ((0xffffff - r) >> 31);
	g += (0xffffff - g) & ((0xffffff - g) >> 31);
	b += (0xffffff - b) & ((0xffffff - b) >> 31);

	return (r & 0xff0000) + ((g & 0xff0000) >> 8) + (b >> 16);
}

void VDConvertYCbCrToRGB(int y0, int cb0, int cr0, int ref, VDPixmapFormatEx& format, float& r, float& g, float& b) {
	int mref = vd2::chroma_neutral(ref);
	float  y =  float(y0)/ref;
	float cb = float(cb0-mref)/ref + 128.0f / 255.0f;
	float cr = float(cr0-mref)/ref + 128.0f / 255.0f;

	if (format.colorSpaceMode==vd2::kColorSpaceMode_709) {
		if (format.colorRangeMode==vd2::kColorRangeMode_Full) {
			ConvertYCC72ToRGB_709_FR(y,cb,cr,r,g,b);
		} else {
			ConvertYCC72ToRGB_709(y,cb,cr,r,g,b);
		}
	} else {
		if (format.colorRangeMode==vd2::kColorRangeMode_Full) {
			ConvertYCC72ToRGB_FR(y,cb,cr,r,g,b);
		} else {
			ConvertYCC72ToRGB(y,cb,cr,r,g,b);
		}
	}

	r = std::clamp(r, 0.f, 1.f);
	g = std::clamp(g, 0.f, 1.f);
	b = std::clamp(b, 0.f, 1.f);
}

uint32 VDPackRGB(float r0, float g0, float b0) {
	int r = VDRoundToInt(r0*0xFF0000 + 0x8000);
	int g = VDRoundToInt(g0*0xFF0000 + 0x8000);
	int b = VDRoundToInt(b0*0xFF0000 + 0x8000);

	r &= ~(r >> 31);
	g &= ~(g >> 31);
	b &= ~(b >> 31);
	r += (0xffffff - r) & ((0xffffff - r) >> 31);
	g += (0xffffff - g) & ((0xffffff - g) >> 31);
	b += (0xffffff - b) & ((0xffffff - b) >> 31);

	return (r & 0xff0000) + ((g & 0xff0000) >> 8) + (b >> 16);
}

uint32 VDPackRGBA(float r0, float g0, float b0, float a0) {
	int a = VDRoundToInt(a0*0xFF0000 + 0x8000);
	a &= ~(a >> 31);
	a += (0xffffff - a) & ((0xffffff - a) >> 31);

	return ((a & 0xff0000)<<8) + VDPackRGB(r0,g0,b0);
}

uint32 VDPackRGBA8(float r0, float g0, float b0, uint8 a) {
	return (a<<24) + VDPackRGB(r0,g0,b0);
}

uint32 VDConvertRGBToYCbCr(uint32 c, bool use709, bool useFullRange) {
	return VDConvertRGBToYCbCr((uint8)(c >> 16), (uint8)(c >> 8), (uint8)c, use709, useFullRange);
}

uint32 VDConvertRGBToYCbCr(uint8 r8, uint8 g8, uint8 b8, bool use709, bool useFullRange) {
	sint32 r  = r8;
	sint32 g  = g8;
	sint32 b  = b8;
	sint32 y;
	sint32 cb;
	sint32 cr;

	if (use709) {
		if (useFullRange) {
			y  = ( 13933*r + 46871*g +  4732*b +   0x8000) >> 8;
			cb = ( -7509*r - 25259*g + 32768*b + 0x808000) >> 16;
			cr = ( 32768*r - 29763*g -  3005*b + 0x808000);
		} else {
			y =  ( 11966*r + 40254*g +  4064*b + 0x108000) >> 8;
			cb = ( -6596*r - 22189*g + 28784*b + 0x808000) >> 16;
			cr = ( 28784*r - 26145*g -  2639*b + 0x808000);
		}
	} else {
		if (useFullRange) {
			y =  ( 19595*r + 38470*g +  7471*b +   0x8000) >> 8;
			cb = (-11058*r - 21710*g + 32768*b + 0x808000) >> 16;
			cr = ( 32768*r - 27439*g -  5329*b + 0x808000);
		} else {
			y  = ( 16829*r + 33039*g +  6416*b + 0x108000) >> 8;
			cb = ( -9714*r - 19071*g + 28784*b + 0x808000) >> 16;
			cr = ( 28784*r - 24103*g -  4681*b + 0x808000);
		}
	}

	return (uint8)cb + (y & 0xff00) + (cr&0xff0000);
}

void VDConvertRGBToYCbCr(float r, float g, float b, float& y, float& cb, float& cr, bool use709, bool useFullRange) {
	float scale = 0x10000;
	if (use709) {
		if (useFullRange) {
			y  = ( 13933*r + 46871*g +  4732*b +      0) / scale;
			cb = ( -7509*r - 25259*g + 32768*b + 0x8000) / scale;
			cr = ( 32768*r - 29763*g -  3005*b + 0x8000) / scale;
		} else {
			y =  ( 11966*r + 40254*g +  4064*b + 0x1000) / scale;
			cb = ( -6596*r - 22189*g + 28784*b + 0x8000) / scale;
			cr = ( 28784*r - 26145*g -  2639*b + 0x8000) / scale;
		}
	} else {
		if (useFullRange) {
			y =  ( 19595*r + 38470*g +  7471*b +      0) / scale;
			cb = (-11058*r - 21710*g + 32768*b + 0x8000) / scale;
			cr = ( 32768*r - 27439*g -  5329*b + 0x8000) / scale;
		} else {
			y  = ( 16829*r + 33039*g +  6416*b + 0x1000) / scale;
			cb = ( -9714*r - 19071*g + 28784*b + 0x8000) / scale;
			cr = ( 28784*r - 24103*g -  4681*b + 0x8000) / scale;
		}
	}

	cb += 0.5f / 255.0f;
	cr += 0.5f / 255.0f;
}

void VDRoundYCbCr(float y, float cb, float cr, int ref, int& iy, int& icb, int& icr)
{
	int mref = vd2::chroma_neutral(ref);

	iy  = std::clamp(VDRoundToInt(y*ref), 0, ref);
	icb = std::clamp(VDRoundToInt((cb-(128.0f / 255.0f))*ref) + mref, 0, ref);
	icr = std::clamp(VDRoundToInt((cr-(128.0f / 255.0f))*ref) + mref, 0, ref);
}

//---------------------------------------------------------------------------------
// notes:
// floating point chroma is neutral at 128/255 ~= 0.50196...
// integer chroma (std) = (128+x)*2^(n-8)
// integer chroma (full) = 128*2^(n-8) + x*2^(n-1) -> max x (8bit) = 127/255 ~= 0.498, thus 0xFF converts to 0xFF7F

// other instances of ycbcr coefficients:
// uberblit_ycbcr* (multiple places)
// Riza\utils.fxh
// VDDisplay\utils.fxh

//---------------------------------------------------------------------------------
