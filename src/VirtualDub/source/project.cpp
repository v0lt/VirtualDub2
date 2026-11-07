// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2004 Avery Lee
// Copyright (C) 2015-2020 Anton Shekhovtsov
// Copyright (C) 2024-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include <stdafx.h>

#include <vd2/system/filesys.h>
#include <vd2/system/file.h>
#include <vd2/system/thread.h>
#include <vd2/system/atomic.h>
#include <vd2/system/time.h>
#include <vd2/system/strutil.h>
#include <vd2/system/VDScheduler.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <vd2/Dita/resources.h>
#include <vd2/Riza/bitmap.h>
#include "project.h"
#include "VideoSource.h"
#include "AudioSource.h"
#include "InputFile.h"
#include "prefs.h"
#include "dub.h"
#include "DubOutput.h"
#include "DubStatus.h"
#include "filter.h"
#include "command.h"
#include "job.h"
#include "jobControl.h"
#include "server.h"
#include "capture.h"
#include "script.h"
#include "SceneDetector.h"
#include "oshelper.h"
#include "resource.h"
#include "uiframe.h"
#include "filters.h"
#include "FilterFrameRequest.h"
#include "gui.h"
#include "tool.h"

///////////////////////////////////////////////////////////////////////////

namespace {
	enum {
		kVDST_Project = 9
	};

	enum {
		kVDM_ReopenChangesImminent,			// The new video file has fewer video frames than the current file. Switching to it will result in changes to the edit list. Do you want to continue?
		kVDM_DeleteFrame,					// delete frame %lld (Undo/Redo)
		kVDM_DeleteFrames,					// delete %lld frames at %lld (Undo/Redo)
		kVDM_CutFrame,						// cut frame %lld (Undo/Redo)
		kVDM_CutFrames,						// cut %lld frames at %lld (Undo/Redo)
		kVDM_MaskFrame,						// mask frame %lld (Undo/Redo)
		kVDM_MaskFrames,					// mask %lld frames at %lld (Undo/Redo)
		kVDM_Paste,							// paste (Undo/Redo)
		kVDM_ScanForErrors,					// scan for errors
		kVDM_ResetTimeline,					// reset timeline
		kVDM_Crop							// crop
	};

	enum {
		kUndoLimit = 50,
		kRedoLimit = 50
	};
}

///////////////////////////////////////////////////////////////////////////

extern const wchar_t g_szError[];
extern const wchar_t g_szWarning[];

extern HINSTANCE g_hInst;

extern VDProject *g_project;
extern InputFileOptions	*g_pInputOpts;
extern COMPVARS2 g_Vcompression;

DubSource::ErrorMode	g_videoErrorMode			= DubSource::kErrorModeReportAll;
DubSource::ErrorMode	g_audioErrorMode			= DubSource::kErrorModeReportAll;

vdrefptr<AudioSource>	inputAudio;
vdrefptr<AudioSource>	inputAudio2;

extern bool				g_fDropFrames;
extern bool				g_fDropSeeking;
extern bool				g_fSwapPanes;
extern bool				g_bExit;

extern bool g_fJobMode;

extern wchar_t g_szInputAVIFile[MAX_PATH];
extern wchar_t g_szInputWAVFile[MAX_PATH];

extern char g_serverName[256];

extern int VDPreferencesGetPreviewPriority();
extern int VDPreferencesGetDubPriority();
extern int VDPreferencesGetSceneCutThreshold();
extern int VDPreferencesSceneFadeThreshold();
extern uint32 VDPreferencesGetRenderThrottlePercent();
extern int VDPreferencesGetVideoCompressionThreadCount();
extern bool VDPreferencesGetFilterAccelEnabled();
extern bool VDPreferencesGetRenderBackgroundPriority();
extern bool VDPreferencesGetAutoRecoverEnabled();
extern bool VDPreferencesGetTimelineWarnReloadTruncation();

int VDRenderSetVideoSourceInputFormat(IVDVideoSource *vsrc, VDPixmapFormatEx format);

extern bool FiltersEditorDisplayFrame(IVDVideoSource *pVS);

///////////////////////////////////////////////////////////////////////////

namespace {

	void CopyFrameToClipboard(HWND hwnd, const VDPixmap& px) {
		if (OpenClipboard(hwnd)) {
			if (EmptyClipboard()) {
				VDPixmapLayout layout;
				uint32 imageSize = VDMakeBitmapCompatiblePixmapLayout(layout, px.w, px.h, nsVDPixmap::kPixFormat_RGB888, 0);

				vdstructex<VDAVIBitmapInfoHeader> bih;
				VDMakeBitmapFormatFromPixmapFormat(bih, nsVDPixmap::kPixFormat_RGB888, 0, px.w, px.h);

				uint32 headerSize = bih.size();

				HANDLE hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, headerSize + imageSize);
				if (hMem) {
					void *lpvMem = GlobalLock(hMem);
					if (lpvMem) {
						memcpy(lpvMem, bih.data(), headerSize);

						VDPixmap dst = VDPixmapFromLayout(layout, (char *)lpvMem + headerSize);
						//VDPixmapBlt(dst, px);
						IVDPixmapBlitter* blt = VDPixmapCreateBlitter(dst,px);
						blt->Blit(dst,px);
						delete blt;

						GlobalUnlock(lpvMem);
						SetClipboardData(CF_DIB, hMem);
						CloseClipboard();
						return;
					}
					GlobalFree(hMem);
				}
			}
			CloseClipboard();
		}
	}
}

///////////////////////////////////////////////////////////////////////////

VDProjectAutoSave::VDProjectAutoSave()
{
}

VDProjectAutoSave::~VDProjectAutoSave() {
	Delete();
}

