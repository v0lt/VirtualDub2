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

#include "stdafx.h"
#include "DubSource.h"

DubSource::DubSource() {
}

DubSource::~DubSource() {
	if (format) {
		delete[] format;
	}
}

void *DubSource::allocFormat(int format_len) {
	if (this->format_len != format_len) {
		delete[] format;
		this->format_len = format_len;
		format = new char[format_len];
	}

	return (void *)format;
}

bool DubSource::isStreaming() {
	return false;
}

int DubSource::read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
	if (lStart < mSampleFirst || lStart >= mSampleLast) {
		if (lSamplesRead)
			*lSamplesRead = 0;
		if (lBytesRead)
			*lBytesRead = 0;
		return 0;
	}

	if (lCount != (uint32)-1 && lCount > mSampleLast - lStart) lCount = (uint32)(mSampleLast - lStart);

	return _read(lStart, lCount, lpBuffer, cbBuffer, lBytesRead, lSamplesRead);
}

void DubSource::streamBegin(bool fRealTime, bool bForceReset) {
}

void DubSource::streamEnd() {
}
