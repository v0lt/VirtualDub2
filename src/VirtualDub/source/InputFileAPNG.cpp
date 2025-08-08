//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2009 Avery Lee
//
//	Animated PNG support by Max Stepin
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
//	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "stdafx.h"

#include <vd2/system/file.h>
#include <vd2/system/fraction.h>
#include <vd2/system/binary.h>
#include <vd2/system/log.h>
#include <vd2/kasumi/pixmapops.h>
#include "InputFile.h"
#include "VideoSource.h"
#include "gui.h"
#include "APNG.h"
#include "resource.h"

extern HINSTANCE g_hInst;

#define notabc(c) ((c) < 65 || (c) > 122 || ((c) > 90 && (c) < 97))

#define ROWBYTES(pixel_bits, width) \
    ((pixel_bits) >= 8 ? \
    ((width) * (((uint32)(pixel_bits)) >> 3)) : \
    (( ((width) * ((uint32)(pixel_bits))) + 7) >> 3) )

///////////////////////////////////////////////////////////////////////////

class VDInputFileAPNGSharedData : public vdrefcounted<IVDRefCount> {
public:
	VDInputFileAPNGSharedData();
	~VDInputFileAPNGSharedData();

	void Parse(const wchar_t *filename);

	vdblock<uint8>	mImage;
	uint32		mNumFrames;
	uint32		mNumLoops;
	uint32		mWidth;
	uint32		mHeight;
	uint32		mPixelDepth;
	uint32		hasTRNS;
	uint16		mTRNSr;
	uint16		mTRNSg;
	uint16		mTRNSb;
	uint8		mBitDepth;
	uint8		mColorType;
	bool		mbKeyframeOnly;
	VDFraction	mFrameRate;

	struct ImageInfo {
		uint32	mOffsetAndKey;
		uint32	mWidth;
		uint32	mHeight;
		uint32	mXOffset;
		uint32	mYOffset;
		uint8	mDispose;
		uint8	mBlend;
	};

	typedef vdfastvector<ImageInfo> Images;
	Images		mImages;
	uint32		mPalette[256];
};

VDInputFileAPNGSharedData::VDInputFileAPNGSharedData()
	: mNumFrames(0)
	, mNumLoops(0)
	, mWidth(0)
	, mHeight(0)
	, hasTRNS(0)
{
}

VDInputFileAPNGSharedData::~VDInputFileAPNGSharedData() {
}

