//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2004 Avery Lee
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
#include <process.h>
#include <windows.h>
#include <commctrl.h>
#include <fcntl.h>
#include <io.h>

#include "AudioSource.h"
#include "VideoSource.h"
#include "FastReadStream.h"
#include <vd2/system/error.h>
#include <vd2/system/fraction.h>
#include <vd2/system/log.h>
#include <vd2/system/file.h>
#include <vd2/system/thread.h>
#include <vd2/system/VDRingBuffer.h>
#include <vd2/system/memory.h>
#include <vd2/Dita/resources.h>

#include "misc.h"
#include "mpeg.h"
#include "resource.h"
#include "gui.h"
#include <vd2/system/cpuaccel.h>
#include <vd2/Priss/decoder.h>
#include <vd2/Meia/MPEGDecoder.h>
#include <vd2/Meia/MPEGPredict.h>
#include <vd2/Meia/MPEGIDCT.h>
#include <vd2/Meia/MPEGConvert.h>

//////////////////////////////////////////////////////////////////////////

extern HINSTANCE g_hInst;

//////////////////////////////////////////////////////////////////////////

namespace {
	enum { kVDST_Mpeg = 5 };

	enum {
		kVDM_AudioConcealingError,		// MPEGAudio: Concealing decoding error on frame %lu: %hs.
		kVDM_OpeningFile,				// MPEG: Opening file "%hs"
		kVDM_OOOTimestamp,				// MPEG: Anachronistic or discontinuous timestamp found in %ls stream %d at byte position %lld (may indicate improper join)
		kVDM_Incomplete,				// MPEG: File ended unexpectedly during parsing at position %lld -- file may be damaged or incomplete.
	};
}

//////////////////////////////////////////////////////////////////////////

#define VIDPKT_TYPE_SEQUENCE_START		(0xb3)
#define	VIDPKT_TYPE_SEQUENCE_END		(0xb7)
#define VIDPKT_TYPE_GROUP_START			(0xb8)
#define VIDPKT_TYPE_PICTURE_START		(0x00)
#define VIDPKT_TYPE_SLICE_START_MIN		(0x01)
#define	VIDPKT_TYPE_SLICE_START_MAX		(0xaf)
#define VIDPKT_TYPE_EXT_START			(0xb5)
#define VIDPKT_TYPE_USER_START			(0xb2)

#define MPEG_FRAME_TYPE_I		1
#define MPEG_FRAME_TYPE_P		2
#define MPEG_FRAME_TYPE_B		3

#define MPEG_BUFFER_BIDIRECTIONAL (0)
#define MPEG_BUFFER_BACKWARD (1)
#define MPEG_BUFFER_FORWARD (2)

//////////////////////////////////////////////////////////////////////////
//
//
//							DataVector
//
//
//////////////////////////////////////////////////////////////////////////

class DataVectorBlock {
public:
	enum { BLOCK_SIZE = 4096 };

	DataVectorBlock *next;

	char heap[BLOCK_SIZE];
	int index;

	DataVectorBlock() {
		next = NULL;
		index = 0;
	}
};

class DataVector {
private:
	DataVectorBlock *first, *last;
	int item_size;
	int count;

	void _Add(void *);

public:
	DataVector(int item_size);
	~DataVector();

	void Add(void *pp) {
		if (!last || last->index >= DataVectorBlock::BLOCK_SIZE - item_size) {
			_Add(pp);
			return;
		}

		memcpy(last->heap + last->index, pp, item_size);
		last->index += item_size;
		++count;
	}

	void *MakeArray();

	int Length() { return count; }

	bool empty() const { return !count; }
};

DataVector::DataVector(int _item_size) : item_size(_item_size) {
	first = last = NULL;
	count = 0;
}

DataVector::~DataVector() {
	DataVectorBlock *i, *j;

	j = first;
	while(i=j) {
		j = i->next;
		delete i;
	}
}

void DataVector::_Add(void *pp) {
	if (!last || last->index > DataVectorBlock::BLOCK_SIZE - item_size) {
		DataVectorBlock *ib = new DataVectorBlock();

		if (!ib) throw MyMemoryError();

		if (last)		last->next = ib;
		else			first = ib;

		last = ib;
	}

	memcpy(last->heap + last->index, pp, item_size);
	last->index += item_size;
	++count;
}

void *DataVector::MakeArray() {
	char *array = new char[count * item_size], *ptr = array;
	DataVectorBlock *dvb = first;

	if (!array) throw MyMemoryError();

	while(dvb) {
		memcpy(ptr, dvb->heap, dvb->index);
		ptr += dvb->index;

		dvb=dvb->next;
	}

	return array;
}



//////////////////////////////////////////////////////////////////////////
//
//
//					InputFileMPEGOptions
//
//
//////////////////////////////////////////////////////////////////////////

class InputFileMPEGOptions : public InputFileOptions {
public:
	enum {
		DECODE_NO_B	= 1,
		DECODE_NO_P	= 2,
	};

	struct InputFileMPEGOpts {
		int len;
		int iDecodeMode;
		bool fAcceptPartial;
	} opts;


	~InputFileMPEGOptions();

	bool read(const char *buf);
	int write(char *buf, int buflen) const;

	static INT_PTR APIENTRY SetupDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};

InputFileMPEGOptions::~InputFileMPEGOptions() {
}

bool InputFileMPEGOptions::read(const char *buf) {
	const InputFileMPEGOpts *pp = (const InputFileMPEGOpts *)buf;

	if (pp->len != sizeof(InputFileMPEGOpts))
		return false;

	opts = *pp;

	return true;
}

int InputFileMPEGOptions::write(char *buf, int buflen) const {
	if (buf) {
		InputFileMPEGOpts *pp = (InputFileMPEGOpts *)buf;

		if (buflen<sizeof(InputFileMPEGOpts))
			return 0;

		*pp = opts;
		pp->len = sizeof(InputFileMPEGOpts);
	}

	return sizeof(InputFileMPEGOpts);
}

///////

INT_PTR APIENTRY InputFileMPEGOptions::SetupDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	InputFileMPEGOptions *thisPtr = (InputFileMPEGOptions *)GetWindowLongPtr(hDlg, DWLP_USER);

	switch(message) {
		case WM_INITDIALOG:
			SetWindowLongPtr(hDlg, DWLP_USER, lParam);
			CheckDlgButton(hDlg, IDC_MPEG_ALL_FRAMES, TRUE);
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDCANCEL) {
				if (IsDlgButtonChecked(hDlg, IDC_MPEG_I_FRAMES_ONLY	))
					thisPtr->opts.iDecodeMode = InputFileMPEGOptions::DECODE_NO_B | InputFileMPEGOptions::DECODE_NO_P;

				if (IsDlgButtonChecked(hDlg, IDC_MPEG_IP_FRAMES_ONLY))
					thisPtr->opts.iDecodeMode = InputFileMPEGOptions::DECODE_NO_B;

				if (IsDlgButtonChecked(hDlg, IDC_MPEG_ALL_FRAMES	))
					thisPtr->opts.iDecodeMode = 0;

				thisPtr->opts.fAcceptPartial = true;

				EndDialog(hDlg, 0);
				return TRUE;
			}
			break;
	}

	return FALSE;
}

//////////////////////////////////////////////////////////////////////////
//
//
//					MPEGPacketInfo/MPEGSampleInfo
//
//
//////////////////////////////////////////////////////////////////////////


struct MPEGPacketInfo {
	__int64		file_pos;
	__int64		stream_pos;
};

struct MPEGSampleInfo {
	__int64			stream_pos;
	int				size;
	union {
		struct {
			char			frame_type;
			bool			broken_link;
			unsigned short	subframe_num;
		};

		struct {
			long			header;
		};
	};
};

//////////////////////////////////////////////////////////////////////////
//
//
//					InputFileMPEG
//
//
//////////////////////////////////////////////////////////////////////////

class AudioSourceMPEG;
class VideoSourceMPEG;

class InputFileMPEGPrefetcher : protected VDThread {
public:
	InputFileMPEGPrefetcher(VDFile& file, VDFile *unfile, int blocksize, int blockcount);
	~InputFileMPEGPrefetcher();

	sint32 readData(void *p, sint32 count);

protected:
	void ThreadRun();

	VDAtomicInt			mbQuit;
	int					mBlockSize;
	VDSignal			msigRead;
	VDSignal			msigWrite;
	VDRingBuffer<char, VDFileUnbufferAllocator<char> >	mBuffer;
	VDFile&				mFile;
	VDFile *const		mpUnbufferedFile;
	MyError				mError;
};

InputFileMPEGPrefetcher::InputFileMPEGPrefetcher(VDFile& file, VDFile *unfile, int blocksize, int blockcount)
	: mbQuit(0)
	, mBlockSize(blocksize)
	, mBuffer(blocksize * blockcount)
	, mFile(file)
	, mpUnbufferedFile(unfile)
{
	ThreadStart();
}

InputFileMPEGPrefetcher::~InputFileMPEGPrefetcher() {
	mbQuit = true;
	msigRead.signal();
	ThreadWait();
}

sint32 InputFileMPEGPrefetcher::readData(void *p, sint32 count) {
	sint32 actual = 0;

	const int threshold = mBuffer.getSize() - mBlockSize;

	while(count > 0) {
		int avail = 0;
		const void *s = mBuffer.LockRead(count, avail);

		if (avail > 0) {
			if (p) {
				memcpy(p, s, avail);
				p = (char *)p + avail;
			}

			int newlevel = mBuffer.UnlockRead(avail);
			int oldlevel = newlevel + avail;
			if (oldlevel > threshold && newlevel <= threshold)
				msigRead.signal();

			actual += avail;
			count -= avail;
			continue;
		}

		if (mbQuit) {
			if (mError.gets())
				throw mError;

			break;
		}

		msigWrite.wait();
	}

	return actual;
}

void InputFileMPEGPrefetcher::ThreadRun() {
	const int blocksize = mBlockSize;

	try {
		sint64 pos = mFile.tell();
		sint64 bufferedThreshold = mFile.size() - blocksize + 1;

		if (mpUnbufferedFile)
			mpUnbufferedFile->seek(pos);
		else
			bufferedThreshold = 0;

		while(!mbQuit) {
			int actual;
			void *p;

			p = mBuffer.LockWrite(blocksize, actual);
			if (actual < blocksize) {
				msigRead.wait();
				continue;
			}

			if (pos >= bufferedThreshold) {
				mFile.seek(pos);
				actual = mFile.readData(p, blocksize);
			} else {
				actual = mpUnbufferedFile->readData(p, blocksize);
			}

			if (actual >= 0) {
				pos += actual;

				if (actual == mBuffer.UnlockWrite(actual))
					msigWrite.signal();
			}

			if (actual < blocksize)
				break;
		}
	} catch(const MyError& e) {
		mError.assign(e);
	}

	mbQuit = true;
	msigWrite.signal();
}

class InputFileMPEG : public InputFile {
friend VideoSourceMPEG;
friend AudioSourceMPEG;
private:
	__int64 file_len, file_cpos;
	char *video_packet_buffer;
	char *audio_packet_buffer;
	MPEGPacketInfo *video_packet_list;
	MPEGSampleInfo *video_sample_list;
	MPEGPacketInfo *audio_packet_list;
	MPEGSampleInfo *audio_sample_list;
	int packets, apackets;
	int frames, aframes;
	int last_packet[2];
	int width, height;
	VDFraction mFrameRate;
	VDFraction mPixelAspectRatio;
	bool fInterleaved, fHasAudio, fIsVCD;
	bool fAbort;

	long audio_first_header;

	int iDecodeMode;
	bool fAcceptPartial;
	bool fAudioBadPad;

	FastReadStream *pFastRead;

	VDFile	mFile;
	VDFile	mFileUnbuffered;

	static const char szME[];

	enum {
		RESERVED_STREAM		= 0xbc,
		PRIVATE_STREAM1		= 0xbd,
		PADDING_STREAM		= 0xbe,
		PRIVATE_STREAM2		= 0xbf,
	};

	enum {
		SCAN_BUFFER_SIZE	= 262144
	};

	char *	pScanBuffer;
	char *	pScan;
	char *	pScanLimit;
	sint64	i64ScanCpos;
	InputFileMPEGPrefetcher		*mpScanPrefetcher;

	bool	mbCustomIntraQuantMatrix;
	bool	mbCustomNonintraQuantMatrix;

	uint8	mIntraQuantMatrix[64];
	uint8	mNonintraQuantMatrix[64];

private:
	void	StartScan();
	bool	NextStartCode();
	void	Skip(int bytes);

	int		Read() {
		return pScan < pScanLimit ? (unsigned char)*pScan++ : _Read();
	}
	int		Read(void *, int, bool);

	int		_Read();
	void	UnRead() {
		--pScan;
	}
	void	EndScan();
	__int64	Tell();

	void	ReadStream(void *buffer, __int64 pos, long len, bool fAudio);
	int		FindStartingPacket(sint64 pos, bool bAudio, sint32& offset);
	sint64	GetStartingBytePosition(VDPosition pos, bool fAudio);

	static INT_PTR CALLBACK ParseDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

	void setOptions(InputFileOptions *) override;
	InputFileOptions *createOptions(const void *buf, uint32 len) override;
	InputFileOptions *promptForOptions(VDGUIHandle) override;
public:
	InputFileMPEG();
	~InputFileMPEG() override;

	void Init(const wchar_t *szFile) override;
	static void _InfoDlgThread(void *pvInfo);
	static INT_PTR CALLBACK _InfoDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	void InfoDialog(VDGUIHandle hwndParent) override;

	bool GetVideoSource(int index, IVDVideoSource **ppSrc) override;
	bool GetAudioSource(int index, AudioSource **ppSrc) override;
};


//////////////////////////////////////////////////////////////////////////
//
//
//						VideoSourceMPEG
//
//
//////////////////////////////////////////////////////////////////////////

class VideoSourceMPEG : public VideoSource {
private:
	vdrefptr<InputFileMPEG> parentPtr;

	long frame_forw, frame_back, frame_bidir;
	long frame_type;

	bool mbFBValid;

	vdrefptr<IVDMPEGDecoder> mpDecoder;

	uint32	mAccelerationFlags;

