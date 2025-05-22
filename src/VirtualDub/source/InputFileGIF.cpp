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

#include <vd2/system/file.h>
#include <vd2/system/fraction.h>
#include <vd2/system/binary.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include "InputFile.h"
#include "VideoSource.h"
#include "resource.h"
#include "gui.h"

extern HINSTANCE g_hInst;

///////////////////////////////////////////////////////////////////////////

class VDInputFileGIFSharedData : public vdrefcounted<IVDRefCount> {
public:
	VDInputFileGIFSharedData();

	void Parse(const wchar_t *filename);

	vdblock<uint8>	mImage;
	uint32		mWidth;
	uint32		mHeight;
	uint32		mPixelDepth;
	uint32		mColors;
	uint32		mBackgroundColor;
	bool		mbKeyframeOnly;
	VDFraction	mFrameRate;

	enum {
		kRestoreNone,
		kRestoreToColor,
		kRestoreToPrevious
	};

	struct ImageInfo {
		uint32	mOffsetAndKey;
		sint16	mTranspColor;
		uint8	mRestoreMode;
	};

	typedef vdfastvector<ImageInfo> Images;
	Images		mImages;
	uint32		mGlobalColorTable[256];
};

VDInputFileGIFSharedData::VDInputFileGIFSharedData()
	: mWidth(0)
	, mHeight(0)
{
}