void VDProjectAutoSave::Save(VDProject *proj) {
	VDStringW fileName;
	VDStringW path;

	const uint32 signature = VDCreateAutoSaveSignature();
	for(int counter = 1; counter <= 100; ++counter) {
		fileName.sprintf(L"VirtualDub_AutoSave_%x_%u.vdproject", signature, counter);

		path = VDMakePath(VDGetDataPath(), fileName.c_str());

		if (!VDDoesPathExist(path.c_str()))
			break;

		path.clear();
	}

	if (path.empty())
		return;

	try {
		// Note that we intentionally KEEP THIS FILE OPEN. This prevents other instances of VirtualDub
		// from trying to recover while we're active!
		mAutoSaveFile.open(path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		proj->SaveData(path,mDataSubdir);
		proj->SaveScript(mAutoSaveFile,mDataSubdir,false);

		mAutoSaveFile.flushNT();
	} catch(...) {
		return;
	}

	mAutoSavePath = path;
}

void VDProjectAutoSave::Delete() {
	try {
		if (!mAutoSavePath.empty()) {
			mAutoSaveFile.seek(0);
			mAutoSaveFile.truncate();
			mAutoSaveFile.close();

			VDRemoveFile(mAutoSavePath.c_str());
			VDProject::DeleteData(mAutoSavePath,mDataSubdir);
			mAutoSavePath.clear();
			mDataSubdir.clear();
		}
	} catch(...) {
		// whatever it is, eat it -- we do NOT want to terminate due to
		// throwing from the dtor.
	}
}

void VDProjectAutoSave::Load(VDProject *proj) {
	if (!mAutoSavePath.empty()) {
		mAutoSaveFile.close();
		proj->OpenProject(mAutoSavePath.c_str(), true);
		VDRemoveFile(mAutoSavePath.c_str());
		VDProject::DeleteData(mAutoSavePath,mDataSubdir);
		mAutoSavePath.clear();
		mDataSubdir.clear();
	}
}

///////////////////////////////////////////////////////////////////////////

class VDFilterSystemMessageLoopScheduler : public vdrefcounted<IVDFilterSystemScheduler> {
public:
	VDFilterSystemMessageLoopScheduler();

	void Reschedule();
	bool Block();

protected:
	DWORD		mThreadId;
};

VDFilterSystemMessageLoopScheduler::VDFilterSystemMessageLoopScheduler()
	: mThreadId(::GetCurrentThreadId())
{
}

void VDFilterSystemMessageLoopScheduler::Reschedule() {
	PostThreadMessageW(mThreadId, WM_NULL, 0, 0);
}

bool VDFilterSystemMessageLoopScheduler::Block() {
	return true;
}

///////////////////////////////////////////////////////////////////////////

class VDProjectTimelineTimingSource : public vdrefcounted<IVDTimelineTimingSource> {
public:
	VDProjectTimelineTimingSource(IVDTimelineTimingSource *pTS, VDProject *pProject);
	~VDProjectTimelineTimingSource();

	sint64 GetStart();
	sint64 GetLength();
	const VDFraction GetRate();
	sint64 GetPrevKey(sint64 pos);
	sint64 GetNextKey(sint64 pos);
	sint64 GetNearestKey(sint64 pos);
	bool IsKey(sint64 pos);
	bool IsNullSample(sint64 pos);

protected:
	bool CheckFilters();

	vdrefptr<IVDTimelineTimingSource> mpTS;
	VDProject *mpProject;
};

VDProjectTimelineTimingSource::VDProjectTimelineTimingSource(IVDTimelineTimingSource *pTS, VDProject *pProject)
	: mpTS(pTS)
	, mpProject(pProject)
{
}

VDProjectTimelineTimingSource::~VDProjectTimelineTimingSource() {
}

sint64 VDProjectTimelineTimingSource::GetStart() {
	return 0;
}

sint64 VDProjectTimelineTimingSource::GetLength() {
	return CheckFilters() ? filters.GetOutputFrameCount() : mpTS->GetLength();
}

const VDFraction VDProjectTimelineTimingSource::GetRate() {
	return CheckFilters() ? filters.GetOutputFrameRate() : mpTS->GetRate();
}

sint64 VDProjectTimelineTimingSource::GetPrevKey(sint64 pos) {
	if (!CheckFilters())
		return mpTS->GetPrevKey(pos);

	while(--pos >= 0) {
		sint64 pos2 = filters.GetSourceFrame(pos);
		if (pos2 < 0 || mpTS->IsKey(pos2))
			break;
	}

	return pos;
}

sint64 VDProjectTimelineTimingSource::GetNextKey(sint64 pos) {
	if (!CheckFilters())
		return mpTS->GetNextKey(pos);

	sint64 len = filters.GetOutputFrameCount();

	while(++pos < len) {
		sint64 pos2 = filters.GetSourceFrame(pos);
		if (pos2 < 0 || mpTS->IsKey(pos2))
			return pos;
	}

	return -1;
}

sint64 VDProjectTimelineTimingSource::GetNearestKey(sint64 pos) {
	if (!CheckFilters())
		return mpTS->GetPrevKey(pos);

	while(pos >= 0) {
		sint64 pos2 = filters.GetSourceFrame(pos);
		if (pos2 < 0 || mpTS->IsKey(pos2))
			break;
		--pos;
	}

	return pos;
}

bool VDProjectTimelineTimingSource::IsKey(sint64 pos) {
	if (!CheckFilters())
		return mpTS->IsKey(pos);

	sint64 pos2 = filters.GetSourceFrame(pos);
	return pos2 < 0 || mpTS->IsKey(pos2);
}

bool VDProjectTimelineTimingSource::IsNullSample(sint64 pos) {
	if (!CheckFilters())
		return mpTS->IsKey(pos);

	sint64 pos2 = filters.GetSourceFrame(pos);
	return pos2 >= 0 && mpTS->IsNullSample(pos2);
}

bool VDProjectTimelineTimingSource::CheckFilters() {
	if (filters.isRunning())
		return true;

	try {
		mpProject->StartFilters();
	} catch(const MyError&) {
		// eat the error for now
	}

	return filters.isRunning();
}

///////////////////////////////////////////////////////////////////////////

VDProject::VDProject()
	: mhwnd(NULL)
	, mpCB(NULL)
	, mpSceneDetector(0)
	, mShuttleMode(0)
	, mSceneShuttleMode(0)
	, mStickyShuttle(false)
	, mSceneShuttleAdvance(0)
	, mSceneShuttleCounter(0)
	, mpDubStatus(0)
	, mposCurrentFrame(0)
	, mposSelectionStart(0)
	, mposSelectionEnd(0)
	, mposZoomStart(0)
	, mposZoomEnd(0)
	, mbZoomEnabled(false)
	, mbPositionCallbackEnabled(false)
	, mbFilterChainLocked(false)
	, mbTimelineRateDirty(true)
	, mDesiredOutputFrame(-1)
	, mDesiredTimelineFrame(-1)
	, mDesiredNextInputFrame(-1)
	, mDesiredNextOutputFrame(-1)
	, mDesiredNextTimelineFrame(-1)
	, mLastDisplayedInputFrame(-1)
	, mLastDisplayedTimelineFrame(-1)
	, mbUpdateInputFrame(false)
	, mbPendingInputFrameValid(false)
	, mbPendingOutputFrameValid(false)
	, mbUpdateLong(false)
	, mFramesDecoded(0)
	, mLastDecodeUpdate(0)
	, mDisplayFrameLocks(0)
	, mbDisplayFrameDeferred(false)
	, mPreviewRestartMode(kPreviewRestart_None)
	, mVideoInputFrameRate(0,0)
	, mVideoOutputFrameRate(0,0)
	, mVideoTimelineFrameRate(0,0)
	, mVideoMarkedPostFiltersFrameRate(0,0)
	, mVideoMarkedPostFiltersFrameCount(0)
	, mAudioSourceMode(kVDAudioSourceMode_Source)
{
	filterModTimeline.project = this;
	mProjectLoading = false;
	mAudioSplit = false;
}

VDProject::~VDProject() {
#if 0
	// We have to issue prestops first to make sure that all threads have
	// spinners running.
	for(int i=0; i<mThreadCount; ++i)
		mpSchedulerThreads[i].PreStop();

	delete[] mpSchedulerThreads;
#endif
}

bool VDProject::Attach(VDGUIHandle hwnd) {
	mhwnd = hwnd;
	return true;
}

void VDProject::Detach() {
	if (mpSceneDetector) {
		delete mpSceneDetector;
		mpSceneDetector = NULL;
	}

	if (mpDubStatus) {
		delete mpDubStatus;
		mpDubStatus = 0;
	}

	mhwnd = NULL;
}

void VDProject::SetUICallback(IVDProjectUICallback *pCB) {
	mpCB = pCB;
}

void VDProject::BeginTimelineUpdate(const wchar_t *undostr) {
	if (!undostr)
		ClearUndoStack();
	else {
		if (mUndoStack.size()+1 > kUndoLimit)
			mUndoStack.pop_back();

		UndoEntry ue(undostr);
		SaveUndo(ue);
		mUndoStack.push_front(ue);
	}

	mRedoStack.clear();
}

void VDProject::EndTimelineUpdate() {
	UpdateDubParameters();
	if (mpCB) {
		mpCB->UITimelineUpdated();
		mpCB->UICurrentPositionUpdated();
	}
}

void VDProject::BeginLoading() {
	mProjectLoading = true;
}

void VDProject::EndLoading() {
	if (mProjectLoading)
		ClearUndoStack();
	mProjectLoading = false;
	MoveToFrame(mposZoomStart);
	EndTimelineUpdate();
	UpdateFilterList();
}

void VDProject::SaveUndo(UndoEntry& ue) {
	ue.mSubset = mTimeline.GetSubset();
	ue.SetSelection(mposCurrentFrame, mposSelectionStart, mposSelectionEnd);
	ue.SetZoom(mposZoomStart, mposZoomEnd);
}

void VDProject::LoadUndo(UndoEntry& ue) {
	mTimeline.GetSubset() = ue.mSubset;
	SetSelection(ue.mSelStart, ue.mSelEnd, false);
	mposZoomStart = ue.mZoomStart;
	mposZoomEnd = ue.mZoomEnd;
}

bool VDProject::Undo() {
	if (mUndoStack.empty())
		return false;

	UndoEntry& ue = mUndoStack.front();
	UndoEntry re(ue.mDescription.c_str());
	SaveUndo(re);
	LoadUndo(ue);
	EndTimelineUpdate();
	MoveToFrame(ue.mFrame);

	if (mRedoStack.size()+1 > kRedoLimit)
		mRedoStack.pop_back();

	mRedoStack.insert(mRedoStack.begin(), re);
	mUndoStack.pop_front();

	return true;
}

bool VDProject::Redo() {
	if (mRedoStack.empty())
		return false;

	UndoEntry& re = mRedoStack.front();
	UndoEntry ue(re.mDescription.c_str());
	SaveUndo(ue);
	LoadUndo(re);
	EndTimelineUpdate();
	MoveToFrame(re.mFrame);

	if (mUndoStack.size()+1 > kUndoLimit)
		mUndoStack.pop_back();

	mUndoStack.insert(mUndoStack.begin(), ue);
	mRedoStack.pop_front();

	return true;
}

void VDProject::ClearUndoStack() {
	mUndoStack.clear();
	mRedoStack.clear();
}

const wchar_t *VDProject::GetCurrentUndoAction() {
	if (mUndoStack.empty())
		return NULL;

	const UndoEntry& ue = mUndoStack.front();

	return ue.mDescription.c_str();
}

const wchar_t *VDProject::GetCurrentRedoAction() {
	if (mRedoStack.empty())
		return NULL;

	const UndoEntry& ue = mRedoStack.front();

	return ue.mDescription.c_str();
}

bool VDProject::Tick() {
	bool active = false;

	if (inputVideo && mSceneShuttleMode) {
		if (!mpSceneDetector)
			mpSceneDetector = new_nothrow SceneDetector();

		if (mpSceneDetector) {
			mpSceneDetector->SetThresholds(VDPreferencesGetSceneCutThreshold(), VDPreferencesSceneFadeThreshold());

			SceneShuttleStep();
			active = true;
		} else
			SceneShuttleStop();
	} else {
		if (mpSceneDetector) {
			delete mpSceneDetector;
			mpSceneDetector = NULL;
		}

	  if (inputVideo && mShuttleMode) {
		  SceneShuttleStep();
		  active = true;
	  }
  }

	if (UpdateFrame())
		active = true;

	return active;
}

VDPosition VDProject::GetCurrentFrame() {
	return mposCurrentFrame;
}

VDPosition VDProject::GetFrameCount() {
	return mTimeline.GetLength();
}

VDFraction VDProject::GetInputFrameRate() {
	return mVideoInputFrameRate;
}

VDFraction VDProject::GetTimelineFrameRate() {
	VDASSERT(!mbTimelineRateDirty);
	return mVideoTimelineFrameRate;
}

void VDProject::ClearSelection(bool notifyUser) {
	mposSelectionStart = 0;
	mposSelectionEnd = -1;
	g_dubOpts.video.mSelectionStart.mOffset = 0;
	g_dubOpts.video.mSelectionEnd.mOffset = -1;
	if (mpCB)
		mpCB->UISelectionUpdated(notifyUser);
}

bool VDProject::IsSelectionEmpty() {
	return mposSelectionStart >= mposSelectionEnd;
}

bool VDProject::IsSelectionPresent() {
	return mposSelectionStart <= mposSelectionEnd;
}

VDPosition VDProject::GetSelectionStartFrame() {
	return mposSelectionStart;
}

VDPosition VDProject::GetSelectionEndFrame() {
	return mposSelectionEnd;
}

bool VDProject::IsClipboardEmpty() {
	return mClipboard.empty();
}

bool VDProject::IsSceneShuttleRunning() {
	return (mSceneShuttleMode != 0) || (mShuttleMode != 0);
}

void VDProject::SetPositionCallbackEnabled(bool enable) {
	mbPositionCallbackEnabled = enable;
}

void VDProject::Cut() {
	Copy();
	DeleteInternal(true, false);
}

void VDProject::Copy() {
	VDPosition pos = GetCurrentFrame();
	VDPosition start = GetSelectionStartFrame();
	VDPosition end = GetSelectionEndFrame();

	FrameSubset& s = mTimeline.GetSubset();
	VDPosition len = 1;

	if (IsSelectionEmpty())
		start = pos;
	else
		len = end-start;

	mClipboard.assign(s, start, len);
}

void VDProject::Paste() {
	FrameSubset& s = mTimeline.GetSubset();

	BeginTimelineUpdate(VDLoadString(0, kVDST_Project, kVDM_Paste));
	if (!IsSelectionEmpty())
		DeleteInternal(false, true);
	VDPosition start = mposCurrentFrame;
	VDPosition len = mClipboard.getTotalFrames();
	s.insert(start, mClipboard);

	if (mbZoomEnabled) {
		if (start<mposZoomStart || start>mposZoomEnd) {
			ClearZoomRange();
		} else {
			mposZoomEnd += len;
		}
	}

	EndTimelineUpdate();
	MoveToFrame(start);
}

void VDProject::Delete() {
	DeleteInternal(false, false);
}

void VDProject::DeleteInternal(bool tagAsCut, bool noTag) {
	VDPosition pos = GetCurrentFrame();
	VDPosition start = GetSelectionStartFrame();
	VDPosition end = GetSelectionEndFrame();

	FrameSubset& s = mTimeline.GetSubset();
	VDPosition len = 1;

	if (IsSelectionEmpty())
		start = pos;
	else
		len = end-start;

	if (!noTag) {
		if (tagAsCut) {
			if (len > 1)
				BeginTimelineUpdate(VDswprintf(VDLoadString(0, kVDST_Project, kVDM_CutFrames), 2, &start, &len).c_str());
			else
				BeginTimelineUpdate(VDswprintf(VDLoadString(0, kVDST_Project, kVDM_CutFrame), 1, &start).c_str());
		} else {
			if (len > 1)
				BeginTimelineUpdate(VDswprintf(VDLoadString(0, kVDST_Project, kVDM_DeleteFrames), 2, &start, &len).c_str());
			else
				BeginTimelineUpdate(VDswprintf(VDLoadString(0, kVDST_Project, kVDM_DeleteFrame), 1, &start).c_str());
		}
	}

	s.deleteRange(start, len);

	if (mbZoomEnabled) {
		if (start<mposZoomStart || start+len>mposZoomEnd) {
			ClearZoomRange();
		} else if (start==mposZoomStart && start+len==mposZoomEnd) {
			ClearZoomRange();
		} else {
			mposZoomEnd -= len;
		}
	}

	if (!noTag)
		EndTimelineUpdate();

	ClearSelection(false);
	MoveToFrame(start);
}

void VDProject::CropToSelection() {
	FrameSubset& s = mTimeline.GetSubset();

	if (!IsSelectionEmpty()) {
		VDPosition start = GetSelectionStartFrame();
		VDPosition end = GetSelectionEndFrame();

		BeginTimelineUpdate(VDLoadString(0, kVDST_Project, kVDM_Crop));
		s.clip(start, end-start);
		EndTimelineUpdate();

		ClearSelection(false);
		ClearZoomRange();
		MoveToFrame(0);
	}
}

void VDProject::MaskSelection(bool bNewMode) {
	VDPosition pos = GetCurrentFrame();
	VDPosition start = GetSelectionStartFrame();
	VDPosition end = GetSelectionEndFrame();

	FrameSubset& s = mTimeline.GetSubset();
	VDPosition len = 1;

	if (IsSelectionEmpty())
		start = pos;
	else
		len = end-start;

	if (len) {
		if (len > 1)
			BeginTimelineUpdate(VDswprintf(VDLoadString(0, kVDST_Project, kVDM_MaskFrames), 2, &start, &len).c_str());
		else
			BeginTimelineUpdate(VDswprintf(VDLoadString(0, kVDST_Project, kVDM_MaskFrame), 1, &start).c_str());

		s.setRange(start, len, bNewMode, 0);

		EndTimelineUpdate();
	}
}

void VDProject::LockDisplayFrame() {
	++mDisplayFrameLocks;
}

void VDProject::UnlockDisplayFrame() {
	VDASSERT(mDisplayFrameLocks > 0);

	if (mDisplayFrameLocks) {
		--mDisplayFrameLocks;

		if (mbDisplayFrameDeferred) {
			mbDisplayFrameDeferred = false;

			DisplayFrame();
		}
	}
}

void VDProject::DisplayFrame(bool bDispInput, bool bDispOutput, bool forceInput, bool forceOutput) {
	VDPosition pos = mposCurrentFrame;
	VDPosition timeline_pos = pos;

	if (!mpCB)
		return;

	if (!inputVideo)
		return;

	if (g_dubber)
		return;

	if (mDisplayFrameLocks) {
		mbDisplayFrameDeferred = true;
		return;
	}

	const bool showInputFrame = bDispInput && (g_dubOpts.video.fShowInputFrame || forceInput);
	const bool showOutputFrame = bDispOutput && (g_dubOpts.video.fShowOutputFrame || forceOutput);
	if (!showInputFrame && !showOutputFrame)
		return;

	try {
		sint64 outpos = mTimeline.TimelineToSourceFrame(pos);

		if (!g_filterChain.IsEmpty() && outpos >= 0) {
			if (!filters.isRunning()) {
				StartFilters();
			}

			pos = filters.GetSourceFrame(outpos);
		} else {
			pos = outpos;
		}

		IVDStreamSource *pVSS = inputVideo->asStream();
		if (pos < 0)
			pos = pVSS->getEnd();

		bool updateRequired = false;

		if (showInputFrame && mLastDisplayedInputFrame != pos) {
			mLastDisplayedInputFrame = pos;
			updateRequired = true;
		}

		if (showOutputFrame && mLastDisplayedTimelineFrame != timeline_pos) {
			mLastDisplayedTimelineFrame = timeline_pos;
			updateRequired = true;
		}

		if (updateRequired) {
			if (pos >= pVSS->getEnd()) {
				mDesiredOutputFrame = -1;
				mDesiredNextInputFrame = -1;
				mDesiredNextOutputFrame = -1;

				mpPendingInputFrame = NULL;
				mpCurrentInputFrame = NULL;
				mpPendingOutputFrame = NULL;
				mpCurrentOutputFrame = NULL;
				mbPendingInputFrameValid = false;
				mbPendingOutputFrameValid = false;

				if (showInputFrame)
					mpCB->UIRefreshInputFrame(NULL);
				if (showOutputFrame)
					mpCB->UIRefreshOutputFrame(NULL);
			} else {
				mDesiredOutputFrame = outpos;
				mDesiredTimelineFrame = timeline_pos;
				mDesiredNextInputFrame = -1;
				mDesiredNextOutputFrame = -1;
				mDesiredNextTimelineFrame = -1;

				mbUpdateInputFrame	= bDispInput;
				mbUpdateLong		= false;
				mFramesDecoded		= 0;
				mLastDecodeUpdate	= VDGetCurrentTick();

				UpdateTimelineRate();

				if (!filters.isRunning())
					StartFilters();

				if (filters.isRunning()) {
					if (showInputFrame)
						mpVideoFrameSource->CreateRequest(pos, false, 0, ~mpPendingInputFrame);

					if (showOutputFrame) {
						if (mDesiredOutputFrame >= 0) {
							filters.RequestFrame(mDesiredOutputFrame, 0, ~mpPendingOutputFrame);
						} else {
							mpPendingOutputFrame = NULL;
						}

						mbPendingOutputFrameValid = true;
					}
				}

				mbUpdateInputFrame = true;

				UpdateFrame();
			}
		}

	} catch(const MyError& e) {
		const wchar_t* src = e.gets();
		wchar_t* dst = _wcsdup(src);

		if (!dst)
			guiSetStatus(e.gets(), 255);
		else {
			for (wchar_t* t = dst; *t; ++t) {
				if (*t == L'\n') {
					*t = L' ';
				}
			}

			guiSetStatus(dst, 255);
			free(dst);
		}
		SceneShuttleStop();
	}
}

bool VDProject::UpdateFrame(bool updateInputFrame) {
	if (!mpCB)
		return false;

	if (!inputVideo) {
		if (mbUpdateLong)
			guiSetStatus("", 255);

		mDesiredOutputFrame = -1;
		mDesiredTimelineFrame = -1;
		mDesiredNextInputFrame = -1;
		mDesiredNextOutputFrame = -1;
		mDesiredNextTimelineFrame = -1;
		return false;
	}

	uint32 startTime = VDGetCurrentTick();

	bool workCompleted;
	bool runToEnd = false;

	try {
		for(;;) {
			workCompleted = false;

			if (mpPendingInputFrame && mpPendingInputFrame->IsCompleted()) {
				mpCurrentInputFrame = mpPendingInputFrame;
				mpPendingInputFrame = NULL;

				if (mpCB && updateInputFrame) {
					if (mpCurrentInputFrame->IsSuccessful()) {
						VDFilterFrameBuffer *buf = mpCurrentInputFrame->GetResultBuffer();
						VDPixmap px(VDPixmapFromLayout(filters.GetInputLayout(), (void *)buf->LockRead()));
						px.info = buf->info;
						mpCB->UIRefreshInputFrame(&px);
						buf->Unlock();
					} else {
						mpCB->UIRefreshInputFrame(NULL);
					}
				}
			}

			if (mbPendingOutputFrameValid) {
				if (mpPendingOutputFrame)
					workCompleted = (filters.Run(NULL, false) != FilterSystem::kRunResult_Idle);

				if (!mpPendingOutputFrame || mpPendingOutputFrame->IsCompleted()) {
					mpCurrentOutputFrame = mpPendingOutputFrame;
					mpPendingOutputFrame = NULL;
					mbPendingOutputFrameValid = false;

					if (mbUpdateLong) {
						guiSetStatus("", 255);
						mbUpdateLong = false;
					}

					mDesiredOutputFrame = mDesiredNextOutputFrame;
					mDesiredTimelineFrame = mDesiredNextTimelineFrame;
					mDesiredNextInputFrame = -1;
					mDesiredNextOutputFrame = -1;
					mFramesDecoded = 0;

					if (mpCB) {
						if (!mpCurrentOutputFrame) {
							mpCB->UIRefreshOutputFrame(NULL);
						} else if (mpCurrentOutputFrame->IsSuccessful()) {
							VDFilterFrameBuffer *buf = mpCurrentOutputFrame->GetResultBuffer();
							VDPixmap px(VDPixmapFromLayout(filters.GetOutputLayout(), (void *)buf->LockRead()));
							px.info = buf->info;
							mpCB->UIRefreshOutputFrame(&px);
							buf->Unlock();
						} else {
							const VDFilterFrameRequestError *err = mpCurrentOutputFrame->GetError();

							throw MyError("%s", err ? err->mError.c_str() : "An unknown error occurred during filter processing.");
						}
					}

					workCompleted = true;
				}
			}

			bool gotSource = false;

			if (mpVideoFrameSource) {
				loop:
				IVDFilterFrameSource::RunResult rr = mpVideoFrameSource->RunRequests(NULL,0);
				switch(rr) {
					case IVDFilterFrameSource::kRunResult_Running:
					case IVDFilterFrameSource::kRunResult_IdleWasActive:
					case IVDFilterFrameSource::kRunResult_BlockedWasActive:
						workCompleted = true;
						if (!mpVideoFrameSource->IsPreroll())
							gotSource = true;
						break;
				}
				if (rr!=IVDFilterFrameSource::kRunResult_Idle && gotSource)
					goto loop;
			}

			if (!workCompleted)
				return false;

			uint32 nCurrentTime = VDGetCurrentTick();

			if (gotSource) {
				startTime = nCurrentTime;
				if (!g_fDropSeeking) runToEnd = true;
			}

			if (nCurrentTime - mLastDecodeUpdate > 500) {
				mLastDecodeUpdate = nCurrentTime;
				mbUpdateLong = true;

				if (mpPendingOutputFrame)
					guiSetStatus("Decoding frame %lu...", 255, (unsigned long)mpPendingOutputFrame->GetFrameNumber());
			}

			if (nCurrentTime - startTime > 100) {
				if (!gotSource && !runToEnd) break;
			}
		}
	} catch(const MyError& e) {
		guiSetStatus(L"%s", 255, e.gets());

		SceneShuttleStop();
		mDesiredOutputFrame = -1;
		mDesiredTimelineFrame = -1;
		mDesiredNextInputFrame = -1;
		mDesiredNextOutputFrame = -1;
		mDesiredNextTimelineFrame = -1;
	}

	return workCompleted;
}

bool VDProject::RefilterFrame(VDPosition timelinePos) {
	if (!inputVideo)
        return false;

	try {
		StartFilters();
		if (!filters.isRunning())
			return false;
	} catch(const MyError& e) {
		guiSetStatus(L"%s", 255, e.gets());
		return false;
	}

	VDPosition outputFrame = mTimeline.TimelineToSourceFrame(timelinePos);

	if (outputFrame >= 0) {
		filters.InvalidateCachedFrames(NULL);

		filters.RequestFrame(outputFrame, 0, ~mpPendingOutputFrame);
		mbPendingOutputFrameValid = true;

		while(UpdateFrame())
			;
	} else {
		mpPendingOutputFrame = NULL;
		mpCurrentOutputFrame = NULL;
		mbPendingOutputFrameValid = false;
	}

	if (mpCurrentOutputFrame == NULL) {
		if (mpCB)
			mpCB->UIRefreshOutputFrame(NULL);
		return false;
	} else {
		if (mpCB) {
			VDFilterFrameBuffer *buf = mpCurrentOutputFrame->GetResultBuffer();
			VDPixmap px(VDPixmapFromLayout(filters.GetOutputLayout(), (void *)buf->LockRead()));
			px.info = buf->info;
			mpCB->UIRefreshOutputFrame(&px);
			buf->Unlock();
		}
		return true;
	}
}

void VDProject::LockFilterChain(bool enableLock) {
	mbFilterChainLocked = enableLock;
}

///////////////////////////////////////////////////////////////////////////

void VDProject::SaveScript(VDFile& f, const VDStringW& dataSubdir, bool relative) {
	if (inputAVI)
		JobWriteProjectScript(f, this, relative, dataSubdir, &g_dubOpts, g_szInputAVIFile, mInputDriverName.c_str(), inputAVI->GetFileFlags(), &inputAVI->listFiles);
	else
		JobWriteProjectScript(f, this, false, VDStringW(), &g_dubOpts, 0, 0, 0, 0);
}

void VDProject::Quit() {
	VDUIFrame *pFrame = VDUIFrame::GetFrame((HWND)mhwnd);

	if (VDINLINEASSERT(pFrame))
		pFrame->Destroy();
}

void VDProject::Reopen() {
	if (!inputAVI) return;
	VDStringW filename(VDGetFullPath(g_szInputAVIFile));
	try {
		InnerReopen();
	} catch (MyError& e) {
		if (!VDToolsHandleFileOpenError(filename.c_str(), 0, e)) throw;
	}
}

void VDProject::Reopen(FileNameCommand* cmd) {
	VDStringW filename = cmd->fileName;
	if (!filename.empty()) {
		size_t l = filename.length();
		if (l > 10) {
			if (!_wcsicmp(filename.c_str() + l - 10, L".vdproject")) {
				OpenProject(filename.c_str());
				return;
			}
		}
		if (!inputAVI) {
			Open(filename.c_str(),0,0,1);
			return;
		}
	}

	try {
		InnerReopen();
	} catch (MyError& e) {
		if (!VDToolCatchError(cmd,e)) throw;
	}
}

void VDProject::CmdOpen(const wchar_t *token) {
	VDStringW filename(VDGetFullPath(token));
	size_t l = filename.length();
	if (l > 10) {
		if (!_wcsicmp(filename.c_str() + l - 10, L".vdproject")) {
			OpenProject(filename.c_str());
			return;
		}
	}

	mProjectFilename.clear();
	Open(token,0,0,1);
}

void VDProject::OpenProject(const wchar_t *pFilename, bool readOnly) {
	try {
		VDJobQueue jproject;
		jproject.LoadProject(pFilename);
		if (jproject.ListSize()==1) {
			VDJob* job = jproject.ListGet(0);
			VDStringA subDir(job->GetProjectSubdir());
			SaveProjectPath(VDStringW(pFilename), VDTextU8ToW(subDir), readOnly);
			mProjectName = job->GetName();
			VDStringW base = VDFileSplitPathLeft(mProjectFilename);
			VDStringW back = VDTextU8ToW(job->GetProjectDir());
			if (back != base) {
				mProjectBack = back;
			}
			RunScriptMemory(job->GetScript(), job->GetScriptLine(), true);
			mProjectBack = L"";
		}
	} catch (MyTextError& e) {
		if (!VDToolsHandleFileOpenError(pFilename, L"vdproject", e, e.line)) {
			throw;
		}
	}
}

void VDProject::OpenJob(const wchar_t *pFilename, VDJob* job) {
	VDStringA subDir(job->GetProjectSubdir());
	SaveProjectPath(VDStringW(pFilename), VDTextU8ToW(subDir), true);
	RunScriptMemory(job->GetScript(), job->GetScriptLine(), true);
}

void VDProject::SaveProjectPath(const VDStringW& path, const VDStringW& dataSubdir, bool readOnly) {
	mProjectReadonly = readOnly;
	mProjectFilename = path;
	mProjectSubdir = dataSubdir;
	mProjectBack = L"";
}

VDStringA CreateDataPrefix() {
	char buf[4];
	static char list[] = "0123456789abcdefghijkmnpqrstuvwxyz";
	for(int i=0; i<4; i++) {
		int x = rand() % 34;
		buf[i] = list[x];
	}

	return VDStringA(buf,4);
}

bool ComparePrefix(const VDStringW& name, const VDStringA& prefix) {
	int size = prefix.length();
	if (name.length()<size+2)
		return false;
	if (name[0]!='.')
		return false;
	if (name[size+1]!='.')
		return false;
	{for(int i=0; i<size; i++){
		if (name[i+1]!=prefix[i])
			return false;
	}}
	return true;
}

bool LooksLikePrefix(const VDStringW& name) {
	int size = 4;
	if (name.length()<size+2)
		return false;
	if (name[0]!='.')
		return false;
	if (name[size+1]!='.')
		return false;
	return true;
}

void EmptyDataDirectory(const VDStringW& dst) {
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dst+L"\\*.*").c_str(),&fd);
	if (h!=INVALID_HANDLE_VALUE) {
		do {
			VDStringW name(fd.cFileName);
			if(name==L".") continue;
			if(name==L"..") continue;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				continue;
			} else {
				DeleteFileW((dst+L"\\"+name).c_str());
			}

		} while(FindNextFileW(h,&fd));
		FindClose(h);
	}
}