	void DecodeFrameBuffer(int buffer);
	LONG renumber_frame(LONG lSample);
	LONG translate_frame(LONG lSample);
	long prev_IP(long f);
	long prev_I(long f);
	bool is_I(long lSample);
	void UpdateAcceleration();

public:
	VideoSourceMPEG(InputFileMPEG *);
	~VideoSourceMPEG();

	void init();

	const VDFraction getPixelAspectRatio() const { return parentPtr->mPixelAspectRatio; }

	char getFrameTypeChar(VDPosition lFrameNum);
	bool _isKey(VDPosition lSample);
	virtual VDPosition nearestKey(VDPosition lSample);
	virtual VDPosition prevKey(VDPosition lSample);
	virtual VDPosition nextKey(VDPosition lSample);
	bool setTargetFormat(VDPixmapFormatEx format);
	void invalidateFrameBuffer();
	bool isFrameBufferValid();
	void streamBegin(bool fRealTime, bool bForceReset);
	void streamEnd();
	void streamRestart();
	void streamSetDesiredFrame(VDPosition frame_num);
	VDPosition streamGetNextRequiredFrame(bool& is_preroll);
	int streamGetRequiredCount(long *);
	const void *streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num, VDPosition target_num);
	uint32 streamGetDecodePadding() { return 8; }

	const void *getFrame(VDPosition frameNum);
	eDropType getDropType(VDPosition);
	int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead);
	sint64 getSampleBytePosition(VDPosition lStart64);

	VDPosition streamToDisplayOrder(VDPosition sample_num) {
		if (sample_num < mSampleFirst || sample_num >= mSampleLast)
			return sample_num;

		long gopbase = (long)sample_num;

		if (!is_I(gopbase))
			gopbase = prev_I(gopbase);

		return gopbase + parentPtr->video_sample_list[sample_num].subframe_num;
	}

	VDPosition displayToStreamOrder(VDPosition display_num) {
		return (display_num<mSampleFirst || display_num >= mSampleLast)
			? display_num
			: translate_frame(renumber_frame((long)display_num));
	}

	bool isDecodable(VDPosition sample_num);
};

VideoSourceMPEG::VideoSourceMPEG(InputFileMPEG *parent)
	: mpDecoder(CreateVDMPEGDecoder())
	, mAccelerationFlags((uint32)-1)
{
	parentPtr = parent;
}

void VideoSourceMPEG::init() {
	BITMAPINFOHEADER *bmih;
	int w, h;

	mbFBValid = false;
	frame_forw = frame_back = frame_bidir =-1;

	mSampleFirst = 0;
	mSampleLast = parentPtr->frames;

	w = (parentPtr->width+15) & -16;
	h = parentPtr->height;

	mpDecoder->Init(parentPtr->width, parentPtr->height);
	mpDecoder->SetIntraQuantizers(parentPtr->mbCustomIntraQuantMatrix ? parentPtr->mIntraQuantMatrix : NULL);
	mpDecoder->SetNonintraQuantizers(parentPtr->mbCustomNonintraQuantMatrix ? parentPtr->mNonintraQuantMatrix : NULL);

	if (!AllocFrameBuffer(w * h * 4 + 4))
		throw MyMemoryError();

	if (!(bmih = (BITMAPINFOHEADER *)allocFormat(sizeof(BITMAPINFOHEADER))))
		throw MyMemoryError();

	mpTargetFormatHeader.resize(getFormatLen());

	bmih->biSize		= sizeof(BITMAPINFOHEADER);
	bmih->biWidth		= w;
	bmih->biHeight		= h;
	bmih->biPlanes		= 1;
	bmih->biBitCount	= 32;
	bmih->biCompression	= 0xffffffff;
	bmih->biSizeImage	= 0;
	bmih->biXPelsPerMeter	= 0;
	bmih->biYPelsPerMeter	= 0;
	bmih->biClrUsed		= 0;
	bmih->biClrImportant	= 0;

	streamInfo.fccType					= VDAVIStreamInfo::kTypeVideo;
	streamInfo.fccHandler				= 0;
	streamInfo.dwFlags					= 0;
	streamInfo.wPriority				= 0;
	streamInfo.wLanguage				= 0;
	streamInfo.dwScale					= parentPtr->mFrameRate.getLo();
	streamInfo.dwRate					= parentPtr->mFrameRate.getHi();
	streamInfo.dwStart					= 0;
	streamInfo.dwLength					= parentPtr->frames;
	streamInfo.dwInitialFrames			= 0;
	streamInfo.dwSuggestedBufferSize	= 0;
	streamInfo.dwQuality				= 0xffffffffL;
	streamInfo.dwSampleSize				= 0;
	streamInfo.rcFrameLeft				= 0;
	streamInfo.rcFrameTop				= 0;
	streamInfo.rcFrameRight				= (uint16)w;
	streamInfo.rcFrameBottom			= (uint16)h;
}

VideoSourceMPEG::~VideoSourceMPEG() {
}

bool VideoSourceMPEG::setTargetFormat(VDPixmapFormatEx format) {
	using namespace nsVDPixmap;

	if (!format)
		format = kPixFormat_XRGB8888;

	switch(format) {
	case kPixFormat_XRGB1555:
	case kPixFormat_RGB565:
	case kPixFormat_RGB888:
	case kPixFormat_XRGB8888:
	case kPixFormat_YUV422_UYVY:
	case kPixFormat_YUV422_YUYV:
	case kPixFormat_YUV420_Planar:
	case kPixFormat_Y8:
		return VideoSource::setTargetFormat(format);
	}

	return false;
}

void VideoSourceMPEG::invalidateFrameBuffer() {
	mbFBValid = false;
}

bool VideoSourceMPEG::isFrameBufferValid() {
	return mbFBValid;
}

char VideoSourceMPEG::getFrameTypeChar(VDPosition lFrameNum64) {
	long lFrameNum = (long)lFrameNum64;

	if (lFrameNum<mSampleFirst || lFrameNum >= mSampleLast)
		return ' ';

	lFrameNum = translate_frame(renumber_frame(lFrameNum));

	switch(parentPtr->video_sample_list[lFrameNum].frame_type) {
	case MPEG_FRAME_TYPE_I:	return 'I';
	case MPEG_FRAME_TYPE_P: return 'P';
	case MPEG_FRAME_TYPE_B:
		return parentPtr->video_sample_list[lFrameNum].broken_link ? 'r' : 'B';
	default:
		return ' ';
	}
}

VideoSource::eDropType VideoSourceMPEG::getDropType(VDPosition lFrameNum64) {
	long lFrameNum = (long)lFrameNum64;

	if (lFrameNum<mSampleFirst || lFrameNum >= mSampleLast)
		return kDroppable;

	switch(parentPtr->video_sample_list[translate_frame(renumber_frame(lFrameNum))].frame_type) {
	case MPEG_FRAME_TYPE_I:	return kIndependent;
	case MPEG_FRAME_TYPE_P: return kDependant;
	case MPEG_FRAME_TYPE_B: return kDroppable;
	default:
		return kDroppable;
	}
}

bool VideoSourceMPEG::isDecodable(VDPosition sample_num64) {
	long sample_num = (long)sample_num64;
	if (sample_num<mSampleFirst || sample_num >= mSampleLast)
		return false;

	long dep;

	switch(parentPtr->video_sample_list[sample_num].frame_type) {
	case MPEG_FRAME_TYPE_B:
		dep = prev_IP(sample_num);
		if (dep>=0) {
			if (mpDecoder->GetFrameBuffer(dep)<0)
				return false;
			sample_num = dep;
		}
	case MPEG_FRAME_TYPE_P:
		dep = prev_IP(sample_num);
		if (dep>=0 && mpDecoder->GetFrameBuffer(dep)<0)
			return false;
	default:
		break;
	}

	return true;
}

bool VideoSourceMPEG::_isKey(VDPosition lSample) {
	return lSample<0 || lSample>=mSampleLast ? false : parentPtr->video_sample_list[translate_frame(renumber_frame((long)lSample))].frame_type == MPEG_FRAME_TYPE_I;
}

VDPosition VideoSourceMPEG::nearestKey(VDPosition lSample) {
	if (_isKey(lSample))
		return lSample;

	return prevKey(lSample);
}

VDPosition VideoSourceMPEG::prevKey(VDPosition lSample64) {
	if (lSample64 < mSampleFirst) return -1;
	if (lSample64 >= mSampleLast) lSample64 = mSampleLast-1;

	long lSample = (long)lSample64;
	lSample = translate_frame(renumber_frame(lSample));

	bool skipkey = false;

	if (parentPtr->video_sample_list[lSample].frame_type == MPEG_FRAME_TYPE_B)
		skipkey = true;

	while(--lSample >= mSampleFirst) {
		if (parentPtr->video_sample_list[lSample].frame_type != MPEG_FRAME_TYPE_B) {
			if (skipkey) {
				skipkey = false;
			} else if (parentPtr->video_sample_list[lSample].frame_type == MPEG_FRAME_TYPE_I)
				return lSample + parentPtr->video_sample_list[lSample].subframe_num;
		}
	}

	return -1;
}

VDPosition VideoSourceMPEG::nextKey(VDPosition lSample64) {
	if (lSample64 >= mSampleLast) return -1;
	if (lSample64 < mSampleFirst) lSample64 = mSampleFirst;

	long lSample = (long)lSample64;
	lSample = translate_frame(renumber_frame(lSample));

	// For a B-frame, the next display I/P is actually before the B-frame in
	// the stream order.  Check the preceding I/P and

	if (parentPtr->video_sample_list[lSample].frame_type == MPEG_FRAME_TYPE_B) {
		LONG pos = prev_IP(lSample);

		if (pos >= 0 && parentPtr->video_sample_list[pos].frame_type == MPEG_FRAME_TYPE_I)
			return pos + parentPtr->video_sample_list[pos].subframe_num;
	}

	while(++lSample < mSampleLast) {
		if (parentPtr->video_sample_list[lSample].frame_type == MPEG_FRAME_TYPE_I)
			return lSample + parentPtr->video_sample_list[lSample].subframe_num;
	}

	return -1;
}

void VideoSourceMPEG::streamBegin(bool fRealTime, bool bForceReset) {
	UpdateAcceleration();
}

void VideoSourceMPEG::streamEnd() {
	// We are transitioning from predicted frames to current frames, so we
	// must invalidate everything.
	streamRestart();
}

void VideoSourceMPEG::streamRestart() {
	frame_forw = frame_back = frame_bidir =-1;
}

void VideoSourceMPEG::streamSetDesiredFrame(VDPosition frame_num64) {
	long frame_num = (long)frame_num64;
	stream_desired_frame	= translate_frame(renumber_frame(frame_num));

	frame_type = parentPtr->video_sample_list[stream_desired_frame].frame_type;

	stream_current_frame	= stream_desired_frame;

//	_RPT2(0,"Requested frame: %ld (%ld)\n", frame_num, stream_desired_frame);

	switch(frame_type) {
	case MPEG_FRAME_TYPE_P:
		while(frame_back != stream_current_frame && parentPtr->video_sample_list[stream_current_frame].frame_type != MPEG_FRAME_TYPE_I && stream_current_frame>0)
//			--stream_current_frame;
			stream_current_frame = prev_IP((long)stream_current_frame);

		// Avoid decoding a I/P frame twice.

		if (stream_current_frame < 0)
			stream_current_frame = stream_desired_frame;
		else if (frame_forw == stream_current_frame && stream_current_frame != stream_desired_frame)
			++stream_current_frame;

		break;
	case MPEG_FRAME_TYPE_B:
		{
			long f,b;
			long last_IP;

			while(stream_current_frame>0 && parentPtr->video_sample_list[stream_current_frame].frame_type == MPEG_FRAME_TYPE_B)
				--stream_current_frame;

			b = (long)stream_current_frame;	// backward predictive frame

			if (stream_current_frame>0) --stream_current_frame;

			while(stream_current_frame>0 && parentPtr->video_sample_list[stream_current_frame].frame_type == MPEG_FRAME_TYPE_B)
				--stream_current_frame;

			f = (long)stream_current_frame;	// forward predictive frame

//			VDDEBUG("MPEG-1: B-frame requested: desire (%d,%d), have (%d,%d)\n", f, b, frame_forw, frame_back);

			if (frame_forw == f && frame_back == b) {
				stream_current_frame = stream_desired_frame;
				return;	// we got lucky!!!
			}

			// Maybe we only need to read the next I/P frame...

			if (frame_back == f) {
				stream_current_frame = b;
				return;
			}

			// DAMN.  Back to last I/P; if it's an I, back up to previous I

			if (f==0) return; // No forward predictive frame, use first I

			stream_current_frame = prev_IP(prev_IP((long)stream_current_frame));
			while(stream_current_frame>0 && parentPtr->video_sample_list[stream_current_frame].frame_type != MPEG_FRAME_TYPE_I) {
				last_IP = (long)stream_current_frame;

				stream_current_frame = prev_IP((long)stream_current_frame);

				if (stream_current_frame == frame_back && last_IP == frame_forw) {
					stream_current_frame = frame_forw+1;
					break;
				}
			}

			if (stream_current_frame < 0)
				stream_current_frame = 0;

//			_RPT1(0,"Beginning B-frame scan: %ld\n", stream_current_frame);

		}
		break;
	}
}

VDPosition VideoSourceMPEG::streamGetNextRequiredFrame(bool& is_preroll) {
	if (	frame_forw == stream_desired_frame
		||	frame_back == stream_desired_frame
		||	frame_bidir == stream_desired_frame) {

		is_preroll = false;

		if (frame_forw == stream_desired_frame)
			std::swap(frame_forw, frame_back);

		return -1;
	}

//	_RPT1(0,"current: %ld\n", stream_current_frame);

	switch(frame_type) {

		case MPEG_FRAME_TYPE_P:
		case MPEG_FRAME_TYPE_B:

			while(stream_current_frame != stream_desired_frame
					&& parentPtr->video_sample_list[stream_current_frame].frame_type == MPEG_FRAME_TYPE_B)

					++stream_current_frame;

			break;
	}

	// Reorder backward/forward frames so that they are in the correct order -- the
	// closest frame less than the current frame should be the backward prediction
	// source (fwd < rev < current).
	if (frame_forw - (uint32)stream_desired_frame > frame_back - (uint32)stream_desired_frame)
		std::swap(frame_forw, frame_back);

	// stream_current_frame beyond the end at this point.


	switch(parentPtr->video_sample_list[stream_current_frame].frame_type) {
		case MPEG_FRAME_TYPE_I:
		case MPEG_FRAME_TYPE_P:
			frame_forw = frame_back;
			frame_back = (long)stream_current_frame;
			break;
		case MPEG_FRAME_TYPE_B:
			frame_bidir = (long)stream_current_frame;
			break;
	}

	is_preroll = (stream_desired_frame != stream_current_frame);

	return stream_current_frame++;
}