void VDInputFileAPNGSharedData::Parse(const wchar_t *filename)
{
	uint32 i;
	uint32 numFCTL = 0;
	VDFile file(filename, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting);

	sint64 len64 = file.size();

	if (len64 > 0x3FFFFFFF)
		throw MyError("The PNG image \"%ls\" is too large to read.", filename);

	uint32 len = (uint32)len64;

	mImage.resize(len);
	file.read(mImage.data(), len);

	for (i=0; i<256; i++)
		mPalette[i] = 0x010101*i + 0xFF000000;

	const uint8 *src = mImage.data();
	uint32 pos = 0;
	if (len - pos < 8 || src[0] != 137 || src[1] != 'P' || src[2] != 'N' || src[3] != 'G' || src[4] != 13 || src[5] != 10 || src[6] != 26 || src[7] != 10)
		throw MyError("File \"%ls\" is not a PNG file.", filename);

	pos += 8;

	if (len - pos < 25)
		throw MyError("File \"%ls\" is an invalid PNG file.", filename);

	uint32 length = VDReadUnalignedBEU32(&src[pos]);
	uint32 chunk  = VDReadUnalignedBEU32(&src[pos+4]);

	if ((length != 13) || (chunk != 'IHDR'))
		throw MyError("IHDR missing in \"%ls\" file.", filename);

	pos += 8;

	mWidth           = VDReadUnalignedBEU32(&src[pos]);
	mHeight          = VDReadUnalignedBEU32(&src[pos+4]);
	mBitDepth        = src[pos+8];
	mColorType       = src[pos+9];

	if (mWidth == 0 || mHeight == 0)
		throw MyError("Image width or height is zero");

	if (mBitDepth != 1 && mBitDepth != 2 && mBitDepth != 4 && mBitDepth != 8 && mBitDepth != 16)
		throw MyError("BitDepth = %d is not supported", mBitDepth);

	if (mColorType != 0 && mColorType != 2 && mColorType != 3 && mColorType != 4 && mColorType != 6)
		throw MyError("ColorType = %d is not supported", mColorType);

	if (((mColorType == PNG_COLOR_TYPE_PALETTE) && mBitDepth > 8) ||
		((mColorType == PNG_COLOR_TYPE_RGB ||
		mColorType == PNG_COLOR_TYPE_GRAY_ALPHA ||
		mColorType == PNG_COLOR_TYPE_RGB_ALPHA) && mBitDepth < 8))
		throw MyError("Invalid color type/bit depth combination");

	if (src[pos+10] != PNG_COMPRESSION_TYPE_BASE)
		throw MyError("Unknown compression method %d", src[pos+10]);

	if (src[pos+11] != PNG_FILTER_TYPE_BASE)
		throw MyError("Filter method %d is not supported", src[pos+11]);

	if (src[pos+12] > PNG_INTERLACE_ADAM7)
		throw MyError("Unknown interlace method %d", src[pos+12]);

	switch (mColorType)
	{
		case PNG_COLOR_TYPE_GRAY:
		case PNG_COLOR_TYPE_PALETTE:
			mPixelDepth = mBitDepth;
			break;
		case PNG_COLOR_TYPE_RGB:
			mPixelDepth = mBitDepth*3;
			break;
		case PNG_COLOR_TYPE_GRAY_ALPHA:
			mPixelDepth = mBitDepth*2;
			break;
		case PNG_COLOR_TYPE_RGB_ALPHA:
		default:
			mPixelDepth = mBitDepth*4;
			break;
	}

	pos += (length+4);

	// parse chunks
	uint32      idat_pos = -1;
	vdfastvector<VDFraction> presentationTimes;
	VDFraction	timebase;
	VDFraction	spantotal;
	VDFraction	delay;
	uint32 spancount = 0;
	timebase.Assign(0, 1);
	spantotal.Assign(0, 1);
	ImageInfo imageinfo = { 0, 0, 0, 0, 0, PNG_DISPOSE_OP_NONE, PNG_BLEND_OP_SOURCE };

	while (pos + 8 <= len)
	{
		length = VDReadUnalignedBEU32(&src[pos]);
		chunk  = VDReadUnalignedBEU32(&src[pos+4]);

		if (len < pos + length + 12)
			break;
		if (notabc(src[pos+4]))
			break;
		if (notabc(src[pos+5]))
			break;
		if (notabc(src[pos+6]))
			break;
		if (notabc(src[pos+7]))
			break;

		if (chunk == 'PLTE')
		{
			for (i=0; (i<length/3 && i<256); i++)
			{
				mPalette[i] &= 0xFF000000;
				mPalette[i] += src[pos+8+i*3] << 16;
				mPalette[i] += src[pos+8+i*3+1] << 8;
				mPalette[i] += src[pos+8+i*3+2];
			}
		}
		else
		if (chunk == 'tRNS')
		{
			if (mColorType == PNG_COLOR_TYPE_GRAY)
			{
				mTRNSg = VDReadUnalignedBEU16(&src[pos+8]);
			}
			else
			if (mColorType == PNG_COLOR_TYPE_RGB)
			{
				mTRNSr = VDReadUnalignedBEU16(&src[pos+8]);
				mTRNSg = VDReadUnalignedBEU16(&src[pos+10]);
				mTRNSb = VDReadUnalignedBEU16(&src[pos+12]);
			}
			else
			if (mColorType == PNG_COLOR_TYPE_PALETTE)
			{
				for (i=0; (i<length && i<256); i++)
				{
					mPalette[i] &= 0x00FFFFFF;
					mPalette[i] += src[pos+8+i] << 24;
				}
			}
			hasTRNS = 1;
		}
		else
		if (chunk == 'acTL')
		{
			mNumFrames  = VDReadUnalignedBEU32(&src[pos+8]);
			mNumLoops   = VDReadUnalignedBEU32(&src[pos+12]);
		}
		else
		if (chunk == 'fcTL')
		{
			imageinfo.mWidth   = VDReadUnalignedBEU32(&src[pos+12]);
			imageinfo.mHeight  = VDReadUnalignedBEU32(&src[pos+16]);
			imageinfo.mXOffset = VDReadUnalignedBEU32(&src[pos+20]);
			imageinfo.mYOffset = VDReadUnalignedBEU32(&src[pos+24]);
			uint16 delay_num   = VDReadUnalignedBEU16(&src[pos+28]);
			uint16 delay_den   = VDReadUnalignedBEU16(&src[pos+30]);
			imageinfo.mDispose = src[pos+32];
			imageinfo.mBlend   = src[pos+33];

			if (!delay_num)
				delay_num = 1;
			if (!delay_den)
				delay_den = 100;

			delay.Assign(delay_num, delay_den);
			timebase += delay;
			presentationTimes.push_back(timebase);

			if (presentationTimes.size() > 2) {
				spantotal += timebase - *(presentationTimes.end() - 3);
				++spancount;
			} else if (presentationTimes.size() == 2) {
				spantotal += timebase;
				++spancount;
			}

			if ((numFCTL==0) && (imageinfo.mDispose == PNG_DISPOSE_OP_PREVIOUS))
				imageinfo.mDispose = PNG_DISPOSE_OP_BACKGROUND;

			if (!(mColorType & PNG_COLOR_MASK_ALPHA) && !(hasTRNS))
				imageinfo.mBlend = PNG_BLEND_OP_SOURCE;

			numFCTL++;
		}
		else
		if (chunk == 'IDAT')
		{
			if (numFCTL == 0)
			{
				if (idat_pos == -1)
				idat_pos = pos;
			}
			else
			if ((numFCTL == 1) && mImages.empty())
			{
				imageinfo.mOffsetAndKey = pos | 0x80000000;
				mImages.push_back(imageinfo);
			}
		}
		else
		if (chunk == 'fdAT')
		{
			if (mImages.size() == numFCTL - 1)
			{
				imageinfo.mOffsetAndKey = pos;
				if (mImages.empty() || (!imageinfo.mXOffset && !imageinfo.mYOffset && imageinfo.mWidth == mWidth && imageinfo.mHeight == mHeight && imageinfo.mBlend == PNG_BLEND_OP_SOURCE))
					imageinfo.mOffsetAndKey |= 0x80000000;
				mImages.push_back(imageinfo);
			}
		}
		else
		if (chunk == 'IEND')
			break;

		pos += (length+12);
	}

	if (mImages.empty())
	{
		if (idat_pos > 0)
		{
			imageinfo.mOffsetAndKey = idat_pos | 0x80000000;
			imageinfo.mWidth   = mWidth;
			imageinfo.mHeight  = mHeight;
			imageinfo.mXOffset = 0;
			imageinfo.mYOffset = 0;
			imageinfo.mDispose = PNG_DISPOSE_OP_NONE;
			imageinfo.mBlend   = PNG_BLEND_OP_SOURCE;
			mImages.push_back(imageinfo);
		}
		else
			throw MyError("No video frames detected in PNG file.");
	}

	if (mImages.size() < mNumFrames)
		VDLogF(kVDLogWarning, L"APNG: File appears to be truncated or damaged. \nLoaded %d frames (should be %d). ", mImages.size(), mNumFrames);

	mNumFrames = mImages.size();

	// compute time to frame mapping
	mFrameRate.Assign(10, 1);
	if (mNumFrames > 1)
	{
		vdfastvector<ImageInfo> images;
		mFrameRate.Assign(spancount * 2, 1);
		mFrameRate /= spantotal;

		for (i=0; i<mNumFrames; i++) {
			VDFraction t = presentationTimes[i] - presentationTimes.front();
			int frame = (mFrameRate * t).asInt();

			if (frame > images.size()) {
				ImageInfo dummy = { 0, 0, 0, 0, 0, PNG_DISPOSE_OP_NONE, PNG_BLEND_OP_SOURCE };
				images.resize(frame, dummy);
			}

			images.push_back(mImages[i]);
		}
		mImages.swap(images);
	}

	mbKeyframeOnly = true;
	for(Images::const_iterator it(mImages.begin()), itEnd(mImages.end()); it!=itEnd; ++it) {
		if (!(it->mOffsetAndKey & 0x80000000))
			mbKeyframeOnly = false;
	}
}

