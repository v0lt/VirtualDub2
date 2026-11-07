// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2017-2019 Anton Shekhovtsov
// Copyright (C) 2025-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#include "gui.h"
#include "crash.h"

#include <vd2/system/strutil.h>
#include <vd2/system/fraction.h>
#include <vd2/system/math.h>
#include <vd2/system/protscope.h>
#include <vd2/Riza/audiocodec.h>
#include <vd2/Riza/w32audiocodec.h>
#include "AudioFilterSystem.h"
#include "AudioSource.h"
#include "AVIOutputPlugin.h"
#include "af_sink.h"

#include "audio.h"

bool VDPreferencesIsPreferInternalAudioDecodersEnabled();

AudioFormatConverter AudioPickConverter(const VDWaveFormat *src, bool to_16bit, bool to_stereo);

//////////////// no change converters /////////////////////////////////////

static void convert_audio_nochange8(void *dest, void *src, long count) {
	memcpy(dest, src, count);
}

static void convert_audio_nochange16(void *dest, void *src, long count) {
	memcpy(dest, src, count*2);
}

static void convert_audio_nochange32(void *dest, void *src, long count) {
	memcpy(dest, src, count*4);
}

//////////////// regular converters /////////////////////////////////////

static void convert_audio_mono8_to_mono16(void *dest, void *src, long count) {
	unsigned char *s = (unsigned char *)src;
	signed short *d = (signed short *)dest;

	do {
		*d++ = (signed short)((unsigned long)(*s++-0x80)<<8);
	} while(--count);
}

static void convert_audio_mono8_to_stereo8(void *dest, void *src, long count) {
	unsigned char c,*s = (unsigned char *)src;
	unsigned char *d = (unsigned char *)dest;

	do {
		c = *s++;
		*d++ = c;
		*d++ = c;
	} while(--count);
}

static void convert_audio_mono8_to_stereo16(void *dest, void *src, long count) {
	unsigned char *s = (unsigned char *)src;
	unsigned long c, *d = (unsigned long *)dest;

	do {
		c = ((*s++-0x80)&0xff) << 8;
		*d++ = c | (c<<16);
	} while(--count);
}

static void convert_audio_mono16_to_mono8(void *dest, void *src, long count) {
	signed short *s = (signed short *)src;
	unsigned char *d = (unsigned char *)dest;

	do {
		*d++ = (unsigned char)((((unsigned long)*s++)+0x8000)>>8);
	} while(--count);
}

static void convert_audio_mono16_to_stereo8(void *dest, void *src, long count) {
	signed short *s = (signed short *)src;
	unsigned char c, *d = (unsigned char *)dest;

	do {
		c = (unsigned char)((((unsigned long)*s++)+0x8000)>>8);
		*d++ = c;
		*d++ = c;
	} while(--count);
}

static void convert_audio_mono16_to_stereo16(void *dest, void *src, long count) {
	signed short *s = (signed short *)src;
	unsigned long *d = (unsigned long *)dest, c;

	do {
		c = 0xffff & *s++;
		*d++ = (c | (c<<16));
	} while(--count);
}

static void convert_audio_stereo8_to_mono8(void *dest, void *src, long count) {
	unsigned short *s = (unsigned short *)src;
	unsigned char *d = (unsigned char *)dest;
	unsigned long c;

	do {
		c = *s++;
		*d++ = (unsigned char)(((c&0xff) + (c>>8))/2);
	} while(--count);
}

static void convert_audio_stereo8_to_mono16(void *dest, void *src, long count) {
	unsigned short *s = (unsigned short *)src;
	signed short *d = (signed short *)dest;
	unsigned long c;

	do {
		c = *s++;
		*d++ = (signed short)((((c&0xff) + (c>>8))<<7)-0x8000);
	} while(--count);
}

static void convert_audio_stereo8_to_stereo16(void *dest, void *src, long count) {
	unsigned short c,*s = (unsigned short *)src;
	unsigned long *d = (unsigned long *)dest;

	do {
		c = *s++;
		*d++ = ((unsigned long)((c-0x80)&0xff)<<8) | ((unsigned long)((c&0xff00)-0x8000)<<16);
	} while(--count);
}

static void convert_audio_stereo16_to_mono8(void *dest, void *src, long count) {
	unsigned long c, *s = (unsigned long *)src;
	unsigned char *d = (unsigned char *)dest;

	do {
		c = *s++;
		*d++ = (unsigned char)(((((c&0xffff)+0xffff8000)^0xffff8000) + ((signed long)c>>16) + 0x10000)>>9);
	} while(--count);
}

static void convert_audio_stereo16_to_mono16(void *dest, void *src, long count) {
	unsigned long c, *s = (unsigned long *)src;
	signed short *d = (signed short *)dest;

	do {
		c = *s++;
		*d++ = (signed short)(((((c&0xffff)+0xffff8000)^0xffff8000) + ((signed long)c>>16))/2);
	} while(--count);
}

static void convert_audio_stereo16_to_stereo8(void *dest, void *src, long count) {
	unsigned long c,*s = (unsigned long *)src;
	unsigned char *d = (unsigned char *)dest;

	do {
		c = *s++;
		*d++ = (unsigned char)((((unsigned long)(c & 0xffff))+0x8000)>>8);
		*d++ = (unsigned char)((((unsigned long)(c>>16))+0x8000)>>8);
	} while(--count);
}

static void convert_audio_dual8_to_mono8(void *dest, void *src, long count) {
	const unsigned char *s = (unsigned char *)src;
	unsigned char *d = (unsigned char *)dest;

	do {
		*d++ = *s;
		s+=2;
	} while(--count);
}

static void convert_audio_dual8_to_mono16(void *dest, void *src, long count) {
	unsigned char *s = (unsigned char *)src;
	signed short *d = (signed short *)dest;

	do {
		*d++ = (signed short)((unsigned long)(*s-0x80)<<8);
		s += 2;
	} while(--count);
}

static void convert_audio_dual16_to_mono8(void *dest, void *src, long count) {
	signed short *s = (signed short *)src;
	unsigned char *d = (unsigned char *)dest;

	do {
		*d++ = (unsigned char)((((unsigned long)*s)+0x8000)>>8);
		s += 2;
	} while(--count);
}

static void convert_audio_dual16_to_mono16(void *dest, void *src, long count) {
	const signed short *s = (signed short *)src;
	signed short *d = (signed short *)dest;

	do {
		*d++ = *s;
		s+=2;
	} while(--count);
}

////////////////////////////////////////////

static const AudioFormatConverter acv[]={
	convert_audio_nochange8,
	convert_audio_mono8_to_mono16,
	convert_audio_mono8_to_stereo8,
	convert_audio_mono8_to_stereo16,
	convert_audio_mono16_to_mono8,
	convert_audio_nochange16,
	convert_audio_mono16_to_stereo8,
	convert_audio_mono16_to_stereo16,
	convert_audio_stereo8_to_mono8,
	convert_audio_stereo8_to_mono16,
	convert_audio_nochange16,
	convert_audio_stereo8_to_stereo16,
	convert_audio_stereo16_to_mono8,
	convert_audio_stereo16_to_mono16,
	convert_audio_stereo16_to_stereo8,
	convert_audio_nochange32,
};

static const AudioFormatConverter acv2[]={
	convert_audio_nochange8,
	convert_audio_mono8_to_mono16,
	convert_audio_mono16_to_mono8,
	convert_audio_nochange16,
	convert_audio_dual8_to_mono8,
	convert_audio_dual8_to_mono16,
	convert_audio_dual16_to_mono8,
	convert_audio_dual16_to_mono16,
};

AudioFormatConverter AudioPickConverter(const VDWaveFormat *src, bool to_16bit, bool to_stereo) {
	return acv[
			  (src->mChannels>1 ? 8 : 0)
			 +(src->mSampleBits>8 ? 4 : 0)
			 +(to_stereo ? 2 : 0)
			 +(to_16bit ? 1 : 0)
		];
}

AudioFormatConverter AudioPickConverterSingleChannel(const VDWaveFormat *src, bool to_16bit) {
	return acv2[
			  (src->mChannels>1 ? 4 : 0)
			 +(src->mSampleBits>8 ? 2 : 0)
			 +(to_16bit ? 1 : 0)
		];
}

///////////////////////////////////

AudioStream::AudioStream()
	: format(NULL)
	, format_len(0)
	, source(NULL)
	, samples_read(0)
	, stream_len(0)
	, stream_limit(0x7FFFFFFFFFFFFFFF)
{
}

AudioStream::~AudioStream() {
	freemem(format);
}

VDWaveFormat *AudioStream::AllocFormat(long len) {
	if (format) { freemem(format); format = 0; }

	if (!(format = (VDWaveFormat *)allocmem(len)))
		throw MyError("AudioStream: Out of memory");

    format_len = len;

	return format;
}

VDWaveFormat *AudioStream::GetFormat() const {
	return format;
}