int VideoSourceMPEG::streamGetRequiredCount(long *pSize) {
	int current = (int)stream_current_frame;
	int needed = 1;
	long size = 0;

	if (frame_forw == stream_desired_frame
		|| frame_back == stream_desired_frame
		|| frame_bidir == stream_desired_frame) {
		if (pSize)
			*pSize = 0;
		return 0;
	}


	if (frame_type == MPEG_FRAME_TYPE_I) {
		if (pSize)
			*pSize = parentPtr->video_sample_list[current].size;
		return 1;
	}

	while(current < stream_desired_frame) {

		while(current != stream_desired_frame
				&& parentPtr->video_sample_list[current].frame_type == MPEG_FRAME_TYPE_B)

				++current;

		size += parentPtr->video_sample_list[current].size;

		++needed;
		++current;
	}

	if (pSize)
		*pSize = size;

	return needed;
}

const void *VideoSourceMPEG::streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num64, VDPosition target_num64) {
	long target_num = (long)target_num64;
	long frame_num = (long)frame_num64;
	int buffer;

	if (frame_num < 0)
		frame_num = target_num;

//	VDDEBUG("Attempting to fetch frame %d [%c] (target=%d).\n", frame_num, "0IPBD567"[parentPtr->video_sample_list[frame_num].frame_type], (int)target_num);

	if (is_preroll || (buffer = mpDecoder->GetFrameBuffer(target_num))<0) {
		if (!frame_num) {
//			mpDecoder->Reset();		hmm...
		}

		// the "read" function gave us the extra 3 bytes we need
		if (data_len<=3)
			return mpFrameBuffer.get();	// HACK

		const int type = parentPtr->video_sample_list[frame_num].frame_type;

		// Reorder backward/forward frames so that they are in the correct order -- the
		// closest frame less than the current frame should be the backward prediction
		// source (fwd < rev < current).
		const uint32 fwdframe = mpDecoder->GetFrameNumber(MPEG_BUFFER_FORWARD);
		const uint32 revframe = mpDecoder->GetFrameNumber(MPEG_BUFFER_BACKWARD);

		if (fwdframe - (uint32)frame_num > revframe - (uint32)frame_num)
			mpDecoder->SwapFrameBuffers(MPEG_BUFFER_FORWARD, MPEG_BUFFER_BACKWARD);

		int dstbuffer, fwdbuffer, revbuffer;

		switch(type) {
		case MPEG_FRAME_TYPE_I:
			mpDecoder->SwapFrameBuffers(MPEG_BUFFER_FORWARD, MPEG_BUFFER_BACKWARD);
			dstbuffer = MPEG_BUFFER_BACKWARD;
			fwdbuffer = -1;
			revbuffer = -1;
			break;
		case MPEG_FRAME_TYPE_P:
			mpDecoder->SwapFrameBuffers(MPEG_BUFFER_FORWARD, MPEG_BUFFER_BACKWARD);
			dstbuffer = MPEG_BUFFER_BACKWARD;
			fwdbuffer = MPEG_BUFFER_FORWARD;
			revbuffer = -1;
			break;
		case MPEG_FRAME_TYPE_B:
			dstbuffer = MPEG_BUFFER_BIDIRECTIONAL;
			fwdbuffer = MPEG_BUFFER_FORWARD;
			revbuffer = MPEG_BUFFER_BACKWARD;
			break;
		default:
			VDASSERT(false);
		}

		if (parentPtr->video_sample_list[frame_num].broken_link) {
//			VDDEBUG("MPEG-1: Decoding %c-frame %u (broken link)\n", "0IPBD567"[parentPtr->video_sample_list[frame_num].frame_type], frame_num);
			mpDecoder->CopyFrameBuffer(dstbuffer, revbuffer, frame_num);
		} else {
//			VDDEBUG("MPEG-1: Decoding %c-frame %-4u (%u > %u < %u)\n", "0IPBD567"[parentPtr->video_sample_list[frame_num].frame_type], frame_num, fwdbuffer, dstbuffer, revbuffer);
			mpDecoder->DecodeFrame((char *)inputBuffer+4, data_len-4, frame_num, dstbuffer, fwdbuffer, revbuffer);
		}
	} else {
		if (parentPtr->video_sample_list[frame_num].frame_type == MPEG_FRAME_TYPE_B)
			mpDecoder->SwapFrameBuffers(buffer, MPEG_BUFFER_BIDIRECTIONAL);
		else
			mpDecoder->SwapFrameBuffers(buffer, MPEG_BUFFER_BACKWARD);
	}

	if (!is_preroll) {
		DecodeFrameBuffer(mpDecoder->GetFrameBuffer(target_num));
		mbFBValid = true;
	}

#ifndef _M_AMD64
	if (MMX_enabled)
		__asm emms
#endif

	return mpFrameBuffer.get();
}

const void *VideoSourceMPEG::getFrame(VDPosition frameNum64) {
	long frameNum = (long)frameNum64;
	LONG lCurrent, lKey;
	MPEGSampleInfo *msi;
	int buffer;

	UpdateAcceleration();

	// invalidate stream frame counters
	frame_forw = frame_back = frame_bidir = -1;

	frameNum = translate_frame(renumber_frame(frameNum));

	// Do we have the buffer stored somewhere?

	if ((buffer = mpDecoder->GetFrameBuffer(frameNum))>=0) {
		DecodeFrameBuffer(buffer);
		mbFBValid = true;

		return mpFrameBuffer.get();
	}

	// I-frames have no prediction, so all we have to do there is decode the I-frame.
	// P-frames decode from the last P or I, so we have to decode all frames from the
	//		last I-frame.
	// B-frames decode from the last two P or I.  If the last I/P-frame is a P-frame,
	//		we can just decode from the last I.  If the last frame is an I-frame, we
	//		need to back up *two* I-frames.

	// possible cases:
	//
	//	I-frame		1)	Decompress the I-frame.
	//	P-frame		1)	No buffer contains the required pre-frame - read up from I-frame.
	//				2)	Earlier I/P-frame in a buffer - predict from that.
	//	B-frame		1)	No buffers with required prediction frames - read up from I-frame.
	//				2)	Forward prediction frame only - read the backward prediction I/P.
	//				3)	Backward prediction frame only -

	lCurrent = frameNum;

	if (!is_I(frameNum) && -1 == (lCurrent = prev_I(frameNum)))
		throw MyError("Unable to decode: cannot find I-frame");

	switch(parentPtr->video_sample_list[frameNum].frame_type) {

	// B-frame:
	//
	// We need the last two I/P frames.

	case MPEG_FRAME_TYPE_B:
		{
			int forw_buffer = -1, back_buffer = -1;
			long forw_frame, back_frame;

			// Look for backward prediction frame and swap to backward buffer.
			back_frame = prev_IP(frameNum);
			if (back_frame >= 0) {
				back_buffer = mpDecoder->GetFrameBuffer(back_frame);
				if (back_buffer >= 0)
					mpDecoder->SwapFrameBuffers(back_buffer, MPEG_BUFFER_BACKWARD);
			}

			// Look for forward prediction frame and swap to forward buffer.
			forw_frame = prev_IP(back_frame);
			if (forw_frame >= 0) {
				forw_buffer = mpDecoder->GetFrameBuffer(forw_frame);
				if (forw_buffer >= 0)
					mpDecoder->SwapFrameBuffers(forw_buffer, MPEG_BUFFER_FORWARD);
			}

			if (forw_frame >= 0)
				forw_buffer = mpDecoder->GetFrameBuffer(forw_frame);
			if (back_frame >= 0)
				back_buffer = mpDecoder->GetFrameBuffer(back_frame);

			// If we are missing the backward frame, decode off the forward frame.
			// If we are missing the forward frame, decode from prev I/P of the
			// forward frame.

			if (forw_buffer < 0) {

				for(lCurrent = forw_frame; lCurrent >= 0 && !is_I(lCurrent); --lCurrent)
					if (parentPtr->video_sample_list[lCurrent].frame_type != MPEG_FRAME_TYPE_B) {
						if ((buffer = mpDecoder->GetFrameBuffer(lCurrent))>=0) {
							mpDecoder->SwapFrameBuffers(buffer, MPEG_BUFFER_BACKWARD);
							++lCurrent;
							break;
						}
					}

				if (lCurrent < 0)
					lCurrent = 0;
			} else if (back_buffer < 0) {
				lCurrent = forw_frame + 1;
				mpDecoder->SwapFrameBuffers(forw_buffer, MPEG_BUFFER_BACKWARD);
			} else
				lCurrent = frameNum;
		}
		break;

	// P-frame: start backing up from the current frame to the last I-frame.  If we find
	//			a I or P frame in one of our buffers beforehand, then swap it to the FORWARD
	//			buffer and start predicting off of that.

	case MPEG_FRAME_TYPE_P:
		for(lKey = frameNum-1; lKey > lCurrent; --lKey) {
			if (parentPtr->video_sample_list[lKey].frame_type != MPEG_FRAME_TYPE_B)
				if ((buffer = mpDecoder->GetFrameBuffer(lKey))>=0) {
					mpDecoder->SwapFrameBuffers(buffer, MPEG_BUFFER_BACKWARD);
					++lKey;
					break;
				}
		}

		lCurrent = lKey;
		break;
	}

	msi = &parentPtr->video_sample_list[lCurrent];
	do {
		//_RPT4(0,"getFrame: looking for %ld, at %ld (%c-frame, #%d)\n"
		//			,frameNum
		//			,lCurrent
		//			," IPBD567"[msi->frame_type]
		//			,msi->subframe_num);

		if (lCurrent == frameNum || (msi->frame_type == MPEG_FRAME_TYPE_I || msi->frame_type == MPEG_FRAME_TYPE_P)) {

			int dstbuffer, fwdbuffer, revbuffer;

			switch(parentPtr->video_sample_list[lCurrent].frame_type) {
			case MPEG_FRAME_TYPE_I:
				mpDecoder->SwapFrameBuffers(MPEG_BUFFER_FORWARD, MPEG_BUFFER_BACKWARD);
				dstbuffer = MPEG_BUFFER_BACKWARD;
				fwdbuffer = -1;
				revbuffer = -1;
				break;
			case MPEG_FRAME_TYPE_P:
				mpDecoder->SwapFrameBuffers(MPEG_BUFFER_FORWARD, MPEG_BUFFER_BACKWARD);
				dstbuffer = MPEG_BUFFER_BACKWARD;
				fwdbuffer = MPEG_BUFFER_FORWARD;
				revbuffer = -1;
				break;
			case MPEG_FRAME_TYPE_B:
				dstbuffer = MPEG_BUFFER_BIDIRECTIONAL;
				fwdbuffer = MPEG_BUFFER_FORWARD;
				revbuffer = MPEG_BUFFER_BACKWARD;
				break;
			default:
				VDASSERT(false);
			}

			if (parentPtr->video_sample_list[lCurrent].broken_link) {
				mpDecoder->CopyFrameBuffer(dstbuffer, revbuffer, lCurrent);
			} else {
				parentPtr->ReadStream(parentPtr->video_packet_buffer, msi->stream_pos, msi->size, FALSE);

				parentPtr->video_packet_buffer[msi->size] = 0;
				parentPtr->video_packet_buffer[msi->size+1] = 0;
				parentPtr->video_packet_buffer[msi->size+2] = 1;
				parentPtr->video_packet_buffer[msi->size+3] = 0;

				mpDecoder->DecodeFrame(parentPtr->video_packet_buffer+4, msi->size, lCurrent, dstbuffer, fwdbuffer, revbuffer);
			}
		}

		++msi;
	} while(lCurrent++ < frameNum);

	--msi;

	DecodeFrameBuffer(msi->frame_type>2 ? MPEG_BUFFER_BIDIRECTIONAL : MPEG_BUFFER_BACKWARD);

#ifndef _M_AMD64
	if (MMX_enabled)
		__asm emms
#endif

	mbFBValid = true;

	return getFrameBuffer();
}

void VideoSourceMPEG::DecodeFrameBuffer(int buffer) {
	char *pBuffer = (char *)mpFrameBuffer.get();
	const long w = getImageFormat()->biWidth;
	const long h = getImageFormat()->biHeight;

	VDASSERT(buffer >= 0);

	using namespace nsVDPixmap;

	switch(mTargetFormat.format) {
	case kPixFormat_YUV420_Planar:
		{
			ptrdiff_t ypitch, cbpitch, crpitch;
			const void *py = mpDecoder->GetYBuffer(buffer, ypitch);
			const void *pcr = mpDecoder->GetCrBuffer(buffer, crpitch);
			const void *pcb = mpDecoder->GetCbBuffer(buffer, cbpitch);
			VDMemcpyRect(pBuffer, w, py, ypitch, w, h);
			pBuffer += w*h;
			VDMemcpyRect(pBuffer, w>>1, pcr, crpitch, w >> 1, h >> 1);
			pBuffer += w*h >> 2;
			VDMemcpyRect(pBuffer, w>>1, pcb, cbpitch, w >> 1, h >> 1);
		}
		break;

	case kPixFormat_Y8:
		{
			ptrdiff_t ypitch;
			const void *py = mpDecoder->GetYBuffer(buffer, ypitch);
			VDMemcpyRect(pBuffer, w, py, ypitch, w, h);
		}
		break;

	case kPixFormat_YUV422_UYVY:
		mpDecoder->DecodeUYVY(pBuffer, w*2, buffer);
		break;
	case kPixFormat_YUV422_YUYV:
		mpDecoder->DecodeYUYV(pBuffer, w*2, buffer);
		break;
	case kPixFormat_XRGB1555:
		mpDecoder->DecodeRGB15(pBuffer + w*2*(h-1), -w*2, buffer);
		break;
	case kPixFormat_RGB565:
		mpDecoder->DecodeRGB16(pBuffer + w*2*(h-1), -w*2, buffer);
		break;
	case kPixFormat_RGB888:
		mpDecoder->DecodeRGB24(pBuffer + w*3*(h-1), -w*3, buffer);
		break;
	case kPixFormat_XRGB8888:
		mpDecoder->DecodeRGB32(pBuffer + w*4*(h-1), -w*4, buffer);
		break;
	}
}

