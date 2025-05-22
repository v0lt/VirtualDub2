//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2011 Avery Lee
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
#include <windows.h>
#include <mmsystem.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/profile.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Tessa/Context.h>
#include "FilterAccelEngine.h"
#include "FilterFrameBufferAccel.h"
#include "FilterAccelReadbackBuffer.h"

#include "FilterAccelEngine.inl"

bool VDTCreateContextD3D9(int width, int height, int refresh, bool fullscreen, bool vsync, void *hwnd, IVDTContext **ppctx);
bool VDTCreateContextD3D10(IVDTContext **ppctx);

void VDFilterAccelInterleaveYUV_SSE2(
		void *dst, ptrdiff_t dstpitch,
		const void *srcY, ptrdiff_t srcYPitch,
		const void *srcCb, ptrdiff_t srcCbPitch,
		const void *srcCr, ptrdiff_t srcCrPitch,
		uint32 w,
		uint32 h);

VDFilterAccelEngineMessage::VDFilterAccelEngineMessage()
	: mpCleanup(NULL)
	, mbRePoll(false)
	, mbCompleted(false)
	, mpCompleteSignal(NULL)
{
}

VDFilterAccelEngineDispatchQueue::VDFilterAccelEngineDispatchQueue()
	: mbActive(true)
	, mpProfChan(NULL)
{
}

void VDFilterAccelEngineDispatchQueue::Shutdown() {
	mbActive = false;
	mMessagesReady.signal();
}

void VDFilterAccelEngineDispatchQueue::SetProfilingChannel(VDRTProfileChannel *chan) {
	mpProfChan = chan;
}

void VDFilterAccelEngineDispatchQueue::Run(VDScheduler *sch) {
	HANDLE h[2];
	int n = 0;
	h[n++] = mMessagesReady.getHandle();
	if (sch)
		h[n++] = sch->getSignal()->getHandle();

	bool pollQueueEmpty = false;
	bool checkingPollQueue = false;
	bool pollScopeOpen = false;

	while(mbActive) {
		if (sch) {
			while(sch->Run())
				;
		}

		Message *msg = NULL;
		mMutex.Lock();

		switch(checkingPollQueue) {
			case true:
				if (!mPollQueue.empty()) {
					msg = mPollQueue.front();
					mPollQueue.pop_front();

					pollQueueEmpty = mPollQueue.empty();
					break;
				} else {
					pollQueueEmpty = true;
					checkingPollQueue = false;
				}

				// fall through

			case false:
				if (!mQueue.empty()) {
					msg = mQueue.front();
					mQueue.pop_front();
				}
				break;
		}

		mMutex.Unlock();

		if (!msg) {
			if (mpProfChan && !pollQueueEmpty && !pollScopeOpen) {
				pollScopeOpen = true;
				mpProfChan->Begin(0x404040, "Poll");
			}

			DWORD waitResult;

			for(;;) {
				if (pollQueueEmpty) {
					waitResult = ::MsgWaitForMultipleObjects(n, h, FALSE, INFINITE, QS_ALLEVENTS);
				} else {
					VDPROFILEBEGIN("Poll");
					waitResult = ::MsgWaitForMultipleObjects(n, h, FALSE, 1, QS_ALLEVENTS);
					VDPROFILEEND();
				}

				if (waitResult != WAIT_OBJECT_0 + n)
					break;

				MSG msg;
				while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			}

			if (waitResult == WAIT_OBJECT_0)
				checkingPollQueue = false;
			else if (waitResult == WAIT_TIMEOUT && !pollQueueEmpty)
				checkingPollQueue = true;

			continue;
		}

		if (pollScopeOpen) {
			mpProfChan->End();
			pollScopeOpen = false;
		}

		msg->mbRePoll = false;
		msg->mpCallback(this, msg);

		if (msg->mbRePoll) {
			mMutex.Lock();
			mPollQueue.push_back(msg);
			mMutex.Unlock();

			pollQueueEmpty = false;
			checkingPollQueue = false;
			continue;
		}

		checkingPollQueue = true;

		msg->mbCompleted = true;

		VDFilterAccelEngineMessage::CleanupFn cleanup = msg->mpCleanup;

		VDSignal *sig = msg->mpCompleteSignal;

		if (cleanup)
			cleanup(this, msg);

		if (sig)
			sig->signal();
	}

	if (pollScopeOpen)
		mpProfChan->End();

	for(;;) {
		Message *msg = NULL;
		mMutex.Lock();
		if (!mQueue.empty()) {
			msg = mQueue.front();
			mQueue.pop_front();
		} else if (!mPollQueue.empty()) {
			msg = mPollQueue.front();
			mPollQueue.pop_front();
		}
		mMutex.Unlock();

		if (!msg)
			break;

		VDFilterAccelEngineMessage::CleanupFn cleanup = msg->mpCleanup;

		if (cleanup)
			cleanup(this, msg);
	}
}

void VDFilterAccelEngineDispatchQueue::Post(Message *msg) {
	msg->mbCompleted = false;

	mMutex.Lock();
	mQueue.push_back(msg);
	mMutex.Unlock();

	mMessagesReady.signal();
}

static VDAtomicInt sThread = 0;
static VDAtomicInt sThread2 = 0;

void VDFilterAccelEngineDispatchQueue::Wait(Message *msg, VDFilterAccelEngineDispatchQueue& localQueue) {
	VDASSERT(!sThread2.xchg(VDGetCurrentThreadID()));

	HANDLE h[2];
	int n = 1;

	h[0] = localQueue.mSyncMessageCompleted.getHandle();

	VDSignal *sig = msg->mpCompleteSignal;
	if (sig && sig != &localQueue.mSyncMessageCompleted)
		h[n++] = sig->getHandle();

	while(!msg->mbCompleted) {
		DWORD waitResult = ::MsgWaitForMultipleObjects(n, h, FALSE, INFINITE, QS_SENDMESSAGE);
		if (waitResult == WAIT_OBJECT_0 + n) {
			MSG msg;
			while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_QS_SENDMESSAGE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			continue;
		}
	}
	VDASSERT((VDThreadID)sThread2.xchg(0) == VDGetCurrentThreadID());
}