///////////////////////////////////////////////////////////////////////////

class VDVideoSourceAPNG : public VideoSource {
private:
	vdrefptr<VDInputFileAPNGSharedData> mpSharedData;
	VDPosition	mCachedFrame;
	uint32		mWidth;
	uint32		mHeight;

	vdfastvector<uint8>  mUnpackBuffer;
	vdfastvector<uint32> mFrameBuffer;
	vdfastvector<uint32> mRestoreBuffer;

	png_structp	 png_ptr;
	const uint8	*png_src;
	uint32		 png_pos;

	void read_filter_row(uint8 * row, uint8 * prev_row, int filter);
	void read_row(uint8 * row);

public:
	VDVideoSourceAPNG(VDInputFileAPNGSharedData *sharedData);
	~VDVideoSourceAPNG() override;

	int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) override;

	bool setTargetFormat(int format);

	void invalidateFrameBuffer() override		{ mCachedFrame = -1; }
	bool isFrameBufferValid() override			{ return mCachedFrame >= 0; }
	bool isStreaming() override					{ return false; }

	const void *getFrame(VDPosition lFrameDesired) override;
	uint8  getBitDepth() const						{ return mpSharedData->mBitDepth; }
	uint8  getColorType() const						{ return mpSharedData->mColorType; }
	uint32 getPixelDepth() const					{ return mpSharedData->mPixelDepth; }
	void streamBegin(bool, bool bForceReset) override {
		if (bForceReset)
			streamRestart();
	}

	const void *streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition sample_num, VDPosition target_sample) override;

	char getFrameTypeChar(VDPosition lFrameNum) override	{ return isKey(lFrameNum) ? 'K' : ' '; }
	eDropType getDropType(VDPosition lFrameNum) override	{ return isKey(lFrameNum) ? kDependant : kIndependent; }
	bool isKey(VDPosition lSample) override					{ return lSample >= 0 && lSample < (VDPosition)mpSharedData->mImages.size() && (0x80000000 & mpSharedData->mImages[(uint32)lSample].mOffsetAndKey) != 0; }

	VDPosition nearestKey(VDPosition lSample) override {
		return isKey(lSample) ? lSample : prevKey(lSample);
	}

	VDPosition prevKey(VDPosition lSample) override {
		if (lSample < 0)
			return -1;

		VDPosition limit = (VDPosition)mpSharedData->mImages.size();
		if (lSample > limit)
			lSample = limit;

		uint32 i = (uint32)lSample;
		while(i) {
			if (mpSharedData->mImages[--i].mOffsetAndKey & 0x80000000)
				return i;
		}

		return -1;
	}

	VDPosition nextKey(VDPosition lSample) override {
		if (lSample < 0)
			lSample = 0;

		VDPosition limit = (VDPosition)mpSharedData->mImages.size();
		if (lSample >= limit)
			return -1;

		uint32 i = (uint32)lSample;
		while(++i < limit) {
			if (mpSharedData->mImages[i].mOffsetAndKey & 0x80000000)
				return i;
		}

		return -1;
	}

	bool isKeyframeOnly() override						{ return mpSharedData->mbKeyframeOnly; }
	bool isDecodable(VDPosition sample_num) override	{ return (mCachedFrame >= 0 && (mCachedFrame == sample_num || mCachedFrame == sample_num - 1)) || isKey(sample_num); }
};


