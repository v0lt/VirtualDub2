#include "stdafx.h"
#include <ctype.h>
#include <memory>

#include "oshelper.h"
#include <vd2/system/file.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/error.h>
#include <vd2/system/filesys.h>
#include <vd2/system/vdalloc.h>
#include <vd2/Dita/resources.h>
#include <vd2/Meia/decode_png.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/libav_tiff/tiff_image.h>
#include "ProgressDialog.h"
#include "InputFileImages.h"
#include "VideoSourceImages.h"
#include "image.h"
#include "imagejpegdec.h"
#include "imageiff.h"
#include "VBitmap.h"
#include "resource.h"
#include "gui.h"

extern HINSTANCE g_hInst;

///////////////////////////////////////////////////////////////////////////

namespace {
	enum { kVDST_PNGDecodeErrors = 100 };
}

///////////////////////////////////////////////////////////////////////////

class VideoSourceImages : public VideoSource {
public:
	VideoSourceImages(VDInputFileImages *parent);

	int _read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *lBytesRead, uint32 *lSamplesRead) override;
	bool _isKey(VDPosition samp) override { return true; }
	VDPosition nearestKey(VDPosition lSample) override	{ return lSample; }
	VDPosition prevKey(VDPosition lSample) override		{ return lSample>0 ? lSample-1 : -1; }
	VDPosition nextKey(VDPosition lSample) override		{ return lSample<mSampleLast ? lSample+1 : -1; }

	bool setTargetFormat(VDPixmapFormatEx format) override;

	void invalidateFrameBuffer() override	{ mCachedFrame = -1; }
	bool isFrameBufferValid() override		{ return mCachedFrame >= 0; }
	bool isStreaming() override				{ return false; }

	const void *streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition sample_num, VDPosition target_sample) override;

	const void *getFrame(VDPosition frameNum) override;

	char getFrameTypeChar(VDPosition lFrameNum) override { return 'K'; }
	eDropType getDropType(VDPosition lFrameNum) override { return kIndependent; }
	bool isKeyframeOnly() override			{ return true; }
	bool isType1() override					{ return false; }
	bool isDecodable(VDPosition sample_num) override { return true; }
	bool getInitAlpha()						{ return mInitAlpha; }
	nsVDPixmap::VDPixmapFormat getInitFormat() { return mInitFormat; }

private:
	vdrefptr<VDInputFileImages> mpParent;
	vdfastvector<wchar_t> mPathBuf;

	VDPosition	mCachedFrame;
	VBitmap	mvbFrameBuffer;

	VDPosition	mCachedHandleFrame;
	VDFile	mCachedFile;

	nsVDPixmap::VDPixmapFormat mInitFormat;
	bool mInitAlpha;
	bool mTGADecoder;

	vdautoptr<IVDJPEGDecoder> mpJPEGDecoder;
	vdautoptr<IVDImageDecoderIFF> mpIFFDecoder;
	vdautoptr<IVDImageDecoderPNG> mpPNGDecoder;
	vdautoptr<IVDImageDecoderTIFF> mpTIFFDecoder;
};

IVDVideoSource *VDCreateVideoSourceImages(VDInputFileImages *parent) {
	return new VideoSourceImages(parent);
}

INT_PTR APIENTRY VDInputFileImages::_InfoDlgProc( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
			{
				char buf[128];

				VideoSourceImages *pvs = (VideoSourceImages *)lParam;
				char *s;

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

				const VDPixmapFormatInfo& info = VDPixmapGetInfo(pvs->getInitFormat());

				s = buf + sprintf(buf, pvs->getInitAlpha() ? "%s with alpha" : "%s", info.name);

				SetDlgItemTextA(hDlg, IDC_VIDEO_COMPRESSION, buf);
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

void VDInputFileImages::InfoDialog(VDGUIHandle hwndParent)
{
	IVDVideoSource* vs;
	GetVideoSource(0, &vs);
	VideoSourceImages* images = static_cast<VideoSourceImages*>(vs);

	DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_IMAGES_INFO), (HWND)hwndParent, _InfoDlgProc, (LPARAM)images);
	vs->Release();
}

///////////////////////////////////////////////////////////////////////////