void VDFilterAccelEngineDispatchQueue::Send(Message *msg, VDFilterAccelEngineDispatchQueue& localQueue) {

	VDASSERT(!sThread.xchg(VDGetCurrentThreadID()));
	msg->mpCompleteSignal = &localQueue.mSyncMessageCompleted;

	Post(msg);
	Wait(msg, localQueue);

	VDASSERT((VDThreadID)sThread.xchg(0) == VDGetCurrentThreadID());
}

VDFilterAccelEngine::VDFilterAccelEngine()
	: VDThread("3D accel worker")
	, mpTC(NULL)
	, mpTP(NULL)
	, mpFPConvertRGBToYUV(NULL)
	, mpFPConvertYUVToRGB(NULL)
	, mpFPNull(NULL)
	, mpFPExtractPlane(NULL)
	, mpFPClear(NULL)
	, mpVP(NULL)
	, mpVPClear(NULL)
	, mpVF(NULL)
	, mpVFC(NULL)
	, mpVB(NULL)
	, mpIB(NULL)
	, mpRS(NULL)
	, mpBS(NULL)
{
	memset(mpSamplerStates, 0, sizeof mpSamplerStates);
}

VDFilterAccelEngine::~VDFilterAccelEngine() {
	Shutdown();
}

struct VDFilterAccelEngine::InitMsg : public VDFilterAccelEngineMessage {
	VDFilterAccelEngine *mpThis;
	bool mbVisDebug;
	bool mbSuccess;
};

bool VDFilterAccelEngine::Init(bool visualDebugging) {
	mScheduler.setSignal(&mSchedulerSignal);

	if (!ThreadStart())
		return false;

	InitMsg initMsg;
	initMsg.mpCallback	= InitCallback;
	initMsg.mpThis		= this;
	initMsg.mbVisDebug	= visualDebugging;
	initMsg.mbSuccess	= false;
	SyncCall(&initMsg);
	return initMsg.mbSuccess;
}

void VDFilterAccelEngine::InitCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message) {
	InitMsg& msg = *static_cast<InitMsg *>(message);
	msg.mbSuccess = msg.mpThis->InitCallback2(msg.mbVisDebug);
}