VDVideoSourceAPNG::VDVideoSourceAPNG(VDInputFileAPNGSharedData *sharedData)
	: mpSharedData(sharedData)
	, mCachedFrame(-1)
	, mWidth(mpSharedData->mWidth)
	, mHeight(mpSharedData->mHeight)
{
	BITMAPINFOHEADER *bih = (BITMAPINFOHEADER *)allocFormat(sizeof(BITMAPINFOHEADER));
	bih->biSize = sizeof(BITMAPINFOHEADER);
	bih->biWidth = mWidth;
	bih->biHeight = mHeight;
	bih->biPlanes = 1;
	bih->biCompression = BI_RGB;
	bih->biSizeImage = mWidth * mHeight * 4;
	bih->biBitCount = 32;
	bih->biXPelsPerMeter = 0;
	bih->biYPelsPerMeter = 0;
	bih->biClrUsed = 0;
	bih->biClrImportant = 0;

	mSampleFirst	= 0;
	mSampleLast		= 1;

	memset(&streamInfo, 0, sizeof streamInfo);
	streamInfo.fccType		= VDAVIStreamInfo::kTypeVideo;
	streamInfo.dwLength		= (DWORD)mSampleLast;
	streamInfo.dwRate		= mpSharedData->mFrameRate.getHi();
	streamInfo.dwScale		= mpSharedData->mFrameRate.getLo();

	mFrameBuffer.resize(mWidth * mHeight);
	mRestoreBuffer.resize(mWidth * mHeight);
	AllocFrameBuffer(bih->biSizeImage);

	mSampleLast = mSampleFirst + mpSharedData->mImages.size();

	png_ptr = (png_structp)malloc(sizeof(png_struct));

	if (png_ptr != NULL)
	{
		png_ptr->zbuf_size = PNG_ZBUF_SIZE;
		png_ptr->zbuf1 = (uint8 *)malloc(png_ptr->zbuf_size);
		png_ptr->zstream1.zalloc = Z_NULL;
		png_ptr->zstream1.zfree = Z_NULL;
		png_ptr->zstream1.opaque = Z_NULL;

		inflateInit(&png_ptr->zstream1);

		png_ptr->zstream1.next_out = png_ptr->zbuf1;
		png_ptr->zstream1.avail_out = (uInt)png_ptr->zbuf_size;

		png_ptr->width = mpSharedData->mWidth;
		png_ptr->height = mpSharedData->mHeight;
		png_ptr->color_type = mpSharedData->mColorType;
		png_ptr->bit_depth = mpSharedData->mBitDepth;
		png_ptr->pixel_depth = mpSharedData->mPixelDepth;
		png_ptr->bpp = (png_ptr->pixel_depth + 7) >> 3;
		png_ptr->rowbytes = ROWBYTES(png_ptr->pixel_depth, png_ptr->width);
		png_ptr->idat_size = 0;

		int big_width = ((png_ptr->width + 7) & ~((uint32)7));
		size_t big_row_bytes = ROWBYTES(png_ptr->pixel_depth, big_width) + 1 + (png_ptr->bpp);

		png_ptr->big_row_buf = (uint8 *)malloc(big_row_bytes + 64);
		png_ptr->row_buf = png_ptr->big_row_buf + 32;

		png_ptr->prev_row = (uint8 *)malloc(png_ptr->rowbytes + 1);

		if (png_ptr->prev_row != NULL)
			memset(png_ptr->prev_row, 0, png_ptr->rowbytes + 1);

		mUnpackBuffer.resize(png_ptr->height * png_ptr->rowbytes);
	}
}

VDVideoSourceAPNG::~VDVideoSourceAPNG()
{
	if (png_ptr != NULL)
	{
		if (png_ptr->zbuf1 != NULL)
			free(png_ptr->zbuf1);

		if (png_ptr->big_row_buf != NULL)
			free(png_ptr->big_row_buf);

		if (png_ptr->prev_row != NULL)
			free(png_ptr->prev_row);

		inflateEnd(&png_ptr->zstream1);

		memset(png_ptr, 0, sizeof(png_struct));

		free(png_ptr);
		png_ptr = NULL;
	}
}

int VDVideoSourceAPNG::_read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
	if (lCount > 1)
		lCount = 1;

	int ret = 0;
	uint32 bytes = 0;

	if (lCount > 0) {
		if (mpSharedData->mImages[(uint32)lStart].mOffsetAndKey)
			bytes = sizeof(VDPosition);

		if (lpBuffer) {
			if (cbBuffer < bytes)
				ret = IVDStreamSource::kBufferTooSmall;
			else if (bytes)
				*(VDPosition *)lpBuffer = lStart;
		}
	}

	if (lBytesRead)
		*lBytesRead = bytes;
	if (lSamplesRead)
		*lSamplesRead = lCount;

	return ret;
}

const void *VDVideoSourceAPNG::getFrame(VDPosition frameNum) {
	uint32 lBytes;

	if (mCachedFrame == frameNum)
		return mpFrameBuffer.get();

	VDPosition current = mCachedFrame + 1;
	if (current < 0 || current > frameNum)
		current = 0;

	vdfastvector<char> buffer;
	while(current <= frameNum) {
		read(current, 1, NULL, 0x7FFFFFFF, &lBytes, NULL);

		if (lBytes) {
			buffer.resize(lBytes);

			uint32 lReadBytes;

			read(current, 1, buffer.data(), lBytes, &lReadBytes, NULL);
			streamGetFrame(buffer.data(), lReadBytes, current != frameNum, current, frameNum);
		}

		++current;
	}

	return getFrameBuffer();
}