bool CleanupDataDir(const VDStringW& dst, const VDStringW& src, vdfastvector<FilterInstance*>& used_prefix) {
	bool empty = true;

	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((src+L"\\*.*").c_str(),&fd);
	if (h!=INVALID_HANDLE_VALUE) {
		do {
			VDStringW name(fd.cFileName);
			if(name==L".") continue;
			if(name==L"..") continue;

			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				empty = false;
				continue;
			} else {
				bool used = false;
				{for(FilterInstance** p=used_prefix.begin(); p<used_prefix.end(); p++){
					if (ComparePrefix(name,(*p)->fmProject.dataPrefix)) {
						used = true;
						break;
					}
				}}
				if (!LooksLikePrefix(name))
					used = true;

				if (used && !dst.empty())
					CopyFileW((src+L"\\"+name).c_str(),(dst+L"\\"+name).c_str(),false);

				if (!used && dst.empty())
					DeleteFileW((src+L"\\"+name).c_str());

				if (used)
					empty = false;
			}

		} while(FindNextFileW(h,&fd));
		FindClose(h);
	}

	return empty;
}

void VDProject::DeleteData(const VDStringW& path, const VDStringW& dataSubdir) {
	if (!dataSubdir.empty()) {
		VDStringW src = VDFileSplitPathLeft(path);
		src += dataSubdir;
		EmptyDataDirectory(src);
		if (VDDoesPathExist(src.c_str()))
			VDRemoveDirectory(src.c_str());
	}
}

void VDProject::DeleteProject(const VDStringW& path) {
	VDJobQueue jproject;
	jproject.LoadProject(path.c_str());
	if (jproject.ListSize()==1) {
		VDJob* job = jproject.ListGet(0);
		VDStringA subDir(job->GetProjectSubdir());
		DeleteData(path, VDTextU8ToW(subDir));
	}
	VDRemoveFile(path.c_str());
}

void VDProject::SaveData(const VDStringW& path, VDStringW& dataSubdir, bool make_unique) const {
	JobFlushFilterConfig();

	vdfastvector<FilterInstance*> used_prefix;
	bool data_empty = true;

	{for(VDFilterChainDesc::Entries::const_iterator it(g_filterChain.mEntries.begin()), itEnd(g_filterChain.mEntries.end()); it != itEnd; ++it) {
		FilterInstance *fa = (*it)->mpInstance;
		if (!fa->fmProject.dataPrefix.empty())
			used_prefix.push_back(fa);
		if (!fa->fmProject.data.empty())
			data_empty = false;
	}}

	bool move_project = !VDFileIsPathEqual(path.c_str(), mProjectFilename.c_str());
	bool alloc_dir = move_project;
	if (!move_project) {
		if (mProjectSubdir.empty() && !data_empty) alloc_dir = true;
	}

	if (!alloc_dir) {
		dataSubdir = mProjectSubdir;
		if (!dataSubdir.empty()) {
			VDStringW src = VDFileSplitPathLeft(mProjectFilename);
			src += dataSubdir;
			if (!CleanupDataDir(VDStringW(), src, used_prefix)) data_empty = false;
			if (data_empty) {
				if (VDDoesPathExist(src.c_str()))
					VDRemoveDirectory(src.c_str());
				dataSubdir.clear();
			}
		}
	} else {
		VDStringW base2 = VDFileSplitPathLeft(path);
		if (dataSubdir.empty())
			dataSubdir = VDFileSplitExtLeft(VDFileSplitPathRight(path));

		if (make_unique) {
			const uint32 signature = VDCreateAutoSaveSignature();
			for(int counter = 1; counter <= 100; ++counter) {
				VDStringW s;
				s.sprintf(L"_%x_%u", signature, counter);
				VDStringW test = base2+dataSubdir+s+L".vd";
				if (!VDDoesPathExist(test.c_str())){
					dataSubdir += s;
					break;
				}
			}
		}

		dataSubdir += L".vd";
		VDStringW dst = base2+dataSubdir;
		if ((!data_empty || !mProjectSubdir.empty()) && !VDDoesPathExist(dst.c_str()))
			VDCreateDirectory(dst.c_str());

		EmptyDataDirectory(dst);

		if (!mProjectSubdir.empty()) {
			VDStringW src = VDFileSplitPathLeft(mProjectFilename);
			src += mProjectSubdir;
			if (!CleanupDataDir(dst, src, used_prefix)) data_empty = false;
		}

		if (data_empty) {
			if (VDDoesPathExist(dst.c_str()))
				VDRemoveDirectory(dst.c_str());
			dataSubdir.clear();
		}
	}

	if (!data_empty) {
		VDStringW dst = VDFileSplitPathLeft(path);
		dst += dataSubdir;

		{for(VDFilterChainDesc::Entries::const_iterator it(g_filterChain.mEntries.begin()), itEnd(g_filterChain.mEntries.end()); it != itEnd; ++it) {
			FilterInstance *fa = (*it)->mpInstance;
			if (!fa->fmProject.data.empty()) {
				if (fa->fmProject.dataPrefix.empty()) {
					VDStringA s;
					while (1) {
						s = CreateDataPrefix();
						bool failed = false;
						{for(FilterInstance** p=used_prefix.begin(); p<used_prefix.end(); p++){
							if ((*p)->fmProject.dataPrefix==s) {
								failed = true;
								break;
							}
						}}
						if (!failed) break;
					}

					fa->fmProject.dataPrefix = s;
					used_prefix.push_back(fa);
				}

				VDStringW name = dst;
				name += L"\\.";
				name += VDTextU8ToW(fa->fmProject.dataPrefix);
				name += L".";

				{for(FilterModProject::Data** p=fa->fmProject.data.begin(); p<fa->fmProject.data.end(); p++){
					VDStringW filename = name + (*p)->id;
					size_t size = (*p)->data.size();
					if (size==0) {
						DeleteFileW(filename.c_str());
					} else {
						VDFile file;
						file.open(filename.c_str(),nsVDFile::kWrite|nsVDFile::kCreateAlways);
						file.write((*p)->data.begin(),size);
					}
				}}
			}
		}}
	}
}

