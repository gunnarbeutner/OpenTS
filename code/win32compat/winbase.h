/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The kernel32 surface of <winbase.h>: files, modules, threads, synchronization, memory, clocks, and the crash path. Each entry point is a stub except where its implementation says otherwise.

#pragma once

#include "windef.h"
#include "winerror.h"

typedef DWORD (WINAPI * LPTHREAD_START_ROUTINE)(LPVOID);
typedef struct _SYSTEMTIME {
	WORD wYear;
	WORD wMonth;
	WORD wDayOfWeek;
	WORD wDay;
	WORD wHour;
	WORD wMinute;
	WORD wSecond;
	WORD wMilliseconds;
} SYSTEMTIME, * PSYSTEMTIME, * LPSYSTEMTIME;
typedef struct _SECURITY_ATTRIBUTES {
	DWORD nLength;
	LPVOID lpSecurityDescriptor;
	BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, * PSECURITY_ATTRIBUTES, * LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
	ULONG_PTR Internal;
	ULONG_PTR InternalHigh;
	DWORD Offset;
	DWORD OffsetHigh;
	HANDLE hEvent;
} OVERLAPPED, * LPOVERLAPPED;
typedef struct _WIN32_FIND_DATAA {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	CHAR cFileName[260];
	CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, * PWIN32_FIND_DATAA, * LPWIN32_FIND_DATAA, * LPWIN32_FIND_DATA;

typedef struct _BY_HANDLE_FILE_INFORMATION {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD dwVolumeSerialNumber;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD nNumberOfLinks;
	DWORD nFileIndexHigh;
	DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, * LPBY_HANDLE_FILE_INFORMATION;

typedef struct _SYSTEM_INFO {
	DWORD dwOemId;
	DWORD dwPageSize;
	LPVOID lpMinimumApplicationAddress;
	LPVOID lpMaximumApplicationAddress;
	DWORD_PTR dwActiveProcessorMask;
	DWORD dwNumberOfProcessors;
	DWORD dwProcessorType;
	DWORD dwAllocationGranularity;
	WORD wProcessorLevel;
	WORD wProcessorRevision;
} SYSTEM_INFO, * LPSYSTEM_INFO;

typedef struct _MEMORYSTATUS {
	DWORD dwLength;
	DWORD dwMemoryLoad;
	SIZE_T dwTotalPhys;
	SIZE_T dwAvailPhys;
	SIZE_T dwTotalPageFile;
	SIZE_T dwAvailPageFile;
	SIZE_T dwTotalVirtual;
	SIZE_T dwAvailVirtual;
} MEMORYSTATUS, * LPMEMORYSTATUS;
#define INVALID_HANDLE_VALUE	((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_SIZE		((DWORD)0xFFFFFFFF)
#define INVALID_FILE_ATTRIBUTES	((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define INFINITE				0xFFFFFFFF

#define WAIT_OBJECT_0			0x00000000L
#define WAIT_ABANDONED			0x00000080L
#define WAIT_TIMEOUT			0x00000102L
#define WAIT_FAILED				0xFFFFFFFF
#define MAXIMUM_WAIT_OBJECTS	64

#define CREATE_NEW				1
#define CREATE_ALWAYS			2
#define OPEN_EXISTING			3
#define OPEN_ALWAYS				4
#define TRUNCATE_EXISTING		5
#define FILE_FLAG_WRITE_THROUGH		0x80000000
#define FILE_FLAG_RANDOM_ACCESS		0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN	0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE	0x04000000
#define FILE_BEGIN				0
#define FILE_CURRENT			1
#define FILE_END				2

#define DRIVE_UNKNOWN			0
#define DRIVE_NO_ROOT_DIR		1
#define DRIVE_REMOVABLE			2
#define DRIVE_FIXED				3
#define DRIVE_REMOTE			4
#define DRIVE_CDROM				5
#define DRIVE_RAMDISK			6
#define THREAD_PRIORITY_IDLE			(-15)
#define THREAD_PRIORITY_LOWEST			(-2)
#define THREAD_PRIORITY_BELOW_NORMAL	(-1)
#define THREAD_PRIORITY_NORMAL			0
#define THREAD_PRIORITY_ABOVE_NORMAL	1
#define THREAD_PRIORITY_HIGHEST			2
#define THREAD_PRIORITY_TIME_CRITICAL	15

#define NORMAL_PRIORITY_CLASS		0x00000020
#define HIGH_PRIORITY_CLASS			0x00000080
#define IDLE_PRIORITY_CLASS			0x00000040

#define GMEM_FIXED		0x0000
#define GMEM_MOVEABLE	0x0002
#define GMEM_ZEROINIT	0x0040
#define GPTR			(GMEM_FIXED | GMEM_ZEROINIT)
// Clocks are computed from the host.
DWORD GetTickCount(void);
BOOL QueryPerformanceCounter(LARGE_INTEGER * count);
BOOL QueryPerformanceFrequency(LARGE_INTEGER * frequency);
void GetSystemTime(SYSTEMTIME * time);
void GetLocalTime(SYSTEMTIME * time);
void GetSystemTimeAsFileTime(FILETIME * filetime);
inline int lstrlenA(LPCSTR string) { return(string != nullptr ? (int)strlen(string) : 0); }
inline LPSTR lstrcpyA(LPSTR destination, LPCSTR source) { return(strcpy(destination, source)); }
inline LPSTR lstrcatA(LPSTR destination, LPCSTR source) { return(strcat(destination, source)); }
inline int lstrcmpiA(LPCSTR left, LPCSTR right) { return(stricmp(left, right)); }
#define lstrlen		lstrlenA
#define lstrcpy		lstrcpyA
#define lstrcat		lstrcatA
#define lstrcmpi	lstrcmpiA
// The stub layer keeps a last-error slot so callers read back what they set.
DWORD GetLastError(void);
void SetLastError(DWORD error);

/* Process, module, and thread. */
HMODULE GetModuleHandleA(LPCSTR name);
DWORD GetModuleFileNameA(HMODULE module, LPSTR filename, DWORD size);
HMODULE LoadLibraryA(LPCSTR name);
BOOL FreeLibrary(HMODULE module);
FARPROC GetProcAddress(HMODULE module, LPCSTR name);
LPSTR GetCommandLineA(void);
LPWSTR GetCommandLineW(void);
void ExitProcess(UINT code);
HANDLE GetCurrentProcess(void);
HANDLE GetCurrentThread(void);
DWORD GetCurrentThreadId(void);
DWORD GetCurrentProcessId(void);
BOOL SetPriorityClass(HANDLE process, DWORD priorityclass);
BOOL SetThreadPriority(HANDLE thread, int priority);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES attributes, DWORD stacksize, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags, LPDWORD threadid);
BOOL TerminateThread(HANDLE thread, DWORD exitcode);
DWORD ResumeThread(HANDLE thread);
DWORD SuspendThread(HANDLE thread);
void Sleep(DWORD milliseconds);
void GetSystemInfo(LPSYSTEM_INFO info);
void GlobalMemoryStatus(LPMEMORYSTATUS status);
void OutputDebugStringA(LPCSTR string);
#define GetModuleHandle		GetModuleHandleA
#define GetModuleFileName	GetModuleFileNameA
#define LoadLibrary			LoadLibraryA
#define GetCommandLine		GetCommandLineA
#define OutputDebugString	OutputDebugStringA

/* Synchronization. */
void InitializeCriticalSection(LPCRITICAL_SECTION section);
void DeleteCriticalSection(LPCRITICAL_SECTION section);
void EnterCriticalSection(LPCRITICAL_SECTION section);
void LeaveCriticalSection(LPCRITICAL_SECTION section);
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION section);
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manualreset, BOOL initialstate, LPCSTR name);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL initialowner, LPCSTR name);
BOOL ReleaseMutex(HANDLE mutex);
DWORD WaitForSingleObject(HANDLE object, DWORD milliseconds);
DWORD WaitForMultipleObjects(DWORD count, HANDLE const * objects, BOOL waitall, DWORD milliseconds);
LONG InterlockedIncrement(LONG volatile * addend);
LONG InterlockedDecrement(LONG volatile * addend);
LONG InterlockedExchange(LONG volatile * target, LONG value);
#define CreateEvent		CreateEventA
#define CreateMutex		CreateMutexA