void VDVideoSourceAPNG::read_filter_row(uint8 * row, uint8 * prev_row, int filter)
{
	uint32 i;
	switch (filter)
	{
		case PNG_FILTER_VALUE_NONE:
			break;
		case PNG_FILTER_VALUE_SUB:
		{
			uint8 * rp = row + png_ptr->bpp;
			uint8 * lp = row;

			for (i = png_ptr->bpp; i < png_ptr->rowbytes; i++)
			{
				*rp = (uint8)(((int)(*rp) + (int)(*lp++)) & 0xff);
				rp++;
			}
			break;
		}
		case PNG_FILTER_VALUE_UP:
		{
			uint8 * rp = row;
			uint8 * pp = prev_row;

			for (i = 0; i < png_ptr->rowbytes; i++)
			{
				*rp = (uint8)(((int)(*rp) + (int)(*pp++)) & 0xff);
				rp++;
			}
			break;
		}
		case PNG_FILTER_VALUE_AVG:
		{
			uint8 * rp = row;
			uint8 * pp = prev_row;
			uint8 * lp = row;

			for (i = 0; i < png_ptr->bpp; i++)
			{
				*rp = (uint8)(((int)(*rp) + ((int)(*pp++) / 2 )) & 0xff);
				rp++;
			}

			for (i = 0; i < png_ptr->rowbytes - png_ptr->bpp; i++)
			{
				*rp = (uint8)(((int)(*rp) + (int)(*pp++ + *lp++) / 2 ) & 0xff);
				rp++;
			}
			break;
		}
		case PNG_FILTER_VALUE_PAETH:
		{
			uint8 * rp = row;
			uint8 * pp = prev_row;
			uint8 * lp = row;
			uint8 * cp = prev_row;

			for (i = 0; i < png_ptr->bpp; i++)
			{
				*rp = (uint8)(((int)(*rp) + (int)(*pp++)) & 0xff);
				rp++;
			}

			for (i = 0; i < png_ptr->rowbytes - png_ptr->bpp; i++)
			{
				int a, b, c, pa, pb, pc, p;

				a = *lp++;
				b = *pp++;
				c = *cp++;

				p = b - c;
				pc = a - c;

				pa = abs(p);
				pb = abs(pc);
				pc = abs(p + pc);

				p = (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;

				*rp = (uint8)(((int)(*rp) + p) & 0xff);
				rp++;
			}
			break;
		}
		default:
			*row = 0;
			break;
	}
}

void VDVideoSourceAPNG::read_row(uint8 * row)
{
	if (png_ptr == NULL) return;

	png_ptr->zstream1.next_out = png_ptr->row_buf;
	png_ptr->zstream1.avail_out = (uInt)png_ptr->rowbytes + 1;

	do
	{
		if (!(png_ptr->zstream1.avail_in))
		{
			while (png_ptr->idat_size == 0)
			{
				uint32 length = VDReadUnalignedBEU32(&png_src[png_pos]);
				uint32 chunk  = VDReadUnalignedBEU32(&png_src[png_pos+4]);

				if (notabc(png_src[png_pos+4]))
					break;
				if (notabc(png_src[png_pos+5]))
					break;
				if (notabc(png_src[png_pos+6]))
					break;
				if (notabc(png_src[png_pos+7]))
					break;

				if (chunk == 'IDAT')
				{
					png_pos += 8;
					png_ptr->idat_size = length;
				}
				else
				if (chunk == 'fdAT')
				{
					png_pos += 12;
					png_ptr->idat_size = length-4;
				}
				else
				if (chunk == 'IEND')
					break;
				else
					png_pos += (length+8);
			}

			if (png_ptr->idat_size == 0)
				break;

			png_ptr->zstream1.avail_in = (uInt)png_ptr->zbuf_size;
			png_ptr->zstream1.next_in = png_ptr->zbuf1;
			if (png_ptr->zbuf_size > png_ptr->idat_size)
				png_ptr->zstream1.avail_in = (uInt)png_ptr->idat_size;

			if (png_ptr->zbuf1 != NULL)
			{
				memcpy(png_ptr->zbuf1, png_src+png_pos, png_ptr->zstream1.avail_in);
				png_pos += png_ptr->zstream1.avail_in;
			}
			png_ptr->idat_size -= png_ptr->zstream1.avail_in;

			if (png_ptr->idat_size == 0)
				png_pos += 4;
		}

		int ret = inflate(&png_ptr->zstream1, Z_PARTIAL_FLUSH);

		if (ret == Z_STREAM_END)
			break;

	} while (png_ptr->zstream1.avail_out);

	if (png_ptr->row_buf[0])
		read_filter_row(png_ptr->row_buf + 1, png_ptr->prev_row + 1, (int)(png_ptr->row_buf[0]));

	memcpy(png_ptr->prev_row, png_ptr->row_buf, png_ptr->rowbytes + 1);
	memcpy(row, png_ptr->row_buf + 1, png_ptr->rowbytes);
}

const void *VDVideoSourceAPNG::streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num, VDPosition target_sample)
{
	if (frame_num < 0)
		frame_num = target_sample;
	const VDInputFileAPNGSharedData::ImageInfo& imageinfo = mpSharedData->mImages[(uint32)frame_num];

	png_src = mpSharedData->mImage.data();
	png_pos = imageinfo.mOffsetAndKey & 0x7FFFFFFF;

	if (!data_len || !png_pos) {
		if (mpSharedData->mColorType >= 4)
			mTargetFormat.info.alpha_type = FilterModPixmapInfo::kAlphaOpacity;
		return getFrameBuffer();
	}

	if (imageinfo.mOffsetAndKey & 0x80000000)
		VDMemset32Rect(mFrameBuffer.data(), mWidth * sizeof(uint32), 0, mWidth, mHeight);

	int w = imageinfo.mWidth;
	int h = imageinfo.mHeight;
	int x = imageinfo.mXOffset;
	int y = imageinfo.mYOffset;

	// bounds check positions
	if ((uint32)x >= mWidth || (uint32)y >= mHeight)
		return NULL;

	if ((uint32)(x+w) >= mWidth)
		w = mWidth - x;

	if ((uint32)(y+h) >= mHeight)
		h = mHeight - y;

	png_ptr->width = imageinfo.mWidth;
	png_ptr->height = imageinfo.mHeight;
	png_ptr->rowbytes = ROWBYTES(png_ptr->pixel_depth, png_ptr->width);

	memset(png_ptr->prev_row, 0, png_ptr->rowbytes + 1);

	if (imageinfo.mDispose == PNG_DISPOSE_OP_PREVIOUS)
		VDMemcpyRect(mRestoreBuffer.data(), w * sizeof(uint32), &mFrameBuffer[x + y*mWidth], mWidth * sizeof(uint32), w*sizeof(uint32), h);

	int    i, j;
	uint32 r, g, b, a;
	uint32 r2, g2, b2, a2;
	uint8  col;
	uint8  *sp;
	uint32 *dp;
	uint32 u, v, al;
	uint32 step = (png_ptr->bit_depth+7)/8;

	const uint32 mask4[2]={240,15};
	const uint32 shift4[2]={4,0};

	const uint32 mask2[4]={192,48,12,3};
	const uint32 shift2[4]={6,4,2,0};

	const uint32 mask1[8]={128,64,32,16,8,4,2,1};
	const uint32 shift1[8]={7,6,5,4,3,2,1,0};

	png_ptr->zstream1.avail_in = 0;

	for (j=0; j<h; j++)
		read_row(mUnpackBuffer.data() + j*png_ptr->rowbytes);

	inflateReset(&png_ptr->zstream1);

	uint8  *row = mUnpackBuffer.data();

	for (j=0; j<h; j++)
	{
		sp = row;
		dp = &mFrameBuffer[(y+j)*mWidth+x];
		if (png_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) // TRUECOLOR + ALPHA
		{
			if (imageinfo.mBlend == PNG_BLEND_OP_SOURCE)
			{
				for (i=0; i<w; i++)
				{
					r = *sp; sp += step;
					g = *sp; sp += step;
					b = *sp; sp += step;
					a = *sp; sp += step;
					*dp++ = (a != 0) ? (a << 24) + (r << 16) + (g << 8) + b : 0;
				}
			}
			else  // PNG_BLEND_OP_OVER
			{
				for (i=0; i<w; i++)
				{
					r = *sp; sp += step;
					g = *sp; sp += step;
					b = *sp; sp += step;
					a = *sp; sp += step;
					if (a == 255)
						*dp++ = (a << 24) + (r << 16) + (g << 8) + b;
					else
					if (a != 0)
					{
						if (a2 = (*dp)>>24)
						{
							u = a*255;
							v = (255-a)*a2;
							al = u + v;
							r2 = (((*dp)>>16)&255);
							g2 = (((*dp)>>8)&255);
							b2 = ((*dp)&255);
							r = (r*u + r2*v)/al;
							g = (g*u + g2*v)/al;
							b = (b*u + b2*v)/al;
							a = al/255;
						}
						*dp++ = (a << 24) + (r << 16) + (g << 8) + b;
					}
					else
						dp++;
				}
			}
		}
		else if (png_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) // GRAYSCALE + ALPHA
		{
			if (imageinfo.mBlend == PNG_BLEND_OP_SOURCE)
			{
				for (i=0; i<w; i++)
				{
					g = *sp; sp += step;
					a = *sp; sp += step;
					*dp++ = (a != 0) ? (a << 24) + (g << 16) + (g << 8) + g : 0;
				}
			}
			else // PNG_BLEND_OP_OVER
			{
				for (i=0; i<w; i++)
				{
					g = *sp; sp += step;
					a = *sp; sp += step;
					if (a == 255)
					{
						*dp++ = (a << 24) + (g << 16) + (g << 8) + g;
					}
					else
					if (a != 0)
					{
						if (a2 = (*dp)>>24)
						{
							u = a*255;
							v = (255-a)*a2;
							al = u + v;
							g2 = ((*dp)&255);
							g = (g*u + g2*v)/al;
							a = al/255;
						}
						*dp++ = (a << 24) + (g << 16) + (g << 8) + g;
					}
					else
						dp++;
				}
			}
		}
		else if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE) // INDEXED
		{
			for (i=0; i<w; i++)
			{
				switch (png_ptr->bit_depth)
				{
					case 8: col = sp[i]; break;
					case 4: col = (sp[i>>1] & mask4[i&1]) >> shift4[i&1]; break;
					case 2: col = (sp[i>>2] & mask2[i&3]) >> shift2[i&3]; break;
					case 1: col = (sp[i>>3] & mask1[i&7]) >> shift1[i&7]; break;
				}

				if (imageinfo.mBlend == PNG_BLEND_OP_SOURCE)
				{
					*dp++ = mpSharedData->mPalette[col];
				}
				else // PNG_BLEND_OP_OVER
				{
					a = mpSharedData->mPalette[col] >> 24;
					if (a == 255)
						*dp++ = mpSharedData->mPalette[col];
					else
					if (a != 0)
					{
						r = (mpSharedData->mPalette[col])&255;
						g = (mpSharedData->mPalette[col]>>8)&255;
						b = (mpSharedData->mPalette[col]>>16)&255;
						if (a2 = (*dp)>>24)
						{
							u = a*255;
							v = (255-a)*a2;
							al = u + v;
							r2 = (((*dp)>>16)&255);
							g2 = (((*dp)>>8)&255);
							b2 = ((*dp)&255);
							r = (r*u + r2*v)/al;
							g = (g*u + g2*v)/al;
							b = (b*u + b2*v)/al;
							a = al/255;
						}
						*dp++ = (a << 24) + (r << 16) + (g << 8) + b;
					}
					else
						dp++;
				}
			}
		}
		else if (png_ptr->color_type == PNG_COLOR_TYPE_RGB) // TRUECOLOR
		{
			if (imageinfo.mBlend == PNG_BLEND_OP_SOURCE)
			{
				if (png_ptr->bit_depth == 8)
				{
					for (i=0; i<w; i++)
					{
						r = *sp++;
						g = *sp++;
						b = *sp++;
						if (mpSharedData->hasTRNS && r==mpSharedData->mTRNSr && g==mpSharedData->mTRNSg && b==mpSharedData->mTRNSb)
							*dp++ = 0;
						else
							*dp++ = 0xFF000000 + (r << 16) + (g << 8) + b;
					}
				}
				else
				{
					for (i=0; i<w; i++, sp+=6)
					{
						r = *sp;
						g = *(sp+2);
						b = *(sp+4);
						if (mpSharedData->hasTRNS && VDReadUnalignedBEU16(sp)==mpSharedData->mTRNSr && VDReadUnalignedBEU16(sp+2)==mpSharedData->mTRNSg && VDReadUnalignedBEU16(sp+4)==mpSharedData->mTRNSb)
							*dp++ = 0;
						else
							*dp++ = 0xFF000000 + (r << 16) + (g << 8) + b;
					}
				}
			}
			else // PNG_BLEND_OP_OVER
			{
				if (png_ptr->bit_depth == 8)
				{
					for (i=0; i<w; i++, sp+=3, dp++)
						if ((*sp != mpSharedData->mTRNSr) || (*(sp+1) != mpSharedData->mTRNSg) || (*(sp+2) != mpSharedData->mTRNSb))
							*dp = 0xFF000000 + (*sp << 16) + (*(sp+1) << 8) + *(sp+2);
				}
				else
				{
					for (i=0; i<w; i++, sp+=6, dp++)
						if ((VDReadUnalignedBEU16(sp) != mpSharedData->mTRNSr) || (VDReadUnalignedBEU16(sp+2) != mpSharedData->mTRNSg) || (VDReadUnalignedBEU16(sp+4) != mpSharedData->mTRNSb))
							*dp = 0xFF000000 + (*sp << 16) + (*(sp+2) << 8) + *(sp+4);
				}
			}
		}
		else // GRAYSCALE
		{
			if (imageinfo.mBlend == PNG_BLEND_OP_SOURCE)
			{
				switch (png_ptr->bit_depth)
				{
					case 16: for (i=0; i<w; i++) { if (mpSharedData->hasTRNS && VDReadUnalignedBEU16(sp)==mpSharedData->mTRNSg) *dp++ = 0; else *dp++ = 0xFF000000 + (*sp << 16) + (*sp << 8) + *sp; sp+=2; }  break;
					case 8:  for (i=0; i<w; i++) { if (mpSharedData->hasTRNS && *sp==mpSharedData->mTRNSg)                      *dp++ = 0; else *dp++ = 0xFF000000 + (*sp << 16) + (*sp << 8) + *sp; sp++;  }  break;
					case 4:  for (i=0; i<w; i++) { g = (sp[i>>1] & mask4[i&1]) >> shift4[i&1]; if (mpSharedData->hasTRNS && g==mpSharedData->mTRNSg) *dp++ = 0; else *dp++ = 0xFF000000 + g*0x111111; } break;
					case 2:  for (i=0; i<w; i++) { g = (sp[i>>2] & mask2[i&3]) >> shift2[i&3]; if (mpSharedData->hasTRNS && g==mpSharedData->mTRNSg) *dp++ = 0; else *dp++ = 0xFF000000 + g*0x555555; } break;
					case 1:  for (i=0; i<w; i++) { g = (sp[i>>3] & mask1[i&7]) >> shift1[i&7]; if (mpSharedData->hasTRNS && g==mpSharedData->mTRNSg) *dp++ = 0; else *dp++ = 0xFF000000 + g*0xFFFFFF; } break;
				}
			}
			else // PNG_BLEND_OP_OVER
			{
				switch (png_ptr->bit_depth)
				{
					case 16: for (i=0; i<w; i++, dp++) { if (VDReadUnalignedBEU16(sp) != mpSharedData->mTRNSg) { *dp = 0xFF000000 + (*sp << 16) + (*sp << 8) + *sp; } sp+=2; } break;
					case 8:  for (i=0; i<w; i++, dp++) { if (*sp != mpSharedData->mTRNSg)                      { *dp = 0xFF000000 + (*sp << 16) + (*sp << 8) + *sp; } sp++;  } break;
					case 4:  for (i=0; i<w; i++, dp++) { g = (sp[i>>1] & mask4[i&1]) >> shift4[i&1]; if (g != mpSharedData->mTRNSg) *dp = 0xFF000000 + g*0x111111; } break;
					case 2:  for (i=0; i<w; i++, dp++) { g = (sp[i>>2] & mask2[i&3]) >> shift2[i&3]; if (g != mpSharedData->mTRNSg) *dp = 0xFF000000 + g*0x555555; } break;
					case 1:  for (i=0; i<w; i++, dp++) { g = (sp[i>>3] & mask1[i&7]) >> shift1[i&7]; if (g != mpSharedData->mTRNSg) *dp = 0xFF000000 + g*0xFFFFFF; } break;
				}
			}
		}
		row += png_ptr->rowbytes;
    }

	const sint32 fw = mTargetFormat.w;
	const sint32 fh = mTargetFormat.h;

	if (!is_preroll) {
		VDPixmap srcbm = {0};
		srcbm.data		= mFrameBuffer.data();
		srcbm.pitch		= fw*sizeof(uint32);
		srcbm.w			= fw;
		srcbm.h			= fh;
		srcbm.format	= nsVDPixmap::kPixFormat_XRGB8888;
		if (mpSharedData->mColorType >= 4)
			srcbm.info.alpha_type = FilterModPixmapInfo::kAlphaOpacity;

		VDPixmapBlt(mTargetFormat, srcbm);
	}

	if (imageinfo.mDispose == PNG_DISPOSE_OP_PREVIOUS)
		VDMemcpyRect(&mFrameBuffer[x + y*mWidth], mWidth * sizeof(uint32), mRestoreBuffer.data(), w * sizeof(uint32), w*sizeof(uint32), h);
	else if (imageinfo.mDispose == PNG_DISPOSE_OP_BACKGROUND)
		VDMemset32Rect(&mFrameBuffer[x + y*mWidth], mWidth * sizeof(uint32), 0, w, h);

	mCachedFrame = frame_num;

	return getFrameBuffer();
}