VDStringW VDProject::BuildProjectPath(const wchar_t* path) const {
	if (!mProjectFilename.empty()) {
		VDStringW base = VDFileSplitPathLeft(mProjectFilename);

		if (!mProjectSubdir.empty()) {
			VDStringW data = base;
			data += mProjectSubdir;
			VDStringW rel_data = VDFileGetRelativePath(data.c_str(),path,false);
			if (!rel_data.empty())
				return VDStringW(L"$(DATA)\\") + rel_data;
		}

		VDStringW rel_base = VDFileGetRelativePath(base.c_str(),path,true);
		if (!rel_base.empty())
			return VDStringW(L"$(PROJECT)\\") + rel_base;
	}

	return VDStringW(path);
}

VDStringW VDFileResolvePath2(const VDStringW& base, const VDStringW& back, const wchar_t *path) {
	VDStringW r1 = VDFileResolvePath(base.c_str(),path);
	if (!VDDoesPathExist(r1.c_str()) && !back.empty()) {
		VDStringW r2 = VDFileResolvePath(back.c_str(),path);
		if (VDDoesPathExist(r2.c_str()))
			return r2;
	}
	return r1;
}

VDStringW VDProject::ExpandProjectPath(const wchar_t* path) const {
	if (!mProjectFilename.empty()) {
		VDStringW base = VDFileSplitPathLeft(mProjectFilename);

		if (wcsncmp(path,L"$(PROJECT)",10)==0)
			return VDFileResolvePath2(base,mProjectBack,path+10);

		if (wcsncmp(path,L"$(DATA)",7)==0) {
			VDStringW data = base + mProjectSubdir;
			VDStringW back = mProjectBack; if (!back.empty()) back += mProjectSubdir;
			return VDFileResolvePath2(data,back,path+7);
		}
	}

	return VDGetFullPath(path);
}

void VDProject::Open(const wchar_t *pFilename, IVDInputDriver *pSelectedDriver, int open_flags, int fAutoscan, const char *pInputOpts, uint32 inputOptsLen) {
	Close();
	VDStringW filename(ExpandProjectPath(pFilename));

	try {
		// attempt to determine input file type
		if (!pSelectedDriver) {
			pSelectedDriver = VDAutoselectInputDriverForFile(filename.c_str(), IVDInputDriver::kF_Video);
			mInputDriverName.clear();
		} else {
			mInputDriverName = pSelectedDriver->GetSignatureName();
		}

		// open file

		int flags = 0;
		if (open_flags & f_open_quiet) flags |= IVDInputDriver::kOF_Quiet;
		if (fAutoscan==1) flags |= IVDInputDriver::kOF_AutoSegmentScan;
		if (fAutoscan==0) flags |= IVDInputDriver::kOF_SingleFile;

		inputAVI = pSelectedDriver->CreateInputFile(flags);
		if (!inputAVI) throw MyMemoryError();
		g_inputDriver = pSelectedDriver->GetSignatureName();

		// Extended open?
		bool fExtendedOpen = open_flags & f_open_extended;
		if (!(pSelectedDriver->GetFlags() & IVDInputDriver::kF_SupportsOpts))
			fExtendedOpen = false;

		if (pInputOpts) {
			g_pInputOpts = inputAVI->createOptions(pInputOpts, inputOptsLen);
			if (!g_pInputOpts)
				throw MyError("Options to input driver are invalid.");
		} else if (fExtendedOpen || (pSelectedDriver->GetFlags() & IVDInputDriver::kF_PromptForOpts)) {
			g_pInputOpts = inputAVI->promptForOptions(mhwnd);
			if (!g_pInputOpts)
				throw MyUserAbortError();
		}

		if (g_pInputOpts)
			inputAVI->setOptions(g_pInputOpts);

		inputAVI->Init(filename.c_str());

		mInputAudioSources.clear();

		{
			vdrefptr<AudioSource> pTempAS;
			for(int i=0; inputAVI->GetAudioSource(i, ~pTempAS); ++i) {
				mInputAudioSources.push_back(pTempAS);
				pTempAS->setDecodeErrorMode(g_audioErrorMode);
			}
		}

		if (!inputAVI->GetVideoSource(0, ~inputVideo)) {
			throw MyError(L"File \"%s\" does not have a video stream.", filename.c_str());
		}


		VDRenderSetVideoSourceInputFormat(inputVideo, g_dubOpts.video.mInputFormat);

		IVDStreamSource *pVSS = inputVideo->asStream();
		pVSS->setDecodeErrorMode(g_videoErrorMode);
		if (!pVSS->GetProfileComment())
			pVSS->SetProfileComment(VDTextWToA(pSelectedDriver->GetSignatureName()).c_str());

		// How many items did we get?

		{
			InputFilenameNode *pnode = inputAVI->listFiles.AtHead();
			InputFilenameNode *pnode_next;
			int nFiles = 0;

			while(pnode_next = pnode->NextFromHead()) {
				++nFiles;
				pnode = pnode_next;
			}

			guiSetStatus("", 255);

			if (nFiles > 1)
				guiSetStatus(L"Autoloaded %d segments (last was \"%s\")", 255, nFiles, pnode->NextFromTail()->name);

			if (nFiles==1 && fAutoscan==2) {
				int flags = inputAVI->GetFileFlags();
				if (flags==-1 || !(flags & IVDInputDriver::kFF_Sequence))
					AppendAVIAutoscan(filename.c_str(), true);
			}
		}

		// Retrieve info text

		inputAVI->GetTextInfo(mTextInfo);

		// Set current filename

		wcscpy(g_szInputAVIFile, filename.c_str());

		vdrefptr<IVDTimelineTimingSource> pTS;
		VDCreateTimelineTimingSourceVS(inputVideo, ~pTS);
		pTS = new VDProjectTimelineTimingSource(pTS, this);
		mTimeline.SetTimingSource(pTS);
		mTimeline.SetFromSource();
		mTimeline.ClearMarker();

		// invalidate currently displayed frames
		mLastDisplayedInputFrame = -1;
		mLastDisplayedTimelineFrame = -1;

		ClearSelection(false);
		ClearZoomRange();
		mpCB->UITimelineUpdated();

		if (mAudioSourceMode >= kVDAudioSourceMode_Source)
			mAudioSourceMode = kVDAudioSourceMode_Source;
		SetAudioSource();
		UpdateDubParameters();
		mpCB->UISourceFileUpdated(open_flags);
		mpCB->UIVideoSourceUpdated();
		mpCB->UIAudioSourceUpdated();
		FiltersEditorDisplayFrame(inputVideo);
		MoveToFrame(0);
	} catch(const MyError& e) {
		Close();
		if (pSelectedDriver) {
			const wchar_t* driver_name = pSelectedDriver->GetSignatureName();
			if (VDToolsHandleFileOpenError(filename.c_str(), driver_name, e)) {
				throw MyError();
			}
		}
		throw;
	}

	VDToolsHandleFileOpen(g_szInputAVIFile, pSelectedDriver);
}

void VDProject::InnerReopen() {
	// attempt to determine input file type

	VDStringW filename(VDGetFullPath(g_szInputAVIFile));
	VDStringA options;

	IVDInputDriver *pSelectedDriver;
	if (mInputDriverName.empty()) {
		pSelectedDriver = VDAutoselectInputDriverForFile(filename.c_str(), IVDInputDriver::kF_Video);
	} else {
		pSelectedDriver = VDGetInputDriverByName(mInputDriverName.c_str());
		if (!pSelectedDriver)
			throw MyError(L"The input driver \"%s\" is no more available.", mInputDriverName.c_str());
		if (g_pInputOpts) {
			int len = g_pInputOpts->write(0,0);
			options.resize(len);
			g_pInputOpts->write(&options[0],len);
		}
	}

	// open file

	int flags = IVDInputDriver::kOF_SingleFile;
	int inputFlags = inputAVI->GetFileFlags();
	if (inputFlags!=-1 && (inputFlags & IVDInputDriver::kFF_Sequence)) flags &= ~IVDInputDriver::kOF_SingleFile;

	vdrefptr<InputFile> newInput(pSelectedDriver->CreateInputFile(flags));
	if (!newInput)
		throw MyMemoryError();

	// Extended open?

	InputFileOptions* newOptions = 0;
	if (!options.empty()) {
		newOptions = newInput->createOptions(options.c_str(),options.length());
		newInput->setOptions(newOptions);
	}

	// Open new source

	newInput->Init(filename.c_str());

	vdrefptr<IVDVideoSource> pVS;
	vdrefptr<AudioSource> pAS;
	newInput->GetVideoSource(0, ~pVS);

	VDRenderSetVideoSourceInputFormat(pVS, g_dubOpts.video.mInputFormat);

	IVDStreamSource *pVSS = pVS->asStream();
	pVSS->setDecodeErrorMode(g_videoErrorMode);

	// Check for an irrevocable change to the edit list. Irrevocable changes will occur if
	// there are any ranges other than the last that extend beyond the new length.

	const VDPosition oldFrameCount = inputVideo->asStream()->getLength();
	const VDPosition newFrameCount = pVS->asStream()->getLength();

	FrameSubset& fs = mTimeline.GetSubset();

	if (newFrameCount < oldFrameCount && VDPreferencesGetTimelineWarnReloadTruncation()) {
		FrameSubset::const_iterator it(fs.begin()), itEnd(fs.end());

		if (it != itEnd) {
			--itEnd;

			for(; it!=itEnd; ++it) {
				const FrameSubsetNode& fsn = *it;

				if (fsn.start + fsn.len > newFrameCount) {
					sint64 oldCount = oldFrameCount;
					sint64 newCount = newFrameCount;

					VDStringW msg(VDswprintf(VDLoadString(0, kVDST_Project, kVDM_ReopenChangesImminent), 2, &newCount, &oldCount));

					if (IDCANCEL == MessageBoxW((HWND)mhwnd, msg.c_str(), g_szError, MB_OKCANCEL)) {
						return;
					}

					break;
				}
			}
		}
	}

	// Swap the sources.

	if (g_pInputOpts) delete g_pInputOpts;
	g_pInputOpts = newOptions;

	inputAudio = NULL;
	inputAudio2 = NULL;
	inputAVI = newInput;
	inputVideo = pVS;

	mInputAudioSources.clear();

	{
		vdrefptr<AudioSource> pTempAS;
		for(int i=0; inputAVI->GetAudioSource(i, ~pTempAS); ++i) {
			mInputAudioSources.push_back(pTempAS);
			pTempAS->setDecodeErrorMode(g_audioErrorMode);
		}
	}

	wcscpy(g_szInputAVIFile, filename.c_str());

	// Update vars.

	vdrefptr<IVDTimelineTimingSource> pTS;
	VDCreateTimelineTimingSourceVS(inputVideo, ~pTS);
	pTS = new VDProjectTimelineTimingSource(pTS, this);
	mTimeline.SetTimingSource(pTS);

	ClearUndoStack();

	if (oldFrameCount > newFrameCount)
		fs.trimInputRange(newFrameCount);
	else if (oldFrameCount < newFrameCount)
		fs.addRangeMerge(oldFrameCount, newFrameCount - oldFrameCount, false, 0);

	mpCB->UITimelineUpdated();
	SetAudioSource();
	UpdateDubParameters();
	mpCB->UISourceFileUpdated(f_open_skip_mru);
	mpCB->UIAudioSourceUpdated();
	mpCB->UIVideoSourceUpdated();
	FiltersEditorDisplayFrame(inputVideo);

	// Invalidate currently displayed frames.
	mLastDisplayedInputFrame = -1;
	mLastDisplayedTimelineFrame = -1;

	if (newFrameCount < oldFrameCount) {
		if (!IsSelectionEmpty() && mposSelectionEnd > newFrameCount)
			SetSelectionEnd(newFrameCount, false);

		if (mbZoomEnabled && mposZoomEnd > newFrameCount)
			ClearZoomRange();

		if (mposCurrentFrame > newFrameCount)
			MoveToFrame(newFrameCount);
	}

	// redisplay current frame
	DisplayFrame();

	mpCB->UICurrentPositionUpdated();

	guiSetStatus(L"Reloaded \"%s\" (%I64d frames).", 255, filename.c_str(), newFrameCount);
}

