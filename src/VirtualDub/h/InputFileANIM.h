//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2007 Avery Lee
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

#ifndef f_VD2_INPUTFILEANIM_H
#define f_VD2_INPUTFILEANIM_H

#ifdef _MSC_VER
	#pragma once
#endif

#include "InputFile.h"
#include "VideoSource.h"

class VDInputFileANIM : public InputFile {
public:
	VDInputFileANIM();

	void Init(const wchar_t *szFile) override;

	void setAutomated(bool fAuto);

	bool GetVideoSource(int index, IVDVideoSource **ppSrc) override;
	bool GetAudioSource(int index, AudioSource **ppSrc) override;

public:
	struct FrameInfo {
		sint64	pos;
		sint32	len;
		bool	key;
	};

	uint32				GetFrameWidth() const { return mFrameWidth; }
	uint32				GetFrameHeight() const { return mFrameHeight; }
	uint32				GetFrameCount() const { return mFrameArray.size(); }
	const FrameInfo&	GetFrameInfo(VDPosition pos) const;
	int					GetBitplaneCount() const { return mPlanes; }

	uint32				GetAmigaViewportMode() const { return mAmigaViewportMode; }
	uint8				GetCompressionMode() const { return mCompressionMode; }
	uint32				GetCompressionOptions() const { return mCompressionOptions; }

	const uint32*		GetColorMap() const { return mColorMap; }

	void				ReadSpan(sint64 pos, void *buffer, uint32 len);

protected:
	uint32	mFrameWidth;
	uint32	mFrameHeight;
	int		mPlanes;
	uint8	mCompressionMode;
	uint32	mCompressionOptions;
	uint32	mAmigaViewportMode;

	typedef vdfastvector<FrameInfo>	FrameArray;
	FrameArray mFrameArray;

	VDFile	mFile;

	uint32	mColorMap[256];
};

class VDVideoSourceANIM : public VideoSource {
public:
	VDVideoSourceANIM(VDInputFileANIM *parent);

	int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) override;
	bool _isKey(VDPosition samp) override;
	VDPosition nearestKey(VDPosition lSample) override;
	VDPosition prevKey(VDPosition lSample) override;
	VDPosition nextKey(VDPosition lSample) override;

	bool setTargetFormat(VDPixmapFormatEx format) override;

	void invalidateFrameBuffer() override		{ mCachedFrame = -1; }
	bool isFrameBufferValid() override			{ return mCachedFrame >= 0; }
	bool isStreaming() override					{ return false; }

	const void *getFrame(VDPosition lFrameDesired) override;

	void streamBegin(bool fRealTime, bool bForceReset) override;
	void streamEnd() override;
	const void *streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition sample_num, VDPosition target_sample) override;

	char getFrameTypeChar(VDPosition lFrameNum) override	{ return !lFrameNum ? 'K' : ' '; }
	eDropType getDropType(VDPosition lFrameNum) override	{ return !lFrameNum ? kIndependent : kDependant; }
	bool isKeyframeOnly() override							{ return mFrameCount == 1; }
	bool isType1() override									{ return false; }
	bool isDecodable(VDPosition sample_num) override		{ return !sample_num || mCachedFrame == sample_num-1; }

private:
	void DecompressMode5(const uint8 *src, uint32 srclen, uint8 *dst, ptrdiff_t planepitch);
	void DecompressMode7Short(const uint8 *src, uint32 srclen, uint8 *dst, ptrdiff_t planepitch);
	void DecompressMode7Long(const uint8 *src, uint32 srclen, uint8 *dst, ptrdiff_t planepitch);

	vdrefptr<VDInputFileANIM> mpParent;

	uint32	mWidth;
	uint32	mHeight;
	int		mPlanes;

	VDPosition mCachedFrame;
	VBitmap	mvbFrameBuffer;
	uint32	mFrameCount;
	bool	mbDecodeStarted;
	bool	mbDecodeRealTime;

	vdfastvector<uint8>	mDeltaPlanes[2];
	vdfastvector<uint8>	mPalBuffer;
};

#endif
