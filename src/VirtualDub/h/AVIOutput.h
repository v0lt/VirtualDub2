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

#ifndef f_AVIOUTPUT_H
#define f_AVIOUTPUT_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/file.h>
#include <vd2/system/unknown.h>
#include <vd2/plugin/vdinputdriver.h>

#include "Fixes.h"

struct VDPixmap;
struct FilterModPixmapInfo;

class IVDMediaOutputStream : public IVDUnknown {
public:
	enum { kTypeID = 'mots' };

	virtual ~IVDMediaOutputStream() {}		// shouldn't be here but need to get rid of common delete in root destructor

	virtual void *	getFormat() = 0;
	virtual int		getFormatLen() = 0;
	virtual void	setFormat(const void *pFormat, int len) = 0;

	virtual const VDXStreamInfo& getStreamInfo() = 0;
	virtual void	setStreamInfo(const VDXStreamInfo& hdr) = 0;
	virtual void	updateStreamInfo(const VDXStreamInfo& hdr) = 0;

	enum {
		kFlagKeyFrame = 0x10		// clone of AVIIF_KEYFRAME
	};

	virtual void	write(uint32 flags, const void *pBuffer, uint32 cbBuffer, uint32 samples) = 0;
	virtual void	write(const void *pBuffer, uint32 cbBuffer, IVDXOutputFile::PacketInfo& packetInfo, FilterModPixmapInfo* info=0) {
		write(packetInfo.flags,pBuffer,cbBuffer,packetInfo.samples);
	}

	virtual void	partialWriteBegin(uint32 flags, uint32 bytes, uint32 samples) = 0;
	virtual void	partialWrite(const void *pBuffer, uint32 cbBuffer) = 0;
	virtual void	partialWriteEnd() = 0;

	virtual void	flush() = 0;
	virtual void	finish() = 0;
	virtual bool	isAVIFile(){ return false; }
	virtual bool	isOverflow(){ return false; }
};

class IVDVideoImageOutputStream : public IVDUnknown {
public:
	enum { kTypeID = 'vots' };

	virtual void WriteVideoImage(const VDPixmap *px) = 0;
};

class AVIOutputStream : public IVDMediaOutputStream {
private:
	vdfastvector<char>	mFormat;

protected:
	VDXStreamInfo		streamInfo;

public:
	AVIOutputStream();
	~AVIOutputStream() override;

	void *AsInterface(uint32 id) override;

	virtual void setFormat(const void *pFormat, int len) {
		mFormat.resize(len);
		memcpy(mFormat.data(), pFormat, len);
	}

	void *getFormat() override { return mFormat.data(); }
	int getFormatLen() override { return mFormat.size(); }

	const VDXStreamInfo& getStreamInfo() override {
		return streamInfo;
	}

	void setStreamInfo(const VDXStreamInfo& hdr) override {
		streamInfo = hdr;
		streamInfo.aviHeader.dwLength = 0;
		streamInfo.aviHeader.dwSuggestedBufferSize = 0;
	}

	void updateStreamInfo(const VDXStreamInfo& hdr) override {
		streamInfo = hdr;
	}

	void	write(uint32 flags, const void *pBuffer, uint32 cbBuffer, uint32 samples) override = 0;
	void	write(const void *pBuffer, uint32 cbBuffer, IVDXOutputFile::PacketInfo& packetInfo, FilterModPixmapInfo* info) override {
		write(packetInfo.flags,pBuffer,cbBuffer,packetInfo.samples);
	}

	void flush() override {}
	void finish() override {}
};

class VDINTERFACE IVDMediaOutput : public IVDUnknown {
public:
	enum { kTypeID = 'mout' };

	virtual ~IVDMediaOutput() {}

	virtual bool init(const wchar_t *szFile)=0;
	virtual void finalize()=0;
	virtual const char* GetFormatName(){ return 0; }

	virtual IVDMediaOutputStream *createAudioStream() = 0;
	virtual IVDMediaOutputStream *createVideoStream() = 0;
	virtual IVDMediaOutputStream *getAudioOutput() = 0;		// DEPRECATED
	virtual IVDMediaOutputStream *getVideoOutput() = 0;		// DEPRECATED
};

class VDINTERFACE IVDMediaOutputAutoInterleave : public IVDUnknown {
public:
	enum { kTypeID = 'moai' };

	virtual void GetNextPreferredStreamWrite(int& stream, sint32& count) = 0;
};

class AVIOutput : public IVDMediaOutput {
protected:
	IVDMediaOutputStream	*audioOut;
	IVDMediaOutputStream	*videoOut;

public:
	AVIOutput();
	~AVIOutput() override;

	void *AsInterface(uint32 id);

	bool init(const wchar_t *szFile) override = 0;
	void finalize() override = 0;

	IVDMediaOutputStream *getAudioOutput() override { return audioOut; }
	IVDMediaOutputStream *getVideoOutput() override { return videoOut; }
};

class AVIOutputNull : public AVIOutput {
public:
	~AVIOutputNull() override;
	bool init(const wchar_t *szFile) override;
	void finalize() override;

	IVDMediaOutputStream *createAudioStream() override;
	IVDMediaOutputStream *createVideoStream() override;
};

#endif