void VDProject::OpenWAV(const wchar_t *szFile, IVDInputDriver *pSelectedDriver, bool automated, bool extOpts, const void *optdata, int optlen) {
	if (!pSelectedDriver) {
		pSelectedDriver = VDAutoselectInputDriverForFile(szFile, IVDInputDriver::kF_Audio);
		mAudioInputDriverName.clear();
	} else {
		mAudioInputDriverName = pSelectedDriver->GetSignatureName();
	}
	mpAudioInputOptions = NULL;

	vdrefptr<InputFile> ifile(pSelectedDriver->CreateInputFile(IVDInputDriver::kOF_Quiet));
	if (!ifile)
		throw MyMemoryError();

	if (pSelectedDriver) {
		if (pSelectedDriver->GetFlags() & IVDInputDriver::kF_PromptForOpts)
			extOpts = true;
	}

	if (!(pSelectedDriver->GetFlags() & IVDInputDriver::kF_SupportsOpts))
		extOpts = false;

	if (optdata) {
		mpAudioInputOptions = ifile->createOptions(optdata, optlen);
		if (mpAudioInputOptions)
			ifile->setOptions(mpAudioInputOptions);
	} else if (!automated && extOpts) {
		mpAudioInputOptions = ifile->promptForOptions((VDGUIHandle)mhwnd);
		if (!mpAudioInputOptions)
			throw MyUserAbortError();

		ifile->setOptions(mpAudioInputOptions);

		// force input driver name if we have options, since they have to match
		if (mAudioInputDriverName.empty())
			mAudioInputDriverName = pSelectedDriver->GetSignatureName();
	}

	ifile->Init(szFile);

	vdrefptr<AudioSource> pNewAudio;
	if (!ifile->GetAudioSource(0, ~pNewAudio)) {
		throw MyError(L"The file \"%s\" does not contain an audio track.", szFile);
	}

	pNewAudio->setDecodeErrorMode(g_audioErrorMode);

	vdwcslcpy(g_szInputWAVFile, szFile, std::size(g_szInputWAVFile));

	mAudioSourceMode = kVDAudioSourceMode_External;
	inputAudio = mpInputAudioExt = pNewAudio;
	SetAudioSource();
	if (mpCB)
		mpCB->UIAudioSourceUpdated();
}

void VDProject::CloseWAV() {
	mpAudioInputOptions = NULL;

	if (mpInputAudioExt) {
		if (inputAudio == mpInputAudioExt) {
			inputAudio = NULL;
			inputAudio2 = NULL;
			mAudioSourceMode = kVDAudioSourceMode_None;
		}
		mpInputAudioExt = NULL;
	}
}

void VDProject::PreviewInput(VDPosition* restart) {
	UpdateTimelineRate();

	VDPosition start = restart ? *restart : GetCurrentFrame();
	DubOptions dubOpt(g_dubOpts);

	LONG preload = inputAudio && inputAudio->getWaveFormat()->mTag != WAVE_FORMAT_PCM ? 1000 : 500;

	if (dubOpt.audio.preload > preload)
		dubOpt.audio.preload = preload;

	dubOpt.audio.enabled				= TRUE;
	dubOpt.audio.interval				= 1;
	dubOpt.audio.is_ms					= FALSE;
	dubOpt.video.mSelectionStart.mOffset = start;
	if (start>=dubOpt.video.mSelectionEnd.mOffset)
		dubOpt.video.mSelectionEnd.mOffset = mTimeline.GetLength();

	dubOpt.audio.fStartAudio			= TRUE;
	dubOpt.audio.new_rate				= 0;
	dubOpt.audio.newPrecision			= DubAudioOptions::P_NOCHANGE;
	dubOpt.audio.newChannels			= DubAudioOptions::C_NOCHANGE;
	dubOpt.audio.mVolume				= -1.0f;
	dubOpt.audio.bUseAudioFilterGraph	= false;

	dubOpt.video.mOutputFormat			= dubOpt.video.mInputFormat;

	dubOpt.video.mode					= DubVideoOptions::M_SLOWREPACK;
	dubOpt.video.fShowInputFrame		= TRUE;
	dubOpt.video.fShowOutputFrame		= FALSE;
	dubOpt.video.frameRateDecimation	= 1;
	dubOpt.video.mSelectionEnd.mOffset	= -1;
	dubOpt.video.mbUseSmartRendering	= false;

	dubOpt.audio.mode					= DubAudioOptions::M_FULL;

	dubOpt.fShowStatus = false;
	dubOpt.fMoveSlider = true;

	if (start < mTimeline.GetLength()) {
		mPreviewRestartMode = kPreviewRestart_Input;
		Preview(&dubOpt);
	} else {
		if (mpCB)
			mpCB->UISetDubbingMode(false, false);
	}
}

void VDProject::PreviewOutput(VDPosition* restart) {
	UpdateTimelineRate();

	VDPosition start = restart ? *restart : GetCurrentFrame();
	DubOptions dubOpt(g_dubOpts);
	if (dubOpt.video.mSelectionEnd.mOffset==-1 && mbZoomEnabled)
		dubOpt.video.mSelectionEnd.mOffset = mposZoomEnd;

	const VDPixmapLayout& layout = (dubOpt.video.mode == DubVideoOptions::M_FULL) ? filters.GetOutputLayout() : filters.GetInputLayout();
	dubOpt.video.mOutputFormat = layout.format;

	long preload = inputAudio && inputAudio->getWaveFormat()->mTag != WAVE_FORMAT_PCM ? 1000 : 500;

	if (dubOpt.audio.preload > preload)
		dubOpt.audio.preload = preload;

	dubOpt.audio.enabled				= TRUE;
	dubOpt.audio.interval				= 1;
	dubOpt.audio.is_ms					= FALSE;

	if (dubOpt.video.mode != DubVideoOptions::M_FULL) dubOpt.video.mode = DubVideoOptions::M_SLOWREPACK;
	dubOpt.video.mSelectionStart.mOffset = start;
	if (start>=dubOpt.video.mSelectionEnd.mOffset)
		dubOpt.video.mSelectionEnd.mOffset = mTimeline.GetLength();
	dubOpt.video.mbUseSmartRendering	= false;

	dubOpt.fShowStatus = false;
	dubOpt.fMoveSlider = true;

	if (start < mTimeline.GetLength()) {
		mPreviewRestartMode = kPreviewRestart_Output;
		Preview(&dubOpt);
	} else {
		if (mpCB)
			mpCB->UISetDubbingMode(false, false);
	}
}

void VDProject::PreviewAll(VDPosition* restart) {
	DubOptions dubOpt(g_dubOpts);

	if (restart) {
		dubOpt.video.mSelectionStart.mOffset = *restart;
		if (*restart>=dubOpt.video.mSelectionEnd.mOffset)
			dubOpt.video.mSelectionEnd.mOffset = mTimeline.GetLength();
	}

	dubOpt.video.fShowDecompressedFrame = g_drawDecompressedFrame;
	dubOpt.fShowStatus = !!g_showStatusWindow;
	dubOpt.fMoveSlider = true;

	mPreviewRestartMode = kPreviewRestart_All;
	Preview(&dubOpt);
}

void VDProject::Preview(DubOptions *options) {
	if (!inputVideo)
		throw MyError("No input video stream to process.");

	DubOptions opts(*options);
	opts.audio.enabled = true;

	VDAVIOutputPreviewSystem outpreview;
	VideoOperation op;
	op.opt = &opts;
	op.propagateErrors = true;
	op.iDubPriority = VDPreferencesGetPreviewPriority();
	RunOperation(&outpreview, op);

	//RunOperation(&outpreview, false, &opts, VDPreferencesGetPreviewPriority(), true, 0, 0);
}

void VDProject::PreviewRestart() {
	if (mPreviewRestartMode) {
		PreviewRestartMode restartMode = mPreviewRestartMode;
		mPreviewRestartMode = kPreviewRestart_None;

		switch(restartMode) {
			case kPreviewRestart_Input:
				PreviewInput(&mPreviewRestartPosition);
				break;
			case kPreviewRestart_Output:
				PreviewOutput(&mPreviewRestartPosition);
				break;
			case kPreviewRestart_All:
				PreviewAll(&mPreviewRestartPosition);
				break;
		}
	}
}

void VDProject::RunNullVideoPass(bool benchmark) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	DubOptions dubOpt(g_dubOpts);
	if (dubOpt.video.mSelectionStart.mOffset==0 && mbZoomEnabled)
		dubOpt.video.mSelectionStart.mOffset = mposZoomStart;
	if (dubOpt.video.mSelectionEnd.mOffset==-1 && mbZoomEnabled)
		dubOpt.video.mSelectionEnd.mOffset = mposZoomEnd;
	dubOpt.perf.fBenchmark = benchmark;
	VDAVIOutputNullVideoSystem nullout;
	VideoOperation op;
	op.opt = &dubOpt;
	op.propagateErrors = true;
	op.setPrefs();
	RunOperation(&nullout, op);

	//RunOperation(&nullout, FALSE, &dubOpt, VDPreferencesGetDubPriority(), true, 0, 0, VDPreferencesGetRenderBackgroundPriority());
}

void VDProject::QueueNullVideoPass() {
	if (!inputVideo)
		throw MyError("No input file to process.");

	JobRequest req;
	SetProject(req, this);
	JobAddConfigurationRunVideoAnalysisPass(req);
}

void VDProject::CloseAVI() {
	// kill current seek
	mDesiredOutputFrame = -1;
	mDesiredTimelineFrame = -1;
	mDesiredNextInputFrame = -1;
	mDesiredNextOutputFrame = -1;
	mDesiredNextTimelineFrame = -1;

	StopFilters();		// needs to happen before we take down the video source

	mTimeline.SetTimingSource(NULL);

	if (g_pInputOpts) {
		delete g_pInputOpts;
		g_pInputOpts = NULL;
	}

	while(!mInputAudioSources.empty()) {
		if (inputAudio == mInputAudioSources.back()) {
			inputAudio = NULL;
			inputAudio2 = NULL;
		}

		mInputAudioSources.pop_back();
	}

	inputVideo = NULL;
	inputAVI = NULL;
	g_inputDriver.clear();

	mTextInfo.clear();

	ClearUndoStack();
}

void VDProject::Close() {
	CloseAVI();
	if (mpCB) {
		mpCB->UIVideoSourceUpdated();
		mpCB->UIAudioSourceUpdated();
		mpCB->UISourceFileUpdated();
		mpCB->UITimelineUpdated();
		mpCB->UICurrentPositionUpdated();
	}
}

void VDProject::SaveAVI(RequestVideo& creq) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	if (creq.job) {
		DubOptions opts = g_dubOpts;
		opts.removeAudio = creq.removeAudio;
		JobRequestVideo req;
		SetProject(req, this);
		req.opt = &opts;
		req.fileOutput = creq.fileOutput;
		req.fCompatibility = creq.compat;

		JobAddConfiguration(req);
	} else {
		::SaveAVI(creq);
	}
}

void VDProject::SavePlugin(RequestVideo& creq) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	if (creq.job) {
		DubOptions opts = g_dubOpts;
		opts.removeAudio = creq.removeAudio;
		JobRequestVideo req;
		SetProject(req, this);
		req.opt = &opts;
		req.fileOutput = creq.fileOutput;

		if (creq.removeVideo)
			JobAddConfigurationSaveAudioPlugin(req);
		else
			JobAddConfiguration(req);

	} else {
		::SavePlugin(creq);
	}
}

void VDProject::SaveFilmstrip(const wchar_t *pFilename, bool propagateErrors) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	VDAVIOutputFilmstripSystem out(pFilename);
	VideoOperation op;
	op.propagateErrors = propagateErrors;
	RunOperation(&out, op);
	//RunOperation(&out, TRUE, NULL, 0, propagateErrors);
}

void VDProject::SaveAnimatedGIF(const wchar_t *pFilename, int loopCount, bool propagateErrors, DubOptions *optsOverride) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	VDAVIOutputGIFSystem out(pFilename);
	out.SetLoopCount(loopCount);
	VideoOperation op;
	op.opt = optsOverride;
	op.propagateErrors = propagateErrors;
	RunOperation(&out, op);
	//RunOperation(&out, TRUE, optsOverride, 0, propagateErrors);
}

void VDProject::SaveAnimatedPNG(const wchar_t *pFilename, int loopCount, int alpha, int grayscale, bool propagateErrors, DubOptions *optsOverride) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	int mSaveVideoOutputFormat = g_dubOpts.video.mOutputFormat;

	if (alpha)
		g_dubOpts.video.mOutputFormat = nsVDPixmap::kPixFormat_XRGB8888;

	VDAVIOutputAPNGSystem out(pFilename);
	out.SetLoopCount(loopCount);
	out.SetAlpha(alpha);
	out.SetGrayscale(grayscale);
	VideoOperation op;
	op.opt = optsOverride;
	op.propagateErrors = propagateErrors;
	RunOperation(&out, op);
	//RunOperation(&out, TRUE, optsOverride, 0, propagateErrors);

	if (alpha)
		g_dubOpts.video.mOutputFormat = mSaveVideoOutputFormat;
}

void VDProject::SaveRawAudio(const wchar_t *pFilename, bool propagateErrors, DubOptions *optsOverride) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	if (!inputAudio)
		throw MyError("No audio stream to process.");

	VDAVIOutputRawSystem out(pFilename);
	VideoOperation op;
	op.opt = optsOverride;
	op.propagateErrors = propagateErrors;
	RunOperation(&out, op);
	//RunOperation(&out, TRUE, optsOverride, 0, propagateErrors);
}

void VDProject::SaveRawVideo(const wchar_t *pFilename, const VDAVIOutputRawVideoFormat& format, bool propagateErrors, DubOptions *optsOverride) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	VDAVIOutputRawVideoSystem out(pFilename, format);
	VideoOperation op;
	op.opt = optsOverride;
	op.propagateErrors = propagateErrors;
	RunOperation(&out, op);
	//RunOperation(&out, TRUE, optsOverride, 0, propagateErrors);
}

void VDProject::ExportViaEncoder(const wchar_t *filename, const wchar_t *setName, bool propagateErrors, DubOptions *optsOverride) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	VDAVIOutputCLISystem out(filename, setName);
	out.SetOpt(optsOverride ? *optsOverride : g_dubOpts);

	VideoOperation op;
	op.opt = optsOverride;
	op.propagateErrors = propagateErrors;
	RunOperation(&out, op);
	//RunOperation(&out, TRUE, optsOverride, 0, propagateErrors);
}

