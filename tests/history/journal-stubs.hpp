// Standalone prelude of the journal target: Windows and WinRT base headers, the standard library,
// and stubs for what the extracted journal reads outside itself: the unload flag, the windows'
// notifier (counting), Windhawk's log, storage path and values (the storage folder in a test
// folder, values in memory), LocalAppData (another test folder), the clock, and failure injection
// for the file APIs the journal writes through and for allocations. Never the user's real storage,
// registry or Windhawk values.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <sddl.h>
#include <bcrypt.h>
#include <unknwn.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define WH_MOD_ID L"history-journal-test"

static std::atomic<bool> g_unloading{false};
// The journal's status notifier posts to the settings and history windows; here it only counts.
static int settingsNotifications = 0;
static void NotifySettingsWindowChanged(bool = false) { ++settingsNotifications; }

// Windhawk: the log (counted, the latest format kept; it never allocates, as a failure path may
// log out of memory), the mod's storage folder (this test's, none when empty) and its values.
static int g_testLogs = 0;
static const wchar_t* g_testLastLog = L"";
static void Wh_Log(const wchar_t* format, ...) {
    ++g_testLogs;
    g_testLastLog = format;
}
static std::wstring g_testStorage;
static size_t Wh_GetModStoragePath(PWSTR buffer, size_t size) {
    if (g_testStorage.empty() || g_testStorage.size() >= size) return 0;
    wcscpy_s(buffer, size, g_testStorage.c_str());
    return g_testStorage.size();
}
static std::map<std::wstring, std::vector<BYTE>> g_testValues;
static size_t Wh_GetBinaryValue(PCWSTR name, void* buffer, size_t size) {
    auto value = g_testValues.find(name);
    if (value == g_testValues.end() || value->second.size() > size) return 0;
    memcpy(buffer, value->second.data(), value->second.size());
    return value->second.size();
}
static BOOL Wh_SetBinaryValue(PCWSTR name, const void* buffer, size_t size) {
    g_testValues[name].assign(static_cast<const BYTE*>(buffer), static_cast<const BYTE*>(buffer) + size);
    return TRUE;
}
static BOOL Wh_DeleteValue(PCWSTR name) { return g_testValues.erase(name) != 0; }

// The clock.
static ULONGLONG g_testNow = 1800000000000ULL;
static ULONGLONG NowUnixMs() { return g_testNow; }

// LocalAppData, where versions before 2.18.0 kept the journal: this test's folder.
static std::wstring g_testLocalAppData;
static HRESULT TestKnownFolderPath(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR* path) {
    *path = static_cast<PWSTR>(CoTaskMemAlloc((g_testLocalAppData.size() + 1) * sizeof(wchar_t)));
    if (!*path) return E_OUTOFMEMORY;
    wcscpy_s(*path, g_testLocalAppData.size() + 1, g_testLocalAppData.c_str());
    return g_testLocalAppData.empty() ? E_FAIL : S_OK;
}
#define SHGetKnownFolderPath TestKnownFolderPath

// File API failures: after `skip` more calls of an API succeed, the next `count` fail (a failing
// write first writes half its bytes when `partial`). Every call is counted, and each read or write
// first runs its hook (a case's unload, say).
enum TestFileApi { kTestRead, kTestWrite, kTestFlush, kTestMove, kTestSetEnd, kTestApis };
struct TestFault {
    long skip = -1, count = 0;
};
static TestFault g_testFaults[kTestApis];
static long g_testCalls[kTestApis];
static bool g_testPartialWrite = false;
static std::function<void()> g_testOnRead, g_testOnWrite;
static bool TestFails(TestFileApi api) {
    ++g_testCalls[api];
    TestFault& fault = g_testFaults[api];
    if (fault.skip < 0) return false;
    if (fault.skip > 0) {
        --fault.skip;
        return false;
    }
    if (--fault.count <= 0) fault.skip = -1;
    return true;
}
static BOOL TestReadFile(HANDLE file, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped) {
    if (g_testOnRead) g_testOnRead();
    if (TestFails(kTestRead)) {
        if (read) *read = 0;
        SetLastError(ERROR_READ_FAULT);
        return FALSE;
    }
    return ReadFile(file, buffer, size, read, overlapped);
}
static BOOL TestWriteFile(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped) {
    if (g_testOnWrite) g_testOnWrite();
    if (TestFails(kTestWrite)) {
        DWORD half = 0;
        if (g_testPartialWrite && size > 1) WriteFile(file, buffer, size / 2, &half, overlapped);
        if (written) *written = half;
        SetLastError(ERROR_DISK_FULL);
        return FALSE;
    }
    return WriteFile(file, buffer, size, written, overlapped);
}
static BOOL TestFlushFileBuffers(HANDLE file) {
    if (TestFails(kTestFlush)) {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return FlushFileBuffers(file);
}
static BOOL TestMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (TestFails(kTestMove)) {
        SetLastError(ERROR_SHARING_VIOLATION);
        return FALSE;
    }
    return MoveFileExW(from, to, flags);
}
static BOOL TestSetEndOfFile(HANDLE file) {
    if (TestFails(kTestSetEnd)) {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return SetEndOfFile(file);
}
#define ReadFile TestReadFile
#define WriteFile TestWriteFile
#define FlushFileBuffers TestFlushFileBuffers
#define MoveFileExW TestMoveFileExW
#define SetEndOfFile TestSetEndOfFile

// Allocations that still succeed before every further one throws (-1: no limit), this thread's.
static thread_local long g_testAllocations = -1;
static void* TestAllocate(std::size_t size) {
    if (g_testAllocations == 0) throw std::bad_alloc();
    if (g_testAllocations > 0) --g_testAllocations;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new(std::size_t size) { return TestAllocate(size); }
void* operator new[](std::size_t size) { return TestAllocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
