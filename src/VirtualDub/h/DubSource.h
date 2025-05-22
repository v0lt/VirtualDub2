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

#ifndef f_DUBSOURCE_H
#define f_DUBSOURCE_H

#ifdef _MSC_VER
	#pragma once
#endif
#ifndef f_VD2_SYSTEM_FRACTION_H
	#include <vd2/system/fraction.h>
#endif

#ifndef f_VD2_SYSTEM_REFCOUNT_H
	#include <vd2/system/refcount.h>
#endif

#ifndef f_VD2_RIZA_AVI_H
	#include <vd2/Riza/avi.h>
#endif

class InputFile;

class IVDStreamSource : public IVDRefCount {
public:
	virtual VDPosition	getLength() = 0;
	virtual VDPosition	getStart() = 0;
	virtual VDPosition	getEnd() = 0;
	virtual const VDFraction getRate() = 0;

	virtual const VDAVIStreamInfo& getStreamInfo() = 0;

	// Temporary replacements for AVIERR_OK and AVIERR_BUFFERTOOSMALL
	enum {
		kOK				= 0,
		kBufferTooSmall = 0x80044074,
		kFileReadError	= 0x8004406d
	};
	enum {
		kConvenient = -1
	};

	virtual int read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) = 0;

	virtual void *getFormat() const = 0;
	virtual int getFormatLen() const = 0;

	virtual bool isStreaming() = 0;

	virtual void streamBegin(bool fRealTime, bool bForceReset) = 0;
	virtual void streamEnd() = 0;

	virtual void applyStreamMode(sint32 flags){}

	enum ErrorMode {
		kErrorModeReportAll = 0,
		kErrorModeConceal,
		kErrorModeDecodeAnyway,
		kErrorModeCount
	};

	virtual ErrorMode getDecodeErrorMode() = 0;
	virtual void setDecodeErrorMode(ErrorMode mode) = 0;
	virtual bool isDecodeErrorModeSupported(ErrorMode mode) = 0;

	virtual VDPosition msToSamples(VDTime lMs) const = 0;
	virtual VDTime samplesToMs(VDPosition lSamples) const = 0;

	enum VBRMode {
		kVBRModeNone,
		kVBRModeTimestamped,
		kVBRModeVariableFrames
	};

	virtual void IsVBR() const {}

	virtual VBRMode GetVBRMode() const = 0;
	virtual VDPosition TimeToPositionVBR(VDTime us) const = 0;
	virtual VDTime PositionToTimeVBR(VDPosition samples) const = 0;
	virtual const char* GetProfileComment() const { return 0; }
	virtual void SetProfileComment(const char* s) {}
};

class DubSource : public vdrefcounted<IVDStreamSource> {
private:
	std::unique_ptr<char[]>	format;
	int		format_len{};

protected:
	void *allocFormat(int format_len);

	VDPosition	mSampleFirst;
	VDPosition	mSampleLast;
	VDAVIStreamInfo	streamInfo;

	DubSource();

public:
	VDStringA profile_comment;

	const char* GetProfileComment() const override { return profile_comment.empty() ? 0:profile_comment.c_str(); }
	void SetProfileComment(const char* s) override { profile_comment = s; }

	VDPosition getLength() override {
		return mSampleLast - mSampleFirst;
	}

	VDPosition getStart() override {
		return mSampleFirst;
	}

	VDPosition getEnd() override {
		return mSampleLast;
	}

	const VDFraction getRate() override {
		return VDFraction(streamInfo.dwRate, streamInfo.dwScale);
	}

	const VDAVIStreamInfo& getStreamInfo() override {
		return streamInfo;
	}

	int read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) override;
	virtual int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) = 0;

	void *getFormat() const override { return format.get(); }
	int getFormatLen() const override { return format_len; }

	bool isStreaming() override;


	void streamBegin(bool fRealTime, bool bForceReset) override;
	void streamEnd() override;

	ErrorMode getDecodeErrorMode() override { return kErrorModeReportAll; }
	void setDecodeErrorMode(ErrorMode mode) override  {}
	bool isDecodeErrorModeSupported(ErrorMode mode) override { return mode == kErrorModeReportAll; }

	VDPosition msToSamples(VDTime lMs) const override {
		const sint64 denom = (sint64)1000 * streamInfo.dwScale;
		return (lMs * streamInfo.dwRate + (denom >> 1)) / denom;
	}

	VDTime samplesToMs(VDPosition lSamples) const override {
		return ((lSamples * streamInfo.dwScale) * 1000 + (streamInfo.dwRate >> 1)) / streamInfo.dwRate;
	}

	VDPosition TimeToPositionVBR(VDTime us) const override {
		return VDRoundToInt64(us * (double)streamInfo.dwRate / (double)streamInfo.dwScale * (1.0 / 1000000.0));
	}

	VDTime PositionToTimeVBR(VDPosition samples) const override {
		return VDRoundToInt64(samples * (double)streamInfo.dwScale / (double)streamInfo.dwRate * 1000000.0);
	}

	VBRMode GetVBRMode() const override { return kVBRModeNone; }
};

#endif