bool VDVideoSourceAPNG::setTargetFormat(int format) {
	if (!format)
		format = nsVDPixmap::kPixFormat_XRGB8888;

	switch(format) {
	case nsVDPixmap::kPixFormat_XRGB1555:
	case nsVDPixmap::kPixFormat_RGB565:
	case nsVDPixmap::kPixFormat_RGB888:
	case nsVDPixmap::kPixFormat_XRGB8888:
		if (!VideoSource::setTargetFormat(format))
			return false;

		stream_current_frame = -1;
		invalidateFrameBuffer();
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

class VDInputFileAPNG : public InputFile {
private:
	static INT_PTR APIENTRY _InfoDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
public:
	VDInputFileAPNG();
	~VDInputFileAPNG();

	void Init(const wchar_t *szFile);

	void setAutomated(bool fAuto);
	void InfoDialog(VDGUIHandle hwndParent);

	bool GetVideoSource(int index, IVDVideoSource **ppSrc);
	bool GetAudioSource(int index, AudioSource **ppSrc);

protected:
	vdrefptr<VDInputFileAPNGSharedData> mpSharedData;
};

VDInputFileAPNG::VDInputFileAPNG()
	: mpSharedData(new VDInputFileAPNGSharedData)
{
}

VDInputFileAPNG::~VDInputFileAPNG() {
}

void VDInputFileAPNG::Init(const wchar_t *szFile) {
	mpSharedData->Parse(szFile);
}

void VDInputFileAPNG::setAutomated(bool fAuto) {
}

bool VDInputFileAPNG::GetVideoSource(int index, IVDVideoSource **ppSrc) {
	if (index)
		return false;

	*ppSrc = new VDVideoSourceAPNG(mpSharedData);
	(*ppSrc)->AddRef();
	return true;
}

bool VDInputFileAPNG::GetAudioSource(int index, AudioSource **ppSrc) {
	return false;
}

namespace {
	struct MyFileInfo {
		vdrefptr<IVDVideoSource> mpVideo;
	};
}

INT_PTR APIENTRY VDInputFileAPNG::_InfoDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	MyFileInfo *pInfo = (MyFileInfo *)GetWindowLongPtr(hDlg, DWLP_USER);

	switch (message)
	{
		case WM_INITDIALOG:
			{
				char buf[128];

				SetWindowLongPtr(hDlg, DWLP_USER, lParam);
				pInfo = (MyFileInfo *)lParam;

				if (pInfo->mpVideo)
				{
					char *s;
					VDVideoSourceAPNG *pvs = static_cast<VDVideoSourceAPNG *>(&*pInfo->mpVideo);

					sprintf(buf, "%dx%d, %.3f fps (%ld µs)",
								pvs->getImageFormat()->biWidth,
								pvs->getImageFormat()->biHeight,
								pvs->getRate().asDouble(),
								VDRoundToLong(1000000.0 / pvs->getRate().asDouble()));
					SetDlgItemTextA(hDlg, IDC_VIDEO_FORMAT, buf);

					const sint64 length = pvs->getLength();
					s = buf + sprintf(buf, "%I64d frames (", length);
					DWORD ticks = VDRoundToInt(1000.0*length/pvs->getRate().asDouble());
					ticks_to_str(s, (buf + std::size(buf)) - s, ticks);
					sprintf(s+strlen(s),".%02d)", (ticks/10)%100);
					SetDlgItemTextA(hDlg, IDC_VIDEO_NUMFRAMES, buf);

					s = buf + sprintf(buf, "%d bpp (", pvs->getPixelDepth());
					uint8 col = pvs->getColorType();
					if (col == 0) s = s + sprintf(s, "Grayscale"); else
					if (col == 2) s = s + sprintf(s, "Truecolor"); else
					if (col == 3) s = s + sprintf(s, "Indexed"); else
					if (col == 4) s = s + sprintf(s, "Grayscale with alpha"); else
					if (col == 6) s = s + sprintf(s, "Truecolor with alpha");
					if (pvs->getBitDepth() == 16) sprintf(s, " 16 bits)"); else sprintf(s, ")");
					SetDlgItemTextA(hDlg, IDC_VIDEO_COMPRESSION, buf);
				}
			}

			return (TRUE);

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, TRUE);
			break;

		case WM_DESTROY:
			break;

		case WM_USER+256:
			EndDialog(hDlg, TRUE);
			break;
	}
	return FALSE;
}