int VideoSourceMPEG::_read(VDPosition lStart64, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
	long lStart = (long)lStart64;
	MPEGSampleInfo *msi = &parentPtr->video_sample_list[lStart];
	long len = msi->size;

	// Check to see if this is a frame type we're omitting

	switch(msi->frame_type) {
		case MPEG_FRAME_TYPE_P:
			if (parentPtr->iDecodeMode & InputFileMPEGOptions::DECODE_NO_P) {
				*lBytesRead = 1;
				*lSamplesRead = 1;
				return IVDStreamSource::kOK;
			}
			break;
		case MPEG_FRAME_TYPE_B:
			if (parentPtr->iDecodeMode & InputFileMPEGOptions::DECODE_NO_B) {
				*lBytesRead = 1;
				*lSamplesRead = 1;
				return IVDStreamSource::kOK;
			}
			break;
	}

	// We must add 4 bytes to hold the end marker when we decode

	if (!lpBuffer) {
		if (lSamplesRead) *lSamplesRead = 1;
		if (lBytesRead) *lBytesRead = len+4;
		return IVDStreamSource::kOK;
	}

	if (len+4 > cbBuffer) {
		if (lSamplesRead) *lSamplesRead = 0;
		if (lBytesRead) *lBytesRead = 0;
		return IVDStreamSource::kBufferTooSmall;
	}

	parentPtr->ReadStream(lpBuffer, msi->stream_pos, len, FALSE);

	// add marker at the end of the block so the decoder knows when to
	// stop without having to constantly check the length
	((char *)lpBuffer)[len] = 0;
	((char *)lpBuffer)[len+1] = 0;
	((char *)lpBuffer)[len+2] = 1;
	((char *)lpBuffer)[len+3] = (char)0xff;

	if (lSamplesRead) *lSamplesRead = 1;
	if (lBytesRead) *lBytesRead = len+4;

	return IVDStreamSource::kOK;
}

sint64 VideoSourceMPEG::getSampleBytePosition(VDPosition lStart64) {
	if (lStart64 < mSampleFirst || lStart64 >= mSampleLast)
		return -1;

	long lStart = (long)lStart64;
	MPEGSampleInfo *msi = &parentPtr->video_sample_list[lStart];

	return parentPtr->GetStartingBytePosition(msi->stream_pos, false);
}

///////

long VideoSourceMPEG::renumber_frame(LONG lSample) {
	LONG lKey=lSample, lCurrent;

	if (lSample < mSampleFirst || lSample >= mSampleLast || (!is_I(lSample) && (lKey = prev_I(lSample))==-1))
		throw MyError("Frame not found (looking for %ld)", lSample);

	lCurrent = lKey;
	do {
		if (lSample-lKey == parentPtr->video_sample_list[lCurrent].subframe_num)
			return lCurrent;
	} while(++lCurrent < mSampleLast && !is_I(lCurrent));

	throw MyError("Frame not found (looking for %ld)", lSample);

	return -1;	// throws can't return!
}

bool VideoSourceMPEG::is_I(long lSample) {
	return parentPtr->video_sample_list[lSample].frame_type == MPEG_FRAME_TYPE_I;
}

long VideoSourceMPEG::prev_I(long lSample) {
	if (lSample < mSampleFirst) return -1;
	if (lSample >= mSampleLast) lSample = (long)(mSampleLast-1);

	while(--lSample >= mSampleFirst) {
		if (parentPtr->video_sample_list[lSample].frame_type == MPEG_FRAME_TYPE_I)
			return lSample;
	}

	return -1;
}

long VideoSourceMPEG::prev_IP(long f) {
	if (f<0) return f;

	do
		--f;
	while (f>=0 && parentPtr->video_sample_list[f].frame_type == MPEG_FRAME_TYPE_B);

	return f;
}

long VideoSourceMPEG::translate_frame(LONG lSample) {
	MPEGSampleInfo *msi = &parentPtr->video_sample_list[lSample];

	// Check to see if this is a frame type we're omitting; if so,
	// keep backing up until it's one we're not.

	while(lSample > 0) {
		switch(msi->frame_type) {
			case MPEG_FRAME_TYPE_P:
				if (!(parentPtr->iDecodeMode & InputFileMPEGOptions::DECODE_NO_P))
					return lSample;

				break;
			case MPEG_FRAME_TYPE_B:
				if (!(parentPtr->iDecodeMode & InputFileMPEGOptions::DECODE_NO_B))
					return lSample;

				break;
			default:
				return lSample;
		}

		--lSample;
		--msi;
	}

	return lSample;
}

void VideoSourceMPEG::UpdateAcceleration() {
	uint32 flags = CPUGetEnabledExtensions();

	if (mAccelerationFlags != flags) {
		mAccelerationFlags = flags;

		static const uint32 sse2_flags = (CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_MMX);
		static const uint32 isse_flags = (CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_MMX);
		static const uint32 mmx_flags = CPUF_SUPPORTS_MMX;

#ifdef _M_AMD64
		mpDecoder->SetPredictors(&g_VDMPEGPredict_sse2);
		mpDecoder->SetConverters(&g_VDMPEGConvert_reference);
		mpDecoder->SetIDCTs(&g_VDMPEGIDCT_sse2);
#else
		if ((flags & sse2_flags) == sse2_flags) {
			mpDecoder->SetPredictors(&g_VDMPEGPredict_sse2);
			mpDecoder->SetConverters(&g_VDMPEGConvert_isse);
			mpDecoder->SetIDCTs(&g_VDMPEGIDCT_isse);
		} else if ((flags & isse_flags) == isse_flags) {
			mpDecoder->SetPredictors(&g_VDMPEGPredict_isse);
			mpDecoder->SetConverters(&g_VDMPEGConvert_isse);
			mpDecoder->SetIDCTs(&g_VDMPEGIDCT_isse);
		} else if ((flags & mmx_flags) == mmx_flags) {
			mpDecoder->SetPredictors(&g_VDMPEGPredict_mmx);
			mpDecoder->SetConverters(&g_VDMPEGConvert_mmx);
			mpDecoder->SetIDCTs(&g_VDMPEGIDCT_mmx);
		} else {
			mpDecoder->SetPredictors(&g_VDMPEGPredict_scalar);
			mpDecoder->SetConverters(&g_VDMPEGConvert_scalar);
			mpDecoder->SetIDCTs(&g_VDMPEGIDCT_scalar);
		}
#endif
	}
}

//////////////////////////////////////////////////////////////////////////
//
//
//						AudioSourceMPEG
//
//
//////////////////////////////////////////////////////////////////////////

	// 0000F0FF 12 bits	sync mark
	//
	// 00000800  1 bit	version
	// 00000600  2 bits	layer (3 = layer I, 2 = layer II, 1 = layer III)
	// 00000100  1 bit	error protection (0 = enabled)
	//
	// 00F00000  4 bits	bitrate_index
	// 000C0000  2 bits	sampling_freq
	// 00020000  1 bit	padding
	// 00010000  1 bit	extension
	//
	// C0000000  2 bits	mode (0=stereo, 1=joint stereo, 2=dual channel, 3=mono)
	// 30000000  2 bits	mode_ext
	// 08000000  1 bit	copyright
	// 04000000  1 bit	original
	// 03000000  2 bits	emphasis

#define MPEGAHDR_SYNC_MASK			(0x0000F0FF)
#define MPEGAHDR_VERSION_MASK		(0x00000800)
#define	MPEGAHDR_LAYER_MASK			(0x00000600)
#define MPEGAHDR_CRC_MASK			(0x00000100)
#define MPEGAHDR_BITRATE_MASK		(0x00F00000)
#define MPEGAHDR_SAMPLERATE_MASK	(0x000C0000)
#define MPEGAHDR_PADDING_MASK		(0x00020000)
#define MPEGAHDR_EXT_MASK			(0x00010000)
#define MPEGAHDR_MODE_MASK			(0xC0000000)
#define MPEGAHDR_MODEEXT_MASK		(0x30000000)
#define MPEGAHDR_COPYRIGHT_MASK		(0x08000000)
#define MPEGAHDR_ORIGINAL_MASK		(0x04000000)
#define MPEGAHDR_EMPHASIS_MASK		(0x03000000)

static const int bitrate[2][3][16] = {
		{
			{ 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 },	// MPEG-1 layer I
			{ 0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0 },	// MPEG-1 layer II
			{ 0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0 },	// MPEG-1 layer III
		},
		{
			{ 0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, 0 },	// MPEG-2 layer I
			{ 0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 },	// MPEG-2 layer II
			{ 0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 },	// MPEG-2 layer III
		}
};

static const long samp_freq[2][2][4] = {
	{
		{ 44100, 48000, 32000, 0 },		// MPEG-1
		{ 22050, 24000, 16000, 0 },		// MPEG-2
	},
	{
		{     0,     0,     0, 0 },		// impossible
		{ 11025, 12000,  8000, 0 },		// MPEG-2.5
	}
};

struct MPEGAudioHeader {
	enum {
		kMaskNone		= 0x00000000,
		kMaskSync		= 0x0000E0FF,
		kMaskMPEG25		= 0x00001000,
		kMaskVersion	= 0x00000800,
		kMaskLayer		= 0x00000600,
		kMaskCRC		= 0x00000100,
		kMaskBitrate	= 0x00F00000,
		kMaskSampleRate	= 0x000C0000,
		kMaskPadding	= 0x00020000,
		kMaskPrivate	= 0x00010000,
		kMaskMode		= 0xC0000000,
		kMaskModeExt	= 0x30000000,
		kMaskCopyright	= 0x08000000,
		kMaskOriginal	= 0x04000000,
		kMaskEmphasis	= 0x03000000,
		kMaskAll		= 0xFFFFFFFF
	};

	const uint32 mHeader;

	MPEGAudioHeader(uint32 hdr) : mHeader(hdr) {}

	bool		IsSyncValid() const				{ return (mHeader & kMaskSync) == kMaskSync; }
	bool		IsMPEG2() const					{ return !(mHeader & kMaskVersion); }
	bool		IsMPEG25() const				{ return IsMPEG2() && !(mHeader & kMaskMPEG25); }
	unsigned	GetLayer() const				{ return 4 - ((mHeader >> 9)&3); }
	bool		IsCRCProtected() const			{ return !(mHeader & kMaskCRC); }
	unsigned	GetBitrateIndex() const			{ return (mHeader >> 20) & 15; }
	unsigned	GetSamplingRateIndex() const	{ return (mHeader >> 18) & 3; }
	bool		IsPadded() const				{ return 0!=(mHeader & kMaskPadding); }
	unsigned	GetModeIndex() const			{ return (mHeader >> 28) & 3; }
	bool		IsStereo() const				{ return (mHeader & kMaskMode) != kMaskMode; }

	unsigned	GetBitrateKbps() const			{ return bitrate[IsMPEG2()][GetLayer()-1][GetBitrateIndex()]; }
	unsigned	GetSamplingRateHz() const		{ return samp_freq[IsMPEG25()][IsMPEG2()][GetSamplingRateIndex()]; }

	bool IsValid() const;
	bool IsConsistent(uint32 hdr) const;
	unsigned GetFrameSize() const;
	unsigned GetPayloadSizeL3() const;
};

bool MPEGAudioHeader::IsValid() const {
	return IsSyncValid()					// need at least 11 bits for sync mark
		&& (!IsMPEG25() || IsMPEG2())		// either twelfth bit is set or it's MPEG-2.5
		&& GetLayer() != 4					// layer IV invalid
		&& GetBitrateIndex() != 15			// bitrate=1111 invalid
		&& GetSamplingRateIndex() != 3		// sampling_rate=11 reserved
		;
}

bool MPEGAudioHeader::IsConsistent(uint32 hdr) const {
	uint32 headerdiff = (hdr ^ mHeader);

	// do not allow MPEG version, layer, or sampling rate to change
	if (headerdiff & (kMaskSampleRate | kMaskVersion | kMaskLayer | kMaskMPEG25))
		return false;

	// only layer III decoders must accept VBR
	if (GetLayer() != 3 && (headerdiff & kMaskBitrate))
		return false;

	return true;
}

unsigned MPEGAudioHeader::GetFrameSize() const {
	const bool		is_mpeg2	= IsMPEG2();
	const unsigned	bitrate		= GetBitrateKbps();
	const unsigned	freq		= GetSamplingRateHz();
	const unsigned	padding		= IsPadded();

	if (GetLayer() == 1)
		return 4*(12000*bitrate/freq + padding);
	else {
		if (is_mpeg2 && GetLayer() == 3)
			return (72000*bitrate/freq + padding);
		else
			return (144000*bitrate/freq + padding);
	}
}

unsigned MPEGAudioHeader::GetPayloadSizeL3() const {
	VDASSERT(GetLayer() == 3);

	static const unsigned sideinfo_size[2][2]={17,32,9,17};

	unsigned size = GetFrameSize() - sideinfo_size[IsMPEG2()][IsStereo()];

	if (IsCRCProtected())
		size -= 2;

	VDASSERT((signed)size > 0);

	return size;
}

class AudioSourceMPEG : public AudioSource, IVDMPEGAudioBitsource {
private:
	vdrefptr<InputFileMPEG> parentPtr;
	IVDMPEGAudioDecoder *mpDecoder;
	char* pkt_buffer = nullptr;
	short sample_buffer[1152*2][2];
	char *pDecoderPoint;
	char *pDecoderLimit;

	long lCurrentPacket;
	int layer;
	int		mSamplesPerFrame;

	bool	mbIsMPEG2;
	ErrorMode	mErrorMode;

	BOOL _isKey(LONG lSample);

public:
	AudioSourceMPEG(InputFileMPEG *);
	~AudioSourceMPEG();

	// AudioSource methods

	void init();
	int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead);

	ErrorMode getDecodeErrorMode() { return mErrorMode; }
	void setDecodeErrorMode(ErrorMode mode);
	bool isDecodeErrorModeSupported(ErrorMode mode);

	// IAMPBitsource methods

	int read(void *buffer, int bytes);

};