void VDInputFileGIFSharedData::Parse(const wchar_t *filename) {
	VDFile file(filename, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting);

	sint64 len64 = file.size();

	if (len64 > 0x3FFFFFFF)
		throw MyError("The GIF image \"%ls\" is too large to read.", filename);

	uint32 len = (uint32)len64;

	mImage.resize(len);
	file.read(mImage.data(), len);

	// check header
	const uint8 *src = mImage.data();
	uint32 pos = 0;
	if (len - pos < 6 || src[0] != 'G' || src[1] != 'I' || src[2] != 'F')
		throw MyError("File \"%ls\" is not a GIF file.", filename);
	pos += 6;

	// read logical screen descriptor
	if (len - pos < 7)
		throw MyError("File \"%ls\" is an invalid GIF file.", filename);

	mWidth = VDReadUnalignedLEU16(&src[pos]);
	mHeight = VDReadUnalignedLEU16(&src[pos + 2]);

	uint8 backgroundColorIndex = src[pos + 5];

	bool hasGlobalColorTable = (src[pos + 4] & 0x80) != 0;
	uint32 globalColorTableBits = (src[pos + 4] & 7) + 1;
	mPixelDepth = ((src[pos + 4] & 0x70)>>4) + 1;

	pos += 7;

	// parse global color table
	mBackgroundColor = 0;
	if (hasGlobalColorTable) {
		mColors = 1 << globalColorTableBits;

		if (len - pos < 3 * mColors)
			throw MyError("File \"%ls\" is an invalid GIF file. (Unable to read global color table at position %x)", filename, pos);

		for(uint32 i=0; i < mColors; ++i) {
			mGlobalColorTable[i] = 0xFF000000 + ((uint32)src[pos+0] << 16) + ((uint32)src[pos+1] << 8) + (uint32)src[pos+2];
			pos += 3;
		}

		VDMemset32(mGlobalColorTable + mColors, 0xFFFFFFFF, 256 - mColors);

		mBackgroundColor = mGlobalColorTable[backgroundColorIndex] & 0x00FFFFFF;
	} else {
		mColors = 0;

		for(int i=0; i<256; ++i)
			mGlobalColorTable[i] = 0x010101*i + 0xFF000000;
	}

	// parse blocks
	vdfastvector<uint32> presentationTimes;
	uint32 timebase = 0;
	uint32 spantotal = 0;
	uint32 spancount = 0;
	ImageInfo imageinfo = { 0, -1, kRestoreNone };
	uint16 nextFrameDelay = 0;
	for(;;) {
		if (len - pos < 1)
			break;

		uint8 blockCode = src[pos++];

		// parse GIF end marker
		if (blockCode == 0x3B)
			break;

		// parse GIF extension block
		if (blockCode == 0x21) {
			if (len - pos < 2)
				break;

			uint8 extensionCode = src[pos++];
			uint8 length = src[pos++];

			if (len - pos < length)
				break;

			if (extensionCode == 0xF9) {
				if (length != 4)
					throw MyError("File \"%ls\" is an invalid GIF file. (Graphic Control Extension header size is not 4 bytes)", filename, pos);

				nextFrameDelay = VDReadUnalignedLEU16(&src[pos + 1]);

				uint8 flags = src[pos];

				imageinfo.mTranspColor = -1;
				if (flags & 0x01)
				{
					imageinfo.mTranspColor = src[pos + 3];
					mGlobalColorTable[imageinfo.mTranspColor] &= 0x00FFFFFF;
				}

				uint8 disposalMode = (flags >> 2) & 7;

				switch(disposalMode) {
					case 2:
						imageinfo.mRestoreMode = kRestoreToColor;
						break;
					case 3:
						imageinfo.mRestoreMode = kRestoreToPrevious;
						break;
					default:
						imageinfo.mRestoreMode = kRestoreNone;
						break;
				}
			}

			pos += length;

			// parse data blocks
			for(;;) {
				if (len - pos < 1)
					goto finish;

				uint8 dataBlockLen = src[pos++];
				if (!dataBlockLen)
					break;

				if (len - pos < dataBlockLen)
					goto finish;

				pos += dataBlockLen;
			}
			continue;
		}

		// parse GIF frame
		if (blockCode != 0x2C)
			break;

		imageinfo.mOffsetAndKey = pos;

		// parse image header
		if (len - pos < 9)
			goto finish;

		uint32 x = VDReadUnalignedLEU16(&src[pos + 0]);
		uint32 y = VDReadUnalignedLEU16(&src[pos + 2]);
		uint32 w = VDReadUnalignedLEU16(&src[pos + 4]);
		uint32 h = VDReadUnalignedLEU16(&src[pos + 6]);

		// detect key frames
		if (mImages.empty() || (!x && !y && w == mWidth && h == mHeight && imageinfo.mTranspColor < 0))
			imageinfo.mOffsetAndKey |= 0x80000000;

		bool hasLocalColorTable = (src[pos + 8] & 0x80) != 0;
		uint32 localColorTableBits = (src[pos + 8] & 7) + 1;

		pos += 9;

		if (hasLocalColorTable) {
			uint32 localColorTableSize = 1 << localColorTableBits;

			if (len - pos < 3 * localColorTableSize)
				throw MyError("File \"%ls\" is an invalid GIF file. (Unable to read local color table at position %x)", filename, pos);

			pos += 3*localColorTableSize;
		}

		// skip minimum code size marker
		if (len - pos < 1)
			goto finish;

		++pos;

		// skip data blocks
		for(;;) {
			if (len - pos < 1)
				goto finish;

			uint8 dataBlockLen = src[pos++];
			if (!dataBlockLen)
				break;

			if (len - pos < dataBlockLen)
				goto finish;

			pos += dataBlockLen;
		}

		if (!nextFrameDelay)
			nextFrameDelay = 10;
		presentationTimes.push_back(timebase);
		timebase += nextFrameDelay;

		if (presentationTimes.size() > 2) {
			spantotal += timebase - *(presentationTimes.end() - 3);
			++spancount;
		} else if (presentationTimes.size() == 2) {
			spantotal += timebase;
			++spancount;
		}

		mImages.push_back(imageinfo);

		imageinfo.mTranspColor = -1;
		imageinfo.mRestoreMode = kRestoreNone;

		nextFrameDelay = 0;
	}
finish:
	;

	// compute time to frame mapping
	if (mImages.empty())
		throw MyError("No video frames detected in GIF file.");

	uint32 numFrames = mImages.size();

	mFrameRate.Assign(10, 1);
	if (numFrames > 1 && !presentationTimes.empty()) {
		vdfastvector<ImageInfo> images;

		VDFraction timeToFrameFactor(spancount * 2);
		timeToFrameFactor /= spantotal;

		for(uint32 i=0; i<numFrames; ++i) {
			uint32 delta = presentationTimes[i] - presentationTimes.front();
			int frame = (timeToFrameFactor * delta).asInt();

			if (frame > images.size()) {
				ImageInfo dummy;
				dummy.mOffsetAndKey = 0;
				dummy.mTranspColor = 0;
				images.resize(frame, dummy);
			}

			images.push_back(mImages[i]);
		}

		mImages.swap(images);

		mFrameRate = timeToFrameFactor * 100;
	}

	mbKeyframeOnly = true;
	for(Images::const_iterator it(mImages.begin()), itEnd(mImages.end()); it!=itEnd; ++it) {
		if (!(it->mOffsetAndKey & 0x80000000))
			mbKeyframeOnly = false;
	}
}

///////////////////////////////////////////////////////////////////////////

class VDVideoSourceGIF : public VideoSource {
private:
	vdrefptr<VDInputFileGIFSharedData> mpSharedData;
	VDPosition	mCachedFrame;
	uint32		mWidth;
	uint32		mHeight;