long AudioStream::GetFormatLen() const {
	return format_len;
}

sint64 AudioStream::GetSampleCount() const {
	return samples_read;
}

sint64 AudioStream::GetLength() const {
	return stream_limit < stream_len ? stream_limit : stream_len;
}

const VDFraction AudioStream::GetSampleRate() const {
	return VDFraction(format->mDataRate, format->mBlockSize);
}

long AudioStream::Read(void *buffer, long max_samples, long *lplBytes) {
	long actual;

	if (max_samples <= 0) {
		*lplBytes = 0;
		return 0;
	}

	if (samples_read >= stream_limit) {
		*lplBytes = 0;
		return 0;
	}

	if (samples_read + max_samples > stream_limit) {
		sint64 limit = stream_limit - samples_read;

		if (limit > 0x0fffffff)		// clamp so we don't issue a ridiculous request
			limit = 0x0fffffff;
		max_samples = (long)limit;
	}

	actual = _Read(buffer, max_samples, lplBytes);

	VDASSERT(actual >= 0 && actual <= max_samples);

	if (buffer) samples_read += actual;

	return actual;
}

long AudioStream::ReadVBR(void *buffer, long size, long *lplBytes, sint64 *duration) {
	if (samples_read >= stream_limit) {
		*lplBytes = 0;
		*duration = -1;
		return 0;
	}

	long actual;
	actual = _ReadVBR(buffer, size, lplBytes, duration);

	VDASSERT(actual >= 0 && actual <= 1);

	if (buffer) samples_read += actual;

	return actual;
}

bool AudioStream::Skip(sint64 samples) {
	return false;
}

void AudioStream::SetLimit(sint64 limit) {
	stream_limit = limit;
}

void AudioStream::SetSource(AudioStream *src) {
	source = src;
	stream_len = src->GetLength();
}

bool AudioStream::isEnd() {
	return samples_read >= stream_limit || _isEnd();
}

long AudioStream::_Read(void *buffer, long max_samples, long *lplBytes) {
	*lplBytes = 0;

	return 0;
}

long AudioStream::_ReadVBR(void *buffer, long size, long *lplBytes, sint64* duration) {
	*lplBytes = 0;
	*duration = -1;

	return 0;
}

bool AudioStream::_isEnd() {
	return FALSE;
}

void AudioStream::Seek(VDPosition pos) {
	VDASSERT(pos >= 0);

	if (pos > 0) {
		VDASSERT(Skip(pos - GetSampleCount()));
	}
}

////////////////////

AudioStreamSource::AudioStreamSource(AudioSource *src, sint64 max_samples, bool allow_decompression, sint64 start_us)
	: AudioStream()
{
	VDWaveFormat *iFormat = (VDWaveFormat *)src->getWaveFormat();
	VDWaveFormat *oFormat;

	fZeroRead = false;
	mPreskip = 0;

	if (max_samples < 0)
		max_samples = 0;

	bool isPCM = false;
	if (is_audio_pcm(iFormat) || is_audio_float(iFormat)) isPCM = true;


	if (!isPCM && allow_decompression) {
		mpCodec = VDLocateAudioDecompressor((const VDWaveFormat *)iFormat, NULL, VDPreferencesIsPreferInternalAudioDecodersEnabled());

		const unsigned oflen = mpCodec->GetOutputFormatSize();

		memcpy(AllocFormat(oflen), mpCodec->GetOutputFormat(), oflen);

		oFormat = GetFormat();
	} else {

		// FIX: If we have a PCMWAVEFORMAT stream, artificially cut the format size
		//		to sizeof(PCMWAVEFORMAT).  LSX-MPEG Encoder doesn't like large PCM
		//		formats!

		if (iFormat->mTag == WAVE_FORMAT_PCM) {
			oFormat = AllocFormat(sizeof(PCMWAVEFORMAT));
			memcpy(oFormat, iFormat, sizeof(PCMWAVEFORMAT));
		} else {
			oFormat = AllocFormat(src->getFormatLen());
			memcpy(oFormat, iFormat, GetFormatLen());
		}
	}

	sint64 first_samp = src->TimeToPositionVBR(start_us);

	mOffset = src->msToSamples((start_us + 500) / 1000);

	mPrefill = 0;
	if (first_samp < 0) {
		mPrefill = -first_samp;
		first_samp = 0;
	}

	aSrc = src;
	stream_len = std::min<sint64>(max_samples, aSrc->getEnd() - first_samp);

	if (mpCodec) {
		const VDWaveFormat *wfexSrc = (VDWaveFormat *)aSrc->getWaveFormat();
		const VDWaveFormat *wfexDst = GetFormat();
		double sampleRatio = (double)wfexDst->mSamplingRate * (double)wfexSrc->mBlockSize / (double)wfexSrc->mDataRate;
		double secOffset = (double)start_us / 1000000.0;
		mPrefill = 0;
		if (start_us < 0)
			mPrefill = (sint64)VDRoundToInt64(-secOffset * wfexDst->mSamplingRate);
		mOffset = (sint64)VDRoundToInt64(secOffset * wfexDst->mSamplingRate);
		stream_len = (sint64)VDRoundToInt64(stream_len * sampleRatio);
	}

	stream_len += mPrefill;
	mBasePos = first_samp;
	cur_samp = first_samp;
	end_samp = first_samp + max_samples;

	mbSourceIsVBR = !mpCodec && src->GetVBRMode() == IVDStreamSource::kVBRModeVariableFrames;
}

AudioStreamSource::~AudioStreamSource() {
	if (mpCodec) {
		mpCodec->Shutdown();
		mpCodec = NULL;
	}
}

bool AudioStreamSource::IsVBR() const {
	return mbSourceIsVBR;
}

const VDFraction AudioStreamSource::GetSampleRate() const {
	if (!mpCodec)
		return aSrc->getRate();

	return AudioStream::GetSampleRate();
}

long AudioStreamSource::_Read(void *buffer, long max_samples, long *lplBytes) {
	if (!buffer) {
		if (lplBytes) *lplBytes = GetFormat()->mBlockSize;
		return 0;
	}

	LONG lAddedBytes=0;
	LONG lAddedSamples=0;

	// add filler samples as necessary

	if (mPrefill > 0) {
		const VDWaveFormat *wfex = GetFormat();
		long tc = max_samples;
		const int mBlockSize = wfex->mBlockSize;

		if (tc > mPrefill)
			tc = (long)mPrefill;

		if (is_audio_pcm(wfex) || is_audio_float(wfex)) {
			fill_silence_pcm(buffer,tc,wfex);
			buffer = (char *)buffer + mBlockSize*tc;
		} else {
			uint32 actualBytes, actualSamples;
			VDPosition startPos = aSrc->getStart();

			int err = aSrc->read(startPos, 1, buffer, mBlockSize, &actualBytes, &actualSamples);

			if (err == DubSource::kFileReadError)
				throw MyError("Audio sample %lu could not be read in the source.  The file may be corrupted.", (unsigned long)startPos);
			else if (err)
				throw MyAVIError("AudioStreamSource", err);

			// replicate sample
			if (tc > 1) {
				char *dst = (char *)buffer;

				for(int count = mBlockSize*(tc-1); count; --count) {
					dst[mBlockSize] = dst[0];
					++dst;
				}

				VDASSERT(dst == (char *)buffer + (tc-1)*mBlockSize);
				buffer = dst;
			}

			buffer = (char *)buffer + mBlockSize;
		}

		max_samples -= tc;
		lAddedBytes = tc*mBlockSize;
		lAddedSamples = tc;
		mPrefill -= tc;
	}

	VDASSERT(cur_samp >= 0);

	// read actual samples

	if (mpCodec) {
		uint32 ltActualBytes, ltActualSamples;
		LONG lBytesLeft = max_samples * GetFormat()->mBlockSize;
		LONG lTotalBytes = lBytesLeft;
		const unsigned mBlockSize = ((VDWaveFormat *)aSrc->getWaveFormat())->mBlockSize;

		while(lBytesLeft > 0) {
			// hmm... data still in the output buffer?
			if (mPreskip) {
				unsigned actual = mpCodec->CopyOutput(NULL, mPreskip > 0x10000 ? 0x10000 : (uint32)mPreskip);

				if (actual) {
					VDASSERT(actual <= mPreskip);
					mPreskip -= actual;
					continue;
				}
			} else {
				unsigned actual = mpCodec->CopyOutput(buffer, lBytesLeft);

				VDASSERT(actual <= lBytesLeft);

				if (actual) {
					buffer = (void *)((char *)buffer + actual);
					lBytesLeft -= actual;
					continue;
				}
			}

			// fill the input buffer up... if we haven't gotten a zero yet.
//			if (!fZeroRead)
			{
				unsigned totalBytes = 0;
				unsigned bytes;
				void *dst = mpCodec->LockInputBuffer(bytes);

				bool successfulRead = false;

				if (bytes >= mBlockSize) {
					int err;
					do {
						long to_read = bytes/mBlockSize;

						if (to_read > end_samp - cur_samp)
							to_read = (long)(end_samp - cur_samp);

						vdprotected3("reading %u compressed audio samples starting at %I64d (stream length=%I64d)"
									, unsigned, to_read
									, sint64, cur_samp
									, sint64, aSrc->getLength())
						{
							err = aSrc->read(cur_samp, to_read, dst, bytes, &ltActualBytes, &ltActualSamples);
						}

						if (err != DubSource::kOK && err != DubSource::kBufferTooSmall) {
							if (err == DubSource::kFileReadError)
								throw MyError("Audio samples %lu-%lu could not be read in the source.  The file may be corrupted.", (unsigned long)cur_samp, (unsigned long)(cur_samp+to_read-1));
							else
								throw MyAVIError("AudioStreamSource", err);
						}

						VDASSERT(ltActualBytes <= bytes);

						if (!ltActualBytes)
							break;

						totalBytes += ltActualBytes;
						bytes -= ltActualBytes;
						cur_samp += ltActualSamples;
						dst = (char *)dst + ltActualBytes;

						successfulRead = true;

					} while(bytes >= mBlockSize && err != DubSource::kBufferTooSmall && cur_samp < end_samp);
				}

				mpCodec->UnlockInputBuffer(totalBytes);

				if (!successfulRead)
					fZeroRead = true;
			}

			if (!mpCodec->Convert(fZeroRead, true))
				break;
		};

		long bytes = (lTotalBytes - lBytesLeft);
		*lplBytes = bytes + lAddedBytes;

		long samples = bytes / GetFormat()->mBlockSize + lAddedSamples;

		return samples;
	} else {
		uint32 lSamples=0;

		if (max_samples > end_samp - cur_samp)
			max_samples = (long)(end_samp - cur_samp);

		if (max_samples > 0) {
			uint32 bytes;
			int err;

			vdprotected3("reading %u raw audio samples starting at %I64d (stream length=%I64d)"
						, unsigned, max_samples
						, sint64, cur_samp
						, sint64, aSrc->getLength())
			{
				err = aSrc->read(cur_samp, max_samples, buffer, max_samples * GetFormat()->mBlockSize, &bytes, &lSamples);
			}

			*lplBytes = bytes;

			if (DubSource::kOK != err) {
				if (err == DubSource::kFileReadError)
					throw MyError("Audio samples %lu-%lu could not be read in the source.  The file may be corrupted.", (unsigned long)cur_samp, (unsigned long)(cur_samp+max_samples-1));
				else
					throw MyAVIError("AudioStreamSource", err);
			}

			if (!lSamples) fZeroRead = true;
		} else
			lSamples = *lplBytes = 0;

		*lplBytes += lAddedBytes;

		if (buffer) cur_samp += lSamples;

		return lSamples + lAddedSamples;
	}
}