AudioSourceMPEG::AudioSourceMPEG(InputFileMPEG *pp)
	: AudioSource()
	, mErrorMode(kErrorModeReportAll)
{
	parentPtr = pp;

	pkt_buffer = new(std::nothrow) char[8192];
	if (!pkt_buffer) {
		throw MyMemoryError();
	}

	mpDecoder = VDCreateMPEGAudioDecoder();
	if (!mpDecoder) {
		delete[] pkt_buffer;
		throw MyMemoryError();
	}

	lCurrentPacket = -1;
}

AudioSourceMPEG::~AudioSourceMPEG() {
	delete[] pkt_buffer;
	delete mpDecoder;
}

BOOL AudioSourceMPEG::_isKey(LONG lSample) {
	return TRUE;
}

void AudioSourceMPEG::init() {
	WAVEFORMATEX *wfex;

	MPEGAudioHeader	header(parentPtr->audio_first_header);

	layer = header.GetLayer();

	mSamplesPerFrame = (layer==1 ? 384 : layer == 3 && header.IsMPEG2() ? 576 : 1152);

	mSampleFirst = 0;
	mSampleLast = parentPtr->aframes * mSamplesPerFrame;

	if (!(wfex = (WAVEFORMATEX*)allocFormat(sizeof(PCMWAVEFORMAT))))
		throw MyMemoryError();

	// [?] * [bits/sec] / [samples/sec] = [bytes/frame]
	// [?] * [bits/sample] = [bytes/frame]
	// [?] * [.125 byte/bit] * [bits/sample] = [bytes/frame]
	// [samples/frame]


	wfex->wFormatTag		= WAVE_FORMAT_PCM;
	wfex->nChannels			= (WORD)(header.IsStereo() ? 2 : 1);
	wfex->nSamplesPerSec	= header.GetSamplingRateHz();
	wfex->nBlockAlign		= (WORD)(wfex->nChannels*2);
	wfex->nAvgBytesPerSec	= wfex->nSamplesPerSec * wfex->nBlockAlign;
	wfex->wBitsPerSample	= 16;

	streamInfo.fccType					= VDAVIStreamInfo::kTypeAudio;
	streamInfo.fccHandler				= 0;
	streamInfo.dwCaps					= 0;
	streamInfo.wPriority				= 0;
	streamInfo.wLanguage				= 0;
	streamInfo.dwFlags					= 0;
	streamInfo.dwScale					= wfex->nBlockAlign;
	streamInfo.dwRate					= wfex->nSamplesPerSec * wfex->nBlockAlign;
	streamInfo.dwStart					= 0;
	streamInfo.dwLength					= parentPtr->aframes * mSamplesPerFrame;
	streamInfo.dwInitialFrames			= 0;
	streamInfo.dwSuggestedBufferSize	= 0;
	streamInfo.dwQuality				= 0xffffffffL;
	streamInfo.dwSampleSize				= wfex->nBlockAlign;
	streamInfo.rcFrameLeft				= 0;
	streamInfo.rcFrameTop				= 0;
	streamInfo.rcFrameRight				= 0;
	streamInfo.rcFrameBottom			= 0;

	try {
		mpDecoder->Init();
		mpDecoder->SetSource(this);
	} catch(int i) {
		throw MyError(mpDecoder->GetErrorString(i));
	}
}

int AudioSourceMPEG::_read(VDPosition lStart64, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) {
	long lStart = (long)lStart64;
	long lAudioPacket;
	MPEGSampleInfo *msi;
	long len;
	long samples, ba = getWaveFormat()->mBlockSize;

	lAudioPacket = lStart/mSamplesPerFrame;
	samples = mSamplesPerFrame - (lStart % mSamplesPerFrame);

	if (lCount != IVDStreamSource::kConvenient)
		if (samples > lCount) samples = lCount;

	if (samples*ba > cbBuffer && lpBuffer) {
		samples = cbBuffer / ba;

		if (samples <= 0) {
			if (lSamplesRead) *lSamplesRead = 0;
			if (lBytesRead) *lBytesRead = 0;
			return IVDStreamSource::kBufferTooSmall;
		}
	}

	if (!lpBuffer) {
		if (lSamplesRead) *lSamplesRead = samples;
		if (lBytesRead) *lBytesRead = samples * ba;
		return IVDStreamSource::kOK;
	}

	// Because of the overlap from the subband synthesis window, we must
	// decode the previous packet in order to avoid glitches in the output.
	// For layer III, we must also ensure that at least 511 bytes are present
	// in the bit reservoir, as packets may dig that far back using
	// main_data_begin.  An MPEG-1 stereo stream may use up to 32 bytes of
	// the data payload for sideband information.

	if (lCurrentPacket != lAudioPacket) {
		try {
			long nDecodeStart;

			if (layer!=3 || lCurrentPacket<0 || lCurrentPacket+1 != lAudioPacket) {
//				_RPT0(0,"Resetting...\n");
				mpDecoder->Reset();

				lCurrentPacket = lAudioPacket;
				--lCurrentPacket;
				if (lCurrentPacket < 0)
					lCurrentPacket = 0;

				nDecodeStart = lCurrentPacket;

				// layer III: add packets to preload bit reservoir
				if (layer == 3) {
					long nReservoirDelay = 511;		// main_data_start is 9 bits (0..511)

					while(lCurrentPacket > 0 && nReservoirDelay > 0) {
						--lCurrentPacket;

						nReservoirDelay -= MPEGAudioHeader(parentPtr->audio_sample_list[lCurrentPacket].header).GetPayloadSizeL3();
					}
				}
			} else {
				lCurrentPacket	= lAudioPacket;
				nDecodeStart	= lAudioPacket;
			}

			do {
//				_RPT1(0,"Decoding packet: %d\n", lCurrentPacket);

				msi = &parentPtr->audio_sample_list[lCurrentPacket];
				len = msi->size;

				parentPtr->ReadStream(pkt_buffer, msi->stream_pos, len, TRUE);

				pDecoderPoint = pkt_buffer;
				pDecoderLimit = pkt_buffer + len;

				if ((unsigned char)pDecoderPoint[0] != 0xff || ((unsigned char)pDecoderPoint[1]&0xe0)!=0xe0)
					throw MyInternalError("MPEG audio header in sample list has bad sync mark.\n(%s:%d)", __FILE__, __LINE__);

				mpDecoder->SetDestination((sint16 *)sample_buffer);
				mpDecoder->ReadHeader();

				try {
					if (lCurrentPacket < nDecodeStart)
						mpDecoder->PrereadFrame();
					else
						mpDecoder->DecodeFrame();
				} catch(int i) {
					if (mErrorMode != kErrorModeReportAll) {
						const char *pszError = mpDecoder->GetErrorString(i);
						VDLogAppMessage(kVDLogWarning, kVDST_Mpeg, kVDM_AudioConcealingError, 2, &lAudioPacket, &pszError);

						if (lCurrentPacket >= nDecodeStart)
							memset(sample_buffer, 0, sizeof sample_buffer);

						mpDecoder->ConcealFrame();
					} else
						throw;
				}

			} while(++lCurrentPacket <= lAudioPacket);
			--lCurrentPacket;
		} catch(int i) {
			char buf[64];
			long badpacket = lCurrentPacket;

			uint32 ticks = (uint32)(0.5 + (badpacket * 1000.0 * mSamplesPerFrame) / getWaveFormat()->mSamplingRate);
			ticks_to_str(buf, std::size(buf), ticks);

			lCurrentPacket = -1;

			throw MyError("Error decoding MPEG audio frame %lu (%s): %s", (unsigned long)badpacket, buf, mpDecoder->GetErrorString(i));
		}
	}

	memcpy(lpBuffer, (short *)sample_buffer + (lStart%mSamplesPerFrame)*(ba/2), samples*ba);

	if (lSamplesRead) *lSamplesRead = samples;
	if (lBytesRead) *lBytesRead = samples*ba;

	return IVDStreamSource::kOK;
}

int AudioSourceMPEG::read(void *buffer, int bytes) {
	if (pDecoderPoint+bytes > pDecoderLimit)
		throw MyError("Incomplete audio frame");

	memcpy(buffer, pDecoderPoint, bytes);
	pDecoderPoint += bytes;

	return bytes;
}

void AudioSourceMPEG::setDecodeErrorMode(ErrorMode mode) {
	if (mode == kErrorModeDecodeAnyway)
		mode = kErrorModeConceal;
	mErrorMode = mode;
}

bool AudioSourceMPEG::isDecodeErrorModeSupported(ErrorMode mode) {
	return mode == kErrorModeReportAll || mode == kErrorModeConceal;
}

//////////////////////////////////////////////////////////////////////////
//
//
//					MPEGAudioParser
//
//
//////////////////////////////////////////////////////////////////////////

class MPEGAudioParser {
private:
	unsigned long lFirstHeader;
	int hstate, skip;
	unsigned long header;
	MPEGSampleInfo msi;
	__int64 bytepos;

public:
	MPEGAudioParser();

	void Parse(const void *, int, DataVector *);
	unsigned long getHeader();
};

MPEGAudioParser::MPEGAudioParser() {
	lFirstHeader = 0;
	header = 0;
	hstate = 0;
	skip = 0;
	bytepos = 0;
}

unsigned long MPEGAudioParser::getHeader() {
	return lFirstHeader;
}

void MPEGAudioParser::Parse(const void *pData, int len, DataVector *dst) {
	unsigned char *src = (unsigned char *)pData;

	while(len>0) {
		if (skip) {
			int tc = skip;

			if (tc > len)
				tc = len;

			len -= tc;
			skip -= tc;
			src += tc;

			// Audio frame finished?

			if (!skip) {
				dst->Add(&msi);
			}

			continue;
		}

		// Collect header bytes.

		++hstate;
		header = (header>>8) | ((long)*src++ << 24);
		--len;

		MPEGAudioHeader hdr(header);
		if (hstate>=4 && hdr.IsValid()) {

			if (lFirstHeader && !hdr.IsConsistent(lFirstHeader))
				continue;

			// Okay, we like the header.

			hstate = 0;

			// Must be a frame start.

			if (!lFirstHeader)
				lFirstHeader = header;

			// Setup the sample information.  Don't add the sample, in case it's incomplete.

			msi.stream_pos		= bytepos + (src - (unsigned char *)pData) - 4;
			msi.header			= header;
			msi.size			= hdr.GetFrameSize();

			// Now skip the remainder of the sample.

			skip = msi.size-4;
		}
	}

	bytepos += src - (unsigned char *)pData;
}

//////////////////////////////////////////////////////////////////////////
//
//
//					MPEGVideoParser
//
//
//////////////////////////////////////////////////////////////////////////

class MPEGVideoParser {
private:
	unsigned char buf[72+64];
	uint8 nonintramatrix[64];
	uint8 intramatrix[64];

	int idx, bytes;

	MPEGSampleInfo msi;
	__int64 bytepos;
	long header;

	bool fCustomIntra, fCustomNonintra;
	bool fPicturePending;
	bool fFoundSequenceStart;
	bool mbFirstGOP;
	bool mbBrokenLink;
	bool mbIPFoundInGroup;

public:
	VDFraction mFrameRate;
	int width, height;
	uint8	mAspectRatioCode;

	MPEGVideoParser();

	void setPos(__int64);
	void Parse(const void *, int, DataVector *);

	const uint8 *GetIntraQuantMatrix() const { return fCustomIntra ? intramatrix : NULL; }
	const uint8 *GetNonintraQuantMatrix() const { return fCustomNonintra ? nonintramatrix : NULL; }
};

MPEGVideoParser::MPEGVideoParser()
	: mbFirstGOP(true)
	, mbBrokenLink(false)
	, mbIPFoundInGroup(false)
{
	bytepos = 0;
	header = -1;

	fCustomIntra = false;
	fCustomNonintra = false;
	fPicturePending = false;
	fFoundSequenceStart = false;

	idx = bytes = 0;

	mAspectRatioCode = 0;
}

void MPEGVideoParser::setPos(__int64 pos) {
	bytepos = pos;
}