bool VDFilterAccelEngine::InitCallback2(bool visibleDebugWindow) {
	mbVisualDebugEnabled = visibleDebugWindow;

	WNDCLASSW wc;
    wc.style			= 0;
    wc.lpfnWndProc		= StaticWndProc;
    wc.cbClsExtra		= 0;
    wc.cbWndExtra		= sizeof(VDFilterAccelEngine *);
	wc.hInstance		= VDGetLocalModuleHandleW32();
    wc.hIcon			= NULL;
    wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground	= NULL;
    wc.lpszMenuName		= NULL;

	wchar_t buf[64];
	swprintf_s(buf, L"VDFilterAccelEngine[%08p]", this);
    wc.lpszClassName	= buf;

	mWndClass = RegisterClassW(&wc);

	int dispw = 16;
	int disph = 16;

	if (visibleDebugWindow) {
		dispw = 800;
		disph = 600;

		RECT r = { 0, 0, dispw, disph };
		DWORD dwStyle = WS_OVERLAPPEDWINDOW;

		AdjustWindowRect(&r, dwStyle, FALSE);

		mhwnd = CreateWindowW(MAKEINTATOM(mWndClass), L"VirtualDub 3D filter acceleration debug window", dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, this);

		if (mhwnd)
			ShowWindow(mhwnd, SW_SHOWNOACTIVATE);
	} else {
		mhwnd = CreateWindowW(MAKEINTATOM(mWndClass), L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, wc.hInstance, this);
	}

	if (!VDTCreateContextD3D9(dispw, disph, 0, false, false, mhwnd, &mpTC)) {
		ShutdownCallback2();
		return false;
	}

	mpTC->SetGpuPriority(-1);

	VDSetThreadExecutionStateW32(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);

	mpTP = vdpoly_cast<IVDTProfiler *>(mpTC);

	static const VDTVertexElement els[]={
		{ (uint32)offsetof(VDFilterAccelVertex, x), kVDTET_Float3, kVDTEU_Position, 0 },
		{ (uint32)offsetof(VDFilterAccelVertex, uv[0]), kVDTET_Float4, kVDTEU_TexCoord, 0 },
		{ (uint32)offsetof(VDFilterAccelVertex, uv[1]), kVDTET_Float4, kVDTEU_TexCoord, 1 },
		{ (uint32)offsetof(VDFilterAccelVertex, uv[2]), kVDTET_Float4, kVDTEU_TexCoord, 2 },
		{ (uint32)offsetof(VDFilterAccelVertex, uv[3]), kVDTET_Float4, kVDTEU_TexCoord, 3 },
		{ (uint32)offsetof(VDFilterAccelVertex, uv[4]), kVDTET_Float4, kVDTEU_TexCoord, 4 },
		{ (uint32)offsetof(VDFilterAccelVertex, uv[5]), kVDTET_Float4, kVDTEU_TexCoord, 5 },
		{ (uint32)offsetof(VDFilterAccelVertex, uv[6]), kVDTET_Float4, kVDTEU_TexCoord, 6 },
		{ (uint32)offsetof(VDFilterAccelVertex, uv[7]), kVDTET_Float4, kVDTEU_TexCoord, 7 }
	};

	static const VDTVertexElement kEls2[]={
		{ (uint32)offsetof(VDFilterAccelClearVertex, x), kVDTET_Float2, kVDTEU_Position, 0 },
		{ (uint32)offsetof(VDFilterAccelClearVertex, c), kVDTET_UByte4, kVDTEU_Color, 0 },
	};

	if (!mpTC->CreateVertexBuffer(kVBSize, true, NULL, &mpVB)) {
		ShutdownCallback2();
		return false;
	}

	mVBPos = 0;

	static const uint16 kIndices[]={
		 0, 1, 4, 4, 1, 5, 1, 2, 5, 5, 2, 6, 2, 3, 6, 6, 3, 7,
		 4, 5, 8, 8, 5, 9, 5, 6, 9, 9, 6,10, 6, 7,10,10, 7,11,
		 8, 9,12,12, 9,13, 9,10,13,13,10,14,10,11,14,14,11,15,

		 0, 1, 3, 3, 1, 4, 1, 2, 4, 4, 2, 5,
		 3, 4, 6, 6, 4, 7, 4, 5, 7, 7, 5, 8,

		 0x00, 0x01, 0x02, 0x02, 0x01, 0x03, 0x04, 0x05, 0x06, 0x06, 0x05, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x09, 0x0B, 0x0C, 0x0D, 0x0E, 0x0E, 0x0D, 0x0F,
		 0x10, 0x11, 0x12, 0x12, 0x11, 0x13, 0x14, 0x15, 0x16, 0x16, 0x15, 0x17, 0x18, 0x19, 0x1A, 0x1A, 0x19, 0x1B, 0x1C, 0x1D, 0x1E, 0x1E, 0x1D, 0x1F,
		 0x20, 0x21, 0x22, 0x22, 0x21, 0x23, 0x24, 0x25, 0x26, 0x26, 0x25, 0x27, 0x28, 0x29, 0x2A, 0x2A, 0x29, 0x2B, 0x2C, 0x2D, 0x2E, 0x2E, 0x2D, 0x2F,
		 0x30, 0x31, 0x32, 0x32, 0x31, 0x33, 0x34, 0x35, 0x36, 0x36, 0x35, 0x37, 0x38, 0x39, 0x3A, 0x3A, 0x39, 0x3B, 0x3C, 0x3D, 0x3E, 0x3E, 0x3D, 0x3F,
	};

	if (!mpTC->CreateIndexBuffer(54 + 24 + 6*16, false, false, kIndices, &mpIB)) {
		ShutdownCallback2();
		return false;
	}

	VDTSamplerStateDesc ssdesc;
	ssdesc.mAddressU = kVDTAddr_Clamp;
	ssdesc.mAddressV = kVDTAddr_Clamp;
	ssdesc.mAddressW = kVDTAddr_Clamp;

	static const VDTFilterMode kFilterModes[3]={
		kVDTFilt_Point,
		kVDTFilt_Bilinear,
		kVDTFilt_BilinearMip
	};

	for(int i=0; i<2; ++i) {
		for(int j=0; j<kVDXAFiltCount; ++j) {
			ssdesc.mFilterMode = kFilterModes[j];

			if (!mpTC->CreateSamplerState(ssdesc, &mpSamplerStates[i][j])) {
				ShutdownCallback2();
				return false;
			}
		}
	}

	if (!mpTC->CreateVertexProgram(kVDTPF_D3D9ByteCode, VDTDataView(kVDFilterAccelVP), &mpVP)) {
		ShutdownCallback2();
		return false;
	}

	if (!mpTC->CreateVertexProgram(kVDTPF_D3D9ByteCode, VDTDataView(kVDFilterAccelVP_Clear), &mpVPClear)) {
		ShutdownCallback2();
		return false;
	}

	if (!mpTC->CreateVertexFormat(els, 9, mpVP, &mpVF)) {
		ShutdownCallback2();
		return false;
	}

	if (!mpTC->CreateVertexFormat(kEls2, 2, mpVPClear, &mpVFC)) {
		ShutdownCallback2();
		return false;
	}

	VDTRasterizerStateDesc rsdesc = VDTRasterizerStateDesc();
	rsdesc.mCullMode = kVDTCull_None;

	if (!mpTC->CreateRasterizerState(rsdesc, &mpRS)) {
		ShutdownCallback2();
		return false;
	}

	VDTBlendStateDesc bsdesc = VDTBlendStateDesc();
	bsdesc.mbEnable = false;

	if (!mpTC->CreateBlendState(bsdesc, &mpBS)) {
		ShutdownCallback2();
		return false;
	}

	if (!mpTC->CreateFragmentProgram(kVDTPF_D3D9ByteCode, VDTDataView(kVDFilterAccelFP_ExtractPlane), &mpFPExtractPlane)) {
		ShutdownCallback2();
		return false;
	}

	if (!mpTC->CreateFragmentProgram(kVDTPF_D3D9ByteCode, VDTDataView(kVDFilterAccelFP_RGBToYUV), &mpFPConvertRGBToYUV)) {
		ShutdownCallback2();
		return false;
	}

	if (!mpTC->CreateFragmentProgram(kVDTPF_D3D9ByteCode, VDTDataView(kVDFilterAccelFP_YUVToRGB), &mpFPConvertYUVToRGB)) {
		ShutdownCallback2();
		return false;
	}

	if (!mpTC->CreateFragmentProgram(kVDTPF_D3D9ByteCode, VDTDataView(kVDFilterAccelFP_Null), &mpFPNull)) {
		ShutdownCallback2();
		return false;
	}

	if (!mpTC->CreateFragmentProgram(kVDTPF_D3D9ByteCode, VDTDataView(kVDFilterAccelFP_Clear), &mpFPClear)) {
		ShutdownCallback2();
		return false;
	}

	IVDTProfiler *p = vdpoly_cast<IVDTProfiler *>(mpTC);
	if (p)
		mWorkerQueue.SetProfilingChannel(p->GetProfileChannel());

	return true;
}

void VDFilterAccelEngine::Shutdown() {
	if (isThreadActive()) {
		InitMsg shutdownMsg;
		shutdownMsg.mpCallback	= ShutdownCallback;
		shutdownMsg.mpThis		= this;
		SyncCall(&shutdownMsg);

		mWorkerQueue.Shutdown();
		ThreadWait();
	}
}

void VDFilterAccelEngine::ShutdownCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message) {
	static_cast<InitMsg *>(message)->mpThis->ShutdownCallback2();
}

