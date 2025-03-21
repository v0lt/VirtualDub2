//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2002 Avery Lee
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

#ifndef f_STDAFX_H
#define f_STDAFX_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <wchar.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/math.h>
#include <vd2/system/atomic.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/system/VDRingBuffer.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <list>
#include <algorithm>

#define _WIN32_WINNT 0x0501
#include <windows.h>

#include "VirtualDub.h"


// Disable some stupid VC++ warnings so we can use warning level 4.  Most of these need
// to be disabled because of the STL.
#ifdef _MSC_VER
#pragma warning(disable: 4511)			// warning C4511: copy constructor could not be generated
#pragma warning(disable: 4018)			// warning C4018: signed/unsigned mismatch
										// This one is just too annoying to leave on, and Intel C/C++'s value range warnings are much more useful.

// Compilation workaround for high char annoyance in the Platform SDK:
//
// c:\platsdk5\include\uuids.h : warning C4819: The file contains a character that cannot
// be represented in the current code page (932). Save the file in Unicode format to
// prevent data loss
#pragma warning(disable: 4819)
#endif

#if defined(_DEBUG) || defined(_M_IX86)
	#include <vd2/system/thread.h>
	#include <vd2/system/w32assist.h>
	#include <vd2/system/protscope.h>
	#include <vd2/Dita/interface.h>
	#include <vd2/Kasumi/pixmap.h>
	#include <vd2/Kasumi/pixmapops.h>
	#include <vd2/Kasumi/pixmaputils.h>
	#include "AudioSource.h"
	#include "VideoSource.h"
	#include "InputFile.h"
	#include "gui.h"
	#include "misc.h"
	#include "oshelper.h"
	#include "project.h"
	#include "capture.h"
	#include "captureui.h"
	//#include "resource.h"
	#include "prefs.h"
	#include "command.h"
	#include "Dub.h"
	#include "DubOutput.h"
#endif

#endif