void MPEGVideoParser::Parse(const void *pData, int len, DataVector *dst) {
	unsigned char *src = (unsigned char *)pData;

	while(len>0) {
		if (idx<bytes) {
			int tc = bytes - idx;

			if (tc > len)
				tc = len;

			memcpy(buf+idx, src, tc);

			len -= tc;
			idx += tc;
			src += tc;

			// Finished?

			if (idx>=bytes) {
				switch(header) {
					case VIDPKT_TYPE_PICTURE_START:
						msi.frame_type		= (char)((buf[1]>>3)&7);
						msi.subframe_num	= (uint16)((buf[0]<<2) | (buf[1]>>6));
						fPicturePending		= true;

						if (msi.frame_type == MPEG_FRAME_TYPE_B) {
							msi.broken_link		= mbBrokenLink;
						} else {
							if (mbIPFoundInGroup)
								mbBrokenLink = false;
							mbIPFoundInGroup = true;
							msi.broken_link = false;
						}

						header = 0xFFFFFFFF;
						break;

					case VIDPKT_TYPE_SEQUENCE_START:
						//	12 bits: width
						//	12 bits: height
						//	 4 bits: aspect ratio
						//	 4 bits: picture rate
						//	18 bits: bitrate
						//	 1 bit : ?
						//	10 bits: VBV buffer
						//	 1 bit : const_param
						//	 1 bit : intramatrix present
						//[256 bits: intramatrix]
						//	 1 bit : nonintramatrix present
						//[256 bits: nonintramatrix]
						if (bytes == 8) {
							width	= (buf[0]<<4) + (buf[1]>>4);
							height	= ((buf[1]<<8)&0xf00) + buf[2];

							mAspectRatioCode = (uint8)buf[3] >> 4;

							switch((unsigned char)buf[3] & 15) {
							case 1:		mFrameRate = VDFraction(24000, 1001);	break;		// 1 (23.976) - NTSC FILM interlaced
							case 2:		mFrameRate = VDFraction(24   , 1   );	break;		// 2 (24.000) - FILM
							case 3:		mFrameRate = VDFraction(25   , 1   );	break;		// 3 (25.000) - PAL interlaced
							case 4:		mFrameRate = VDFraction(30000, 1001);	break;		// 4 (29.970) - NTSC color interlaced
							case 5:		mFrameRate = VDFraction(30   , 1   );	break;		// 5 (30.000) - NTSC b&w progressive
							case 6:		mFrameRate = VDFraction(50   , 1   );	break;		// 6 (50.000) - PAL progressive
							case 7:		mFrameRate = VDFraction(60000, 1001);	break;		// 7 (59.940) - NTSC color progressive
							case 8:		mFrameRate = VDFraction(60   , 1   );	break;		// 8 (60.000) - NTSC b&w progressive
							case 9:		mFrameRate = VDFraction(15   , 1   );	break;		// 9 (15.000) - Xing 15fps extension
							default:
								throw MyError("MPEG-1 video stream contains an invalid frame rate (%d).", buf[3] & 15);
							}

							if (buf[7]&2) {		// Intramatrix present
								bytes = 72;	// can't decide yet
								break;
							} else if (buf[7]&1) {	// Nonintramatrix present
								bytes = 72;
								break;
							}
						} else if (bytes == 72) {
							if (buf[7]&2) {
								for(int i=0; i<64; i++)
									intramatrix[i] = (uint8)(((buf[i+7]<<7)&0x80) | (buf[i+8]>>1));

								fCustomIntra = true;

								if (buf[71]&1) {
									bytes = 72+64;		// both matrices
									break;
								}
							} else {		// Nonintramatrix only
								memcpy(nonintramatrix, buf+8, 64);
								fCustomNonintra = true;
							}
						} else if (bytes == 72+64) {	// Both matrices (intra already loaded)
							memcpy(nonintramatrix, buf+72, 64);

							fCustomIntra = fCustomNonintra = true;
						}

						// Initialize MPEG-1 video decoder.
						header = 0xFFFFFFFF;
						break;

					case VIDPKT_TYPE_GROUP_START:
						// +---+-------------------+-------+
						// |DFF|       hours       | min4-5| buf[0]
						// +---+-----------+---+---+-------+
						// |  minutes 0-3  | 1 | secs 3-5  | buf[1]
						// +-----------+---+---+-----------+
						// |  secs 0-3 |   pictures 1-5    | buf[2]
						// +---+---+---+-------------------+
						// |pc0|C_G|B_L|xxxxxxxxxxxxxxxxxxx| buf[3] (closed_gop, broken_link)
						// +---+---+---+-------------------+

						// We can't rely on the timestamp in GOP headers, unfortunately, as
						// some MPEG-1 files have them incorrect in minutes whenever secs=0.
						// But we can use broken_link.

						mbBrokenLink = false;
						if (buf[3] & 0x20)
							mbBrokenLink = true;

						// If this if the first GOP but the GOP is not closed, set broken_link.
						if (!(buf[3] & 0x40) && mbFirstGOP)
							mbBrokenLink = true;

						mbFirstGOP = false;
						mbIPFoundInGroup = false;
						break;
				}
			}
			continue;
		}

		// Look for a valid MPEG-1 header

		header = (header<<8) + *src++;
		--len;

		if ((header&0xffffff00) == 0x00000100) {
			header &= 0xff;
			if (fPicturePending && (header<VIDPKT_TYPE_SLICE_START_MIN || header>VIDPKT_TYPE_SLICE_START_MAX) && header != VIDPKT_TYPE_USER_START) {
				// only add frame types we can decode: I, P, B.
				switch(msi.frame_type) {
				case MPEG_FRAME_TYPE_I:
				case MPEG_FRAME_TYPE_P:
				case MPEG_FRAME_TYPE_B:
					msi.size = (int)(bytepos + (src - (unsigned char *)pData) - 4 - msi.stream_pos);
					dst->Add(&msi);
					break;
				}
				fPicturePending = false;
			}

			switch(header) {
			case VIDPKT_TYPE_SEQUENCE_START:
				if (fFoundSequenceStart) break;
				fFoundSequenceStart = true;

				bytes = 8;
				idx = 0;
				break;

			case VIDPKT_TYPE_PICTURE_START:
				idx = 0;
				bytes = 2;
				msi.stream_pos = bytepos + (src - (unsigned char *)pData) - 4;
				break;

			case VIDPKT_TYPE_EXT_START:
				throw MyError("VirtualDub cannot decode MPEG-2 video streams.");

			case VIDPKT_TYPE_GROUP_START:
				idx = 0;
				bytes = 4;
				break;

			default:
				header = 0xFFFFFFFF;
			}
		}
	}

	bytepos += src - (unsigned char *)pData;
}

//////////////////////////////////////////////////////////////////////////
//
//
//							InputFileMPEG
//
//
//////////////////////////////////////////////////////////////////////////


extern HWND g_hWnd;

const char InputFileMPEG::szME[]="MPEG Import Filter";

#define VIDEO_PACKET_BUFFER_SIZE	(1048576)
#define AUDIO_PACKET_BUFFER_SIZE	(65536)

InputFileMPEG::InputFileMPEG()
	: pScanBuffer(NULL)
{
	// clear variables

	file_cpos = 0;
	video_packet_buffer = NULL;
	audio_packet_buffer = NULL;
	video_packet_list = NULL;
	video_sample_list = NULL;
	audio_packet_list = NULL;
	audio_sample_list = NULL;
	audio_first_header = 0;

	fInterleaved = fHasAudio = FALSE;

	iDecodeMode = 0;
	fAbort = false;
	fIsVCD = false;

	pFastRead = NULL;

	last_packet[0] = last_packet[1] = 0;
}

InputFile *CreateInputFileMPEG() {
	return new InputFileMPEG();
}

void InputFileMPEG::Init(const wchar_t *szFile) {
	VDLogAppMessage(kVDLogMarker, kVDST_Mpeg, kVDM_OpeningFile, 1, &szFile);

	BOOL finished = FALSE;
	HWND hWndStatus = 0;

	AddFilename(szFile);

    // allocate packet buffer

	if (!(video_packet_buffer = new char[VIDEO_PACKET_BUFFER_SIZE]))
		throw MyMemoryError();

	if (!(audio_packet_buffer = new char[AUDIO_PACKET_BUFFER_SIZE]))
		throw MyMemoryError();

	// see if we can open the file

	mFile.open(szFile, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting | nsVDFile::kSequential);

	try {
		mFileUnbuffered.open(szFile, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting | nsVDFile::kUnbuffered);
	} catch(const MyError&) {
		//ignore any errors
	}

	pFastRead = new FastReadStream(mFile.getRawHandle(), 24, 32768);

	// determine the file's size...

	file_len = mFile.size();

	// Begin file parsing!  This is a royal bitch!

	int warning_count = 0;

	StartScan();

	try {
		DataVector video_stream_blocks(sizeof MPEGPacketInfo);
		DataVector video_stream_samples(sizeof MPEGSampleInfo);
		__int64 video_stream_pos = 0;
		MPEGVideoParser videoParser;


		DataVector audio_stream_blocks(sizeof MPEGPacketInfo);
		DataVector audio_stream_samples(sizeof MPEGSampleInfo);
		MPEGAudioParser audioParser;
		__int64 audio_stream_pos = 0;

		bool first_packet = true;
		bool end_of_file = false;
		bool fTrimLastOff = false;

		sint64 last_dts[48] = {0};

		// seek to first pack code

		int hardskip = 0;
		int softskip = 0;

		{
			char ch[3];
			int scan_count = 256;

			mFile.read(ch, 3);

			while(scan_count > 0) {
				if (ch[0]=='R' && ch[1]=='I' && ch[2]=='F') {
					fIsVCD = true;
					fInterleaved = true;

					// The Read() code skips over the last 4 bytes of a sector
					// and the beginning 16 bytes of the next, so we need to
					// back up 4.

					hardskip = 40 + 256 - scan_count;
					i64ScanCpos = hardskip;
					break;
				} else if (ch[0]==0 && ch[1]==0 && ch[2]==1) {
					fIsVCD = false;

					i64ScanCpos = 0;
					softskip = 256 + 3 - scan_count;
					break;
				}

				ch[0] = ch[1];
				ch[1] = ch[2];
				mFile.read(ch+2, 1);

				--scan_count;

				if (!scan_count)
					throw MyError("%s: Invalid MPEG file", szME);
			}
		}

		mFile.seek(0);
		mpScanPrefetcher = new InputFileMPEGPrefetcher(mFile, mFileUnbuffered.isOpen() ? &mFileUnbuffered : NULL, 262144, 4);

		if (hardskip>0)
			mpScanPrefetcher->readData(NULL, hardskip);
		if (softskip>0)
			Skip(softskip);

		bool forceAbort = false;
		int errorCode;

		try {
			do {
				int c;
				int stream_id, pack_length;

				file_cpos = Tell();

				if (!guiDlgMessageLoop(hWndStatus, &errorCode))
					fAbort = forceAbort = true;

				if (fAbort)
					throw MyUserAbortError();

				if (first_packet) {
					c = Read();
					if (!fIsVCD) {
						fInterleaved = (c==0xBA);

						if (!fInterleaved) {
							videoParser.setPos(Tell()-4);

							unsigned char buf[4];

							buf[0] = buf[1] = 0;
							buf[2] = 1;
							buf[3] = (unsigned char)c;

							videoParser.Parse(buf, 4, &video_stream_samples);
						}
					}

					// pop up the dialog...

					if (!(hWndStatus = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PROGRESS), g_hWnd, ParseDialogProc, (LPARAM)this)))
						throw MyMemoryError();

					first_packet = false;
				} else if (fInterleaved)
					c=Read();
				else
					c = 0xe0;

				switch(c) {

//					One for audio and for video?

					case VIDPKT_TYPE_SEQUENCE_END:
					case 0xb9:		// ISO 11172 end code
						break;

					case 0xba:		// new pack
						if ((Read() & 0xf0) != 0x20)
							throw MyError("%s: invalid pack at position %I64u: marker bit not set; possibly MPEG-2 stream", szME, file_cpos);
						Skip(7);
						break;

					case 0xbb:		// system header
						Skip(8);
						while((c=Read()) & 0x80)
							Skip(2);

						UnRead();
						break;

					default:
						if (c < 0xc0 || c>=0xf0)
							break;

						if (fInterleaved) {
							__int64 tagpos = Tell();
							stream_id = c;
							pack_length = Read()<<8;
							pack_length += Read();

//							_RPT3(0,"Encountered packet: stream %02x, pack length %ld, position %08lx\n", stream_id, pack_length, file_cpos);

							if (stream_id != PRIVATE_STREAM2) {
								--pack_length;

								while((c=Read()) == 0xff) {
									--pack_length;
								}

								if ((c>>6) == 1) {	// 01
									pack_length-=2;
									Read();			// skip one byte
									c=Read();
								}

								uint8 buf[10];
								bool bPTSPresent = false;
								bool bDTSPresent = false;

								buf[0] = (uint8)c;
								if ((c>>4) == 2) {			// 0010 (PTS = DTS)
									pack_length -= 4;
									Read(buf+1, 4, false);
									bPTSPresent = true;
								} else if ((c>>4) == 3) {	// 0011 (PTS + DTS)
									pack_length -= 9;
									Read(buf+1, 9, false);
									bPTSPresent = bDTSPresent = true;
								} else if (c != 0x0f)
									throw MyError("%s: packet sync error on packet stream at position %I64u (timestamp marker bits not set)", szME, tagpos);

								if (bPTSPresent) {
									// Validate PTS marker bits.  Force resync on failure.
									if (!(buf[0]&buf[2]&buf[4]&1))
										throw MyError("%s: packet sync error on packet stream at position %I64u (PTS marker bits not set)", szME, tagpos);

									sint64 pts	= ((sint64)(buf[0]&0x0e) << 29)
												+ ((sint64) buf[1]       << 22)
												+ ((sint64)(buf[2]&0xfe) << 14)
												+ ((sint64) buf[3]       <<  7)
												+ (        (buf[4]&0xfe) >>  1);

									// If DTS is not present, it is the same as PTS.
									sint64 dts = pts;

									if (bDTSPresent) {
										// Validate DTS marker bits.  Force resync on failure.
										if ((buf[5]&0xf1)!=0x11 || (buf[7]&buf[9]&1)!=1)
											throw MyError("%s: packet sync error on packet stream at position %I64u (DTS marker bits not set)", szME, tagpos);

										dts	= ((sint64)(buf[5]&0x0e) << 29)
											+ ((sint64) buf[6]       << 22)
											+ ((sint64)(buf[7]&0xfe) << 14)
											+ ((sint64) buf[8]       <<  7)
											+ (        (buf[9]&0xfe) >>  1);
									}

									sint64& last_stream_dts = last_dts[stream_id - 0xc0];
									sint64 dts_delta = (dts - last_stream_dts) & 0x1FFFFFFFF;		// timestamps are 33 bits long.

									if (dts_delta > 63000) {					// decoding timestamps should never run backwards and must be occur at least every 0.7s (90KHz clock)
										const wchar_t *pStreamType = stream_id < 0xe0 ? L"audio" : L"video";
										const int nStream = (stream_id - 0xc0) & 0x1f;
										VDLogAppMessageLimited(warning_count, kVDLogWarning, kVDST_Mpeg, kVDM_OOOTimestamp, 5, &pStreamType, &nStream, &tagpos, &last_stream_dts, &dts);
									}

									last_stream_dts = dts;
								}
							}
						} else {
							stream_id = 0xe0;
							pack_length = 65536; //VIDEO_PACKET_BUFFER_SIZE;
						}

						if (pack_length < 0)
							throw MyError("%s: Packet at position %I64u has an invalid length value.", szME, file_cpos);

						// check packet type

						if ((0xe0 & stream_id) == 0xc0) {			// audio packet

							fHasAudio = TRUE;

							MPEGPacketInfo mpi;

							mpi.file_pos		= Tell();
							mpi.stream_pos		= audio_stream_pos;
							audio_stream_blocks.Add(&mpi);
							audio_stream_pos += pack_length;

							Read(audio_packet_buffer, pack_length, false);
							audioParser.Parse(audio_packet_buffer, pack_length, &audio_stream_samples);
							pack_length = 0;

						} else if ((0xf0 & stream_id) == 0xe0) {	// video packet

							if (fInterleaved) {
								MPEGPacketInfo mpi;

								mpi.file_pos		= Tell();
								mpi.stream_pos		= video_stream_pos;
								video_stream_blocks.Add(&mpi);
								video_stream_pos += pack_length;
							}

							int actual = Read(video_packet_buffer, pack_length, !fInterleaved);

							if (!fInterleaved && actual < pack_length)
								end_of_file = true;

							videoParser.Parse(video_packet_buffer, actual, &video_stream_samples);
							pack_length = 0;
						}

						if (pack_length)
								Skip(pack_length);
						break;
				}
			} while(!finished && (fInterleaved ? NextStartCode() : !end_of_file));
		} catch(const MyUserAbortError&) {
			if (forceAbort) {
				delete mpScanPrefetcher;
				::PostQuitMessage(errorCode);
				throw;
			}

			fTrimLastOff = true;
		} catch(const MyError&) {
			fTrimLastOff = true;

			// check if we actually got any video frames; if we didn't, rethrow the
			// parsing error instead
			if (video_stream_blocks.empty()) {
				delete mpScanPrefetcher;
				throw;
			}

			sint64 pos = Tell();
			VDLogAppMessage(kVDLogWarning, kVDST_Mpeg, kVDM_Incomplete, 1, &pos);
		}

		delete mpScanPrefetcher;

		// We're done scanning the file.  Finish off any ending packets we may have.

		static const unsigned char finish_tag[]={ 0, 0, 1, 0xff };

		videoParser.Parse(finish_tag, 4, &video_stream_samples);

		this->width = videoParser.width;
		this->height = videoParser.height;
		this->mFrameRate = videoParser.mFrameRate;

		switch(videoParser.mAspectRatioCode) {
			case  1:	mPixelAspectRatio.Assign(    1,     1); break;	// 1.0000
			case  2:	mPixelAspectRatio.Assign(10000,  6735); break;	// 0.6735
			case  3:	mPixelAspectRatio.Assign(10000,  7031); break;	// 0.7031
			case  4:	mPixelAspectRatio.Assign(10000,  7615); break;	// 0.7615
			case  5:	mPixelAspectRatio.Assign(10000,  8055); break;	// 0.8055
			case  6:	mPixelAspectRatio.Assign(10000,  8437); break;	// 0.8437
			case  7:	mPixelAspectRatio.Assign(10000,  8935); break;	// 0.8935
			case  8:	mPixelAspectRatio.Assign(   54,    59); break;	// 0.9157 - CCIR Rec.601, 625-line (54/59)
			case  9:	mPixelAspectRatio.Assign(10000,  9815); break;	// 0.9815 = 53/54
			case 10:	mPixelAspectRatio.Assign(10000, 10255); break;	// 1.0255
			case 11:	mPixelAspectRatio.Assign(10000, 10695); break;	// 1.0695
			case 12:	mPixelAspectRatio.Assign(   10,    11); break;	// 1.0950 - CCIR Rec.601, 525-line (11/10)
			case 13:	mPixelAspectRatio.Assign(10000, 11575); break;	// 1.1575
			case 14:	mPixelAspectRatio.Assign(10000, 12015); break;	// 1.2015
			default:	mPixelAspectRatio.Assign(0, 0); break;
		}

		// Construct stream and packet lookup tables.

		if (fInterleaved) {
			MPEGPacketInfo mpi;

			mpi.file_pos		= 0;
			mpi.stream_pos		= video_stream_pos;
			video_stream_blocks.Add(&mpi);

			video_packet_list = (MPEGPacketInfo *)video_stream_blocks.MakeArray();
			packets = video_stream_blocks.Length() - 1;

			mpi.file_pos		= 0;
			mpi.stream_pos		= audio_stream_pos;
			audio_stream_blocks.Add(&mpi);

			audio_packet_list = (MPEGPacketInfo *)audio_stream_blocks.MakeArray();
			apackets = audio_stream_blocks.Length() - 1;

			audio_sample_list = (MPEGSampleInfo *)audio_stream_samples.MakeArray();
			aframes = audio_stream_samples.Length();
			audio_first_header = audioParser.getHeader();
		}

		video_sample_list = (MPEGSampleInfo *)video_stream_samples.MakeArray();
		frames = video_stream_samples.Length();

		// If we are accepting partial streams, then cut off the last video frame, as it may be incomplete.
		// The audio parser checks for the entire frame to arrive, so we don't need to trim the audio.

		if (fTrimLastOff) {
			if (frames) --frames;
		}

		if (!frames)
			throw MyError("No video frames found in MPEG file.");

		// Begin renumbering the frames.  Some MPEG files have incorrectly numbered
		// subframes within each group.  So we do them from scratch.

		{
			int i;
			int sf = 0;		// subframe #
			MPEGSampleInfo *cached_IP = NULL;

			for(i=0; i<frames; i++) {

//				_RPT3(0,"Frame #%d: %c-frame (subframe: %d)\n", i, video_sample_list[i].frame_type[" IPBD567"], video_sample_list[i].subframe_num);

				if (video_sample_list[i].frame_type != MPEG_FRAME_TYPE_B) {
					if (cached_IP) cached_IP->subframe_num = (uint16)sf++;
					cached_IP = &video_sample_list[i];

					if (video_sample_list[i].frame_type == MPEG_FRAME_TYPE_I)
						sf = 0;
				} else
					video_sample_list[i].subframe_num = (uint16)sf++;

//				_RPT3(0,"Frame #%d: %c-frame (subframe: %d)\n", i, video_sample_list[i].frame_type[" IPBD567"], video_sample_list[i].subframe_num);
			}

			if (cached_IP)
				cached_IP->subframe_num = (uint16)sf;
		}

		const uint8 *intraquant = videoParser.GetIntraQuantMatrix();
		const uint8 *nonintraquant = videoParser.GetNonintraQuantMatrix();

		mbCustomIntraQuantMatrix = false;
		mbCustomNonintraQuantMatrix = false;

		if (intraquant) {
			memcpy(mIntraQuantMatrix, intraquant, sizeof mIntraQuantMatrix);
			mbCustomIntraQuantMatrix = true;
		}

		if (nonintraquant) {
			memcpy(mNonintraQuantMatrix, nonintraquant, sizeof mNonintraQuantMatrix);
			mbCustomNonintraQuantMatrix = true;
		}

	} catch(const MyError&) {
		if (hWndStatus) {
			EnableWindow(GetParent(hWndStatus), TRUE);
			DestroyWindow(hWndStatus);
			hWndStatus = NULL;
		}
		throw;
	}

	EndScan();

	mFileUnbuffered.closeNT();

	EnableWindow(GetParent(hWndStatus), TRUE);
	DestroyWindow(hWndStatus);
	hWndStatus = NULL;
}