	vdfastvector<uint8> mUnpackBuffer;
	vdfastvector<uint32> mFrameBuffer;
	vdfastvector<uint32> mRestoreBuffer;

public:
	VDVideoSourceGIF(VDInputFileGIFSharedData *sharedData);

	int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) override;

	bool setTargetFormat(VDPixmapFormatEx format) override;

	void invalidateFrameBuffer() override		{ mCachedFrame = -1; }
	bool isFrameBufferValid() override			{ return mCachedFrame >= 0; }
	bool isStreaming() override					{ return false; }

	const void *getFrame(VDPosition lFrameDesired) override;
	uint32 getPixelDepth()						{ return mpSharedData->mPixelDepth; }
	uint32 getColors()							{ return mpSharedData->mColors; }
	void streamBegin(bool, bool bForceReset) override {
		if (bForceReset)
			streamRestart();
	}

	const void *streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition sample_num, VDPosition target_sample) override;

	char getFrameTypeChar(VDPosition lFrameNum)	override { return isKey(lFrameNum) ? 'K' : ' '; }
	eDropType getDropType(VDPosition lFrameNum)	override { return isKey(lFrameNum) ? kDependant : kIndependent; }
	bool isKey(VDPosition lSample)				override { return lSample >= 0 && lSample < (VDPosition)mpSharedData->mImages.size() && (0x80000000 & mpSharedData->mImages[(uint32)lSample].mOffsetAndKey) != 0; }

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

VDVideoSourceGIF::VDVideoSourceGIF(VDInputFileGIFSharedData *sharedData)
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
	mUnpackBuffer.resize(mWidth * mHeight);
	AllocFrameBuffer(bih->biSizeImage);

	mSampleLast = mSampleFirst + mpSharedData->mImages.size();
}

int VDVideoSourceGIF::_read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
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