void VDProject::StartServer(const char *serverName) {
	if (!inputVideo)
		throw MyError("No input file to process.");

	VDGUIHandle hwnd = mhwnd;

	if (serverName)
		vdstrlcpy(g_serverName, serverName, std::size(g_serverName));
	else
		g_serverName[0] = 0;

	VDUIFrame *pFrame = VDUIFrame::GetFrame((HWND)hwnd);

	pFrame->SetNextMode(3);
	pFrame->Detach();
}

void VDProject::ShowInputInfo() {
	if (inputAVI) {
		LockDisplayFrame();
		inputAVI->InfoDialog(mhwnd);
		UnlockDisplayFrame();
	}
}

void VDProject::SetVideoMode(int mode) {
	g_dubOpts.video.mode = (char)mode;
}

void VDProject::CopySourceFrameToClipboard() {
	if (!inputVideo)
		return;

	mLastDisplayedInputFrame = -1;
	mLastDisplayedTimelineFrame = -1;
	DisplayFrame();

	if (!mpCurrentInputFrame)
		return;

	VDFilterFrameBuffer *buf = mpCurrentInputFrame->GetResultBuffer();
	VDPixmap px = VDPixmapFromLayout(mpVideoFrameSource->GetOutputLayout(), (void *)buf->LockRead());
	px.info = buf->info;
	CopyFrameToClipboard(px);
	buf->Unlock();
}

void VDProject::CopyOutputFrameToClipboard() {
	if (!inputVideo)
		return;

	mLastDisplayedInputFrame = -1;
	mLastDisplayedTimelineFrame = -1;
	DisplayFrame();

	if (!filters.isRunning() || !mpCurrentOutputFrame)
		return;

	VDFilterFrameBuffer *buf = mpCurrentOutputFrame->GetResultBuffer();
	VDPixmap px = VDPixmapFromLayout(filters.GetOutputLayout(), (void *)buf->LockRead());
	px.info = buf->info;
	CopyFrameToClipboard(px);
	buf->Unlock();
}

void VDProject::CopyFrameToClipboard(VDPixmap& px) {
	::CopyFrameToClipboard((HWND)mhwnd, px);
}

void VDProject::CopySourceFrameNumberToClipboard() {
	if (!filters.isRunning())
		StartFilters();

	sint64 pos = filters.GetSourceFrame(mTimeline.TimelineToSourceFrame(GetCurrentFrame()));

	VDStringW s;
	s.sprintf(L"%lld", pos);
	VDCopyTextToClipboard(s.c_str());
}

void VDProject::CopyOutputFrameNumberToClipboard() {
	VDStringW s;
	s.sprintf(L"%lld", GetCurrentFrame());
	VDCopyTextToClipboard(s.c_str());
}

int VDProject::GetAudioSourceCount() const {
	return (int)mInputAudioSources.size();
}

int VDProject::GetAudioSourceMode() const {
	return mAudioSourceMode;
}

void VDProject::SetAudioSourceNone() {
	mAudioSourceMode = kVDAudioSourceMode_None;
	CloseWAV();
	SetAudioSource();
	if (mpCB)
		mpCB->UIAudioSourceUpdated();
}

void VDProject::SetAudioSourceNormal(int index) {
	CloseWAV();
	mAudioSourceMode = kVDAudioSourceMode_Source + index;
	SetAudioSource();
	if (mpCB)
		mpCB->UIAudioSourceUpdated();
}

void VDProject::SetAudioMode(int mode) {
	g_dubOpts.audio.mode = (char)mode;
	SetAudioSource();
	if (mpCB)
		mpCB->UIAudioSourceUpdated();
}

void VDProject::SetAudioEditMode() {
	if (mpCB)
		mpCB->UIAudioSourceUpdated();
}

void VDProject::SetAudioErrorMode(int errorMode0) {
	DubSource::ErrorMode errorMode = (DubSource::ErrorMode)errorMode0;

	AudioSources::iterator it(mInputAudioSources.begin()), itEnd(mInputAudioSources.end());
	for(; it!=itEnd; ++it) {
		AudioSource *as = *it;

		as->setDecodeErrorMode(errorMode);
	}

	if (mpInputAudioExt)
		mpInputAudioExt->setDecodeErrorMode(errorMode);
}

void VDProject::SetSelectionStart() {
	if (inputAVI)
		SetSelectionStart(GetCurrentFrame());
}

void VDProject::SetSelectionStart(VDPosition pos, bool notifyUser) {
	if (inputAVI) {
		UpdateTimelineRate();

		if (pos < 0)
			pos = 0;
		if (pos > GetFrameCount())
			pos = GetFrameCount();
		mposSelectionStart = pos;
		if (mposSelectionEnd < mposSelectionStart) {
			mposSelectionEnd = mposSelectionStart;
			g_dubOpts.video.mSelectionEnd.mOffset = pos;
		}

		g_dubOpts.video.mSelectionStart.mOffset = pos;

		if (mpCB)
			mpCB->UISelectionUpdated(notifyUser);
	}
}

void VDProject::SetSelectionEnd() {
	if (inputAVI)
		SetSelectionEnd(GetCurrentFrame());
}

void VDProject::SetSelectionEnd(VDPosition pos, bool notifyUser) {
	if (inputAVI) {
		UpdateTimelineRate();

		if (pos < 0)
			pos = 0;
		if (pos > GetFrameCount())
			pos = GetFrameCount();

		mposSelectionEnd = pos;
		if (mposSelectionStart > mposSelectionEnd) {
			mposSelectionStart = mposSelectionEnd;
			g_dubOpts.video.mSelectionStart.mOffset = pos;
		}

		g_dubOpts.video.mSelectionEnd.mOffset = pos;

		if (mpCB)
			mpCB->UISelectionUpdated(notifyUser);
	}
}

void VDProject::SetSelection(VDPosition start, VDPosition end, bool notifyUser) {
	if (end < start)
		ClearSelection(notifyUser);
	else {
		UpdateTimelineRate();

		const VDPosition count = GetFrameCount();
		if (start < 0)
			start = 0;
		if (start > count)
			start = count;
		if (end < 0)
			end = 0;
		if (end > count)
			end = count;

		mposSelectionStart = start;
		mposSelectionEnd = end;

		g_dubOpts.video.mSelectionStart.mOffset = start;
		g_dubOpts.video.mSelectionEnd.mOffset = end;

		if (mpCB)
			mpCB->UISelectionUpdated(notifyUser);
	}
}

void VDProject::SetMarker() {
	if (inputAVI)
		SetMarker(GetCurrentFrame());
}

void VDProject::SetMarker(VDPosition pos) {
	if (inputAVI) {
		mTimeline.ToggleMarker(pos);

		if (mpCB)
			mpCB->UIMarkerUpdated();
	}
}

void VDProject::SetZoomRange(VDPosition start, VDPosition end) {
	mbZoomEnabled = true;
	mposZoomStart = start;
	mposZoomEnd = end;
}

bool VDProject::GetZoomRange(VDPosition& start, VDPosition& end) {
	start = mposZoomStart;
	end = mposZoomEnd;
	return mbZoomEnabled;
}

void VDProject::ClearZoomRange() {
	mbZoomEnabled = false;
	mposZoomStart = 0;
	mposZoomEnd = 0;
}

void VDProject::ToggleZoomRange() {
	if (mbZoomEnabled) {
		mbZoomEnabled = false;
		if (mpCB)
			mpCB->UITimelineUpdated();
		return;
	}
	if (mposSelectionEnd>mposSelectionStart) {
		mposZoomStart = mposSelectionStart;
		mposZoomEnd = mposSelectionEnd;
	}
	if (mposZoomEnd<=mposZoomStart) {
		mposZoomStart = 0;
		mposZoomEnd = mTimeline.GetLength()-1;
	}
	if (mposZoomEnd>mposZoomStart) {
		mbZoomEnabled = true;
		if (mpCB)
			mpCB->UITimelineUpdated();

		if (mposCurrentFrame<mposZoomStart)
			MoveToFrame(mposZoomStart);
		else if (mposCurrentFrame>mposZoomEnd)
			MoveToFrame(mposZoomEnd);
	}
}

void VDProject::MoveToFrame(VDPosition frame, int sync_mode) {
	if (inputVideo) {
		if (sync_mode==1 && mposSyncMode==1 && !g_fDropSeeking) {
			if (UpdateFrame()) return;
		}

		VDPosition min = mbZoomEnabled ? mposZoomStart : 0;
		VDPosition max = mbZoomEnabled ? mposZoomEnd : mTimeline.GetLength();
		frame = std::clamp(frame, min, max);

		mposSyncMode = sync_mode;
		mposCurrentFrame = frame;
		mbPositionCallbackEnabled = false;

		if (!g_dubber && !mProjectLoading)
			DisplayFrame();

		if (mpCB)
			mpCB->UICurrentPositionUpdated();
	}
}

void VDProject::MoveToStart() {
	if (inputVideo)
		MoveToFrame(0);
}

void VDProject::MoveToPrevious() {
	if (inputVideo)
		MoveToFrame(GetCurrentFrame() - 1, 1);
}

void VDProject::MoveToNext() {
	if (inputVideo)
		MoveToFrame(GetCurrentFrame() + 1, 1);
}

void VDProject::MoveToEnd() {
	if (inputVideo)
		MoveToFrame(mTimeline.GetEnd());
}

void VDProject::MoveToSelectionStart() {
	if (inputVideo && IsSelectionPresent()) {
		VDPosition pos = GetSelectionStartFrame();

		if (pos >= 0)
			MoveToFrame(pos);
	}
}

void VDProject::MoveToSelectionEnd() {
	if (inputVideo && IsSelectionPresent()) {
		VDPosition pos = GetSelectionEndFrame();

		if (pos >= 0)
			MoveToFrame(pos);
	}
}

void VDProject::MoveToNearestKey(VDPosition pos) {
	if (!inputVideo)
		return;

	pos = mTimeline.GetNearestKey(pos);
	if (pos < 0)
		return;

	if (mbZoomEnabled) {
		if (pos>mposZoomEnd) {
			pos = mTimeline.GetPrevKey(pos);
		} else if (pos<mposZoomStart) {
			pos = mTimeline.GetNextKey(pos);
		}
		if (pos<mposZoomStart || pos>mposZoomEnd)
			return;
	}

	MoveToFrame(pos, 1);
}

void VDProject::MoveToNearestKeyNext(VDPosition pos) {
	if (!inputVideo)
		return;

	pos = mTimeline.GetNearestKeyNext(pos);
	if (pos < 0)
		return;

	if (mbZoomEnabled) {
		if (pos>mposZoomEnd)
			return;
	}

	MoveToFrame(pos, 1);
}

void VDProject::MoveToPreviousKey() {
	if (!inputVideo)
		return;

	VDPosition pos = mTimeline.GetPrevKey(GetCurrentFrame());
	if (pos < 0)
		pos = 0;

	if (mbZoomEnabled) {
		if (pos<mposZoomStart)
			return;
	}

	MoveToFrame(pos, 1);
}

void VDProject::MoveToNextKey() {
	if (!inputVideo)
		return;

	VDPosition pos = mTimeline.GetNextKey(GetCurrentFrame());

	if (pos < 0)
		pos = mTimeline.GetEnd();

	if (mbZoomEnabled) {
		if (pos>mposZoomEnd)
			return;
	}

	MoveToFrame(pos, 1);
}

extern int VDPreferencesGetTimelinePageSize();

void VDProject::MoveBackSome() {
	if (inputVideo)
		MoveToFrame(GetCurrentFrame() - VDPreferencesGetTimelinePageSize(), 1);
}

void VDProject::MoveForwardSome() {
	if (inputVideo)
		MoveToFrame(GetCurrentFrame() + VDPreferencesGetTimelinePageSize(), 1);
}

void VDProject::StartShuttle(int d, bool sticky) {
	if (!inputVideo)
		return;
	mSceneShuttleMode = 0;
	mSceneShuttleAdvance = 0;
	mSceneShuttleCounter = 0;
	mShuttleMode = d;
	mStickyShuttle = sticky;
	if (mpSceneDetector) {
		delete mpSceneDetector;
		mpSceneDetector = NULL;
	}
	if (mpCB)
		mpCB->UIShuttleModeUpdated();
}

void VDProject::StartSceneShuttleReverse() {
	if (!inputVideo)
		return;
	mSceneShuttleMode = -1;
	if (mpCB)
		mpCB->UIShuttleModeUpdated();
}

void VDProject::StartSceneShuttleForward() {
	if (!inputVideo)
		return;
	mSceneShuttleMode = +1;
	if (mpCB)
		mpCB->UIShuttleModeUpdated();
}

void VDProject::MoveToPreviousRange() {
	VDPosition min = mbZoomEnabled ? mposZoomStart : 0;

	if (inputAVI) {
		VDPosition pos = mTimeline.GetPrevEdit(GetCurrentFrame());
		VDPosition mpos = mTimeline.GetPrevMarker(GetCurrentFrame());

		if (mpos>=min && (mpos>pos || pos==-1)) {
			MoveToFrame(mpos);
			guiSetStatus("Marker", 255);
			return;
		}

		if (pos>=min) {
			MoveToFrame(pos);

			sint64 len;
			bool masked;
			int source;
			sint64 start = mTimeline.GetSubset().lookupRange(pos, len, masked, source);
			guiSetStatus("Previous output frame %I64d-%I64d: included source range %I64d-%I64d%s", 255, pos, pos+len-1, start, start+len-1, masked ? " (masked)" : "");
			return;
		}
	}
	MoveToFrame(min);
	guiSetStatus("No previous edit.", 255);
}

void VDProject::MoveToNextRange() {
	VDPosition max = mbZoomEnabled ? mposZoomEnd : GetFrameCount();

	if (inputAVI) {
		VDPosition pos = mTimeline.GetNextEdit(GetCurrentFrame());
		VDPosition mpos = mTimeline.GetNextMarker(GetCurrentFrame());

		if (mpos>=0 && mpos<=max && (mpos<pos || pos==-1)) {
			MoveToFrame(mpos);
			guiSetStatus("Marker", 255);
			return;
		}

		if (pos>=0 && pos<=max) {
			MoveToFrame(pos);

			sint64 len;
			bool masked;
			int source;
			sint64 start = mTimeline.GetSubset().lookupRange(pos, len, masked, source);
			guiSetStatus("Next output frame %I64d-%I64d: included source range %I64d-%I64d%s", 255, pos, pos+len-1, start, start+len-1, masked ? " (masked)" : "");
			return;
		}
	}
	MoveToFrame(max);
	guiSetStatus("No next edit.", 255);
}

