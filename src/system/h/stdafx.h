// Force C locale to avoid this warning:
//
// mmreg.h : warning C4819: The file contains a character that cannot be represented in the current code page (932). Save the file in Unicode format to prevent data loss
#pragma setlocale("C")

#define _WIN32_WINNT 0x0600

#include <vd2/system/vdtypes.h>
#include <vd2/system/atomic.h>
#include <vd2/system/thread.h>
#include <vd2/system/error.h>
#include <windows.h>
#include <process.h>
#include <vd2/system/win32/intrin.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