void VDFilterAccelEngine::ShutdownCallback2() {
	mWorkerQueue.SetProfilingChannel(NULL);

	vdsaferelease <<=
		mpFPConvertYUVToRGB,
		mpFPConvertRGBToYUV,
		mpFPNull,
		mpFPExtractPlane,
		mpFPClear,
		mpIB,
		mpVB,
		mpVFC,
		mpVF,
		mpBS,
		mpRS,
		mpVP,
		mpVPClear;

	for(int j=0; j<2; ++j) {
		for(int i=0; i<kVDXAFiltCount; ++i) {
			if (mpSamplerStates[j][i]) {
				mpSamplerStates[j][i]->Release();
				mpSamplerStates[j][i] = NULL;
			}
		}
	}

	vdsaferelease <<= mpTC;

	VDSetThreadExecutionStateW32(ES_CONTINUOUS);

	if (mhwnd) {
		DestroyWindow(mhwnd);
		mhwnd = NULL;
	}

	if (mWndClass) {
		UnregisterClass(MAKEINTATOM(mWndClass), VDGetLocalModuleHandleW32());
		mWndClass = NULL;
	}
}

void VDFilterAccelEngine::SyncCall(VDFilterAccelEngineMessage *message) {
	if (VDGetCurrentThreadID() == getThreadID()) {
		if (!message->mbCompleted) {
			do {
				message->mbRePoll = false;
				message->mpCallback(&mWorkerQueue, message);
			} while(message->mbRePoll);

			message->mbCompleted = true;

			VDFilterAccelEngineMessage::CleanupFn cleanup = message->mpCleanup;

			VDSignal *sig = message->mpCompleteSignal;

			if (cleanup)
				cleanup(&mWorkerQueue, message);

			if (sig)
				sig->signal();
		}
	} else {
		mWorkerQueue.Send(message, mCallbackQueue);
	}
}

void VDFilterAccelEngine::PostCall(VDFilterAccelEngineMessage *message) {
	mWorkerQueue.Post(message);
}

void VDFilterAccelEngine::WaitForCall(VDFilterAccelEngineMessage *message) {
	mWorkerQueue.Wait(message, mCallbackQueue);
}

bool VDFilterAccelEngine::CommitBuffer(VDFilterFrameBufferAccel *buf, bool renderable) {
	if (buf->GetTexture())
		return true;

	const uint32 w = VDCeilToPow2(buf->GetWidth());
	const uint32 h = VDCeilToPow2(buf->GetHeight());

	vdrefptr<IVDTTexture2D> tex;
	if (!mpTC->CreateTexture2D(w, h, kVDTF_B8G8R8A8, 1, renderable ? kVDTUsage_Render : kVDTUsage_Default, NULL, ~tex))
		return false;

	buf->SetTexture(tex);
	return true;
}

struct VDFilterAccelEngine::DecommitMsg : public VDFilterAccelEngineMessage {
	VDFilterFrameBufferAccel *mpBuf;
};

void VDFilterAccelEngine::DecommitBuffer(VDFilterFrameBufferAccel *buf) {
	DecommitMsg msg;
	msg.mpCallback = DecommitCallback;
	msg.mpBuf = buf;

	SyncCall(&msg);
}

void VDFilterAccelEngine::DecommitCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message) {
	DecommitMsg& msg = *static_cast<DecommitMsg *>(message);

	msg.mpBuf->Decommit();
}

struct VDFilterAccelEngine::UploadMsg : public VDFilterAccelEngineMessage {
	VDFilterFrameBufferAccel *mpDst;
	VDFilterAccelEngine *mpThis;
	const char *mpData;
	const char *mpData2;
	const char *mpData3;
	ptrdiff_t mPitch;
	ptrdiff_t mPitch2;
	ptrdiff_t mPitch3;
	uint32 mWidth;
	uint32 mHeight;
	bool mbYUV;
	bool mbSuccess;
};

void VDFilterAccelEngine::Upload(VDFilterFrameBufferAccel *dst, VDFilterFrameBuffer *src, const VDPixmapLayout& srcLayout) {
	const void *srcp = src->LockRead();
	if (!srcp)
		return;

	Upload(dst, srcp, srcLayout);

	src->Unlock();
}

void VDFilterAccelEngine::Upload(VDFilterFrameBufferAccel *dst, const void *srcp, const VDPixmapLayout& srcLayout) {
	UploadMsg msg;
	msg.mpCallback = UploadCallback;
	msg.mpThis = this;
	msg.mpDst = dst;
	msg.mpData = (char *)srcp + srcLayout.data;
	msg.mpData2 = (char *)srcp + srcLayout.data2;
	msg.mpData3 = (char *)srcp + srcLayout.data3;
	msg.mPitch = srcLayout.pitch;
	msg.mPitch2 = srcLayout.pitch2;
	msg.mPitch3 = srcLayout.pitch3;
	msg.mWidth = srcLayout.w;
	msg.mHeight = srcLayout.h;
	msg.mbYUV = srcLayout.format == nsVDXPixmap::kPixFormat_YUV444_Planar;
	msg.mbSuccess = false;

	SyncCall(&msg);
}