// The file layer sits on POSIX. Sharing modes are not enforced, a path whose
// exact spelling does not exist is resolved case-insensitively with either
// separator, and a search returns its matches in case-insensitive name order.
HANDLE CreateFileA(LPCSTR filename, DWORD access, DWORD sharemode, LPSECURITY_ATTRIBUTES attributes, DWORD creation, DWORD flags, HANDLE templatefile);
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD tobread, LPDWORD read, LPOVERLAPPED overlapped);
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD towrite, LPDWORD written, LPOVERLAPPED overlapped);
DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distancehigh, DWORD method);

DWORD GetFileSize(HANDLE file, LPDWORD sizehigh);
BOOL SetEndOfFile(HANDLE file);
BOOL FlushFileBuffers(HANDLE file);
BOOL CloseHandle(HANDLE object);
BOOL DeleteFileA(LPCSTR filename);
BOOL MoveFileA(LPCSTR existing, LPCSTR newname);
#define MOVEFILE_REPLACE_EXISTING	0x00000001
BOOL MoveFileExA(LPCSTR existing, LPCSTR newname, DWORD flags);
BOOL CopyFileA(LPCSTR existing, LPCSTR newname, BOOL failifexists);
DWORD GetFileAttributesA(LPCSTR filename);
BOOL SetFileAttributesA(LPCSTR filename, DWORD attributes);
BOOL GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write);
BOOL SetFileTime(HANDLE file, FILETIME const * creation, FILETIME const * access, FILETIME const * write);
HANDLE FindFirstFileA(LPCSTR filename, LPWIN32_FIND_DATAA data);
BOOL FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
BOOL FindClose(HANDLE find);
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES attributes);
BOOL RemoveDirectoryA(LPCSTR path);
DWORD GetCurrentDirectoryA(DWORD length, LPSTR buffer);
BOOL SetCurrentDirectoryA(LPCSTR path);
UINT GetDriveTypeA(LPCSTR root);
DWORD GetLogicalDrives(void);
BOOL GetDiskFreeSpaceExA(LPCSTR path, PULARGE_INTEGER freetocaller, PULARGE_INTEGER total, PULARGE_INTEGER totalfree);
BOOL GetVolumeInformationA(LPCSTR root, LPSTR name, DWORD namesize, LPDWORD serial, LPDWORD componentlength, LPDWORD flags, LPSTR filesystem, DWORD filesystemsize);
DWORD GetTempPathA(DWORD length, LPSTR buffer);
BOOL FileTimeToSystemTime(FILETIME const * filetime, LPSYSTEMTIME systemtime);
BOOL SystemTimeToFileTime(SYSTEMTIME const * systemtime, LPFILETIME filetime);
BOOL FileTimeToLocalFileTime(FILETIME const * filetime, LPFILETIME local);
LONG CompareFileTime(FILETIME const * first, FILETIME const * second);
#define CreateFile			CreateFileA
#define DeleteFile			DeleteFileA
#define MoveFile			MoveFileA
#define CopyFile			CopyFileA
#define GetFileAttributes	GetFileAttributesA
#define SetFileAttributes	SetFileAttributesA
#define FindFirstFile		FindFirstFileA
#define FindNextFile		FindNextFileA
#define CreateDirectory		CreateDirectoryA
#define RemoveDirectory		RemoveDirectoryA
#define GetCurrentDirectory	GetCurrentDirectoryA
#define SetCurrentDirectory	SetCurrentDirectoryA
#define GetDriveType		GetDriveTypeA
#define GetDiskFreeSpaceEx	GetDiskFreeSpaceExA
#define GetVolumeInformation GetVolumeInformationA
#define GetTempPath			GetTempPathA