const void *VDVideoSourceGIF::getFrame(VDPosition frameNum) {
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

const void *VDVideoSourceGIF::streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num, VDPosition target_sample) {
	if (frame_num < 0)
		frame_num = target_sample;
	const VDInputFileGIFSharedData::ImageInfo& imageinfo = mpSharedData->mImages[(uint32)frame_num];
	uint32 pos = imageinfo.mOffsetAndKey & 0x7FFFFFFF;
	sint16 transpColor = imageinfo.mTranspColor;

	if (!data_len || !pos) {
		mTargetFormat.info.alpha_type = FilterModPixmapInfo::kAlphaOpacity;
		return getFrameBuffer();
	}

	if (imageinfo.mOffsetAndKey & 0x80000000)
		VDMemset32Rect(mFrameBuffer.data(), mWidth * sizeof(uint32), mpSharedData->mBackgroundColor, mWidth, mHeight);

	// decompress lines
	const uint8 *src = mpSharedData->mImage.data();

	struct DictionaryEntry {
		sint32	mPrev;
		uint8	mFirstChar;
		uint8	mLastChar;
		uint16	mLength;
	} dict[4096] = {0};

	int x = VDReadUnalignedLEU16(&src[pos + 0]);
	int y = VDReadUnalignedLEU16(&src[pos + 2]);
	int w = VDReadUnalignedLEU16(&src[pos + 4]);
	int h = VDReadUnalignedLEU16(&src[pos + 6]);

	// bounds check positions
	if ((uint32)x >= mWidth || (uint32)y >= mHeight)
		return NULL;

	if ((uint32)(x+w) >= mWidth)
		w = mWidth - x;

	if ((uint32)(y+h) >= mHeight)
		h = mHeight - y;

	if (imageinfo.mRestoreMode == VDInputFileGIFSharedData::kRestoreToPrevious)
		VDMemcpyRect(mRestoreBuffer.data(), w * sizeof(uint32), &mFrameBuffer[x + y*mWidth], mWidth * sizeof(uint32), w*sizeof(uint32), h);

	bool hasLocalColorTable = (src[pos + 8] & 0x80) != 0;
	bool interlaced = (src[pos + 8] & 0x40) != 0;
	uint32 localColorTableBits = (src[pos + 8] & 7) + 1;

	pos += 9;

	uint32 localColorTable[256];

	if (hasLocalColorTable) {
		uint32 localColorTableSize = 1 << localColorTableBits;

		for(uint32 i=0; i < localColorTableSize; ++i) {
			localColorTable[i] = 0xFF000000 + ((uint32)src[pos+0] << 16) + ((uint32)src[pos+1] << 8) + (uint32)src[pos+2];
			pos += 3;
		}

		VDMemset32(localColorTable + localColorTableSize, 0xFFFFFFFF, 256 - localColorTableSize);

		if (transpColor >= 0)
			localColorTable[transpColor] &= 0x00FFFFFF;
	}

	uint8 minimumCodeSize = src[pos++];
	if (minimumCodeSize > 8)
		throw MyError("Decoding error in frame %ld (invalid minimum code size).", (long)frame_num);
	uint32 baseCodes = 1 << minimumCodeSize;

	for(uint32 i=0; i<baseCodes; ++i) {
		dict[i].mPrev = -1;
		dict[i].mFirstChar = (uint8)i;
		dict[i].mLastChar = (uint8)i;
		dict[i].mLength = 1;
	}

	const uint32 *palette = hasLocalColorTable ? localColorTable : mpSharedData->mGlobalColorTable;

	uint32 bytesLeft = mWidth * mHeight;
	uint32 bytesLeftInBlock = 0;
	uint8 *dst = mUnpackBuffer.data();

	uint32 accum = 0;
	uint32 accumBits = 0;
	uint32 codeLen = minimumCodeSize + 1;
	uint32 codeMask = (1 << codeLen) - 1;
	uint32 clearCode = 1 << minimumCodeSize;
	uint32 eosCode = clearCode + 1;
	uint32 firstCode = eosCode + 1;
	uint32 nextCode = firstCode;
	int lastCode = -1;

	while(bytesLeft > 0) {
		while(accumBits < codeLen) {
			if (!bytesLeftInBlock) {
				bytesLeftInBlock = src[pos++];
				if (!bytesLeftInBlock)
					goto xit;
			}

			accum += src[pos++] << accumBits;
			accumBits += 8;
			--bytesLeftInBlock;
		}

		uint32 code = accum & codeMask;
		accum >>= codeLen;
		accumBits -= codeLen;

		if (code == eosCode)
			break;

		if (code == clearCode) {
			codeLen = minimumCodeSize + 1;
			codeMask = (1 << codeLen) - 1;
			nextCode = firstCode;
			lastCode = -1;
			continue;
		}

		// handle WOWOW case
		if (code == nextCode) {
			if (lastCode < 0)
				break;

			dict[nextCode].mPrev = (uint32)lastCode;
			dict[nextCode].mFirstChar = dict[lastCode].mFirstChar;
			dict[nextCode].mLastChar = dict[lastCode].mFirstChar;
			dict[nextCode].mLength = dict[lastCode].mLength + 1;
		}

		const DictionaryEntry *ent = &dict[code];
		uint32 l = ent->mLength;
		if (bytesLeft < l || !l)
			break;

		bytesLeft -= l;

		dst += l;
		uint8 *dst2 = dst;

		do {
			uint8 c = ent->mLastChar;
			*--dst2 = c;

			if (ent->mPrev < 0)
				break;
			ent = &dict[ent->mPrev];
		} while(--l);

		// add new code to dictionary
		if (lastCode >= 0 && nextCode < 4096) {
			dict[nextCode].mPrev = (uint32)lastCode;
			dict[nextCode].mLastChar = ent->mLastChar;
			dict[nextCode].mFirstChar = dict[lastCode].mFirstChar;
			dict[nextCode].mLength = dict[lastCode].mLength + 1;

			if (nextCode == codeMask && nextCode < 4095) {
				++codeLen;
				codeMask = (codeMask + codeMask) + 1;
			}

			++nextCode;
		}

		lastCode = code;
	}

xit:
	const sint32 fw = mTargetFormat.w;
	const sint32 fh = mTargetFormat.h;

	static const uint8 kInterlaceTable[5][2]={
		{ 0, 8 },
		{ 4, 8 },
		{ 2, 4 },
		{ 1, 2 },
		{ 0, 1 },
	};

	int passstart = interlaced ? 0 : 4;
	int passend = interlaced ? 4 : 5;
	const uint8 *srcp = mUnpackBuffer.data();
	for(int pass=passstart; pass<passend; ++pass) {
		uint32 dsty = kInterlaceTable[pass][0];
		uint32 dstystep = kInterlaceTable[pass][1];

		while(dsty < h) {
			uint32 *dstp = &mFrameBuffer[fw * (y + dsty) + x];

			if (transpColor >= 0) {
				uint8 transpColor8 = (uint8)transpColor;
				for(uint32 i=0; i<w; ++i) {
					const uint8 c = *srcp++;
					if (c != transpColor8)
						dstp[i] = palette[c];
				}
			} else {
				for(uint32 i=0; i<w; ++i) {
					const uint8 c = *srcp++;
					dstp[i] = palette[c];
				}
			}

			dsty += dstystep;
		}
	}

	if (!is_preroll) {
		VDPixmap srcbm = {0};
		srcbm.data		= mFrameBuffer.data();
		srcbm.pitch		= fw*sizeof(uint32);
		srcbm.w			= fw;
		srcbm.h			= fh;
		srcbm.format	= nsVDPixmap::kPixFormat_XRGB8888;
		srcbm.info.alpha_type = FilterModPixmapInfo::kAlphaOpacity;

		VDPixmapBlt(mTargetFormat, srcbm);
	}

	if (imageinfo.mRestoreMode == VDInputFileGIFSharedData::kRestoreToPrevious)
		VDMemcpyRect(&mFrameBuffer[x + y*mWidth], mWidth * sizeof(uint32), mRestoreBuffer.data(), w * sizeof(uint32), w*sizeof(uint32), h);
	else if (imageinfo.mRestoreMode == VDInputFileGIFSharedData::kRestoreToColor)
		VDMemset32Rect(&mFrameBuffer[x + y*mWidth], mWidth * sizeof(uint32), mpSharedData->mBackgroundColor, w, h);

	mCachedFrame = frame_num;

	return getFrameBuffer();
}