VideoSourceImages::VideoSourceImages(VDInputFileImages *parent)
	: mpParent(parent)
	, mCachedHandleFrame(-1)
{
	mSampleFirst = 0;
	mTGADecoder = false;

	mpTargetFormatHeader.resize(sizeof(BITMAPINFOHEADER));
	allocFormat(sizeof(BITMAPINFOHEADER));

	invalidateFrameBuffer();

	// This has to be 1 so that read() doesn't kick away the request.

	mSampleLast = mpParent->GetFrameCount();
	mInitFormat = nsVDPixmap::kPixFormat_Null;
	getFrame(0);

	// Fill out streamInfo
	extern const VDFraction& VDPreferencesGetImageSequenceFrameRate();
	const VDFraction& fr = VDPreferencesGetImageSequenceFrameRate();

	streamInfo.fccType					= VDAVIStreamInfo::kTypeVideo;
	streamInfo.fccHandler				= NULL;
	streamInfo.dwFlags					= 0;
	streamInfo.dwCaps					= 0;
	streamInfo.wPriority				= 0;
	streamInfo.wLanguage				= 0;
	streamInfo.dwScale					= fr.getLo();
	streamInfo.dwRate					= fr.getHi();
	streamInfo.dwStart					= 0;
	streamInfo.dwLength					= VDClampToUint32(mSampleLast);
	streamInfo.dwInitialFrames			= 0;
	streamInfo.dwSuggestedBufferSize	= 0;
	streamInfo.dwQuality				= (DWORD)-1;
	streamInfo.dwSampleSize				= 0;
	streamInfo.rcFrameLeft				= 0;
	streamInfo.rcFrameTop				= 0;
	streamInfo.rcFrameRight				= (uint16)getImageFormat()->biWidth;
	streamInfo.rcFrameBottom			= (uint16)getImageFormat()->biHeight;
}

int VideoSourceImages::_read(VDPosition lStart, uint32 lCount, void *lpBuffer, uint32 cbBuffer, uint32 *plBytesRead, uint32 *plSamplesRead) {
	if (plBytesRead)
		*plBytesRead = 0;

	if (plSamplesRead)
		*plSamplesRead = 0;

	const wchar_t *buf = mpParent->ComputeFilename(mPathBuf, lStart);

	// Check if we already have the file handle cached.  If not, open the file.

	if (lStart == mCachedHandleFrame) {
		mCachedFile.seek(0);
	} else{
		mCachedHandleFrame = -1;
		mCachedFile.closeNT();
		mCachedFile.open(buf, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting);
		mCachedHandleFrame = lStart;
	}

	// Replace

	uint32 size = (uint32)mCachedFile.size();

	if (size > 0x3fffffff)
		throw MyError("VideoSourceImages: File \"%s\" is too large (>1GB).", VDTextWToA(buf).c_str());

	if (!lpBuffer) {
		if (plBytesRead)
			*plBytesRead = size;

		if (plSamplesRead)
			*plSamplesRead = 1;

		return 0;
	}

	if (size > cbBuffer) {
		if (plBytesRead)
			*plBytesRead = size;

		return IVDStreamSource::kBufferTooSmall;
	}

	mCachedFile.read(lpBuffer, size);

	if (plBytesRead)
		*plBytesRead = size;

	if (plSamplesRead)
		*plSamplesRead = 1;

	return 0;
}