/* Memory. */
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
HGLOBAL GlobalFree(HGLOBAL memory);
LPVOID GlobalLock(HGLOBAL memory);
BOOL GlobalUnlock(HGLOBAL memory);

HLOCAL LocalFree(HLOCAL memory);
HRSRC FindResourceA(HMODULE module, LPCSTR name, LPCSTR type);
HGLOBAL LoadResource(HMODULE module, HRSRC resource);
LPVOID LockResource(HGLOBAL resource);
DWORD SizeofResource(HMODULE module, HRSRC resource);
#define FindResource			FindResourceA
#define STD_INPUT_HANDLE	((DWORD)-10)
#define STD_OUTPUT_HANDLE	((DWORD)-11)
#define STD_ERROR_HANDLE	((DWORD)-12)
#define EXCEPTION_NONCONTINUABLE			0x1
#define EXCEPTION_ACCESS_VIOLATION			((DWORD)0xC0000005L)
#define EXCEPTION_EXECUTE_HANDLER			1
#define EXCEPTION_CONTINUE_SEARCH			0
#define EXCEPTION_CONTINUE_EXECUTION		(-1)
BOOL TerminateProcess(HANDLE process, UINT exitcode);
void RaiseException(DWORD code, DWORD flags, DWORD count, ULONG_PTR const * arguments);
void AcquireSRWLockExclusive(PSRWLOCK lock);
void ReleaseSRWLockExclusive(PSRWLOCK lock);
void AcquireSRWLockShared(PSRWLOCK lock);
void ReleaseSRWLockShared(PSRWLOCK lock);
void InitializeSRWLock(PSRWLOCK lock);
#define ZeroMemory(d, n)	memset((d), 0, (n))
#define CopyMemory(d, s, n)	memcpy((d), (s), (n))
#define MoveMemory(d, s, n)	memmove((d), (s), (n))
#define FillMemory(d, n, f)	memset((d), (f), (n))
BOOL GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION information);
BOOL FileTimeToDosDateTime(FILETIME const * filetime, LPWORD dosdate, LPWORD dostime);
BOOL DosDateTimeToFileTime(WORD dosdate, WORD dostime, LPFILETIME filetime);
BOOL SetStdHandle(DWORD handle, HANDLE value);
BOOL IsDebuggerPresent(void);
BOOL DeviceIoControl(HANDLE device, DWORD code, LPVOID inbuffer, DWORD insize, LPVOID outbuffer, DWORD outsize, LPDWORD returned, LPOVERLAPPED overlapped);
#define FORMAT_MESSAGE_ALLOCATE_BUFFER	0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS	0x00000200
#define FORMAT_MESSAGE_FROM_STRING		0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE		0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM		0x00001000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK	0x000000FF
DWORD FormatMessageA(DWORD flags, LPCVOID source, DWORD messageid, DWORD languageid, LPSTR buffer, DWORD size, va_list * arguments);
#define FormatMessage	FormatMessageA
#define EXCEPTION_DATATYPE_MISALIGNMENT			((DWORD)0x80000002L)
#define EXCEPTION_BREAKPOINT					((DWORD)0x80000003L)
#define EXCEPTION_SINGLE_STEP					((DWORD)0x80000004L)
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED			((DWORD)0xC000008CL)
#define EXCEPTION_FLT_DENORMAL_OPERAND			((DWORD)0xC000008DL)
#define EXCEPTION_FLT_DIVIDE_BY_ZERO			((DWORD)0xC000008EL)
#define EXCEPTION_FLT_INEXACT_RESULT			((DWORD)0xC000008FL)
#define EXCEPTION_FLT_INVALID_OPERATION			((DWORD)0xC0000090L)
#define EXCEPTION_FLT_OVERFLOW					((DWORD)0xC0000091L)
#define EXCEPTION_FLT_STACK_CHECK				((DWORD)0xC0000092L)
#define EXCEPTION_FLT_UNDERFLOW					((DWORD)0xC0000093L)
#define EXCEPTION_INT_DIVIDE_BY_ZERO			((DWORD)0xC0000094L)
#define EXCEPTION_INT_OVERFLOW					((DWORD)0xC0000095L)
#define EXCEPTION_PRIV_INSTRUCTION				((DWORD)0xC0000096L)
#define EXCEPTION_IN_PAGE_ERROR					((DWORD)0xC0000006L)
#define EXCEPTION_ILLEGAL_INSTRUCTION			((DWORD)0xC000001DL)
#define EXCEPTION_NONCONTINUABLE_EXCEPTION		((DWORD)0xC0000025L)
#define EXCEPTION_STACK_OVERFLOW				((DWORD)0xC00000FDL)
#define EXCEPTION_INVALID_DISPOSITION			((DWORD)0xC0000026L)
#define EXCEPTION_GUARD_PAGE					((DWORD)0x80000001L)
#define EXCEPTION_INVALID_HANDLE				((DWORD)0xC0000008L)
typedef LONG (WINAPI * PTOP_LEVEL_EXCEPTION_FILTER)(LPEXCEPTION_POINTERS);
PTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(PTOP_LEVEL_EXCEPTION_FILTER filter);
#define CONTROL_C_EXIT		((DWORD)0xC000013AL)
HANDLE OpenMutexA(DWORD access, BOOL inherit, LPCSTR name);
#define OpenMutex	OpenMutexA
/* Profile files. */
UINT GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT defaultvalue, LPCSTR filename);
DWORD GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR defaultvalue, LPSTR returned, DWORD size, LPCSTR filename);
BOOL WritePrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR filename);
#define GetPrivateProfileInt	GetPrivateProfileIntA
#define GetPrivateProfileString	GetPrivateProfileStringA
#define WritePrivateProfileString WritePrivateProfileStringA