InputFileMPEG::~InputFileMPEG() {
	EndScan();

	delete video_packet_buffer;
	delete audio_packet_buffer;
	delete video_packet_list;
	delete video_sample_list;
	delete audio_packet_list;
	delete audio_sample_list;

	delete pFastRead;
}

void InputFileMPEG::StartScan() {
	if (!(pScanBuffer = new char[SCAN_BUFFER_SIZE]))
		throw MyMemoryError();

	pScan = pScanLimit = pScanBuffer;
	i64ScanCpos = 0;
}

void InputFileMPEG::EndScan() {
	delete pScanBuffer;
	pScanBuffer = NULL;
}

bool InputFileMPEG::NextStartCode() {
	int c;

	while(EOF!=(c=Read())) {
		if (!c) {	// 00
			if (EOF==(c=Read())) return false;

			if (!c) {	// 00 00
				do {
					if (EOF==(c=Read())) return false;
				} while(!c);

				if (c==1)	// (00 00 ...) 00 00 01 xx
					return true;
			}
		}
	}

	return false;
}

void InputFileMPEG::Skip(int bytes) {
	int tc;

	while(bytes>0) {
		tc = pScanLimit - pScan;

		if (!tc) {
			if (EOF == _Read())
				throw MyError("%s: unexpected end of file", szME);

			--bytes;
			continue;
		}


		if (tc >= bytes) {
			pScan += bytes;
			break;
		}

		bytes -= tc;
		i64ScanCpos += (pScanLimit - pScanBuffer);
		pScan = pScanLimit = pScanBuffer;
	}
}

int InputFileMPEG::_Read() {
	char c;

	if (!pScan)
		return EOF;

	if (!Read(&c, 1, true))
		return EOF;

	return (unsigned char)c;
}

int InputFileMPEG::Read(void *buffer, int bytes, bool fShortOkay) {
	int total = 0;
	int tc;

	if (!pScan)
		if (fShortOkay)
			return 0;
		else
			throw MyError("%s: unexpected end of file", szME);

	if (pScan < pScanLimit) {
		tc = pScanLimit - pScan;

		if (tc > bytes)
			tc = bytes;

		memcpy(buffer, pScan, tc);

		pScan += tc;
		buffer = (char *)buffer + tc;
		total = tc;
		bytes -= tc;
	}

	if (bytes) {
		int actual;

		// Split the read into SCAN_BUFFER_SIZE chunks so we can
		// keep reading big chunks throughout the file.

		tc = bytes - bytes % SCAN_BUFFER_SIZE;

		// With a VideoCD, read the header and then the pack.

//		_RPT1(0,"Starting at %I64lx\n", _telli64(fh));

		if (tc) do {
			if (fIsVCD) {
				char hdr[20];

				actual = mpScanPrefetcher->readData(hdr, 20);

				if (actual != 20)
					if (fShortOkay)
						return total;

				i64ScanCpos += 20;

				tc = 2332;
			}

			if (tc > 0) {
				actual = mpScanPrefetcher->readData(buffer, tc);

				if (actual != tc)
					if (fShortOkay)
						return total + actual;
					else
						throw MyError("%s: unexpected end of file", szME);

				total += actual;
				i64ScanCpos += actual;
				buffer = (char *)buffer + actual;
			}

			bytes -= tc;
		} while(fIsVCD && bytes >= 2332);

		tc = bytes;

		if (tc) {
			if (fIsVCD) {
				char hdr[20];

				actual = mpScanPrefetcher->readData( hdr, 20);

				if (actual != 20)
					if (fShortOkay)
						return total;

				i64ScanCpos += 20;

				actual = mpScanPrefetcher->readData(pScanBuffer, 2332);
			} else
				actual = mpScanPrefetcher->readData(pScanBuffer, SCAN_BUFFER_SIZE);

			if (!fShortOkay && actual < tc)
				throw MyError("%s: unexpected end of file", szME);

			i64ScanCpos += (pScan - pScanBuffer);

			if (actual < tc)
				tc = actual;

			memcpy(buffer, pScanBuffer, tc);

			total += tc;

			pScanLimit	= pScanBuffer + actual;
			pScan		= pScanBuffer + tc;
		}
	}

	return total;
}

__int64 InputFileMPEG::Tell() {
	return i64ScanCpos + (pScan - pScanBuffer);
}

//////////////////////

int InputFileMPEG::FindStartingPacket(sint64 pos, bool bAudio, sint32& offset) {
	MPEGPacketInfo *packet_list = bAudio ? audio_packet_list : video_packet_list;
	int pkts = bAudio ? apackets : packets;

	int pkt = 0;

	do {
		int l = 0;
		int r = pkts-1;

		// Check current packet

		pkt = last_packet[bAudio?1:0];

		if (pkt>=0 && pkt<pkts) {
			if (pos < packet_list[pkt].stream_pos)
				r = pkt-1;
			else if (pos < packet_list[pkt+1].stream_pos)
				break;
			else if (pkt+1 < pkts && pos < packet_list[pkt+2].stream_pos) {
				++pkt;
				break;
			} else
				l = pkt+2;
		}

		for(;;) {
			if (l > r) {
				pkt = l;
				break;
			}

			pkt = (l+r)>>1;

			if (pos < packet_list[pkt].stream_pos)
				r = pkt-1;
			else if (pos >= packet_list[pkt+1].stream_pos)
				l = pkt+1;
			else
				break;
		}

		if (pkt<0 || pkt >= pkts)
			throw MyError("MPEG Internal error: Invalid stream read position (%ld)", pos);
	} while(false);

	offset = (sint32)(pos - packet_list[pkt].stream_pos);

	return pkt;
}

sint64 InputFileMPEG::GetStartingBytePosition(VDPosition pos, bool bAudio) {
	if (!fInterleaved)
		return pos;

	MPEGPacketInfo *packet_list = bAudio ? audio_packet_list : video_packet_list;
	sint32 offset;
	int pkt = FindStartingPacket(pos, bAudio, offset);

	return packet_list[pkt].file_pos + offset;
}

void InputFileMPEG::ReadStream(void *buffer, __int64 pos, long len, bool fAudio) {
	if (!fInterleaved) {

		if (pFastRead)
			pFastRead->Read(0, pos, buffer, len);
		else {
			mFile.seek(pos);
			mFile.read(buffer, len);
		}
		return;
	}

	// find the packet containing the data start using a binary search
	MPEGPacketInfo *packet_list = fAudio ? audio_packet_list : video_packet_list;
	int pkts = fAudio ? apackets : packets;
	char *ptr = (char *)buffer;

	sint32 delta;
	int pkt = FindStartingPacket(pos, fAudio, delta);

	// read data from packets

	while(len) {
		if (pkt >= pkts) throw MyError("Attempt to read past end of stream (pos %ld)", pos);

		long tc = (long)((packet_list[pkt+1].stream_pos - packet_list[pkt].stream_pos) - delta);

		if (tc>len) tc=len;

//		_RPT3(0,"Reading %ld bytes at %08lx+%ld\n", tc, video_packet_list[pkt].file_pos,delta);

		if (pFastRead) {
			pFastRead->Read(fAudio ? 1 : 0, packet_list[pkt].file_pos + delta, ptr, tc);
		} else {
			mFile.seek(packet_list[pkt].file_pos + delta);
			mFile.read(ptr, tc);
		}

		len -= tc;
		ptr += tc;
		++pkt;
		delta = 0;
	}

	last_packet[fAudio?1:0] = pkt-1;
}