void VDInputFileAPNG::InfoDialog(VDGUIHandle hwndParent)
{
	MyFileInfo mai;
	memset(&mai, 0, sizeof mai);
	GetVideoSource(0, ~mai.mpVideo);

	DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_APNG_INFO), (HWND)hwndParent, _InfoDlgProc, (LPARAM)&mai);
}

///////////////////////////////////////////////////////////////////////////

class VDInputDriverAPNG : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return L"Animated PNG input driver (internal)"; }

	int GetDefaultPriority() {
		return 0;
	}

	uint32 GetFlags() { return kF_Video; }

	const wchar_t *GetFilenamePattern() {
		return L"Animated PNG (*.png)\0*.png\0";
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		size_t l = wcslen(pszFilename);

		if (l>4 && !_wcsicmp(pszFilename + l - 4, L".png"))
			return true;

		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		if (nHeaderSize < 41) return kDC_Error_MoreData;
		if (nHeaderSize >= 41) {
			const uint8 *buf = (const uint8 *)pHeader;

			if (buf[0] == 137 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G' && buf[4] == 13 && buf[5] == 10 && buf[6] == 26 && buf[7] == 10)
			{
				for (int i=37; i<nHeaderSize-3; i++)
					if (buf[i] == 'a' && buf[i+1] == 'c' && buf[i+2] == 'T' && buf[i+3] == 'L')
						return kDC_High;
			}
		}

		return kDC_None;
	}

	InputFile *CreateInputFile(uint32 flags) {
		return new VDInputFileAPNG;
	}
};

extern IVDInputDriver *VDCreateInputDriverAPNG() { return new VDInputDriverAPNG; }