bool VDVideoSourceGIF::setTargetFormat(VDPixmapFormatEx format) {
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

class VDInputFileGIF : public InputFile {
private:
	static INT_PTR APIENTRY _InfoDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
public:
	VDInputFileGIF();
	~VDInputFileGIF();

	void Init(const wchar_t *szFile);

	void setAutomated(bool fAuto);
	void InfoDialog(VDGUIHandle hwndParent);

	bool GetVideoSource(int index, IVDVideoSource **ppSrc);
	bool GetAudioSource(int index, AudioSource **ppSrc);

protected:
	vdrefptr<VDInputFileGIFSharedData> mpSharedData;
};

VDInputFileGIF::VDInputFileGIF()
	: mpSharedData(new VDInputFileGIFSharedData)
{
}

VDInputFileGIF::~VDInputFileGIF() {
}

void VDInputFileGIF::Init(const wchar_t *szFile) {
	mpSharedData->Parse(szFile);
}

void VDInputFileGIF::setAutomated(bool fAuto) {
}

bool VDInputFileGIF::GetVideoSource(int index, IVDVideoSource **ppSrc) {
	if (index)
		return false;

	*ppSrc = new VDVideoSourceGIF(mpSharedData);
	(*ppSrc)->AddRef();
	return true;
}

bool VDInputFileGIF::GetAudioSource(int index, AudioSource **ppSrc) {
	return false;
}

namespace {
	struct MyFileInfo {
		vdrefptr<IVDVideoSource> mpVideo;
	};
}

INT_PTR APIENTRY VDInputFileGIF::_InfoDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
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
					VDVideoSourceGIF *pvs = static_cast<VDVideoSourceGIF *>(&*pInfo->mpVideo);

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

					s = buf + sprintf(buf, "%d bpp", pvs->getPixelDepth());
					uint32 col = pvs->getColors();
					if (col > 0) s = s + sprintf(s, " (%d colors palette)", col);
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

void VDInputFileGIF::InfoDialog(VDGUIHandle hwndParent)
{
	MyFileInfo mai;
	memset(&mai, 0, sizeof mai);
	GetVideoSource(0, ~mai.mpVideo);

	DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_GIF_INFO), (HWND)hwndParent, _InfoDlgProc, (LPARAM)&mai);
}

///////////////////////////////////////////////////////////////////////////

class VDInputDriverGIF : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return L"Animated GIF input driver (internal)"; }

	int GetDefaultPriority() {
		return 0;
	}

	uint32 GetFlags() { return kF_Video; }

	const wchar_t *GetFilenamePattern() {
		return L"Animated GIF (*.gif)\0*.gif\0";
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		size_t l = wcslen(pszFilename);

		if (l>4 && !_wcsicmp(pszFilename + l - 4, L".gif"))
			return true;

		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		if (nHeaderSize < 3) return kDC_Error_MoreData;
		if (nHeaderSize >= 3) {
			const uint8 *buf = (const uint8 *)pHeader;

			if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F')
				return kDC_High;
		}

		return kDC_None;
	}

	InputFile *CreateInputFile(uint32 flags) {
		return new VDInputFileGIF;
	}
};

extern IVDInputDriver *VDCreateInputDriverGIF() { return new VDInputDriverGIF; }