void VDFilterAccelEngine::UploadCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message) {
	UploadMsg& msg = *static_cast<UploadMsg *>(message);
	IVDTProfiler *p = msg.mpThis->mpTP;

	if (msg.mbYUV) {
		if (p)
			p->BeginScope(0xffe0e0, "Frame upload (YUV)");

		VDPROFILEBEGIN("Upload-YUV");
	} else {
		if (p)
			p->BeginScope(0xffe0e0, "Frame upload (RGB)");
		VDPROFILEBEGIN("Upload-RGB");
	}

	if (msg.mpThis->CommitBuffer(msg.mpDst, false)) {
		IVDTTexture2D *dsttex = msg.mpDst->GetTexture();
		VDTTextureDesc desc;
		dsttex->GetDesc(desc);

		const uint32 w = msg.mWidth;
		const uint32 h = msg.mHeight;
		uint32 padw = msg.mpDst->GetBorderWidth() + 1;
		uint32 padh = msg.mpDst->GetBorderHeight() + 1;

		if (w + padw > desc.mWidth)
			padw = desc.mWidth - w;

		if (h + padh > desc.mHeight)
			padh = desc.mHeight - h;

		vdrect32 r(0, 0, w + padw, h + padh);

		VDTLockData2D lockData;
		if (dsttex->Lock(0, &r, lockData)) {
			if (w && h) {
				// copy main rectangle
				if (msg.mbYUV) {
					const char *srcY = msg.mpData;
					const char *srcCb = msg.mpData2;
					const char *srcCr = msg.mpData3;
					char *dst = (char *)lockData.mpData;

					if (SSE2_enabled) {
						VDFilterAccelInterleaveYUV_SSE2(
							dst, lockData.mPitch,
							srcY, msg.mPitch,
							srcCb, msg.mPitch2,
							srcCr, msg.mPitch3,
							w, h);
					} else {
						for(uint32 y = 0; y < h; ++y) {
							char *dst2 = dst;

							for(uint32 x = 0; x < w; ++x) {
								dst2[0] = srcCb[x];
								dst2[1] = srcY[x];
								dst2[2] = srcCr[x];
								dst2[3] = (char)0xFF;
								dst2 += 4;
							}

							srcY += msg.mPitch;
							srcCb += msg.mPitch2;
							srcCr += msg.mPitch3;
							dst += lockData.mPitch;
						}
					}
				} else {
					VDMemcpyRect(lockData.mpData, lockData.mPitch, msg.mpData, msg.mPitch, 4*w, h);
				}

				// fill right
				if (padw) {
					uint32 *dst = (uint32 *)lockData.mpData + w;
					for(uint32 y=0; y<h; ++y) {
						const uint32 fillval = dst[-1];

						for(uint32 x=0; x<padw; ++x)
							dst[x] = fillval;

						dst = (uint32 *)((char *)dst + lockData.mPitch);
					}
				}

				// fill bottom
				if (padh) {
					const char *src = (const char *)lockData.mpData + lockData.mPitch * (h - 1);
					char *dst = (char *)src + lockData.mPitch;

					for(uint32 y=0; y<padh; ++y) {
						memcpy(dst, src, 4*(w + padw));
						dst += lockData.mPitch;
					}
				}
			} else {
				VDMemset32Rect(lockData.mpData, lockData.mPitch, 0, 4*padw, padh);
			}

			msg.mbSuccess = true;
			dsttex->Unlock(0);
		}
	}

	VDPROFILEEND();
	if (p)
		p->EndScope();
}

VDFilterAccelReadbackBuffer *VDFilterAccelEngine::CreateReadbackBuffer(uint32 width, uint32 height, int format) {
	vdautoptr<VDFilterAccelReadbackBuffer> rb(new VDFilterAccelReadbackBuffer);

	if (!rb || !rb->Init(mpTC, width, height, format == nsVDXPixmap::kPixFormat_VDXA_YUV))
		return NULL;

	return rb.release();
}

void VDFilterAccelEngine::DestroyReadbackBuffer(VDFilterAccelReadbackBuffer *rb) {
	if (rb)
		delete rb;
}

bool VDFilterAccelEngine::BeginDownload(VDFilterAccelEngineDownloadMsg *msg, VDFilterFrameBuffer *dst, const VDPixmapLayout& dstLayout, VDFilterFrameBufferAccel *src, bool srcYUV, VDFilterAccelReadbackBuffer *rb) {
	char *dstp = (char *)dst->LockWrite();
	if (!dstp)
		return false;

	msg->mpCallback = DownloadCallback;
	msg->mpThis = this;
	msg->mpRB = rb;
	msg->mpSrc = src;
	msg->mpDstBuffer = dst;
	msg->mpDst[0] = (char *)dstp + dstLayout.data;
	msg->mpDst[1] = (char *)dstp + dstLayout.data2;
	msg->mpDst[2] = (char *)dstp + dstLayout.data3;
	msg->mDstPitch[0] = dstLayout.pitch;
	msg->mDstPitch[1] = dstLayout.pitch2;
	msg->mDstPitch[2] = dstLayout.pitch3;
	msg->mWidth = dstLayout.w;
	msg->mHeight = dstLayout.h;
	msg->mbSrcYUV = srcYUV;
	msg->mbDstYUV = dstLayout.format != nsVDXPixmap::kPixFormat_XRGB8888;
	msg->mbWaitingForRender = false;
	msg->mbSuccess = false;
	msg->mbDeviceLost = false;

	return true;
}

VDFilterAccelStatus VDFilterAccelEngine::EndDownload(VDFilterAccelEngineDownloadMsg *msg) {
	if (!msg->mbCompleted)
		mWorkerQueue.Wait(msg, mCallbackQueue);

	msg->mpDstBuffer->Unlock();

	return msg->mbSuccess ? kVDFilterAccelStatus_OK : msg->mbDeviceLost ? kVDFilterAccelStatus_DeviceLost : kVDFilterAccelStatus_Failed;
}

bool VDFilterAccelEngine::Download(VDFilterFrameBuffer *dst, const VDPixmapLayout& dstLayout, VDFilterFrameBufferAccel *src, bool srcYUV, VDFilterAccelReadbackBuffer *rb) {
	DownloadMsg msg;

	BeginDownload(&msg, dst, dstLayout, src, srcYUV, rb);
	SyncCall(&msg);
	EndDownload(&msg);

	return msg.mbSuccess;
}

void VDFilterAccelEngine::DownloadCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message) {
	DownloadMsg& msg = *static_cast<DownloadMsg *>(message);

	if (msg.mbWaitingForRender)
		msg.mpThis->DownloadCallback2b(msg);
	else
		msg.mpThis->DownloadCallback2a(msg);
}