bool VideoSourceImages::setTargetFormat(VDPixmapFormatEx format) {
	if (!format) {
		format = nsVDPixmap::kPixFormat_XRGB8888;
    switch (mInitFormat) {
    case nsVDPixmap::kPixFormat_XRGB64:
    case nsVDPixmap::kPixFormat_Y16:
    case nsVDPixmap::kPixFormat_Y8:
    case nsVDPixmap::kPixFormat_Y8_FR:
			format = mInitFormat;
      break;
    }
	}

	int w = getImageFormat()->biWidth;
	int h = getImageFormat()->biHeight;

	switch(format) {
	case nsVDPixmap::kPixFormat_XRGB1555:
	case nsVDPixmap::kPixFormat_RGB888:
	case nsVDPixmap::kPixFormat_XRGB8888:
		if (!AllocFrameBuffer(w * h * 4))
			throw MyMemoryError();

		if (!VideoSource::setTargetFormat(format))
			return false;

		invalidateFrameBuffer();

		mvbFrameBuffer.init((void *)getFrameBuffer(), mpTargetFormatHeader->biWidth, mpTargetFormatHeader->biHeight, mpTargetFormatHeader->biBitCount);
		mvbFrameBuffer.AlignTo4();

		return true;

	case nsVDPixmap::kPixFormat_XRGB64:
		{
			if (!mpTIFFDecoder) break; // legacy code path does not use this format
			if (!AllocFrameBuffer(((w+3)&~3) * h * 8))
				throw MyMemoryError();

			if (!VideoSource::setTargetFormat(format))
				return false;

			VDPixmap px = {0};
			px.format = format;
			px.w = w;
			px.h = h;
			px.pitch = w*8;
			px.data = mpFrameBuffer.get();
			mTargetFormat = px;

			invalidateFrameBuffer();

			return true;
		}

	case nsVDPixmap::kPixFormat_Y16:
		{
			if (!mpTIFFDecoder) break; // legacy code path does not use this format
			if (!AllocFrameBuffer(w * h * 2))
				throw MyMemoryError();

			if (!VideoSource::setTargetFormat(format))
				return false;

			VDPixmap px = {0};
			px.format = format;
			px.w = w;
			px.h = h;
			px.pitch = w*2;
			px.data = mpFrameBuffer.get();
			mTargetFormat = px;

			invalidateFrameBuffer();

			return true;
		}

	case nsVDPixmap::kPixFormat_Y8:
	case nsVDPixmap::kPixFormat_Y8_FR:
		{
			if (!mpTIFFDecoder && !mTGADecoder) break; // legacy code path does not use this format
			if (!AllocFrameBuffer(w * h))
				throw MyMemoryError();

			if (!VideoSource::setTargetFormat(format))
				return false;

			VDPixmap px = {0};
			px.format = format;
			px.w = w;
			px.h = h;
			px.pitch = w;
			px.data = mpFrameBuffer.get();
			mTargetFormat = px;

			invalidateFrameBuffer();

			return true;
		}
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

const void *VideoSourceImages::streamGetFrame(const void *inputBuffer, uint32 data_len, bool is_preroll, VDPosition frame_num, VDPosition target_sample) {
	// We may get a zero-byte frame if we already have the image.

	if (!data_len)
		return getFrameBuffer();

	int w, h;
	int format = 0;
	bool bHasAlpha = false;

	bool bIsPNG = false;
	bool bIsJPG = false;
	bool bIsBMP = false;
	bool bIsIFF = false;
	bool bIsTGA = false;
	bool bIsTIFF = false;

	bIsPNG = VDDecodePNGHeader(inputBuffer, data_len, w, h, bHasAlpha);
	if (!bIsPNG) {
		bIsJPG = VDIsJPEGHeader(inputBuffer, data_len);
		if (!bIsJPG) {
			bIsBMP = DecodeBMPHeader(inputBuffer, data_len, w, h, bHasAlpha);
			if (!bIsBMP) {
				bIsIFF = VDIsMayaIFFHeader(inputBuffer, data_len);
				if (!bIsIFF)
					bIsTIFF = VDIsTiffHeader(inputBuffer, data_len);
					if (!bIsTIFF)
						bIsTGA = DecodeTGAHeader(inputBuffer, data_len, w, h, format, bHasAlpha);
			}
		}
	}

	if (!bIsBMP && !bIsTGA && !bIsJPG && !bIsPNG && !bIsIFF && !bIsTIFF)
		throw MyError("Image file must be in PNG, Windows BMP, truecolor TARGA format, MayaIFF, TIFF, or sequential JPEG format.");

	mTargetFormat.info.clear();
	mTargetFormat.info.frame_num = frame_num;
	if (bHasAlpha && VDPixmapFormatHasAlpha(mTargetFormat.format))
		mTargetFormat.info.alpha_type = FilterModPixmapInfo::kAlphaOpacity;

	if (bIsJPG) {
		if (!mpJPEGDecoder)
			mpJPEGDecoder = VDCreateJPEGDecoder();
		mpJPEGDecoder->Begin(inputBuffer, data_len);
		mpJPEGDecoder->DecodeHeader(w, h);
	}

	VDPixmap pxIFF;
	if (bIsIFF) {
		if (!mpIFFDecoder)
			mpIFFDecoder = VDCreateImageDecoderIFF();
		pxIFF = mpIFFDecoder->Decode(inputBuffer, data_len);
		w = pxIFF.w;
		h = pxIFF.h;
	}

	if (bIsTIFF) {
		if (!mpTIFFDecoder)
			mpTIFFDecoder = VDCreateImageDecoderTIFF();
		mpTIFFDecoder->Decode(inputBuffer, data_len);
		mpTIFFDecoder->GetSize(w, h);
		format = mpTIFFDecoder->GetFormat();

		// for info display
		FilterModPixmapInfo info;
		mpTIFFDecoder->GetPixmapInfo(info);
		if(info.alpha_type) bHasAlpha = true;
	}

	mTGADecoder = bIsTGA;

	// Check image header.

	VDAVIBitmapInfoHeader *pFormat = getImageFormat();

	if (getFrameBuffer()) {
		if (w != pFormat->biWidth || h != pFormat->biHeight) {
			vdfastvector<wchar_t> errBuf;

			throw MyError("Image \"%ls\" (%dx%d) doesn't match the image dimensions of the first image (%dx%d)."
					, mpParent->ComputeFilename(errBuf, frame_num), w, h, pFormat->biWidth, pFormat->biHeight);
		}

	} else {
		mInitFormat = (nsVDPixmap::VDPixmapFormat)format;
		//! old decoders are not very informative
		if (format==0)
			mInitFormat = bHasAlpha ? nsVDPixmap::kPixFormat_XRGB8888 : nsVDPixmap::kPixFormat_RGB888;
		mInitAlpha = bHasAlpha;
		mSourceFormat = mInitFormat;

		pFormat->biSize				= sizeof(BITMAPINFOHEADER);
		pFormat->biWidth			= w;
		pFormat->biHeight			= h;
		pFormat->biPlanes			= 1;
		pFormat->biCompression		= 0xFFFFFFFFUL;
		pFormat->biBitCount			= 0;
		pFormat->biSizeImage		= 0;
		pFormat->biXPelsPerMeter	= 0;
		pFormat->biYPelsPerMeter	= 0;
		pFormat->biClrUsed			= 0;
		pFormat->biClrImportant		= 0;

		// special case for initial read in constructor

		return NULL;
	}

	if (bIsJPG) {
		int format;

		switch(mvbFrameBuffer.depth) {
		case 16:	format = IVDJPEGDecoder::kFormatXRGB1555;	break;
		case 24:	format = IVDJPEGDecoder::kFormatRGB888;		break;
		case 32:	format = IVDJPEGDecoder::kFormatXRGB8888;	break;
		}

		mpJPEGDecoder->DecodeImage((char *)mvbFrameBuffer.data + mvbFrameBuffer.pitch * (mvbFrameBuffer.h - 1), -mvbFrameBuffer.pitch, format);
		mpJPEGDecoder->End();
	}

	if (bIsIFF)
		VDPixmapBlt(getTargetFormat(), pxIFF);

	if (bIsTIFF) {
		VDPixmap& dst = mTargetFormat;
		if (dst.format==format) {
			mpTIFFDecoder->GetPixmapInfo(dst.info);
			mpTIFFDecoder->GetImage((char *)dst.data, dst.pitch, format);
		} else {
			VDPixmapBuffer buf;
			buf.init(w,h,format);
			buf.info.copy_frame(dst.info);
			mpTIFFDecoder->GetPixmapInfo(buf.info);
			mpTIFFDecoder->GetImage(buf.data, buf.pitch, format);
			VDPixmapBlt(dst, buf);
		}
	}

	if (bIsBMP)
		DecodeBMP(inputBuffer, data_len, VDAsPixmap(mvbFrameBuffer));
	if (bIsTGA)
		DecodeTGA(inputBuffer, data_len, mTargetFormat);
	if (bIsPNG) {
		if (!mpPNGDecoder)
			mpPNGDecoder = VDCreateImageDecoderPNG();

		PNGDecodeError err = mpPNGDecoder->Decode(inputBuffer, data_len);

		if (err) {
			if (err == kPNGDecodeOutOfMemory)
				throw MyMemoryError();

			vdfastvector<wchar_t> errBuf;

			throw MyError("Error decoding \"%ls\": %ls\n", mpParent->ComputeFilename(errBuf, frame_num), VDLoadString(0, kVDST_PNGDecodeErrors, err));
		}

		VDPixmapBlt(VDAsPixmap(mvbFrameBuffer), mpPNGDecoder->GetFrameBuffer());
	}

	mCachedFrame = frame_num;

	return mpFrameBuffer.get();
}

const void *VideoSourceImages::getFrame(VDPosition frameNum) {
	uint32 lBytes;
	const void *pFrame = NULL;

	if (mCachedFrame == frameNum)
		return mpFrameBuffer.get();

	if (!read(frameNum, 1, NULL, 0x7FFFFFFF, &lBytes, NULL) && lBytes) {
		auto buffer = std::make_unique<char[]>(lBytes);

		uint32 lReadBytes;

		read(frameNum, 1, buffer.get(), lBytes, &lReadBytes, NULL);
		pFrame = streamGetFrame(buffer.get(), lReadBytes, FALSE, frameNum, frameNum);
	}

	return pFrame;
}
