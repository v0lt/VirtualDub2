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

#ifndef f_VIDEOSOURCE_H
#define f_VIDEOSOURCE_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/VDString.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Riza/videocodec.h>
#include <vd2/Riza/avi.h>

#include "DubSource.h"

class IVDStreamSource;

class IVDVideoSource : public IVDRefCount {
public:
	virtual IVDStreamSource *asStream() = 0;

	virtual VDAVIBitmapInfoHeader *getImageFormat() = 0;

	virtual const void *getFrameBuffer() = 0;
	virtual const VDFraction getPixelAspectRatio() const = 0;

	virtual VDPixmapFormatEx getDefaultFormat() = 0;
	virtual VDPixmapFormatEx getSourceFormat() = 0;
	virtual const VDPixmap& getTargetFormat() = 0;
	virtual bool		setTargetFormat(VDPixmapFormatEx format) = 0;
	virtual bool		setDecompressedFormat(int depth) = 0;
	virtual bool		setDecompressedFormat(const VDAVIBitmapInfoHeader *pbih) = 0;

	virtual VDAVIBitmapInfoHeader *getDecompressedFormat() = 0;
	virtual uint32		getDecompressedFormatLen() = 0;

	virtual void		streamSetDesiredFrame(VDPosition frame_num) = 0;
	virtual VDPosition	streamGetNextRequiredFrame(bool& is_preroll) = 0;
	virtual int			streamGetRequiredCount(uint32 *totalsize) = 0;
	virtual const void *streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition sample_num, VDPosition target_sample) = 0;
	virtual uint32		streamGetDecodePadding() = 0;
	virtual void		streamFillDecodePadding(void *buffer, uint32 data_len) = 0;

	virtual bool		streamOwn(void *owner) = 0;
	virtual void		streamDisown(void *owner) = 0;
	virtual void		streamBegin(bool fRealTime, bool bForceReset) = 0;
	virtual void		streamRestart() = 0;
	virtual void		streamAppendReinit() = 0;

	virtual void		invalidateFrameBuffer() = 0;
	virtual	bool		isFrameBufferValid() = 0;

	virtual const void *getFrame(VDPosition frameNum) = 0;

	virtual char		getFrameTypeChar(VDPosition lFrameNum) = 0;

	enum eDropType {
		kDroppable		= 0,
		kDependant,
		kIndependent,
	};

	virtual eDropType	getDropType(VDPosition lFrameNum) = 0;

	virtual bool isKey(VDPosition lSample) = 0;
	virtual VDPosition nearestKey(VDPosition lSample) = 0;
	virtual VDPosition prevKey(VDPosition lSample) = 0;
	virtual VDPosition nextKey(VDPosition lSample) = 0;

	virtual bool		isKeyframeOnly() = 0;
	virtual bool		isSyncDecode() = 0;
	virtual bool		isType1() = 0;

	virtual VDPosition	streamToDisplayOrder(VDPosition sample_num) = 0;
	virtual VDPosition	displayToStreamOrder(VDPosition display_num) = 0;
	virtual VDPosition	getRealDisplayFrame(VDPosition display_num) = 0;

	virtual bool		isDecodable(VDPosition sample_num) = 0;

	virtual sint64		getSampleBytePosition(VDPosition sample_num) = 0;
};

class VideoSource : public DubSource, public IVDVideoSource {
protected:
	std::unique_ptr<void, decltype(&VDAlignedFree)> mpFrameBuffer{ nullptr, &VDAlignedFree };
	uint32		mFrameBufferSize{};

	vdstructex<VDAVIBitmapInfoHeader> mpTargetFormatHeader;
	VDPixmap	mTargetFormat;
	int			mTargetFormatVariant;
	VDPixmapFormatEx	mDefaultFormat{};
	VDPixmapFormatEx	mSourceFormat{};
	VDPosition	stream_desired_frame;
	VDPosition	stream_current_frame{ -1 };

	void		*mpStreamOwner{};

	uint32		mPalette[256];

	void *AllocFrameBuffer(uint32 size);

	bool setTargetFormatVariant(VDPixmapFormatEx format, int variant);
	virtual bool _isKey(VDPosition lSample);

	VideoSource();

public:
	enum {
		IFMODE_NORMAL		=0,
		IFMODE_SWAP			=1,
		IFMODE_SPLIT1		=2,
		IFMODE_SPLIT2		=3,
		IFMODE_DISCARD1		=4,
		IFMODE_DISCARD2		=5,
	};

	IVDStreamSource *asStream() override { return this; }

	int AddRef() override { return DubSource::AddRef(); }
	int Release() override { return DubSource::Release(); }

	VDAVIBitmapInfoHeader *getImageFormat() override {
		return (VDAVIBitmapInfoHeader *)getFormat();
	}

	const VDFraction getPixelAspectRatio() const override;

	const void *getFrameBuffer() override {
		return mpFrameBuffer.get();
	}

	VDPixmapFormatEx getDefaultFormat() override { return mDefaultFormat; }
	VDPixmapFormatEx getSourceFormat() override { return mSourceFormat; }
	const VDPixmap& getTargetFormat() override { return mTargetFormat; }
	bool setTargetFormat(VDPixmapFormatEx format) override;
	bool setDecompressedFormat(int depth) override;
	bool setDecompressedFormat(const VDAVIBitmapInfoHeader *pbih)override;

	VDAVIBitmapInfoHeader *getDecompressedFormat() override {
		return mpTargetFormatHeader.empty() ? NULL : mpTargetFormatHeader.data();
	}

	uint32 getDecompressedFormatLen() override {
		return mpTargetFormatHeader.size();
	}

	void streamSetDesiredFrame(VDPosition frame_num) override;
	VDPosition streamGetNextRequiredFrame(bool& is_preroll) override;
	int	streamGetRequiredCount(uint32 *totalsize) override;
	uint32 streamGetDecodePadding() override { return 0; }
	void streamFillDecodePadding(void *inputBuffer, uint32 data_len) override {}

	bool streamOwn(void *owner) override;
	void streamDisown(void *owner) override;
	void streamBegin(bool fRealTime, bool bForceReset) override;
	void streamRestart() override;
	void streamAppendReinit() override {}

	void invalidateFrameBuffer() override;

	bool isKey(VDPosition lSample) override;
	VDPosition nearestKey(VDPosition lSample) override;
	VDPosition prevKey(VDPosition lSample) override;
	VDPosition nextKey(VDPosition lSample) override;

	bool isKeyframeOnly() override;
	bool isSyncDecode() override;
	bool isType1() override;

	VDPosition	streamToDisplayOrder(VDPosition sample_num) override { return sample_num; }
	VDPosition	displayToStreamOrder(VDPosition display_num) override { return display_num; }
	VDPosition	getRealDisplayFrame(VDPosition display_num) override { return display_num; }

	sint64		getSampleBytePosition(VDPosition sample_num) override { return -1; }
};

#endif