void VDFilterAccelEngine::DownloadCallback2a(DownloadMsg& msg) {
	VDFilterAccelReadbackBuffer& rbo = *msg.mpRB;
	IVDTProfiler *p = mpTP;

	if (msg.mbSrcYUV) {
		if (msg.mbDstYUV) {
			if (p)
				p->BeginScope(0x8080f0, "RB-Blit (YUV->YUV)");
			VDPROFILEBEGIN("RB-Blit (YUV->YUV)");
		} else {
			if (p)
				p->BeginScope(0x8080f0, "RB-Blit (YUV->RGB)");
			VDPROFILEBEGIN("RB-Blit (YUV->RGB)");
		}
	} else {
		if (msg.mbDstYUV) {
			if (p)
				p->BeginScope(0x8080f0, "RB-Blit (RGB->YUV)");
			VDPROFILEBEGIN("RB-Blit (RGB->YUV)");
		} else {
			if (p)
				p->BeginScope(0x8080f0, "RB-Blit (RGB->RGB)");
			VDPROFILEBEGIN("RB-Blit (RGB->RGB)");
		}
	}

	IVDTTexture2D *srctex = msg.mpSrc->GetTexture();
	vdrefptr<IVDTSurface> srcsurf(srctex->GetLevelSurface(0));

	VDTTextureDesc srcdesc;
	srctex->GetDesc(srcdesc);

	IVDTSurface *const rt = rbo.GetRenderTarget();

	VDTSurfaceDesc rtdesc;
	rt->GetDesc(rtdesc);

	float invTexWidth = 1.0f / (float)srcdesc.mWidth;
	float invTexHeight = 1.0f / (float)srcdesc.mHeight;
	float invRTWidth = 1.0f / (float)rtdesc.mWidth;
	float invRTHeight = 1.0f / (float)rtdesc.mHeight;

	if (msg.mbDstYUV) {
		mpTC->SetSamplerStates(0, 1, &mpSamplerStates[false][kVDXAFilt_Point]);

		IVDTTexture *tex = srctex;
		mpTC->SetTextures(0, 1, &tex);
	}

	if (!msg.mbDstYUV) {
		// RGB destination case (direct download)
		rt->Copy(0, 0, srcsurf, 0, 0, msg.mWidth, msg.mHeight);
	} else {
		// YUV destination case (plane dethreading)
		const uint32 w = msg.mWidth;
		const uint32 h = msg.mHeight;

		mpTC->SetRenderTarget(0, rt);

		VDTSurfaceDesc desc;
		rt->GetDesc(desc);
		const VDTViewport vp = { 0, 0, desc.mWidth, desc.mHeight, 0.0f, 1.0f };
		mpTC->SetViewport(vp);

		for(uint32 plane = 0; plane < 3; ++plane) {
			static const float kExtractFPConstants[2][3][8]={
				// RGB -> YUV
				{
					{ 0.2567882f, 0.5041294f, 0.0979059f, 0.0627451f },
					{ -0.1482229f, -0.2909928f, 0.4392157f, 0.501961f },
					{ 0.4392157f, -0.3677883f, -0.0714274f, 0.501961f }
				},
				// YUV -> YUV
				{
					{ 0, 1, 0, 0 },
					{ 0, 0, 1, 0 },
					{ 1, 0, 0, 0 },
				}
			};

			mpTC->SetFragmentProgramConstF(0, 1, kExtractFPConstants[msg.mbSrcYUV][plane]);

			VDFilterAccelVertex vxs[4] = {0};
			int tilew4 = (w + 3) >> 2;
			int y = plane * h;

			vxs[0].x = -1.0f;
			vxs[0].y = 1.0f - (float)(2*y)*invRTHeight;
			vxs[1].x = (float)(2*tilew4)*invRTWidth - 1.0f;
			vxs[1].y = vxs[0].y;
			vxs[2].x = vxs[0].x;
			vxs[2].y = 1.0f - (float)(2*(y+h))*invRTHeight;
			vxs[3].x = vxs[1].x;
			vxs[3].y = vxs[2].y;

			const float us[2] = {
				0.0f,
				(float)w * invTexWidth
			};

			const float vs[2] = {
				0.0f,
				(float)h * invTexHeight
			};

			for(int i=0; i<4; ++i) {
				VDFilterAccelVertex& vx = vxs[i];
				float u = us[i & 1];
				float v = vs[i >> 1];

				static const float kUOffset[4] = {
					+1.0f / 2.0f,	// 3 of 4 -> red
					-1.0f / 2.0f,	// 2 of 4 -> green
					-3.0f / 2.0f,	// 1 of 4 -> blue
					+3.0f / 2.0f,	// 4 of 4 -> alpha
				};

				for(int j=0; j<4; ++j) {
					vx.uv[j][0] = u + kUOffset[j] * invTexWidth;
					vx.uv[j][1] = v;
				}
			}

			DrawQuads(NULL, mpFPExtractPlane, vxs, 4, sizeof(vxs[0]), VDFilterAccelEngine::kQuadPattern_1);
		}
	}

	mpTC->CloseScene();

	// wait for render to complete
	msg.mFence = mpTC->InsertFence();
	msg.mbWaitingForRender = true;
	msg.mbRePoll = true;
	msg.mbWaitingForRender = true;

	if (p)
		p->EndScope();

	VDPROFILEEND();
}

void VDFilterAccelEngine::DownloadCallback2b(DownloadMsg& msg) {
	if (!mpTC->CheckFence(msg.mFence)) {
		msg.mbRePoll = true;
		return;
	}

	do {
		IVDTProfiler *p = mpTP;
		VDFilterAccelReadbackBuffer& rbo = *msg.mpRB;

		IVDTSurface *const rt = rbo.GetRenderTarget();
		IVDTReadbackBuffer *const rb = rbo.GetReadbackBuffer();

		// do readback
		if (p) {
			p->BeginScope(0x8040c0, "Readback");
		}

		VDPROFILEBEGIN("Readback");
		bool success = rt->Readback(rb);
		VDPROFILEEND();

		if (p) {
			p->EndScope();
		}

		if (!success) {
			break;
		}

		// lock and copy planes
		if (p) {
			p->BeginScope(0x4080c0, "RB-Lock");
		}

		VDPROFILEBEGIN("RB-Lock");

		VDTLockData2D lockData;
		success = rb->Lock(lockData);

		VDPROFILEEND();

		if (p) {
			p->EndScope();
		}

		if (!success) {
			break;
		}

		if (p) {
			p->BeginScope(0x8080f0, "RB-Copy");
		}

		VDPROFILEBEGIN("RB-Copy");
		if (!msg.mbDstYUV) {
			// RGB destination case
			VDMemcpyRect(msg.mpDst[0], msg.mDstPitch[0], lockData.mpData, lockData.mPitch, 4*msg.mWidth, msg.mHeight);
		}
		else {
			// YUV destination case (plane dethreading)
			const uint32 w = msg.mWidth;
			const uint32 h = msg.mHeight;

			VDMemcpyRect(msg.mpDst[0], msg.mDstPitch[0], lockData.mpData, lockData.mPitch, w, h);
			VDMemcpyRect(msg.mpDst[1], msg.mDstPitch[1], (const char *)lockData.mpData + lockData.mPitch * h, lockData.mPitch, w, h);
			VDMemcpyRect(msg.mpDst[2], msg.mDstPitch[2], (const char *)lockData.mpData + lockData.mPitch * (h*2), lockData.mPitch, w, h);
		}

		rb->Unlock();
		VDPROFILEEND();

		if (p) {
			p->EndScope();
		}

		msg.mbSuccess = true;

	} while (false);

	if (mpTC->IsDeviceLost()) {
		msg.mbDeviceLost = true;
	}
}