bool AudioStreamSource::Skip(sint64 samples) {

	if (mPrefill > 0) {
		sint64 tc = std::min<sint64>(mPrefill, samples);

		mPrefill -= tc;
		samples -= tc;

		if (samples <= 0)
			return true;
	}

	// mBlockSize = bytes per block.
	//
	// mDataRate / mBlockSize = blocks per second.
	// mSamplingRate * mBlockSize / mDataRate = samples per block.

	if (mpCodec) {
		const VDWaveFormat *pwfex = (VDWaveFormat *)aSrc->getWaveFormat();

		if (samples < MulDiv(4*pwfex->mBlockSize, pwfex->mSamplingRate, pwfex->mDataRate)) {
			mPreskip += samples*GetFormat()->mBlockSize;
			VDASSERT(mPreskip >= 0);
			return true;
		}

		Seek(samples_read + samples);
		return true;

	} else {
		cur_samp += samples;
		samples_read += samples;

		return true;
	}
}

bool AudioStreamSource::_isEnd() {
	return (cur_samp >= end_samp || fZeroRead) && (!mpCodec || !mpCodec->GetOutputLevel());
}

void AudioStreamSource::Seek(VDPosition pos) {
	pos += mOffset;

	mPrefill = 0;
	if (pos < 0) {
		mPrefill = -pos;
		pos = 0;
	}

	fZeroRead = false;
	mPreskip = 0;
	samples_read = pos;

	const VDWaveFormat *pwfex = (VDWaveFormat *)aSrc->getWaveFormat();
	if (mpCodec) {
		// flush decompression buffers
		mpCodec->Restart();

		// recompute new position
		double samplesPerMicroSec = (double)pwfex->mSamplingRate * (1.0 / 1000000.0);
		cur_samp = aSrc->TimeToPositionVBR(VDRoundToInt64(pos / samplesPerMicroSec));

		double timeError = pos - aSrc->PositionToTimeVBR(cur_samp)*samplesPerMicroSec;
		if (timeError < 0 && cur_samp > aSrc->getStart()) {
			--cur_samp;
			timeError = pos - aSrc->PositionToTimeVBR(cur_samp)*samplesPerMicroSec;
		}

		if (timeError < 0)
			timeError = 0;

		mPreskip = GetFormat()->mBlockSize * VDRoundToInt64(timeError);
		VDASSERT(mPreskip >= 0);

	} else if (aSrc->GetVBRMode() == IVDStreamSource::kVBRModeTimestamped) {
		// This is a bit of a hack -- we convert the position to time using linear
		// conversion and then fix it with the VBR conversion.
		double samplesToMicrosecondsFactor = (double)pwfex->mBlockSize / (double)pwfex->mDataRate * 1000000.0;
		cur_samp = aSrc->TimeToPositionVBR(VDRoundToInt64(pos * samplesToMicrosecondsFactor));
	} else {
		cur_samp = pos;
	}

	if (cur_samp > end_samp)
		cur_samp = end_samp;
}



///////////////////////////////////////////////////////////////////////////
//
//		AudioStreamConverter
//
//		This audio filter handles changes in format between 8/16-bit
//		and mono/stereo.
//
///////////////////////////////////////////////////////////////////////////



AudioStreamConverter::AudioStreamConverter(AudioStream *src, bool to_16bit, bool to_stereo_or_right, bool single_only) {
	VDWaveFormat *iFormat = src->GetFormat();
	VDWaveFormat *oFormat;
	bool to_stereo = single_only ? false : to_stereo_or_right;

	memcpy(oFormat = AllocFormat(src->GetFormatLen()), iFormat, src->GetFormatLen());

	oFormat->mChannels = (uint16)(to_stereo ? 2 : 1);
	oFormat->mSampleBits = (uint16)(to_16bit ? 16 : 8);

	if (iFormat->mChannels != 1 && iFormat->mChannels != 2)
		throw MyError("Cannot convert audio: the source channel count is not supported (must be mono or stereo).");

	if (iFormat->mSampleBits != 8 && iFormat->mSampleBits != 16)
		throw MyError("Cannot convert audio: the source audio format is not supported (must be 8-bit or 16-bit PCM).");

	bytesPerInputSample = (iFormat->mChannels>1 ? 2 : 1)
						* (iFormat->mSampleBits>8 ? 2 : 1);

	bytesPerOutputSample = (to_stereo ? 2 : 1)
						 * (to_16bit ? 2 : 1);

	offset = 0;

	if (single_only) {
		convRout = AudioPickConverterSingleChannel(iFormat, to_16bit);

		if (to_stereo_or_right && iFormat->mChannels>1) {
			offset = 1;

			if (iFormat->mSampleBits>8)
				offset = 2;
		}
	} else
		convRout = AudioPickConverter(iFormat, to_16bit, to_stereo);
	SetSource(src);

	oFormat->mDataRate = oFormat->mSamplingRate * bytesPerOutputSample;
	oFormat->mBlockSize = (uint16)bytesPerOutputSample;


	if (!(cbuffer = allocmem(bytesPerInputSample * BUFFER_SIZE)))
		throw MyError("AudioStreamConverter: out of memory");
}

AudioStreamConverter::~AudioStreamConverter() {
	freemem(cbuffer);
}

long AudioStreamConverter::_Read(void *buffer, long samples, long *lplBytes) {
	long lActualSamples=0;

	while(samples>0) {
		long srcSamples;
		long lBytes;

		// figure out how many source samples we need

		srcSamples = samples;

		if (srcSamples > BUFFER_SIZE) srcSamples = BUFFER_SIZE;

		srcSamples = source->Read(cbuffer, srcSamples, &lBytes);

		if (!srcSamples) break;

		convRout(buffer, (char *)cbuffer + offset, srcSamples);

		buffer = (void *)((char *)buffer + bytesPerOutputSample * srcSamples);
		lActualSamples += srcSamples;
		samples -= srcSamples;

	}

	*lplBytes = lActualSamples * bytesPerOutputSample;

	return lActualSamples;
}

bool AudioStreamConverter::_isEnd() {
	return source->isEnd();
}