void VDProject::MoveToPreviousDrop() {
	if (inputAVI) {
		VDPosition pos = mTimeline.GetPrevDrop(GetCurrentFrame());
		VDPosition min = mbZoomEnabled ? mposZoomStart : 0;

		if (pos >= min)
			MoveToFrame(pos);
		else
			guiSetStatus("No previous dropped frame found.", 255);
	}
}

void VDProject::MoveToNextDrop() {
	if (inputAVI) {
		VDPosition pos = mTimeline.GetNextDrop(GetCurrentFrame());
		VDPosition max = mbZoomEnabled ? mposZoomEnd : GetFrameCount();

		if (pos >= 0 && pos <= max)
			MoveToFrame(pos);
		else
			guiSetStatus("No next dropped frame found.", 255);
	}
}

bool VDProject::IsTimelineReset() {
	if (inputAVI) {
		return mTimeline.IsReset();
	} else return true;
}

void VDProject::ResetTimeline() {
	if (inputAVI) {
		BeginTimelineUpdate(VDLoadString(0, kVDST_Project, kVDM_ResetTimeline));

		mTimeline.SetFromSource();
		ClearZoomRange();
		EndTimelineUpdate();
	}
}

void VDProject::ResetTimelineWithConfirmation() {
	if (inputAVI) {
		if (IDOK == MessageBoxW((HWND)mhwnd, L"Discard edits and reset timeline?", g_szWarning, MB_OKCANCEL|MB_TASKMODAL|MB_SETFOREGROUND|MB_ICONEXCLAMATION)) {
			ResetTimeline();
		}
	}
}

void VDProject::ScanForErrors() {
	if (inputVideo) {
		BeginTimelineUpdate(VDLoadString(0, kVDST_Project, kVDM_ScanForErrors));
		ScanForUnreadableFrames(&mTimeline.GetSubset(), inputVideo);
		EndTimelineUpdate();
	}
}