struct VDFilterAccelEngine::ConvertMsg : public VDFilterAccelEngineMessage {
	VDFilterAccelEngine *mpThis;
	VDFilterFrameBufferAccel *mpDst;
	VDFilterFrameBufferAccel *mpSrc;
	vdrect32	mSrcRect;
	bool mbSrcYUV;
	bool mbDstYUV;
	bool mbSuccess;
};

bool VDFilterAccelEngine::Convert(VDFilterFrameBufferAccel *dst, const VDPixmapLayout& dstLayout, VDFilterFrameBufferAccel *src, const VDPixmapLayout& srcLayout, const vdrect32& srcRect) {
	ConvertMsg msg;
	msg.mpCallback = ConvertCallback;
	msg.mpThis = this;
	msg.mpDst = dst;
	msg.mpSrc = src;
	msg.mbSrcYUV = srcLayout.format == nsVDXPixmap::kPixFormat_VDXA_YUV;
	msg.mbDstYUV = dstLayout.format == nsVDXPixmap::kPixFormat_VDXA_YUV;
	msg.mSrcRect = srcRect;
	msg.mbSuccess = false;

	SyncCall(&msg);

	return msg.mbSuccess;
}

void VDFilterAccelEngine::ConvertCallback(VDFilterAccelEngineDispatchQueue *queue, VDFilterAccelEngineMessage *message) {
	ConvertMsg& msg = *static_cast<ConvertMsg *>(message);
	msg.mpThis->ConvertCallback2(msg);
}

void VDFilterAccelEngine::ConvertCallback2(ConvertMsg& msg) {
	IVDTProfiler *p = mpTP;

	if (p)
		p->BeginScope(0xffffe0, "Frame convert");

	if (!CommitBuffer(msg.mpDst, true)) {
		if (p)
			p->EndScope();
		return;
	}

	IVDTTexture2D *srctex = msg.mpSrc->GetTexture();
	IVDTTexture2D *dsttex = msg.mpDst->GetTexture();
	vdrefptr<IVDTSurface> dstsurf(dsttex->GetLevelSurface(0));

	mpTC->SetRenderTarget(0, dstsurf);
	mpTC->SetSamplerStates(0, 1, &mpSamplerStates[false][kVDXAFilt_Point]);

	IVDTTexture *tex = srctex;
	mpTC->SetTextures(0, 1, &tex);

	VDTTextureDesc srcdesc;
	srctex->GetDesc(srcdesc);

	VDTTextureDesc dstdesc;
	dsttex->GetDesc(dstdesc);

	const int imgw = msg.mpDst->GetWidth();
	const int imgh = msg.mpDst->GetHeight();
	const int borw = msg.mpDst->GetBorderWidth();
	const int borh = msg.mpDst->GetBorderHeight();
	float invTexWidth = 1.0f / (float)srcdesc.mWidth;
	float invTexHeight = 1.0f / (float)srcdesc.mHeight;
	float invRTWidth = 1.0f / (float)dstdesc.mWidth;
	float invRTHeight = 1.0f / (float)dstdesc.mHeight;

	const float xs[4] = {
		0,
		0.5f,
		(float)imgw - 0.5f,
		(float)(imgw + borw)
	};

	const float ys[4] = {
		0,
		0.5f,
		(float)imgh - 0.5f,
		(float)(imgh + borh)
	};

	const float u0 = (msg.mSrcRect.left + 0.5f) * invTexWidth;
	const float u1 = (msg.mSrcRect.right - 0.5f) * invTexWidth;
	const float v0 = (msg.mSrcRect.top + 0.5f) * invTexHeight;
	const float v1 = (msg.mSrcRect.bottom - 0.5f) * invTexHeight;

	VDFilterAccelVertex vxs[16] = {0};
	for(int i=0; i<16; ++i) {
		VDFilterAccelVertex *vx = &vxs[i];
		int xi = i & 3;
		int yi = i >> 2;

		vx->x = (float)(2 * xs[xi] - 1) * invRTWidth - 1.0f;
		vx->y = 1.0f - (float)(2 * ys[yi] - 1) * invRTHeight;
		vx->uv[0][0] = (xi >= 2) ? u1 : u0;
		vx->uv[0][1] = (yi >= 2) ? v1 : v0;
	}

	IVDTFragmentProgram *fp = mpFPNull;
	if (msg.mbSrcYUV != msg.mbDstYUV) {
		if (msg.mbSrcYUV)
			fp = mpFPConvertYUVToRGB;
		else
			fp = mpFPConvertRGBToYUV;
	}

	msg.mbSuccess = DrawQuads(NULL, fp, vxs, 16, sizeof(vxs[0]), kQuadPattern_4x4);

	if (p)
		p->EndScope();
}

void VDFilterAccelEngine::UpdateProfilingDisplay() {
	if (mbVisualDebugEnabled) {
		mpTC->SetRenderTarget(0, NULL);
		mpTC->Clear(kVDTClear_Color, 0x101010, 0.0f, 0);
		mpTC->Present();
	}
}

void VDFilterAccelEngine::SetSamplerState(uint32 index, bool wrap, VDXAFilterMode filter) {
	mpTC->SetSamplerStates(index, 1, &mpSamplerStates[wrap][filter]);
}