INT_PTR CALLBACK InputFileMPEG::ParseDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	InputFileMPEG *thisPtr = (InputFileMPEG *)GetWindowLongPtr(hDlg, DWLP_USER);

	switch(uMsg) {
	case WM_INITDIALOG:
		SetWindowLongPtr(hDlg, DWLP_USER, lParam);
		thisPtr = (InputFileMPEG *)lParam;

		SendMessageA(hDlg, WM_SETTEXT, 0, (LPARAM)"MPEG Import Filter");
		SetDlgItemTextA(hDlg, IDC_STATIC_MESSAGE,
			thisPtr->fIsVCD
				? "Parsing VideoCD stream"
				: thisPtr->fInterleaved ? "Parsing interleaved MPEG file" : "Parsing MPEG video file");
		SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETRANGE, 0, MAKELPARAM(0, 16384));
		SetTimer(hDlg, 1, 250, (TIMERPROC)NULL);

		EnableWindow(GetParent(hDlg), FALSE);
		ShowWindow(hDlg, IsIconic(g_hWnd) ? SW_SHOWMINNOACTIVE : SW_SHOW);
		return TRUE;

	case WM_TIMER:
		{
			char buf[128];

			SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETPOS, (WPARAM)((thisPtr->file_cpos*16384) / thisPtr->file_len), 0);

			if (thisPtr->fIsVCD)
				wsprintfA(buf, "%ld of %ld sectors", (long)(thisPtr->file_cpos/2352), (long)(thisPtr->file_len/2352));
			else
				wsprintfA(buf, "%ldK of %ldK", (long)(thisPtr->file_cpos>>10), (long)((thisPtr->file_len+1023)>>10));

			SendDlgItemMessageA(hDlg, IDC_CURRENT_VALUE, WM_SETTEXT, 0, (LPARAM)buf);
		}
		return TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL)
			thisPtr->fAbort = true;
		return TRUE;
	}

	return FALSE;
}

///////////////////////////////////////////////////////////////////////////

void InputFileMPEG::setOptions(InputFileOptions *_ifo) {
	InputFileMPEGOptions *ifo = (InputFileMPEGOptions *)_ifo;

	iDecodeMode = ifo->opts.iDecodeMode;

	if (iDecodeMode & InputFileMPEGOptions::DECODE_NO_P)
		iDecodeMode |= InputFileMPEGOptions::DECODE_NO_B;
}

InputFileOptions *InputFileMPEG::createOptions(const void *buf, uint32 len) {
	InputFileMPEGOptions *ifo = new InputFileMPEGOptions();

	if (!ifo) throw MyMemoryError();

	if (!ifo->read((const char *)buf)) {
		delete ifo;
		return NULL;
	}

	return ifo;
}

InputFileOptions *InputFileMPEG::promptForOptions(VDGUIHandle hwnd) {
	InputFileMPEGOptions *ifo = new InputFileMPEGOptions();

	if (!ifo) throw MyMemoryError();

	DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_EXTOPENOPTS_MPEG), (HWND)hwnd, InputFileMPEGOptions::SetupDlgProc, (LPARAM)ifo);

	return ifo;
}

///////////////////////////////////////////////////////////////////////////

typedef struct MyFileInfo {
	InputFileMPEG *thisPtr;

	volatile HWND hWndAbort;
	UINT statTimer;

	long lFrames;
	long lTotalSize;
	long lFrameCnt[3];
	long lFrameMinSize[3];
	long lFrameMaxSize[3];
	long lFrameTotalSize[3];
	long lAudioSize;
	long lAudioAvgBitrate;
	const char *lpszAudioMode;

	vdrefptr<IVDVideoSource>	mpVideo;
	vdrefptr<AudioSource>		mpAudio;
} MyFileInfo;

void InputFileMPEG::_InfoDlgThread(void *pvInfo) {
	MyFileInfo *pInfo = (MyFileInfo *)pvInfo;
	InputFileMPEG *thisPtr = pInfo->thisPtr;
	VDPosition i;
	VideoSourceMPEG *vSrc = static_cast<VideoSourceMPEG *>(&*pInfo->mpVideo);
	AudioSourceMPEG *aSrc = static_cast<AudioSourceMPEG *>(&*pInfo->mpAudio);
	MPEGSampleInfo *msi;

	for(i=0; i<3; i++)
		pInfo->lFrameMinSize[i] = 0x7FFFFFFF;

	const VDPosition videoFrameStart	= vSrc->getStart();
	const VDPosition videoFrameEnd		= vSrc->getEnd();

	msi = thisPtr->video_sample_list;
	for(i = videoFrameStart; i < videoFrameEnd; ++i) {
		int iFrameType = msi->frame_type;

		if (iFrameType) {
			long lSize = msi->size;
			--iFrameType;

			++pInfo->lFrameCnt[iFrameType];
			pInfo->lFrameTotalSize[iFrameType] += lSize;

			if (lSize < pInfo->lFrameMinSize[iFrameType])
				pInfo->lFrameMinSize[iFrameType] = lSize;

			if (lSize > pInfo->lFrameMaxSize[iFrameType])
				pInfo->lFrameMaxSize[iFrameType] = lSize;

			pInfo->lTotalSize += lSize;

		}
		++pInfo->lFrames;

		++msi;
		if (pInfo->hWndAbort) {
			SendMessage(pInfo->hWndAbort, WM_USER+256, 0, 0);
			return;
		}
	}

	///////////////////////////////////////////////////////////////////////

	if (aSrc) {
		static const char *szModes[4]={ "stereo", "joint stereo", "dual channel", "mono" };
		bool fAudioMixedMode = false;
		bool fAudioMono = false;
		long lTotalBitrate = 0;

		msi = thisPtr->audio_sample_list;

		for(i = 0; i < thisPtr->aframes; ++i) {
			long fAudioHeader = msi->header;

			if ((thisPtr->audio_first_header ^ fAudioHeader) & MPEGAHDR_MODE_MASK)
				fAudioMixedMode = true;

			// mode==3 is mono, all others are stereo

			if (!(~thisPtr->audio_first_header & MPEGAHDR_MODE_MASK))
				fAudioMono = true;

			lTotalBitrate += MPEGAudioHeader(fAudioHeader).GetBitrateKbps();

			pInfo->lAudioSize += msi->size;

			++msi;
			if (pInfo->hWndAbort) {
				SendMessage(pInfo->hWndAbort, WM_USER+256, 0, 0);
				return;
			}
		}

		pInfo->lAudioAvgBitrate = lTotalBitrate / thisPtr->aframes;

		if (fAudioMixedMode) {
			if (fAudioMono)
				pInfo->lpszAudioMode = "mixed mode";
			else
				pInfo->lpszAudioMode = "mixed stereo";
		} else
			pInfo->lpszAudioMode = szModes[(thisPtr->audio_first_header>>30) & 3];
	}

	pInfo->hWndAbort = (HWND)1;
}

INT_PTR APIENTRY InputFileMPEG::_InfoDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	MyFileInfo *pInfo = (MyFileInfo *)GetWindowLongPtr(hDlg, DWLP_USER);
	InputFileMPEG *thisPtr;

	if (pInfo)
		thisPtr = pInfo->thisPtr;

    switch (message)
    {
        case WM_INITDIALOG:
			SetWindowLongPtr(hDlg, DWLP_USER, lParam);
			pInfo = (MyFileInfo *)lParam;
			thisPtr = pInfo->thisPtr;

			if (pInfo->mpVideo) {
				char *s;
				char buf[128];
				IVDStreamSource *pVSS = pInfo->mpVideo->asStream();

				sprintf(buf, "%dx%d, %.3f fps (%ld µs)",
							thisPtr->width,
							thisPtr->height,
							pVSS->getRate().asDouble(),
							VDRoundToLong(1000000.0 / pVSS->getRate().asDouble()));
				SetDlgItemTextA(hDlg, IDC_VIDEO_FORMAT, buf);

				s = buf + sprintf(buf, "%lu (", (unsigned)pVSS->getLength());
				ticks_to_str(s, (buf + std::size(buf)) - s, VDRoundToLong(1000.0*pVSS->getLength()/pVSS->getRate().asDouble()));
				strcat(s,")");
				SetDlgItemTextA(hDlg, IDC_VIDEO_NUMFRAMES, buf);
			}

			if (pInfo->mpAudio) {
				const VDWaveFormat *fmt = pInfo->mpAudio->getWaveFormat();
				char buf[128];

				sprintf(buf, "%ldHz, %s", fmt->mSamplingRate, fmt->mChannels>1 ? "Stereo" : "Mono");
				SetDlgItemTextA(hDlg, IDC_AUDIO_FORMAT, buf);

				sprintf(buf, "%ld", thisPtr->aframes);
				SetDlgItemTextA(hDlg, IDC_AUDIO_NUMFRAMES, buf);
			}

			_beginthread(_InfoDlgThread, 10000, pInfo);

			pInfo->statTimer = SetTimer(hDlg, 1, 250, NULL);

            return (TRUE);

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
				if (pInfo->hWndAbort == (HWND)1)
					EndDialog(hDlg, TRUE);
                return TRUE;
            }
            break;

		case WM_DESTROY:
			if (pInfo->statTimer) KillTimer(hDlg, pInfo->statTimer);
			break;

		case WM_TIMER:
			{
				char buf[128];

				sprintf(buf, "%ld", pInfo->lFrames);
				SetDlgItemTextA(hDlg, IDC_VIDEO_NUMKEYFRAMES, buf);

				sprintf(buf, "%ld / %ld / %ld", pInfo->lFrameCnt[0], pInfo->lFrameCnt[1], pInfo->lFrameCnt[2]);
				SetDlgItemTextA(hDlg, IDC_VIDEO_FRAMETYPECNT, buf);

				int i;

				UINT uiCtlIds[]={ IDC_VIDEO_IFRAMES, IDC_VIDEO_PFRAMES, IDC_VIDEO_BFRAMES };

				for(i=0; i<3; i++) {

					if (pInfo->lFrameCnt[i])
						sprintf(buf, "%ld / %ld / %ld (%ldK)"
									,pInfo->lFrameMinSize[i]
									,pInfo->lFrameTotalSize[i]/pInfo->lFrameCnt[i]
									,pInfo->lFrameMaxSize[i]
									,(pInfo->lFrameTotalSize[i]+1023)>>10);
					else
						sprintf(buf,"(no %c-frames)", "IPB"[i]);

					SetDlgItemTextA(hDlg, uiCtlIds[i], buf);
				}

				if (pInfo->lTotalSize) {
					long lBytesPerSec;

					// bits * (frames/sec) / frames = bits/sec

					lBytesPerSec = VDRoundToLong((pInfo->lTotalSize * pInfo->mpVideo->asStream()->getRate().asDouble()) / pInfo->lFrames);

					sprintf(buf, "%ld Kbps (%ldKB/s)", (lBytesPerSec+124)/125, (lBytesPerSec+1023)/1024);
					SetDlgItemTextA(hDlg, IDC_VIDEO_AVGBITRATE, buf);
				}

				if (pInfo->lpszAudioMode && pInfo->mpAudio) {
					static const char *szLayers[]={"I","II","III"};
					const VDWaveFormat *fmt = pInfo->mpAudio->getWaveFormat();

					sprintf(buf, "%ldKHz %s, %ldKbps layer %s", fmt->mSamplingRate/1000, pInfo->lpszAudioMode, pInfo->lAudioAvgBitrate, szLayers[3-((thisPtr->audio_first_header>>9)&3)]);
					SetDlgItemTextA(hDlg, IDC_AUDIO_FORMAT, buf);

					sprintf(buf, "%ldK", (pInfo->lAudioSize + 1023) / 1024);
					SetDlgItemTextA(hDlg, IDC_AUDIO_SIZE, buf);
				}
			}

			/////////

			if (pInfo->hWndAbort) {
				KillTimer(hDlg, pInfo->statTimer);
				return TRUE;
			}

			break;

		case WM_USER+256:
			EndDialog(hDlg, TRUE);
			break;
    }
    return FALSE;
}

void InputFileMPEG::InfoDialog(VDGUIHandle hwndParent) {
	MyFileInfo mai;

	memset(&mai, 0, sizeof mai);
	mai.thisPtr = this;

	GetVideoSource(0, ~mai.mpVideo);
	GetAudioSource(0, ~mai.mpAudio);

	DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_MPEG_INFO), (HWND)hwndParent, _InfoDlgProc, (LPARAM)&mai);
}

bool InputFileMPEG::GetVideoSource(int index, IVDVideoSource **ppSrc) {
	if (index)
		return false;

	vdrefptr<VideoSourceMPEG> videoSrc(new VideoSourceMPEG(this));
	videoSrc->init();

	*ppSrc = videoSrc.release();
	return true;
}

bool InputFileMPEG::GetAudioSource(int index, AudioSource **ppSrc) {
	if (index || !fHasAudio)
		return false;

	vdrefptr<AudioSourceMPEG> audioSrc(new AudioSourceMPEG(this));
	audioSrc->init();

	*ppSrc = audioSrc.release();
	return true;
}

/////////////////////////////////////////////////////////////////////

class VDInputDriverMPEG : public vdrefcounted<IVDInputDriver> {
public:
	const wchar_t *GetSignatureName() { return L"MPEG-1 input driver (internal)"; }

	int GetDefaultPriority() {
		return 0;
	}

	uint32 GetFlags() { return kF_Video | kF_Audio | kF_SupportsOpts; }

	const wchar_t *GetFilenamePattern() {
		return L"MPEG-1 video/systems stream (*.mpg,*.mpeg,*.mpv,*.m1v,*.dat)\0*.mpg;*.mpeg;*.mpv;*.m1v;*.dat\0";
	}

	bool DetectByFilename(const wchar_t *pszFilename) {
		return false;
	}

	DetectionConfidence DetectBySignature(const void *pHeader, sint32 nHeaderSize, const void *pFooter, sint32 nFooterSize, sint64 nFileSize) {
		//! limit to Low for compatibility with mpeg-2 plugin

		if (nHeaderSize < 12) return kDC_Error_MoreData;
		if (nHeaderSize >= 12) {
			if (!memcmp(pHeader, "RIFF", 4) && !memcmp((char*)pHeader+8, "CDXA", 4))
				return kDC_Low;
				//return kDC_High;

			if (*(const uint32 *)pHeader == 0xba010000 || *(const uint32 *)pHeader==0xb3010000)
				return kDC_Low;
				//return kDC_High;

			// Second pass for MPEG.  This time, scan the first 64 bytes for 00 00 01 BA.

			int i;
			int limit = nHeaderSize - 3;

			if (limit > 60)
				limit = 60;

			for(i=1; i<limit; ++i)
				if (*(const uint32 *)((const uint8 *)pHeader+i) == 0xba010000 || *(const uint32 *)((const uint8 *)pHeader+i)==0xb3010000)
					break;

			if (i < limit)
				return kDC_Low;
				//return kDC_Moderate;

		}

		return kDC_None;
	}

	InputFile *CreateInputFile(uint32 flags) {
		return new InputFileMPEG;
	}
};

extern IVDInputDriver *VDCreateInputDriverMPEG() { return new VDInputDriverMPEG; }