//void VDProject::RunOperation(IVDDubberOutputSystem *pOutputSystem, BOOL fAudioOnly, DubOptions *pOptions, int iPriority, bool fPropagateErrors, long lSpillThreshold, long lSpillFrameThreshold, bool backgroundPriority) {
void VDProject::RunOperation(IVDDubberOutputSystem *pOutputSystem, VideoOperation& op) {

	if (!inputAVI)
		throw MyError("No source has been loaded to process.");

	VDProjectAutoSave autoSave;

	if (!g_fJobMode && VDPreferencesGetAutoRecoverEnabled() && !pOutputSystem->IsRealTime()) {
		autoSave.Save(this);
	}

	bool fError = false;
	bool bUserAbort = false;
	MyError prop_err;
	DubOptions *opts;

	vdautoptr<VDAVIOutputSegmentedSystem> segmentedOutput;

	{
		const wchar_t *pOpType = pOutputSystem->IsRealTime() ? L"preview" : L"dub";
		VDLog(kVDLogMarker, VDswprintf(L"Beginning %s operation.", 1, &pOpType));
	}

	DubOptions tempOpts(op.opt ? *op.opt : g_dubOpts);

	if (!g_fJobMode && !pOutputSystem->IsRealTime()) {
		extern bool VDPreferencesGetRenderShowFrames();
		if (!VDPreferencesGetRenderShowFrames()) {
			tempOpts.video.fShowInputFrame = false;
			tempOpts.video.fShowOutputFrame = false;
		}
	}

	mbPositionCallbackEnabled = true;

	try {
		VDAutoLogDisplay disp;

		UpdateDubParameters(true);
		StopFilters();

		// Create a dubber.

		opts = &tempOpts;
		if (!op.opt) {
			opts->video.fShowDecompressedFrame = g_drawDecompressedFrame;
			opts->fShowStatus = !!g_showStatusWindow;
		}
		opts->perf.fDropFrames = g_fDropFrames;
		opts->mThrottlePercent = pOutputSystem->IsRealTime() ? 100 : VDPreferencesGetRenderThrottlePercent();
		opts->video.mMaxVideoCompressionThreads = VDPreferencesGetVideoCompressionThreadCount();

		if (pOutputSystem->IsVideoImageOutputRequired()) {
			if (opts->video.mode == DubVideoOptions::M_NONE)
				opts->video.mode = DubVideoOptions::M_FASTREPACK;
		}

		if (!(g_dubber = CreateDubber(opts)))
			throw MyMemoryError();

		// Create dub status window

		delete mpDubStatus;
		mpDubStatus = CreateDubStatusHandler();

		if (opts->fMoveSlider) {
			if (g_fJobMode) {
				mpDubStatus->SetPositionCallback(JobPositionCallback, this);
			} else {
				if (pOutputSystem->IsRealTime())
					mpDubStatus->SetPositionCallback(StaticFastPositionCallback, this);
				else
					mpDubStatus->SetPositionCallback(StaticPositionCallback, this);
			}
		} else {
			if (pOutputSystem->IsNull())
				mpDubStatus->SetPositionCallback(StaticFinalPositionCallback, this);
		}

		// Initialize the dubber.

		if (opts->audio.bUseAudioFilterGraph)
			g_dubber->SetAudioFilterGraph(g_audioFilterGraph);

		g_dubber->SetStatusHandler(mpDubStatus);

		if (!pOutputSystem->IsRealTime() && g_ACompressionFormat)
			g_dubber->SetAudioCompression((const VDWaveFormat *)g_ACompressionFormat, g_ACompressionFormatSize, g_ACompressionFormatHint.c_str(), g_ACompressionConfig);

		// As soon as we call Init(), this value is no longer ours to free.

		if (mpCB)
			mpCB->UISetDubbingMode(true, pOutputSystem->IsRealTime());

		IVDVideoSource *vsrc = inputVideo;
		AudioSource *asrc = inputAudio;
		if (op.removeAudio) asrc = 0;
		COMPVARS2 *comp = &g_Vcompression;
		if(filters.isTrimmedChain() && pOutputSystem->IsNull()) comp = 0;

		if (op.lSegmentCount>1 || op.lSpillThreshold || op.lSpillFrameThreshold) {
			long lSpillFrameThreshold = op.lSpillFrameThreshold;
			if (op.lSegmentCount>1)
				lSpillFrameThreshold = (opts->video.mSelectionEnd.mOffset - opts->video.mSelectionStart.mOffset + op.lSegmentCount-1)/op.lSegmentCount;
			if (op.lSpillFrameThreshold && op.lSpillFrameThreshold<lSpillFrameThreshold)
				lSpillFrameThreshold = op.lSpillFrameThreshold;
			segmentedOutput = new VDAVIOutputSegmentedSystem(pOutputSystem, opts->audio.is_ms, opts->audio.is_ms ? opts->audio.interval * 0.001 : opts->audio.interval, (double)opts->audio.preload / 500.0, (sint64)op.lSpillThreshold << 20, lSpillFrameThreshold);
			g_dubber->Init(&vsrc, 1, &asrc, asrc ? 1 : 0, segmentedOutput, comp, &mTimeline.GetSubset(), mVideoTimelineFrameRate);
		} else {
			g_dubber->Init(&vsrc, 1, &asrc, asrc ? 1 : 0, pOutputSystem, comp, &mTimeline.GetSubset(), mVideoTimelineFrameRate);
		}

		if (!op.opt && mhwnd)
			RedrawWindow((HWND)mhwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);

		g_dubber->Stopped() += mStoppedDelegate(this, &VDProject::OnDubAbort);
		g_dubber->Go(op.iDubPriority);
		g_dubber->SetBackground(op.backgroundPriority);

		if (mpCB)
			bUserAbort = !mpCB->UIRunDubMessageLoop();
		else {
			MSG msg;
			while(g_dubber->isRunning()) {
				BOOL result = GetMessageW(&msg, NULL, 0, 0);

				if (result == (BOOL)-1)
					break;

				if (!result) {
					PostQuitMessage(msg.wParam);
					break;
				}

				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}

		g_dubber->Stop();

		if (g_dubber->isAbortedByUser()) {
			bUserAbort = true;
			mPreviewRestartMode = kPreviewRestart_None;
		} else {
			if (!g_dubber->IsAborted())
				mPreviewRestartMode = kPreviewRestart_None;

			if (!op.propagateErrors)
				disp.Post(mhwnd);
		}

	} catch(char *s) {
		mPreviewRestartMode = kPreviewRestart_None;
		if (op.propagateErrors) {
			prop_err.setf(s);
			fError = true;
		} else
			MyError(s).post((HWND)mhwnd, g_szError);
	} catch(MyError& err) {
		mPreviewRestartMode = kPreviewRestart_None;
		if (op.propagateErrors) {
			prop_err.TransferFrom(err);
			fError = true;
		} else
			err.post((HWND)mhwnd,g_szError);
	}

	if (g_dubber)
		g_dubber->SetStatusHandler(NULL);

	if (mpDubStatus) {
		if (!fError || !mpDubStatus->IsNormalWindow()){
			delete mpDubStatus;
			mpDubStatus = NULL;
		} else {
			mpDubStatus->DeferDestroy((void**)&mpDubStatus);
		}
	}

	_CrtCheckMemory();

	delete g_dubber;
	g_dubber = NULL;

	VDRenderSetVideoSourceInputFormat(inputVideo, g_dubOpts.video.mInputFormat);

	if (mpCB)
		mpCB->UISetDubbingMode(false, false);

	VDLog(kVDLogMarker, VDStringW(L"Ending operation."));

	if (g_bExit)
		PostQuitMessage(0);
	else if (op.propagateErrors) {
		if (fError)
			throw prop_err;
		else if (bUserAbort)
			throw MyUserAbortError();
	}
}

void VDProject::AbortOperation() {
	if (g_dubber)
		g_dubber->Abort();
}

bool VDProject::AbortPreviewing() {
	if (!g_dubber)
		return true;
	if (!g_dubber->IsPreviewing())
		return false;
	g_dubber->Abort();
	mPreviewRestartMode = kPreviewRestart_None;
	return true;
}

void VDProject::StopFilters() {
	mpCurrentInputFrame = NULL;
	mpCurrentOutputFrame = NULL;
	mpPendingInputFrame = NULL;
	mpPendingOutputFrame = NULL;

	filters.DeinitFilters();
	filters.DeallocateBuffers();

	mpVideoFrameSource = NULL;
}

void VDProject::PrepareFilters() {
	if (filters.isRunning() || !inputVideo)
		return;

	IVDStreamSource *pVSS = inputVideo->asStream();

	DubVideoStreamInfo vInfo;
	InitVideoStreamValuesStatic(vInfo, inputVideo, inputAudio, &g_dubOpts, &mTimeline.GetSubset(), NULL, NULL);

	VDFraction framerate(vInfo.mFrameRatePreFilter);
	const VDPixmap& px = inputVideo->getTargetFormat();
	const VDFraction& srcPAR = inputVideo->getPixelAspectRatio();

	filters.prepareLinearChain(&g_filterChain, px.w, px.h, px, framerate, pVSS->getLength(), srcPAR);
}

void VDProject::StartFilters() {
	if (filters.isRunning() || !inputVideo || mbFilterChainLocked)
		return;
	IVDStreamSource *pVSS = inputVideo->asStream();

	DubVideoStreamInfo vInfo;
	InitVideoStreamValuesStatic(vInfo, inputVideo, inputAudio, &g_dubOpts, &mTimeline.GetSubset(), NULL, NULL);

	VDFraction framerate(vInfo.mFrameRatePreFilter);
	const VDPixmap& px = inputVideo->getTargetFormat();

	if (px.format) {
		const VDFraction& srcPAR = inputVideo->getPixelAspectRatio();
		filters.prepareLinearChain(&g_filterChain, px.w, px.h, px, framerate, pVSS->getLength(), srcPAR);

		mpVideoFrameSource = new VDFilterFrameVideoSource;
		mpVideoFrameSource->Init(inputVideo, filters.GetInputLayout());

		filters.SetVisualAccelDebugEnabled(false);
		filters.SetAccelEnabled(VDPreferencesGetFilterAccelEnabled());
		filters.SetAsyncThreadCount(-1);

		// We explicitly use the stream length here as we're interested in the *uncut* filtered length.
		vdrefptr<IVDFilterSystemScheduler> fss(new VDFilterSystemMessageLoopScheduler);

		filters.initLinearChain(fss, VDXFilterStateInfo::kStatePreview, &g_filterChain, mpVideoFrameSource, px.w, px.h, px, px.palette, framerate, pVSS->getLength(), srcPAR);

		filters.ReadyFilters();
	}
}

void VDProject::UpdateFilterList() {
	mLastDisplayedTimelineFrame = -1;
	DisplayFrame();
	if (mpCB)
		mpCB->UIVideoFiltersUpdated();
}

///////////////////////////////////////////////////////////////////////////

void VDProject::BeginFilterUpdates() {
	StartFilters();

	if (filters.isRunning()) {
		mVideoMarkedPostFiltersFrameRate = filters.GetOutputFrameRate();
		mVideoMarkedPostFiltersFrameCount = filters.GetOutputFrameCount();
	} else {
		mVideoMarkedPostFiltersFrameRate = VDFraction(0, 0);
		mVideoMarkedPostFiltersFrameCount = 0;
	}
}

void VDProject::EndFilterUpdates() {
	StartFilters();

	VDFraction oldRate = mVideoMarkedPostFiltersFrameRate;
	sint64 oldCount = mVideoMarkedPostFiltersFrameCount;
	VDFraction newRate = filters.GetOutputFrameRate();
	sint64 newCount = filters.GetOutputFrameCount();

	if (oldRate != newRate ||
		oldCount != newCount)
	{
		mVideoMarkedPostFiltersFrameRate = newRate;
		mVideoMarkedPostFiltersFrameCount = newCount;

		if (oldRate.getLo() > 0)
			AdjustTimelineForFilterChanges(oldRate, oldCount, newRate, newCount);
	}
}

void VDProject::SceneShuttleStop() {
	if (mSceneShuttleMode || mShuttleMode) {
		mShuttleMode = 0;
		mSceneShuttleMode = 0;
		mSceneShuttleAdvance = 0;
		mSceneShuttleCounter = 0;

		if (mpCB)
			mpCB->UIShuttleModeUpdated();

		if (inputVideo) {
			MoveToFrame(GetCurrentFrame());

			// We have to force the input frame to refresh since we may have omitted one of the updates.
			if (mpCurrentInputFrame && mpCurrentInputFrame->IsSuccessful()) {
				VDFilterFrameBuffer *buf = mpCurrentInputFrame->GetResultBuffer();
				VDPixmap px(VDPixmapFromLayout(filters.GetInputLayout(), (void *)buf->LockRead()));
				px.info = buf->info;
				mpCB->UIRefreshInputFrame(&px);
				buf->Unlock();
			} else {
				mpCB->UIRefreshInputFrame(NULL);
			}
		}
	}
}

void VDProject::AdjustTimelineForFilterChanges(const VDFraction& oldRate, sint64 oldFrameCount, const VDFraction& newRate, sint64 newFrameCount) {
	// rescale everything
	mTimeline.Rescale(
			oldRate,
			oldFrameCount,
			newRate,
			newFrameCount);

	mpCB->UITimelineUpdated();

	double rateConversion = newRate.asDouble() / oldRate.asDouble();

	if (IsSelectionPresent()) {
		VDPosition selStart = GetSelectionStartFrame();
		VDPosition selEnd = GetSelectionEndFrame();

		selStart = VDCeilToInt64(selStart * rateConversion - 0.5);
		selEnd = VDCeilToInt64(selEnd * rateConversion - 0.5);

		SetSelection(selStart, selEnd);
	}

	MoveToFrame(VDCeilToInt64(GetCurrentFrame() * rateConversion - 0.5));
}

void VDProject::SceneShuttleStep() {
	bool input_mode = IsInputPaneUsed();

	VDPosition sample = GetCurrentFrame() + mSceneShuttleMode + mShuttleMode;

	if (input_mode) {
		VDPosition ls2 = mTimeline.TimelineToSourceFrame(sample);
		IVDStreamSource *pVSS = inputVideo->asStream();
		if (!inputVideo || ls2 < pVSS->getStart() || ls2 >= pVSS->getEnd()) {
			SceneShuttleStop();
			return;
		}
	} else {
		VDPosition min = mbZoomEnabled ? mposZoomStart : 0;
		VDPosition max = mbZoomEnabled ? mposZoomEnd : mTimeline.GetLength()-1;
		if (sample<min || sample>max) {
			SceneShuttleStop();
			return;
		}
	}

	if (mSceneShuttleAdvance < 1280)
		++mSceneShuttleAdvance;

	mSceneShuttleCounter += 32;

	mposCurrentFrame = sample;

	if (input_mode)
		DisplayFrame(true, false, true, false);
	else
		DisplayFrame(false, true, false, true);

	bool updateInputFrame = false;
	if (mSceneShuttleCounter >= mSceneShuttleAdvance) {
		mSceneShuttleCounter = 0;
		updateInputFrame = true;
	}

	while(UpdateFrame(updateInputFrame));

	if (mpCB)
		mpCB->UICurrentPositionUpdated();

	if (!mpSceneDetector)
		return;
	if (!mpSceneDetector->Enabled())
		return;

	bool sceneBreak = false;
	if (input_mode) {
		VDFilterFrameBuffer *buf = mpCurrentInputFrame->GetResultBuffer();
		if (!buf) {
			SceneShuttleStop();
			return;
		}

		VDPixmap px = VDPixmapFromLayout(mpVideoFrameSource->GetOutputLayout(), (void *)buf->LockRead());
		px.info = buf->info;
		sceneBreak = mpSceneDetector->Submit(px);
		buf->Unlock();
	} else {
		if (!filters.isRunning()) {
			SceneShuttleStop();
			return;
		}

		VDFilterFrameBuffer *buf = mpCurrentOutputFrame->GetResultBuffer();
		if (!buf) {
			SceneShuttleStop();
			return;
		}

		VDPixmap px = VDPixmapFromLayout(filters.GetOutputLayout(), (void *)buf->LockRead());
		px.info = buf->info;
		sceneBreak = mpSceneDetector->Submit(px);
		buf->Unlock();
	}

	if (sceneBreak)
		SceneShuttleStop();
}

void VDProject::StaticPositionCallback(VDPosition start, VDPosition cur, VDPosition end, int progress, bool fast_update, void *cookie) {
	VDProject *pthis = (VDProject *)cookie;

	if (pthis->mbPositionCallbackEnabled && !fast_update) {
		VDPosition frame = std::clamp<VDPosition>(cur, 0, pthis->GetFrameCount());

		pthis->mposCurrentFrame = frame;
		if (pthis->mpCB)
			pthis->mpCB->UICurrentPositionUpdated(false);
	}
}

void VDProject::StaticFastPositionCallback(VDPosition start, VDPosition cur, VDPosition end, int progress, bool fast_update, void *cookie) {
	VDProject *pthis = (VDProject *)cookie;

	if (pthis->mbPositionCallbackEnabled) {
		VDPosition frame = std::clamp<VDPosition>(cur, 0, pthis->GetFrameCount());

		pthis->mposCurrentFrame = frame;
		if (pthis->mpCB)
			pthis->mpCB->UICurrentPositionUpdated(fast_update);
	}
}

void VDProject::StaticFinalPositionCallback(VDPosition start, VDPosition cur, VDPosition end, int progress, bool fast_update, void *cookie) {
	VDProject *pthis = (VDProject *)cookie;

	if (pthis->mbPositionCallbackEnabled && cur==end-1) {
		VDPosition frame = std::clamp<VDPosition>(cur, 0, pthis->GetFrameCount());

		pthis->mposCurrentFrame = frame;
		if (pthis->mpCB)
			pthis->mpCB->UICurrentPositionUpdated(fast_update);
	}
}

void VDProject::UpdateTimelineRate() {
	if (mbTimelineRateDirty)
		UpdateDubParameters(false);
}

void VDProject::UpdateDubParameters(bool forceUpdate) {
	if (!inputVideo) {
		if (forceUpdate)
			throw MyError("Cannot initialize rendering parameters: there is no video stream.");

		return;
	}

	// work around fraction ==(0,0) bug for now
	mVideoInputFrameRate	= VDFraction(1,1);
	mVideoOutputFrameRate	= VDFraction(1,1);
	mVideoTimelineFrameRate	= VDFraction(1,1);

	DubVideoStreamInfo vInfo;

	if (inputVideo) {
		VDRenderSetVideoSourceInputFormat(inputVideo, g_dubOpts.video.mInputFormat);

		try {
			InitVideoStreamValuesStatic(vInfo, inputVideo, inputAudio, &g_dubOpts, &GetTimeline().GetSubset(), NULL, NULL);

			if (mVideoOutputFrameRate != vInfo.mFrameRatePreFilter)
				StopFilters();

			mVideoInputFrameRate	= vInfo.mFrameRateIn;
			mVideoOutputFrameRate	= vInfo.mFrameRatePreFilter;
			mVideoTimelineFrameRate = vInfo.mFrameRatePreFilter;

			StartFilters();

			InitVideoStreamValuesStatic2(vInfo, &g_dubOpts, filters.isRunning() ? &filters : NULL, VDFraction(0, 0));

			mVideoTimelineFrameRate	= vInfo.mFrameRatePostFilter;
			mbTimelineRateDirty = false;
		} catch(const MyError& e) {
			// The input stream may throw an error here trying to obtain the nearest key.
			// If so, bail.
			if (forceUpdate) {
				throw MyError(L"Cannot initialize rendering parameters: %s", e.c_str());
			}
		}
	}

	if (mpCB)
		mpCB->UIDubParametersUpdated();
}

void VDProject::SetAudioSource() {
	switch(mAudioSourceMode) {
		case kVDAudioSourceMode_None:
			inputAudio = NULL;
			break;

		case kVDAudioSourceMode_External:
			inputAudio = mpInputAudioExt;
			break;

		default:
			if (mAudioSourceMode >= kVDAudioSourceMode_Source) {
				int index = mAudioSourceMode - kVDAudioSourceMode_Source;

				if ((unsigned)index < mInputAudioSources.size()) {
					inputAudio = mInputAudioSources[index];
					break;
				}
			}
			inputAudio = NULL;
			break;
	}

	if (inputAudio) {
		inputAudio->streamAppendReinit();
		const VDWaveFormat *fmt = inputAudio->getWaveFormat();
		bool convert = false;
		if (g_dubOpts.audio.newChannels!=DubAudioOptions::C_NOCHANGE) convert = true;
		if (g_dubOpts.audio.newPrecision!=DubAudioOptions::P_NOCHANGE) convert = true;
		if (g_dubOpts.audio.bUseAudioFilterGraph) convert = true;
		if (g_dubOpts.audio.mode==DubAudioOptions::M_NONE) convert = false;
		if ((is_audio_pcm(fmt) || is_audio_float(fmt)) && convert) {
			VDWaveFormat target = *fmt;
			target.mTag = VDWaveFormat::kTagPCM;
			switch (g_dubOpts.audio.newChannels) {
			case DubAudioOptions::C_MONO:
				target.mChannels = 1;
				break;
			case DubAudioOptions::C_NOCHANGE:
				target.mChannels = 0;
				break;
			default:
				target.mChannels = 2;
			}
			switch (g_dubOpts.audio.newPrecision) {
			case DubAudioOptions::P_8BIT:
				target.mSampleBits = 8;
				break;
			case DubAudioOptions::P_16BIT:
				target.mSampleBits = 16;
				break;
			default:
				target.mSampleBits = 0;
			}

			//! filters do not support anything else now
			if (g_dubOpts.audio.bUseAudioFilterGraph) {
				target.mSampleBits = 16;
				target.mChannels = 0;
			}

			inputAudio->SetTargetFormat(&target);
		} else {
			inputAudio->SetTargetFormat(0);
		}
	}

	inputAudio2 = NULL;
	if (inputAudio && mAudioSplit) {
		if (mAudioSourceMode==kVDAudioSourceMode_External) {
			IVDInputDriver *pDriver = NULL;
			if (mAudioInputDriverName.empty())
				pDriver = VDAutoselectInputDriverForFile(g_szInputWAVFile, IVDInputDriver::kF_Audio);
			else
				pDriver = VDGetInputDriverByName(mAudioInputDriverName.c_str());

			vdrefptr<InputFile> ifile(pDriver->CreateInputFile(IVDInputDriver::kOF_Quiet));
			if (!ifile) return;

			if (mpAudioInputOptions)
				ifile->setOptions(mpAudioInputOptions);

			ifile->Init(g_szInputWAVFile);

			vdrefptr<AudioSource> pNewAudio;
			if (!ifile->GetAudioSource(0, ~pNewAudio)) return;

			pNewAudio->setDecodeErrorMode(g_audioErrorMode);
			inputAudio2 = pNewAudio;
		}

		if (mAudioSourceMode >= kVDAudioSourceMode_Source && inputAVI) {
			int api_version = inputAVI->GetInputDriverApiVersion();
			if (api_version<8) return;
			int index = mAudioSourceMode - kVDAudioSourceMode_Source;

			if ((unsigned)index < mInputAudioSources.size()) {
				inputAVI->GetAudioSource(index, ~inputAudio2);
				inputAudio2->setDecodeErrorMode(g_audioErrorMode);
			}
		}
	}

	if (inputAudio2) {
		inputAudio2->streamAppendReinit();
		const VDWaveFormat *fmt = inputAudio2->getWaveFormat();
		bool convert = false;
		if (g_dubOpts.audio.newChannels!=DubAudioOptions::C_NOCHANGE) convert = true;
		if (g_dubOpts.audio.mode==DubAudioOptions::M_NONE) convert = false;
		if ((is_audio_pcm(fmt) || is_audio_float(fmt)) && convert) {
			VDWaveFormat target = *fmt;
			target.mTag = VDWaveFormat::kTagPCM;
			switch (g_dubOpts.audio.newChannels) {
			case DubAudioOptions::C_MONO:
				target.mChannels = 1;
				break;
			case DubAudioOptions::C_NOCHANGE:
				target.mChannels = 0;
				break;
			default:
				target.mChannels = 2;
			}

			inputAudio2->SetTargetFormat(&target);
		} else {
			inputAudio2->SetTargetFormat(0);
		}
	}
}

void VDProject::OnDubAbort(IDubber *, const bool&) {
	if (mpCB)
		mpCB->UIAbortDubMessageLoop();
}

int64 FilterModTimeline::GetTimelinePos() {
	return project->GetCurrentFrame();
}

int64 FilterModTimeline::TimelineToFilterSource(int64 frame) {
	return project->GetTimeline().TimelineToSourceFrame(frame);
}

int64 FilterModTimeline::FilterSourceToTimeline(int64 frame) {
	bool masked;
	return project->GetTimeline().GetSubset().revLookupFrame(frame,masked);
}

FilterDefinitionInstance *VDUIShowDialogAddFilter(VDGUIHandle hParent);

bool FilterModSystem::CreateVideoFilter(VDXHWND hParent, FilterReturnInfo& a)
{
	FilterDefinitionInstance *fdi = VDUIShowDialogAddFilter((VDGUIHandle)hParent);
	if(!fdi) return false;
	a.setName(fdi->GetName().c_str());
	a.setMaker(fdi->GetAuthor().c_str());
	a.setDesc(fdi->GetDescription().c_str());
	VDExternalModule* xm = fdi->GetModule();
	if(xm)
		a.setModulePath(xm->GetFilename().c_str());
	else
		a.setBuiltinDef(&fdi->GetDef());
	return true;
}

bool FilterModSystem::FindVideoFilter(const char* name, FilterReturnInfo& a)
{
	std::list<FilterBlurb>	filterList;

	FilterEnumerateFilters(filterList);

	for (const auto& fb : filterList) {
		if (strfuzzycompare(fb.name.c_str(), name)) {
			a.setName(fb.name.c_str());
			a.setMaker(fb.author.c_str());
			a.setDesc(fb.description.c_str());
			VDExternalModule* xm = fb.key->GetModule();
			if (xm) {
				a.setModulePath(xm->GetFilename().c_str());
			} else {
				a.setBuiltinDef(&fb.key->GetDef());
			}
			return true;
		}
	}

	return false;
}