bool VDFilterAccelEngine::DrawQuads(IVDTVertexFormat *vf, IVDTFragmentProgram *fp, const void *vxs, uint32 vxcount, uint32 vxstride, QuadPattern pattern) {
	const uint32 vxsize = vxcount * vxstride;

	if (mVBPos + vxsize > kVBSize)
		mVBPos = 0;

	if (!mpVB->Load(mVBPos, vxsize, vxs))
		return false;

	mpTC->SetVertexFormat(vf ? vf : mpVF);
	mpTC->SetVertexStream(0, mpVB, mVBPos, vxstride);
	mpTC->SetIndexStream(mpIB);
	mpTC->SetVertexProgram(mpVP);
	mpTC->SetFragmentProgram(fp);
	mpTC->SetRasterizerState(mpRS);
	mpTC->SetBlendState(mpBS);

	uint32 indexStart = 0;
	uint32 primCount = 0;

	switch(pattern) {
		case kQuadPattern_1:
			indexStart = 78;
			primCount = 2;
			break;

		case kQuadPattern_3x3:
			indexStart = 54;
			primCount = 8;
			break;

		case kQuadPattern_4x4:
			indexStart = 0;
			primCount = 18;
			break;
	}

	mpTC->DrawIndexedPrimitive(kVDTPT_Triangles, 0, 0, vxcount, indexStart, primCount);

	mVBPos += vxsize;
	return true;
}

bool VDFilterAccelEngine::FillRects(uint32 rectCount, const VDXRect *rects, uint32 colorARGB, uint32 w, uint32 h, uint32 bw, uint32 bh) {
	if (!rects) {
		if (rectCount)
			mpTC->Clear(kVDTClear_Color, colorARGB, 0, 0);

		return true;
	}

	mpTC->SetVertexFormat(mpVFC);
	mpTC->SetIndexStream(mpIB);
	mpTC->SetRasterizerState(mpRS);
	mpTC->SetBlendState(mpBS);
	mpTC->SetVertexProgram(mpVPClear);
	mpTC->SetFragmentProgram(mpFPClear);

	IVDTSurface *rt = mpTC->GetRenderTarget(0);

	VDTSurfaceDesc rtdesc;
	rt->GetDesc(rtdesc);

	const float xscale = 2.0f / (float)rtdesc.mWidth;
	const float xoffset = -1.0f - 0.5f*xscale;
	const float yscale = -2.0f / (float)rtdesc.mHeight;
	const float yoffset = 1.0f - 0.5f*yscale;

	const uint8 c[4] = {
		(uint8)(colorARGB >> 16),
		(uint8)(colorARGB >>  8),
		(uint8)(colorARGB >>  0),
		(uint8)(colorARGB >> 24)
	};

	VDFilterAccelClearVertex vx[4*16];
	VDFilterAccelClearVertex *dst = vx;
	uint32 queuedRects = 0;

	while(rectCount) {
		int x1 = rects->left;
		int y1 = rects->top;
		int x2 = rects->right;
		int y2 = rects->bottom;
		++rects;
		--rectCount;

		// clip rect
		if (x1 < 0)
			x1 = 0;

		if (y1 < 0)
			y1 = 0;

		if (x2 > w)
			x2 = w;

		if (y2 > h)
			y2 = h;

		// extend rect borders
		if (x2 >= w)
			x2 = w + bw;

		if (y2 >= h)
			y2 = h + bh;

		// write vertices
		dst[0].x = x1 * xscale + xoffset;
		dst[0].y = y1 * yscale + yoffset;
		dst[0].c[0] = c[0];
		dst[0].c[1] = c[1];
		dst[0].c[2] = c[2];
		dst[0].c[3] = c[3];

		dst[1].x = dst[0].x;
		dst[1].y = y2 * yscale + yoffset;
		dst[1].c[0] = c[0];
		dst[1].c[1] = c[1];
		dst[1].c[2] = c[2];
		dst[1].c[3] = c[3];

		dst[2].x = x2 * xscale + xoffset;
		dst[2].y = dst[0].y;
		dst[2].c[0] = c[0];
		dst[2].c[1] = c[1];
		dst[2].c[2] = c[2];
		dst[2].c[3] = c[3];

		dst[3].x = dst[2].x;
		dst[3].y = dst[1].y;
		dst[3].c[0] = c[0];
		dst[3].c[1] = c[1];
		dst[3].c[2] = c[2];
		dst[3].c[3] = c[3];

		dst += 4;
		++queuedRects;

		if (queuedRects >= 16 || !rectCount) {
			uint32 vertCount = queuedRects*4;
			uint32 vertBytes = vertCount * sizeof(VDFilterAccelClearVertex);

			if (mVBPos + vertBytes > kVBSize)
				mVBPos = 0;

			mpTC->SetVertexStream(0, mpVB, mVBPos, sizeof(VDFilterAccelClearVertex));

			if (!mpVB->Load(mVBPos, vertBytes, vx))
				return false;

			mpTC->DrawIndexedPrimitive(kVDTPT_Triangles, 0, 0, vertCount, 78, queuedRects*2);
			mVBPos += vertBytes;

			queuedRects = 0;
		}
	}

	return true;
}

void VDFilterAccelEngine::ThreadRun() {
	uint32 precision = 0;

	TIMECAPS tc;
	if (TIMERR_NOERROR == timeGetDevCaps(&tc, sizeof tc)) {
		if (TIMERR_NOERROR == timeBeginPeriod(tc.wPeriodMin))
			precision = tc.wPeriodMin;
	}

	mWorkerQueue.Run(&mScheduler);
	ShutdownCallback2();

	if (precision)
		timeEndPeriod(precision);
}

VDZLPARAM VDZCALLBACK VDFilterAccelEngine::StaticWndProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_NCCREATE:
			SetWindowLongPtr(hwnd, 0, (LONG_PTR)((LPCREATESTRUCT)lParam)->lpCreateParams);
			break;

		case WM_PAINT:
			{
				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(hwnd, &ps);
				if (hdc) {
					EndPaint(hwnd, &ps);
				}
			}
			return 0;

		case WM_CLOSE:
			return 0;

#if 0
		case WM_TIMER:
			{
				VDFilterAccelEngine *pThis = (VDFilterAccelEngine *)GetWindowLongPtr(hwnd, 0);

				if (pThis)
					pThis->UpdateProfilingDisplay();
			}
			return 0;
#endif
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}
