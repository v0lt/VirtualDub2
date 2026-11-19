// Asuka - VirtualDub Build/Post-Mortem Utility
//
// Copyright (C) 2005-2007 Avery Lee
// Copyright (C) 2024-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include <vd2/system/memory.h>
#include <vd2/system/vectors.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vector>
#include <algorithm>
#include <windows.h>

void VDNORETURN help_fontencode() {
	printf("usage: fontencode <.bmp file> <cell width> <cell height> <row width> <row height> <ascent> <advance> <line gap> <output file> <symbol name>\n");
	exit(5);
}

void tool_fontencode(const vdfastvector<const wchar_t*>& args, const vdfastvector<const wchar_t*>& switches) {
	if (args.size() < 10)
		help_fontencode();

	const int cellWidth   = _wtoi(args[1]);
	const int cellHeight  = _wtoi(args[2]);
	const int gridWidth   = _wtoi(args[3]);
	const int gridHeight  = _wtoi(args[4]);
	const int cellAscent  = _wtoi(args[5]);
	const int cellAdvance = _wtoi(args[6]);
	const int lineGap     = _wtoi(args[7]);

	if (cellWidth < 1 || cellHeight < 1 || cellWidth > 1000 || cellHeight > 1000) {
		printf("Asuka: Invalid cell size %dx%d.\n", cellWidth, cellHeight);
		exit(10);
	}

	printf("Asuka: Extracting %dx%d bitmap font: %ls -> %ls.\n", cellWidth, cellHeight, args[0], args[8]);

	HBITMAP hbm = (HBITMAP)LoadImageW(NULL, args[0], IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
	BITMAP bm = {0};

	if (!hbm || !GetObjectW(hbm, sizeof bm, &bm)) {
		printf("Asuka: Unable to load font bitmap %ls: error code %u.\n", args[0], GetLastError());
		exit(10);
	}

	const int bmWidth = bm.bmWidth;
	const int bmHeight = bm.bmHeight;

	if (bmWidth < gridWidth * (cellWidth + 1) - 1 || bmHeight < gridHeight * (cellHeight + 1) - 1) {
		printf("Asuka: Can't extract %dx%d cells in %dx%d grid from %dx%d bitmap.\n"
			, cellWidth
			, cellHeight
			, gridWidth
			, gridHeight
			, bmWidth
			, bmHeight);

		exit(10);
	}

	vdfastvector<uint32> pixels(bmWidth * bmHeight);

	BITMAPINFO bi = {0};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = bmWidth;
	bi.bmiHeader.biHeight = bmHeight;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biCompression = BI_RGB;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biSizeImage = bmWidth * bmHeight * 4;
	bi.bmiHeader.biXPelsPerMeter = 0;
	bi.bmiHeader.biYPelsPerMeter = 0;
	bi.bmiHeader.biClrUsed = 0;
	bi.bmiHeader.biClrImportant = 0;
	HDC hdc = GetDC(NULL);
	if (!hdc || !GetDIBits(hdc, hbm, 0, bmHeight, pixels.data(), &bi, DIB_RGB_COLORS)) {
		printf("Asuka: Can't extract bitmap bits.\n");
		exit(10);
	}
	ReleaseDC(NULL, hdc);
	
	vdfastvector<uint8> outheap;
	vdfastvector<uint8> chardata(cellWidth * cellHeight);

	int maxCells = std::min<int>(256, gridWidth * gridHeight);
	vdfastvector<uint32> posdata;

	int startChar = 255;
	int emptyCharRun = 0;
	int endChar = 0;
	for(int i=0; i<maxCells; ++i) {
		int cx = i % gridWidth;
		int cy = i / gridWidth;
		int x = (cellWidth + 1) * cx;
		int y = (cellHeight + 1) * cy;

		uint8 *dst = chardata.data();
		bool empty = true;
		int x1 = 0;
		int x2 = cellWidth;

		const uint8 *srct = (const uint8 *)&pixels[bmWidth * (bmHeight - 1 - y) + x];
		while(x2 > x1 && srct[4 * (x2 - 1)] > 192 && srct[4 * (x2 - 1) + 2] < 192)
			--x2;

		while(x1 < x2 && srct[4 * x1] > 192 && srct[4 * x1 + 2] < 192)
			++x1;

		int cw = x2 - x1;

		for(int y2=0; y2<cellHeight; ++y2) {
			const uint8 *src = (const uint8 *)&pixels[bmWidth * (bmHeight - 1 - (y+y2)) + x + x1] + 1;

			for(int x2=0; x2<cw; ++x2) {
				if (src[0] < 128)
					empty = false;

				dst[x2] = src[0] < 128 ? 255 : 0;
				src += 4;
			}

			dst += cw;
		}

		if (empty && cw == cellWidth) {
			++emptyCharRun;
		} else {
			if (outheap.empty())
				startChar = i;
			else if (emptyCharRun)
				posdata.resize(posdata.size() + emptyCharRun, cellWidth);

			emptyCharRun = 0;
			posdata.push_back(((outheap.size() / cellHeight + 1) << 4) + cw);
			outheap.insert(outheap.end(), chardata.data(), chardata.data() + cw * cellHeight);
			endChar = i;
		}
	}

	outheap.resize(((outheap.size() + 7) & ~7), 0);

	FILE *f = nullptr;
	errno_t err = _wfopen_s(&f, args[8], L"w");
	if (err) {
		printf("Asuka: Unable to open output file: %ls\n", args[8]);
		exit(10);
	}

	fprintf(f, "// Created by Asuka from %ls.  DO NOT EDIT!\n\n", VDFileSplitPath(args[0]));
	fprintf(f, "const uint8 %ls_FontData[]={\n", args[9]);

	int datalen = outheap.size();
	for(int i=0; i<datalen; i+=8) {
		uint8 byte	= (outheap[i+0] & 0x80)
					+ (outheap[i+1] & 0x40)
					+ (outheap[i+2] & 0x20)
					+ (outheap[i+3] & 0x10)
					+ (outheap[i+4] & 0x08)
					+ (outheap[i+5] & 0x04)
					+ (outheap[i+6] & 0x02)
					+ (outheap[i+7] & 0x01);

		if (!(i & 127))
			fputc('\t', f);

		fprintf(f, "0x%02x,", byte);

		if ((i & 127) == 120)
			fputc('\n', f);
	}

	if (datalen & 127)
		fputc('\n', f);

	fprintf(f, "};\n\n");

	fprintf(f, "const uint16 %ls_PosData[]={\n", args[9]);

	int poscount = posdata.size();
	for(int i=0; i<poscount; ++i) {
		if (!(i & 15))
			fputc('\t', f);

		fprintf(f, "0x%04x,", posdata[i]);

		if ((i & 15) == 15)
			fputc('\n', f);
	}

	if (poscount & 15)
		fputc('\n', f);

	fprintf(f, "};\n\n");

	// top-level data structure
	fprintf(f, "const VDBitmapFontInfo %ls_FontInfo={\n", args[9]);
	fprintf(f, "\t%ls_FontData,\n", args[9]);
	fprintf(f, "\t%ls_PosData,\n", args[9]);
	fprintf(f, "\t%d, %d,\n", startChar, endChar);
	fprintf(f, "\t%d, %d,\n", cellWidth, cellHeight);
	fprintf(f, "\t%d, %d,\n", cellAscent, cellAdvance);
	fprintf(f, "\t%d,\n", lineGap);
	fprintf(f, "};\n");

	fclose(f);

	printf("Asuka: %d bytes.\n", datalen);
}