bool AudioStreamConverter::Skip(sint64 samples) {
	return source->Skip(samples);
}



///////////////////////////////////////////////////////////////////////////
//
//		AudioStreamResampler
//
//		This audio filter handles changes in sampling rate.
//
///////////////////////////////////////////////////////////////////////////

static long audio_pointsample_8(void *dst, void *src, long accum, long samp_frac, long cnt) {
	unsigned char *d = (unsigned char *)dst;
	unsigned char *s = (unsigned char *)src;

	do {
		*d++ = s[accum>>19];
		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_pointsample_16(void *dst, void *src, long accum, long samp_frac, long cnt) {
	unsigned short *d = (unsigned short *)dst;
	unsigned short *s = (unsigned short *)src;

	do {
		*d++ = s[accum>>19];
		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_pointsample_32(void *dst, void *src, long accum, long samp_frac, long cnt) {
	unsigned long *d = (unsigned long *)dst;
	unsigned long *s = (unsigned long *)src;

	do {
		*d++ = s[accum>>19];
		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_downsample_mono8(void *dst, void *src, long *filter_bank, int filter_width, long accum, long samp_frac, long cnt) {
	unsigned char *d = (unsigned char *)dst;
	unsigned char *s = (unsigned char *)src;

	do {
		long sum = 0;
		int w;
		long *fb_ptr;
		unsigned char *s_ptr;

		w = filter_width;
		fb_ptr = filter_bank + filter_width * ((accum>>11)&0xff);
		s_ptr = s + (accum>>19);
		do {
			sum += *fb_ptr++ * (int)*s_ptr++;
		} while(--w);

		if (sum < 0)
			*d++ = 0;
		else if (sum > 0x3fffff)
			*d++ = 0xff;
		else
			*d++ = (unsigned char)((sum + 0x2000)>>14);

		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_downsample_mono16(void *dst, void *src, long *filter_bank, int filter_width, long accum, long samp_frac, long cnt) {
	signed short *d = (signed short *)dst;
	signed short *s = (signed short *)src;

	do {
		long sum = 0;
		int w;
		long *fb_ptr;
		signed short *s_ptr;

		w = filter_width;
		fb_ptr = filter_bank + filter_width * ((accum>>11)&0xff);
		s_ptr = s + (accum>>19);
		do {
			sum += *fb_ptr++ * (int)*s_ptr++;
		} while(--w);

		if (sum < -0x20000000)
			*d++ = -0x8000;
		else if (sum > 0x1fffffff)
			*d++ = 0x7fff;
		else
			*d++ = (signed short)((sum + 0x2000)>>14);

		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_downsample_stereo8(void *dst, void *src, long *filter_bank, int filter_width, long accum, long samp_frac, long cnt) {
	unsigned char *d = (unsigned char *)dst;
	unsigned char *s = (unsigned char *)src;

	do {
		long sum_l = 0, sum_r = 0;
		int w;
		long *fb_ptr;
		unsigned char *s_ptr;

		w = filter_width;
		fb_ptr = filter_bank + filter_width * ((accum>>11)&0xff);
		s_ptr = s + (accum>>19)*2;
		do {
			long f = *fb_ptr++;

			sum_l += f * (int)*s_ptr++;
			sum_r += f * (int)*s_ptr++;
		} while(--w);

		if (sum_l < 0)
			*d++ = 0;
		else if (sum_l > 0x3fffff)
			*d++ = 0xff;
		else
			*d++ = (unsigned char)((sum_l + 0x2000)>>14);

		if (sum_r < 0)
			*d++ = 0;
		else if (sum_r > 0x3fffff)
			*d++ = 0xff;
		else
			*d++ = (unsigned char)((sum_r + 0x2000)>>14);

		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_downsample_stereo16(void *dst, void *src, long *filter_bank, int filter_width, long accum, long samp_frac, long cnt) {
	signed short *d = (signed short *)dst;
	signed short *s = (signed short *)src;

	do {
		long sum_l = 0, sum_r = 0;
		int w;
		long *fb_ptr;
		signed short *s_ptr;

		w = filter_width;
		fb_ptr = filter_bank + filter_width * ((accum>>11)&0xff);
		s_ptr = s + (accum>>19)*2;
		do {
			long f = *fb_ptr++;

			sum_l += f * (int)*s_ptr++;
			sum_r += f * (int)*s_ptr++;
		} while(--w);

		if (sum_l < -0x20000000)
			*d++ = -0x8000;
		else if (sum_l > 0x1fffffff)
			*d++ = 0x7fff;
		else
			*d++ = (signed short)((sum_l + 0x2000)>>14);

		if (sum_r < -0x20000000)
			*d++ = -0x8000;
		else if (sum_r > 0x1fffffff)
			*d++ = 0x7fff;
		else
			*d++ = (signed short)((sum_r + 0x2000)>>14);

		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_upsample_mono8(void *dst, void *src, long accum, long samp_frac, long cnt) {
	unsigned char *d = (unsigned char *)dst;
	unsigned char *s = (unsigned char *)src;

	do {
		unsigned char *s_ptr = s + (accum>>19);
		long frac = (accum>>3) & 0xffff;

		*d++ = (unsigned char)(((int)s_ptr[0] * (0x10000 - frac) + (int)s_ptr[1] * frac) >> 16);
		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_upsample_mono16(void *dst, void *src, long accum, long samp_frac, long cnt) {
	signed short *d = (signed short *)dst;
	signed short *s = (signed short *)src;

	do {
		signed short *s_ptr = s + (accum>>19);
		long frac = (accum>>3) & 0xffff;

		*d++ = (signed short)(((int)s_ptr[0] * (0x10000 - frac) + (int)s_ptr[1] * frac) >> 16);
		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_upsample_stereo8(void *dst, void *src, long accum, long samp_frac, long cnt) {
	unsigned char *d = (unsigned char *)dst;
	unsigned char *s = (unsigned char *)src;

	do {
		unsigned char *s_ptr = s + (accum>>19)*2;
		long frac = (accum>>3) & 0xffff;

		*d++ = (unsigned char)(((int)s_ptr[0] * (0x10000 - frac) + (int)s_ptr[2] * frac) >> 16);
		*d++ = (unsigned char)(((int)s_ptr[1] * (0x10000 - frac) + (int)s_ptr[3] * frac) >> 16);
		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static long audio_upsample_stereo16(void *dst, void *src, long accum, long samp_frac, long cnt) {
	signed short *d = (signed short *)dst;
	signed short *s = (signed short *)src;

	do {
		signed short *s_ptr = s + (accum>>19)*2;
		long frac = (accum>>3) & 0xffff;

		*d++ = (signed short)(((int)s_ptr[0] * (0x10000 - frac) + (int)s_ptr[2] * frac) >> 16);
		*d++ = (signed short)(((int)s_ptr[1] * (0x10000 - frac) + (int)s_ptr[3] * frac) >> 16);
		accum += samp_frac;
	} while(--cnt);

	return accum;
}

static int permute_index(int a, int b) {
	return (b-(a>>8)-1) + (a&255)*b;
}

static void make_downsample_filter(long *filter_bank, int filter_width, long samp_frac) {
	int i, j, v;
	double filt_max;
	double filtwidth_frac;

	filtwidth_frac = samp_frac/2048.0;

	filter_bank[filter_width-1] = 0;

	filt_max = (16384.0 * 524288.0) / samp_frac;

	for(i=0; i<128*filter_width; i++) {
		int y = 0;
		double d = i / filtwidth_frac;

		if (d<1.0)
			y = VDRoundToInt(filt_max*(1.0 - d));

		filter_bank[permute_index(128*filter_width + i, filter_width)]
			= filter_bank[permute_index(128*filter_width - i, filter_width)]
			= y;
	}

	// Normalize the filter to correct for integer roundoff errors

	for(i=0; i<256*filter_width; i+=filter_width) {
		v=0;
		for(j=0; j<filter_width; j++)
			v += filter_bank[i+j];

//		_RPT2(0,"error[%02x] = %04x\n", i/filter_width, 0x4000 - v);

		v = (0x4000 - v)/filter_width;
		for(j=0; j<filter_width; j++)
			filter_bank[i+j] += v;
	}
}

AudioStreamResampler::AudioStreamResampler(AudioStream *src, long new_rate, bool hi_quality) : AudioStream() {
	VDWaveFormat *iFormat = src->GetFormat();
	VDWaveFormat *oFormat;

	memcpy(oFormat = AllocFormat(src->GetFormatLen()), iFormat, src->GetFormatLen());

	if (iFormat->mChannels != 1 && iFormat->mChannels != 2)
		throw MyError("Cannot resample audio: the source channel count is not supported (must be mono or stereo).");

	if (iFormat->mSampleBits != 8 && iFormat->mSampleBits != 16)
		throw MyError("Cannot resample audio: the source audio format is not supported (must be 8-bit or 16-bit PCM).");

	if (oFormat->mChannels>1)
		if (oFormat->mSampleBits>8) {
			ptsampleRout = audio_pointsample_32;
			upsampleRout = audio_upsample_stereo16;
			dnsampleRout = audio_downsample_stereo16;
		} else {
			ptsampleRout = audio_pointsample_16;
			upsampleRout = audio_upsample_stereo8;
			dnsampleRout = audio_downsample_stereo8;
		}
	else
		if (oFormat->mSampleBits>8) {
			ptsampleRout = audio_pointsample_16;
			upsampleRout = audio_upsample_mono16;
			dnsampleRout = audio_downsample_mono16;
		} else {
			ptsampleRout = audio_pointsample_8;
			upsampleRout = audio_upsample_mono8;
			dnsampleRout = audio_downsample_mono8;
		}

	SetSource(src);

	bytesPerSample = (iFormat->mChannels>1 ? 2 : 1)
						* (iFormat->mSampleBits>8 ? 2 : 1);

	uint64 sampFrac64 = (((uint64)iFormat->mSamplingRate << 20) / new_rate + 1) >> 1;

	if (sampFrac64 > 0x7FFFFFFF)
		throw MyError("Cannot resample audio from %uHz to %uHz: the conversion ratio is too low.", iFormat->mSamplingRate, new_rate);

	samp_frac = (long)sampFrac64;

	if (samp_frac == 0)
		throw MyError("Cannot resample audio from %uHz to %uHz: the conversion ratio is too high.", iFormat->mSamplingRate, new_rate);

	stream_len = (sint64)VDUMulDiv64x32(stream_len, 0x80000L, samp_frac);

	uint64 newSamplingRate = (((uint64)iFormat->mSamplingRate << 20) / (uint32)samp_frac + 1) >> 1;

	if (!newSamplingRate || newSamplingRate > 0x7FFFFFFF)
		throw MyError("Cannot resample audio from %uHz to %uHz: the conversion ratio is too %s.", iFormat->mSamplingRate, new_rate, newSamplingRate ? "high" : "low");

	oFormat->mSamplingRate = (uint32)newSamplingRate;
	oFormat->mDataRate = oFormat->mSamplingRate * bytesPerSample;
	oFormat->mBlockSize = (uint16)bytesPerSample;

	holdover = 0;
	filter_bank = NULL;
	filter_width = 1;
	accum=0;
	fHighQuality = hi_quality;

	// If this is a high-quality downsample, allocate memory for the filter bank

	mBufferSize = 0;

	if (hi_quality) {
		if (samp_frac>0x80000) {

			// HQ downsample: allocate filter bank

			filter_width = ((samp_frac + 0x7ffff)>>19)<<1;

			if (!(filter_bank = new(std::nothrow) long[filter_width * 256])) {
				freemem(cbuffer);
				throw MyMemoryError();
			}

			make_downsample_filter(filter_bank, filter_width, samp_frac);

			holdover = filter_width/2;

			mBufferSize = filter_width + 1;
		}
	}

	// Initialize the buffer.

	if (mBufferSize < MIN_BUFFER_SIZE)
		mBufferSize = MIN_BUFFER_SIZE;

	mBufferSize = (mBufferSize + 3) & ~3;

	if (!(cbuffer = allocmem(bytesPerSample * mBufferSize)))
		throw MyMemoryError();

	if (oFormat->mSampleBits>8)
		memset(cbuffer, 0x00, bytesPerSample * mBufferSize);
	else
		memset(cbuffer, 0x80, bytesPerSample * mBufferSize);
}

AudioStreamResampler::~AudioStreamResampler() {
	freemem(cbuffer);
	delete filter_bank;
}

long AudioStreamResampler::_Read(void *buffer, long samples, long *lplBytes) {

	if (samp_frac == 0x80000)
		return source->Read(buffer, samples, lplBytes);

	if (samp_frac < 0x80000)
		return Upsample(buffer, samples, lplBytes);
	else
		return Downsample(buffer, samples, lplBytes);
}


long AudioStreamResampler::Upsample(void *buffer, long samples, long *lplBytes) {
	long lActualSamples=0;

	// Upsampling: producing more output samples than input
	//
	// There are two issues we need to watch here:
	//
	//	o  An input sample can be read more than once.  In particular, even
	//	   when point sampling, we may need the last input sample again.
	//
	//	o  When interpolating (HQ), we need one additional sample.

	while(samples>0) {
		long srcSamples, dstSamples;
		long lBytes;
		int holdover = 0;

		// A negative accum value indicates that we need to reprocess a sample.
		// The last iteration should have left it at the bottom of the buffer
		// for us.  In interpolation mode, we'll always have at least a 1
		// sample overlap.

		if (accum<0) {
			holdover = 1;
			accum += 0x80000;
		}

		if (fHighQuality)
			++holdover;

		// figure out how many source samples we need

		srcSamples = (long)(((__int64)samp_frac*(samples-1) + accum) >> 19) + 1 - holdover;

		if (fHighQuality)
			++srcSamples;

		if (srcSamples > mBufferSize-holdover) srcSamples = mBufferSize-holdover;

		srcSamples = source->Read((char *)cbuffer + holdover * bytesPerSample, srcSamples, &lBytes);

		if (!srcSamples) break;

		srcSamples += holdover;

		// figure out how many destination samples we'll get out of what we read

		if (fHighQuality)
			dstSamples = ((srcSamples<<19) - accum - 0x80001)/samp_frac + 1;
		else
			dstSamples = ((srcSamples<<19) - accum - 1)/samp_frac + 1;

		if (dstSamples > samples)
			dstSamples = samples;

		if (dstSamples>=1) {

			if (fHighQuality)
				accum = upsampleRout(buffer, cbuffer, accum, samp_frac, dstSamples);
			else
				accum = ptsampleRout(buffer, cbuffer, accum, samp_frac, dstSamples);

			buffer = (void *)((char *)buffer + bytesPerSample * dstSamples);
			lActualSamples += dstSamples;
			samples -= dstSamples;
		}

		if (fHighQuality)
			accum -= ((srcSamples-1)<<19);
		else
			accum -= (srcSamples<<19);

		// do we need to hold a sample over?

		if (fHighQuality)
			if (accum<0)
				memcpy(cbuffer, (char *)cbuffer + (srcSamples-2)*bytesPerSample, bytesPerSample*2);
			else
				memcpy(cbuffer, (char *)cbuffer + (srcSamples-1)*bytesPerSample, bytesPerSample);
		else if (accum<0)
			memcpy(cbuffer, (char *)cbuffer + (srcSamples-1)*bytesPerSample, bytesPerSample);
	}

	*lplBytes = lActualSamples * bytesPerSample;

//	_RPT2(0,"Converter: %ld samples, %ld bytes\n", lActualSamples, *lplBytes);

	return lActualSamples;
}

long AudioStreamResampler::Downsample(void *buffer, long samples, long *lplBytes) {
	long lActualSamples=0;

	// Downsampling is even worse because we have overlap to the left and to the
	// right of the interpolated point.
	//
	// We need (n/2) points to the left and (n/2-1) points to the right.

	while(samples>0) {
		long srcSamples, dstSamples;
		long lBytes;
		int nhold;

		// Figure out how many source samples we need.
		//
		// To do this, compute the highest fixed-point accumulator we'll reach.
		// Truncate that, and add the filter width.  Then subtract however many
		// samples are sitting at the bottom of the buffer.

		srcSamples = (long)(((__int64)samp_frac*(samples-1) + accum) >> 19) + filter_width - holdover;

		// Don't exceed the buffer (mBufferSize - holdover).

		if (srcSamples > mBufferSize - holdover)
			srcSamples = mBufferSize - holdover;

		// Read into buffer.

		srcSamples = source->Read((char *)cbuffer + holdover*bytesPerSample, srcSamples, &lBytes);

		if (!srcSamples) break;

		// Figure out how many destination samples we'll get out of what we
		// read.  We'll have (srcSamples+holdover) bytes, so the maximum
		// fixed-pt accumulator we can hit is
		// (srcSamples+holdover-filter_width)<<16 + 0xffff.

		dstSamples = (((srcSamples+holdover-filter_width)<<19) + 0x7ffff - accum) / samp_frac + 1;

		if (dstSamples > samples)
			dstSamples = samples;

		if (dstSamples>=1) {
			if (filter_bank)
				accum = dnsampleRout(buffer, cbuffer, filter_bank, filter_width, accum, samp_frac, dstSamples);
			else
				accum = ptsampleRout(buffer, cbuffer, accum, samp_frac, dstSamples);

			buffer = (void *)((char *)buffer + bytesPerSample * dstSamples);
			lActualSamples += dstSamples;
			samples -= dstSamples;
		}

		// We're "shifting" the new samples down to the bottom by discarding
		// all the samples in the buffer, so adjust the fixed-pt accum
		// accordingly.

		accum -= ((srcSamples+holdover)<<19);

		// Oops, did we need some of those?
		//
		// If accum=0, we need (n/2) samples back.  accum>=0x10000 is fewer,
		// accum<0 is more.

		nhold = - (accum>>19);

//		_ASSERT(nhold<=(filter_width/2));

		if (nhold>0) {
			memmove(cbuffer, (char *)cbuffer+bytesPerSample*(srcSamples+holdover-nhold), bytesPerSample*nhold);
			holdover = nhold;
			accum += nhold<<19;
		} else
			holdover = 0;

		_ASSERT(accum>=0);
	}

	*lplBytes = lActualSamples * bytesPerSample;

	return lActualSamples;
}

bool AudioStreamResampler::_isEnd() {
	return accum>=0 && source->isEnd();
}



///////////////////////////////////////////////////////////////////////////
//
//		AudioCompressor
//
//		This audio filter handles audio compression.
//
///////////////////////////////////////////////////////////////////////////

AudioCompressor::AudioCompressor(AudioStream *src, const VDWaveFormat *dst_format, long dst_format_len, const wchar_t* pShortNameHint, vdblock<uint8>& config) : AudioStream() {
	VDWaveFormat *iFormat = src->GetFormat();

	SetSource(src);
	name = pShortNameHint;

	mpCodec = VDCreateAudioCompressorPlugin((const VDWaveFormat *)iFormat, pShortNameHint, config, false);
	if (mpCodec) {
		fPlugin = true;
		dst_format_len = mpCodec->GetOutputFormatSize();
		VDWaveFormat *oFormat = AllocFormat(dst_format_len);
		memcpy(oFormat, mpCodec->GetOutputFormat(), dst_format_len);
		dst_format = oFormat;
		fVBR = true;
	} else {
		fPlugin = false;
		VDWaveFormat *oFormat = AllocFormat(dst_format_len);
		memcpy(oFormat, dst_format, dst_format_len);
		mpCodec = VDCreateAudioCompressorW32((const VDWaveFormat *)iFormat, dst_format, pShortNameHint, true);
		fVBR = false;
	}

	bytesPerInputSample = iFormat->mBlockSize;
	bytesPerOutputSample = dst_format->mBlockSize;
	mPrefill = 0;

	fStreamEnded = FALSE;
}

vd2::FormatConfidence AudioCompressor::SuggestFileFormat(const char* name) {
	if (fPlugin) {
		return mpCodec->SuggestFileFormat(name);
	} else {
		if(strcmp(name,"wav")==0 || strcmp(name,"avi")==0) return vd2::kFormat_Good;
		return vd2::kFormat_Unknown;
	}
}

bool AudioCompressor::IsVBR() const {
	return fVBR;
}

void AudioCompressor::GetStreamInfo(VDXStreamInfo& si) const {
	if (mpCodec) mpCodec->GetStreamInfo(si);
}

const VDFraction AudioCompressor::GetSampleRate() const {
	if (fVBR) {
		VDXStreamInfo si;
		VDWaveFormat* format = GetFormat();
		si.aviHeader.dwScale = format->mBlockSize;
		si.aviHeader.dwRate = format->mSamplingRate;
		// mp3: dwScale remains 1152
		// aac: dwScale is changed 1536->1024
		if (mpCodec) mpCodec->GetStreamInfo(si);
		return VDFraction(si.aviHeader.dwRate,si.aviHeader.dwScale);
	} else {
		return AudioStream::GetSampleRate();
	}
}

void AudioCompressor::UpdateFormat() {
	if (mpCodec) {
		long dst_format_len = mpCodec->GetOutputFormatSize();
		VDWaveFormat *oFormat = AllocFormat(dst_format_len);
		memcpy(oFormat, mpCodec->GetOutputFormat(), dst_format_len);
	}
}

AudioCompressor::~AudioCompressor() {
}

void AudioCompressor::SkipSource(long samples) {
	if (samples<0) {
		mPrefill = -samples;
		return;
	}

	if (samples && !source->Skip(samples)) {
		int maxRead = bytesPerInputSample > 16384 ? 1 : 16384 / bytesPerInputSample;

		vdblock<uint8> tempBuf(bytesPerInputSample * maxRead);
		void *dst = tempBuf.data();

		long actualBytes, actualSamples;
		do {
			long tc = samples;

			if (tc > maxRead)
				tc = maxRead;

			actualSamples = source->Read(dst, tc, &actualBytes);

			samples -= actualSamples;
		} while(samples>0 && actualBytes);

		if (!actualBytes || source->isEnd())
			fStreamEnded = TRUE;
	}
}

long AudioCompressor::_ReadVBR(void *buffer, long space, long *lplBytes, sint64 *duration) {
	VDASSERT(fVBR);
	long bytes = 0;

	while(!bytes) {
		bytes = mpCodec->CopyOutput(buffer, space, *duration);
		if (bytes>space) {
			*lplBytes = bytes;
			return 0;
		}

		if (!bytes) {
			if (!Process())
				break;
		}
	}

	if (lplBytes)
		*lplBytes = bytes;

	return bytes ? 1:0;
}

long AudioCompressor::_Read(void *buffer, long samples, long *lplBytes) {
	VDASSERT(!fVBR);
	long bytes = 0;
	long space = samples * bytesPerOutputSample;

	while(space > 0) {
		unsigned actualBytes = mpCodec->CopyOutput(buffer, space);
		VDASSERT(!(actualBytes % bytesPerOutputSample));	// should always be true, since we trim runts in Process()

		if (!actualBytes) {
			if (!Process())
				break;

			continue;
		}

		buffer = (char *)buffer + actualBytes;
		space -= actualBytes;
		bytes += actualBytes;
	}

	if (lplBytes)
		*lplBytes = bytes;

	return bytes / bytesPerOutputSample;
}

bool AudioCompressor::Process() {
	if (mpCodec->GetOutputLevel())
		return true;

	// fill the input buffer up!
	bool audioRead = false;

	if (!fStreamEnded) {
		unsigned inputSpace;
		char *dst0 = (char *)mpCodec->LockInputBuffer(inputSpace);
		if (inputSpace >= bytesPerInputSample) {
			char *dst = dst0;

			unsigned left = inputSpace;
			do {
				const long samples = left / bytesPerInputSample;
				long actualBytes;
				long actualSamples;

				if (mPrefill>0) {
					long n = samples<mPrefill ? samples:long(mPrefill);
					actualSamples = n;
					mPrefill -= n;
					actualBytes = fill_silence_pcm(dst, n, source->GetFormat());
				} else {
					actualSamples = source->Read(dst, samples, &actualBytes);
				}
				VDASSERT(actualSamples * bytesPerInputSample == actualBytes);

				left -= actualBytes;
				dst += actualBytes;

				if (!actualSamples || source->isEnd()) {
					fStreamEnded = TRUE;
					break;
				}
			} while(left >= bytesPerInputSample);

			if (dst > dst0) {
				VDASSERT(dst - dst0 <= inputSpace);

				mpCodec->UnlockInputBuffer(dst - dst0);
				audioRead = true;
			}
		}
	}

	return mpCodec->Convert(fStreamEnded, !audioRead);
}

bool AudioCompressor::isEnd() {
	return fStreamEnded && !mpCodec->GetOutputLevel();
}

///////////////////////////////////////////////////////////////////////////
//
//	Corrects the nAvgBytesPerFrame for that stupid Fraunhofer-IIS
//	codec.


AudioL3Corrector::AudioL3Corrector() {
	samples = frame_bytes = 0;
	read_left = 4;
	frames = 0;
	header_mode = true;
}

long AudioL3Corrector::ComputeByterate(long sample_rate) const {
	return MulDiv(frame_bytes, sample_rate, samples);
}

double AudioL3Corrector::ComputeByterateDouble(long sample_rate) const {
	return (double)frame_bytes*sample_rate/samples;
}

void AudioL3Corrector::Process(void *buffer, long bytes) {
	static const int bitrates[2][16]={
		{0, 8,16,24,32,40,48,56, 64, 80, 96,112,128,144,160,0},
		{0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}
	};
	static const long samp_freq[4] = {44100, 48000, 32000, 0};

	int cnt=0;
	int tc;

	while(cnt < bytes) {
		tc = bytes - cnt;
		if (tc > read_left)
			tc = read_left;

		if (header_mode)
			memcpy(&hdr_buffer[4-read_left], buffer, tc);

		buffer = (char *)buffer + tc;
		cnt += tc;
		read_left -= tc;

		if (read_left <= 0)
			if (header_mode) {
				// We've got a header!  Process it...

				long hdr = *(long *)hdr_buffer;
				long samp_rate, framelen;

				if ((hdr & 0xE0FF) != 0xE0FF)
					throw MyError(
						"VirtualDub was unable to scan the compressed MP3 stream for time correction purposes. "
						"If not needed, MP3 time correction can be disabled in Options, Preferences, AVI.\n"
						"\n"
						"(A sync mark was expected at byte location %lu, but word %02x%02x%02x%02x was found instead.)"
						, (unsigned long)frame_bytes
						, 0xff & hdr_buffer[0]
						, 0xff & hdr_buffer[1]
						, 0xff & hdr_buffer[2]
						, 0xff & hdr_buffer[3]);

				samp_rate = samp_freq[(hdr>>18)&3];

				if (!((hdr>>11)&1)) {
					samp_rate /= 2;
					samples += 576;
				} else
					samples += 1152;

				if (!(hdr & 0x1000))
					samp_rate /= 2;

				framelen = (bitrates[(hdr>>11)&1][(hdr>>20)&15] * (((hdr>>11)&1) ? 144000 : 72000)) / samp_rate;

				if (hdr&0x20000) ++framelen;

				// update statistics

				frame_bytes += framelen;
				++frames;

				// start skipping the remainder

				read_left = framelen - 4;
				header_mode = false;

			} else {

				// Done skipping frame data; collect the next header

				read_left = 4;
				header_mode = true;
			}
	}
}

///////////////////////////////////////////////////////////////////////////

sint64 AudioTranslateVideoSubset(FrameSubset& dst, const FrameSubset& src, const VDFraction& videoFrameRate, const VDWaveFormat *pwfex, sint64 tail, IVDStreamSource *pVBRAudio) {
	const long nBytesPerSec = pwfex->mDataRate;
	const int mBlockSize = pwfex->mBlockSize;
	sint64 total = 0;

	// I like accuracy, so let's strive for accuracy.  Accumulate errors as we go;
	// use them to offset the starting points of subsequent segments, never being
	// more than 1/2 segment off.
	//
	// The conversion equation is in units of (1000000*mBlockSize).

	sint64 nError		= 0;
	sint64 nMultiplier	= (sint64)videoFrameRate.getLo() * nBytesPerSec;
	sint64 nDivisor		= (sint64)videoFrameRate.getHi() * mBlockSize;
	sint64 nRound		= nDivisor/2;
	sint64 nTotalFramesAccumulated = 0;

	for(FrameSubset::const_iterator it = src.begin(), itEnd = src.end(); it != itEnd; ++it) {
		VDPosition start, end;

		// Compute error.
		//
		// Ideally, we want the audio and video streams to be of the exact length.
		//
		// Audiolen = (videolen * usPerFrame * nBytesPerSec) / (1000000*mBlockSize);

		nError = total*nDivisor - (nTotalFramesAccumulated * nMultiplier);

		// Add a block.

		if (pVBRAudio)
			start = pVBRAudio->TimeToPositionVBR(VDRoundToInt64(it->start / videoFrameRate.asDouble() * 1000000.0));
		else
			start = ((__int64)it->start * nMultiplier + nRound + nError) / nDivisor;

		end = start + ((__int64)it->len * nMultiplier + nRound) / nDivisor;

		nTotalFramesAccumulated += it->len;

		dst.addRange(start, end-start, false, it->source);

		total += end-start;
	}

	if (tail) {
		if (dst.empty()) {
			dst.addRange(0, tail, false, 0);
			total += tail;
		} else {
			FrameSubsetNode& fsn = dst.back();

			if (tail > fsn.end()) {
				total -= fsn.len;
				fsn.len = tail - fsn.start;
				total += fsn.len;
			}
		}
	}

	return total;
}

namespace {
	struct AudioSourceInfo {
		double mSamplesPerFrame;
	};
}

void VDTranslateVideoSubsetToAudioSubset(FrameSubset& dst, const vdfastvector<AudioStream *>& sources, const FrameSubset& videoSubset, const VDFraction& videoFrameRate, bool appendTail) {
	sint64 total = 0;

	// I like accuracy, so let's strive for accuracy.  Accumulate errors as we go;
	// use them to offset the starting points of subsequent segments, never being
	// more than 1/2 segment off.
	//
	// The conversion equation is in units of (1000000*mBlockSize).
	double ratio		= sources[0]->GetSampleRate().asDouble() / videoFrameRate.asDouble();
	sint64 nTotalFramesAccumulated = 0;

	int lastSource = -1;
	for(FrameSubset::const_iterator it = videoSubset.begin(), itEnd = videoSubset.end(); it != itEnd; ++it) {
		const int source = it->source;

		lastSource = source;

		VDPosition start, end;

		// Compute error.
		//
		// Ideally, we want the audio and video streams to be of the exact length.
		//
		// Audiolen = (videolen * usPerFrame * nBytesPerSec) / (1000000*mBlockSize);

		// Add a block.

		double error = (double)total - (double)nTotalFramesAccumulated * ratio;
		start = VDRoundToInt64((double)it->start * ratio + error);

		end = VDRoundToInt64((double)(it->start + it->len) * ratio);

		nTotalFramesAccumulated += it->len;

		sint64 addlen = end - start;
		if (addlen > 0) {
			dst.addRange(start, addlen, false, source);
			total += addlen;
		}
	}

	if (appendTail && lastSource >= 0) {
		VDPosition tail = sources[lastSource]->GetLength();

		if (dst.empty()) {
			dst.addRange(0, tail, false, 0);
		} else {
			FrameSubsetNode& fsn = dst.back();

			if (tail > fsn.end()) {
				fsn.len = tail - fsn.start;
			}
		}
	}
}

AudioSubset::AudioSubset(const vdfastvector<AudioStream *>& sources, const FrameSubset *pfs, const VDFraction& videoFrameRate, sint64 preskew, bool appendTail)
	: mCurrentSource(-1)
	, mpCurrentSource(NULL)
	, mbLimited(!appendTail)
	, mbEnded(false)
{
	const VDWaveFormat *pSrcFormat = sources[0]->GetFormat();
	uint32 srcFormatLen = sources[0]->GetFormatLen();

	mSources = sources;

	memcpy(AllocFormat(srcFormatLen), pSrcFormat, srcFormatLen);

	if (!pfs) {
		if (!sources.empty())
			subset.addRange(0, sources[0]->GetLength(), false, 0);
	} else
		VDTranslateVideoSubsetToAudioSubset(subset, sources, *pfs, videoFrameRate, appendTail);

	if (preskew > 0) {
		const double preskewSecs = (double)preskew / 1000000.0;
		const VDFraction& srcSampleRate = sources[0]->GetSampleRate();

		subset.deleteRange(0, VDRoundToInt64(preskewSecs * srcSampleRate.asDouble()));
	}

	stream_len = subset.getTotalFrames();

	SetLimit(stream_len);

	pfsnCur = subset.begin();
	mOffset = 0;
	mSrcPos = 0;
	mSkipSize = kSkipBufferSize / pSrcFormat->mBlockSize;
}

AudioSubset::~AudioSubset() {
}

const VDFraction AudioSubset::GetSampleRate() const {
	if (mSources.empty())
		return VDFraction(format->mDataRate, format->mBlockSize);

	return mSources.front()->GetSampleRate();
}

bool AudioSubset::IsVBR() const {
	for(Sources::const_iterator it(mSources.begin()), itEnd(mSources.end()); it!=itEnd; ++it) {
		AudioStream *src = *it;

		if (src->IsVBR())
			return true;
	}

	return false;
}

long AudioSubset::_Read(void *buffer, long samples, long *lplBytes) {
	int actual;

	if (pfsnCur != subset.end()) {
		const FrameSubsetNode& node = *pfsnCur;

		bool forceSeek = false;

		if (node.source != mCurrentSource) {
			mCurrentSource = node.source;
			mpCurrentSource = mSources[mCurrentSource];
			forceSeek = true;
		}

		for(;;) {
			sint64 targetPos = node.start + mOffset;
			if (mSrcPos == targetPos)
				break;

			sint64 offset = targetPos - mSrcPos;
			long t;

			if (offset < 0 || forceSeek) {
				mpCurrentSource->Seek(targetPos);
				mSrcPos = targetPos;
				break;
			}

			if (mpCurrentSource->Skip(offset)) {
				mSrcPos += offset;
				break;
			}

			sint32 toskip = mSkipSize;
			if (toskip > offset) toskip = (sint32)offset;

			char skipBuffer[kSkipBufferSize];
			actual = mpCurrentSource->Read(skipBuffer, toskip, &t);

			if (!actual) {
				*lplBytes = 0;
				return 0;
			}

			mSrcPos += actual;
		}

		if (samples > node.len - mOffset)
			samples = (long)(node.len - mOffset);
	} else if (mbLimited) {
		samples = 0;
	}

	*lplBytes = 0;
	if (samples) {
		samples = mpCurrentSource->Read(buffer, samples, lplBytes);

		if (buffer) {
			mOffset += samples;
			mSrcPos += samples;
		}
	}

	while (pfsnCur != subset.end() && mOffset >= pfsnCur->len) {
		mOffset -= pfsnCur->len;
		++pfsnCur;
	}

	if (!samples)
		mbEnded = true;

	return samples;
}

long AudioSubset::_ReadVBR(void *buffer, long space, long *lplBytes, sint64 *duration) {
	*duration = -1;
	return _Read(buffer,1,lplBytes);
}

bool AudioSubset::_isEnd() {
	return mbEnded;
}

///////////////////////////////////////////////////////////////////////////
//
//	AudioAmplifier
//
///////////////////////////////////////////////////////////////////////////

static void amplify8(void* buffer, const long lBytes, const int iFactor)
{
	long count = lBytes;
	if (count) {
		int lBias = 0x8080 - 0x80 * iFactor;

		uint8_t* dst = (uint8_t*)buffer;
		do {
			int y = ((int)*dst * iFactor + lBias) >> 8;

			*dst++ = (uint8_t)std::clamp(y, 0, 255);
		} while (--count);
	}
}

static void amplify16(void* buffer, const long lBytes, const int iFactor)
{
	long count = lBytes / sizeof(int16_t);
	if (count) {
		int16_t* dst = (int16_t*)buffer;
		do {
			int y = ((int)*dst * iFactor + 0x80) >> 8;

			*dst++ = (int16_t)std::clamp(y, -0x8000, 0x7FFF);
		} while (--count);
	}
}

static void amplify32f(void* buffer, const long lBytes, const int iFactor)
{
	long count = lBytes / sizeof(float);
	if (count) {
		float factor = (float)iFactor / 256;

		float* dst = (float*)buffer;
		do {
			float y = *dst * factor;

			*dst++ = std::clamp(y, -1.f, 1.f);
		} while (--count);
	}
}

AudioStreamAmplifier::AudioStreamAmplifier(AudioStream *src, float factor)
	: mFactor(VDRoundToInt(256.0f * factor))
{
	VDWaveFormat *iFormat = src->GetFormat();
	VDWaveFormat *oFormat;

	memcpy(oFormat = AllocFormat(src->GetFormatLen()), iFormat, src->GetFormatLen());

	SetSource(src);

	if (is_audio_pcm(format)) {
		switch (format->mSampleBits) {
		case  8: mAmplifyFn = amplify8; break;
		case 16: mAmplifyFn = amplify16; break;
		}
	}
	else if (is_audio_float(format)) {
		mAmplifyFn = amplify32f;
	}
}

AudioStreamAmplifier::~AudioStreamAmplifier() {
}

long AudioStreamAmplifier::_Read(void *buffer, long samples, long *lplBytes) {
	long lActualSamples=0;
	long lBytes;

	lActualSamples = source->Read(buffer, samples, &lBytes);

	if (lActualSamples && mAmplifyFn) {
		mAmplifyFn(buffer, lBytes, mFactor);
	}

	if (lplBytes) {
		*lplBytes = lBytes;
	}

	return lActualSamples;
}

bool AudioStreamAmplifier::_isEnd() {
	return source->isEnd();
}

bool AudioStreamAmplifier::Skip(sint64 samples) {
	return source->Skip(samples);
}

///////////////////////////////////////////////////////////////////////////
//
//	AudioStreamL3Corrector
//
///////////////////////////////////////////////////////////////////////////

AudioStreamL3Corrector::AudioStreamL3Corrector(AudioStream *src){
	VDWaveFormat *iFormat = src->GetFormat();
	VDWaveFormat *oFormat;

	memcpy(oFormat = AllocFormat(src->GetFormatLen()), iFormat, src->GetFormatLen());

	SetSource(src);
}

AudioStreamL3Corrector::~AudioStreamL3Corrector() {
}

long AudioStreamL3Corrector::_Read(void *buffer, long samples, long *lplBytes) {
	long lActualSamples=0;
	long lBytes;

	lActualSamples = source->Read(buffer, samples, &lBytes);

	Process(buffer, lBytes);

	if (lplBytes)
		*lplBytes = lBytes;

	return lActualSamples;
}

long AudioStreamL3Corrector::_ReadVBR(void *buffer, long size, long *lplBytes, sint64 *duration) {
	long lActualSamples=0;
	long lBytes;

	lActualSamples = source->ReadVBR(buffer, size, &lBytes, duration);

	if (lActualSamples)
		Process(buffer, lBytes);

	if (lplBytes)
		*lplBytes = lBytes;

	return lActualSamples;
}

AudioStats::AudioStats(AudioStream *src) {
	VDWaveFormat *iFormat = src->GetFormat();
	VDWaveFormat *oFormat;

	memcpy(oFormat = AllocFormat(src->GetFormatLen()), iFormat, src->GetFormatLen());

	SetSource(src);
	packets = 0;
	total_bytes = 0;
	total_duration = 0;
	max_sample = 0;
}

long AudioStats::_ReadVBR(void *buffer, long size, long *lplBytes, sint64 *duration) {
	long lActualSamples = source->ReadVBR(buffer, size, lplBytes, duration);
	if (lActualSamples){
		packets += lActualSamples;
		if (*lplBytes>max_sample) max_sample = *lplBytes;
		total_bytes += *lplBytes;
		total_duration += *duration;
	}

	return lActualSamples;
}

long AudioStats::ComputeByterate() const {
	return long(total_bytes*GetFormat()->mSamplingRate/total_duration);
}

double AudioStats::ComputeByterateDouble() const {
	return (double)total_bytes*GetFormat()->mSamplingRate/total_duration;
}

///////////////////////////////////////////////////////////////////////////
//
//	AudioFilterSystemStream
//
///////////////////////////////////////////////////////////////////////////

AudioFilterSystemStream::AudioFilterSystemStream(const VDAudioFilterGraph& graph, sint64 start_us) {
	int nOutputFilter = -1;

	VDAudioFilterGraph graph2(graph);
	VDAudioFilterGraph::FilterList::iterator it(graph2.mFilters.begin()), itEnd(graph2.mFilters.end());

	for(unsigned i=0; it!=itEnd; ++it, ++i) {
		if ((*it).mFilterName == L"output") {
			if (nOutputFilter >= 0)
				throw MyError("Audio filter graph contains more than one output node.");

			nOutputFilter = i;
			(*it).mFilterName = L"*sink";
		}
	}

	if (nOutputFilter < 0)
		throw MyError("Audio filter graph lacks an output node.");

	std::vector<IVDAudioFilterInstance *> filterPtrs;
	mFilterSystem.SetScheduler(&mFilterSystemScheduler);
	mFilterSystem.LoadFromGraph(graph2, filterPtrs);
	mFilterSystem.Start();

	mpFilterIF = VDGetAudioFilterSinkInterface(filterPtrs[nOutputFilter]->GetObject());

	mFilterSystem.Seek(start_us);

	int len = mpFilterIF->GetFormatLen();
	const VDWaveFormat *pFormat = (const VDWaveFormat *)mpFilterIF->GetFormat();

	if (pFormat->mTag == WAVE_FORMAT_PCM)
		len = sizeof(PCMWAVEFORMAT);

	memcpy(AllocFormat(len), pFormat, len);

	stream_len = ((mpFilterIF->GetLength() - start_us)*pFormat->mDataRate) / (pFormat->mBlockSize*(sint64)1000000);

	mStartTime = start_us;
	mSamplePos = 0;
}

AudioFilterSystemStream::~AudioFilterSystemStream() {
	mFilterSystem.Stop();
	mFilterSystem.Clear();		// must do to free scheduler
}

long AudioFilterSystemStream::_Read(void *buffer, long samples, long *lplBytes) {
	uint32 total_samples = 0;
	uint32 total_bytes = 0;

	if (samples>0) {
		char *dst = (char *)buffer;

		while(!mpFilterIF->IsEnded()) {
			uint32 actual = mpFilterIF->ReadSamples(dst, samples - total_samples);

			if (actual) {
				total_samples += actual;
				total_bytes += format->mBlockSize*actual;
				dst += format->mBlockSize*actual;
			}

			if (total_samples >= samples)
				break;

			if (!mFilterSystemScheduler.Run())
				break;
		}
	}

	if (lplBytes)
		*lplBytes = total_bytes;

	mSamplePos += total_samples;

	return total_samples;
}

bool AudioFilterSystemStream::_isEnd() {
	return mpFilterIF->IsEnded();
}

bool AudioFilterSystemStream::Skip(sint64 samples) {
	const VDWaveFormat *pFormat = GetFormat();

	// for short skips (<1 sec), just read through
	if (samples < pFormat->mDataRate / pFormat->mBlockSize)
		return false;

	// reseek
	mSamplePos += samples;

	mFilterSystem.Seek(mStartTime + (mSamplePos * pFormat->mBlockSize * 1000000) / pFormat->mDataRate);

	return true;
}

void AudioFilterSystemStream::Seek(VDPosition pos) {
	const VDWaveFormat *pFormat = GetFormat();

	// reseek
	mSamplePos = pos;

	mFilterSystem.Seek(mStartTime + (mSamplePos * pFormat->mBlockSize * 1000000) / pFormat->mDataRate);
}
