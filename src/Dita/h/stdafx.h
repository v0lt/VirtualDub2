#ifndef f_DITA_STDAFX_H
#define f_DITA_STDAFX_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0500
#error _WIN32_WINNT is less than 5.0. This will break the places bar on the load/save dialog.
#endif

#include <vd2/system/vdtypes.h>
#include <vd2/Dita/interface.h>

#include <windows.h>

#include <vd2/system/VDString.h>
#include <vd2/Dita/basetypes.h>
#include <vd2/Dita/controls.h>
#include <vd2/Dita/services.h>
#include <vd2/Dita/w32control.h>

#endif
