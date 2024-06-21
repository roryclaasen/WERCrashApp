#include <iostream>
#include <sstream>
#include <strsafe.h>
#include <tchar.h>
#include <vector>
#include <Windows.h>

#include <werapi.h>
#pragma comment( lib, "wer.lib" )

#include <DbgHelp.h>
#include <Shlwapi.h>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "Shlwapi.lib")

constexpr int MAX_SPRINTF = 1024;
constexpr auto APPLICATION_NAME = TEXT("WERCrashApp.exe");

static bool s_ShouldWait = false;

static DWORD s_ExceptionThreadID = 0;

static void SetExceptionInProgress(DWORD ticketTrackerThreadID)
{
    s_ExceptionThreadID = ticketTrackerThreadID;
}

static bool IsExceptionThread()
{
    return (GetCurrentThreadId() == s_ExceptionThreadID);
}

static bool IsExceptionInProgress()
{
    return (s_ExceptionThreadID != 0);
}

__declspec(noinline) void SpinForever()
{
    while (true)
    {
        SwitchToThread();
    }
}

static LONG WINAPI SpinWaitExceptionHandler(struct _EXCEPTION_POINTERS* ExceptionInfo)
{
    if (IsExceptionThread())
    {
        // Prevent recursive exception locking the dump process
        std::cout << "EXCEPTION HANDLER: Recursive exception encountered, skipping inner exception" << std::endl;
    }
    else
    {
        SpinForever();
    }

    return 0;
}

static bool CreateTempDirectory(std::wstring& outDirectoryPath) {
    WCHAR tempPathBuffer[MAX_PATH];
    DWORD tempPathLength = GetTempPath(MAX_PATH, tempPathBuffer);
    if (tempPathLength == 0 || tempPathLength > MAX_PATH) {
        return false;
    }
    std::wstringstream ss;
    ss << tempPathBuffer << "WERCrashApp";
    if (CreateDirectory(ss.str().c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        outDirectoryPath = ss.str();
        return true;
    }

    return false;
}

/**
 * Get the 4 element version number of the module
 */
static void GetModuleVersion(TCHAR* ModuleName, TCHAR* StringBuffer, DWORD MaxSize)
{
    StringCchCopy(StringBuffer, MaxSize, TEXT("0.0.0.0"));

    DWORD Handle = 0;
    DWORD InfoSize = GetFileVersionInfoSize(ModuleName, &Handle);
    if (InfoSize > 0)
    {
        std::vector<char> VersionInfo(InfoSize);

        if (GetFileVersionInfo(ModuleName, 0, InfoSize, VersionInfo.data()))
        {
            VS_FIXEDFILEINFO* FixedFileInfo;

            UINT InfoLength = 0;
            if (VerQueryValue(VersionInfo.data(), TEXT("\\"), (void**)&FixedFileInfo, &InfoLength))
            {
                StringCchPrintf(StringBuffer, MaxSize, TEXT("%u.%u.%u.%u"),
                    HIWORD(FixedFileInfo->dwProductVersionMS), LOWORD(FixedFileInfo->dwProductVersionMS), HIWORD(FixedFileInfo->dwProductVersionLS), HIWORD(1));
            }
        }
    }
}

static BOOL WriteMinidump(const TCHAR* Path, LPEXCEPTION_POINTERS ExceptionInfo, bool bIsEnsure, DWORD ExceptionThreadId)
{
    HANDLE FileHandle = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    // Initialise structure required by MiniDumpWriteDumps
    MINIDUMP_EXCEPTION_INFORMATION DumpExceptionInfo;
    DumpExceptionInfo.ThreadId = ExceptionThreadId;
    DumpExceptionInfo.ExceptionPointers = ExceptionInfo;
    DumpExceptionInfo.ClientPointers = FALSE;

    MINIDUMP_TYPE mdt = MiniDumpNormal;
    bool shouldBeFullCrashDump = bIsEnsure && true;
    if (shouldBeFullCrashDump)
    {
        mdt = MiniDumpWithFullMemory;
    }

    const BOOL Result = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), FileHandle, mdt, (ExceptionInfo ? &DumpExceptionInfo : NULL), NULL, NULL);
    CloseHandle(FileHandle);

    return Result;
}

/**
 * Set the ten Windows Error Reporting parameters
 *
 * Parameters 0 through 7 are predefined for Windows
 * Parameters 8 and 9 are user defined
 */
static void SetReportParameters(HREPORT ReportHandle, EXCEPTION_POINTERS* ExceptionInfo, const WCHAR* ErrorMessage)
{
    HRESULT Result;
    TCHAR StringBuffer[MAX_SPRINTF] = { 0 };
    TCHAR LocalBuffer[MAX_SPRINTF] = { 0 };

    // Set the parameters for the standard problem signature
    HMODULE ModuleHandle = GetModuleHandle(NULL);

    // TODO: Get the application name
    StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%s"), APPLICATION_NAME);
    Result = WerReportSetParameter(ReportHandle, WER_P0, TEXT("Application Name"), StringBuffer);

    GetModuleFileName(ModuleHandle, LocalBuffer, MAX_SPRINTF);
    PathStripPath(LocalBuffer);
    GetModuleVersion(LocalBuffer, StringBuffer, MAX_SPRINTF);
    Result = WerReportSetParameter(ReportHandle, WER_P1, TEXT("Application Version"), StringBuffer);

    StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%08x"), GetTimestampForLoadedLibrary(ModuleHandle));
    Result = WerReportSetParameter(ReportHandle, WER_P2, TEXT("Application Timestamp"), TEXT("6672cbc2"));

    HMODULE FaultModuleHandle = NULL;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)ExceptionInfo->ExceptionRecord->ExceptionAddress, &FaultModuleHandle);

    GetModuleFileName(FaultModuleHandle, LocalBuffer, MAX_SPRINTF);
    PathStripPath(LocalBuffer);
    Result = WerReportSetParameter(ReportHandle, WER_P3, TEXT("Fault Module Name"), LocalBuffer);

    GetModuleVersion(LocalBuffer, StringBuffer, MAX_SPRINTF);
    Result = WerReportSetParameter(ReportHandle, WER_P4, TEXT("Fault Module Version"), StringBuffer);

    StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%08x"), GetTimestampForLoadedLibrary(FaultModuleHandle));
    Result = WerReportSetParameter(ReportHandle, WER_P5, TEXT("Fault Module Timestamp"), TEXT("6672cbc2"));

    StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%08x"), ExceptionInfo->ExceptionRecord->ExceptionCode);
    Result = WerReportSetParameter(ReportHandle, WER_P6, TEXT("Exception Code"), StringBuffer);

    INT_PTR ExceptionOffset = (char*)(ExceptionInfo->ExceptionRecord->ExceptionAddress) - (char*)FaultModuleHandle;
    StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%p"), (void*)ExceptionOffset);

    // Convert the string buffer to lowercase
    _tcslwr_s(StringBuffer);
    Result = WerReportSetParameter(ReportHandle, WER_P7, TEXT("Exception Offset"), StringBuffer);

    // Set custom parameters

    //Result = WerReportSetParameter(ReportHandle, WER_P8, TEXT("Internal"), TEXT("This is a test"));
    //Result = WerReportSetParameter(ReportHandle, WER_P9, TEXT("BranchBaseDir"), TEXT("LOCAL"));
}

struct ScopedCriticalSection
{
    ScopedCriticalSection(CRITICAL_SECTION* critRef)
    {
        crit = critRef;
        EnterCriticalSection(crit);
    }

    ~ScopedCriticalSection()
    {
        LeaveCriticalSection(crit);
    }

private:
    CRITICAL_SECTION* crit;
};

CRITICAL_SECTION m_cs;

static LONG WINAPI BasicUnhandledExceptionHandler(struct _EXCEPTION_POINTERS* ExceptionInfo)
{
    ScopedCriticalSection scopedCriticalSection(&m_cs);

    SetUnhandledExceptionFilter(SpinWaitExceptionHandler);
    SetExceptionInProgress(GetCurrentThreadId());
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    std::cout << "Unhandled exception occurred!" << std::endl;

    if (s_ShouldWait)
    {
        std::cout << "Waiting for a debugger" << std::endl;
        while (!IsDebuggerPresent())
        {
            Sleep(1);
        }

        //__debugbreak();
    }

    std::wstring tempFolder;
    CreateTempDirectory(tempFolder);

    WER_REPORT_INFORMATION ReportInformation = { sizeof(WER_REPORT_INFORMATION) };
    {
        TCHAR exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);

        StringCchCopy(ReportInformation.wzConsentKey, ARRAYSIZE(ReportInformation.wzConsentKey), TEXT(""));

        StringCchCopy(ReportInformation.wzApplicationPath, ARRAYSIZE(ReportInformation.wzApplicationPath), exePath);
        PathStripPath(exePath);
        StringCchCopy(ReportInformation.wzApplicationName, ARRAYSIZE(ReportInformation.wzApplicationName), exePath);

        StringCchCopy(ReportInformation.wzDescription, ARRAYSIZE(ReportInformation.wzDescription), TEXT("The application crashed while running the game"));
    }

    WER_SUBMIT_RESULT SubmitResult;
    HREPORT ReportHandle = NULL;
    if (WerReportCreate(APPCRASH_EVENT, WerReportApplicationCrash, &ReportInformation, &ReportHandle) == S_OK)
    {
        constexpr auto ErrorMessage = TEXT("Unhandled exception occurred!");

        // Set the standard set of a crash parameters
        SetReportParameters(ReportHandle, ExceptionInfo, ErrorMessage);

        const std::wstring MinidumpFileName = tempFolder + L"\\WERCrashApp.dmp";
        if (WriteMinidump(MinidumpFileName.c_str(), ExceptionInfo, true, GetCurrentThreadId()))
        {
            WerReportAddFile(ReportHandle, MinidumpFileName.c_str(), WerFileTypeMinidump, WER_FILE_ANONYMOUS_DATA);
        }

        const std::wstring KVP_File = tempFolder + L"\\dump_kvps.txt";
        WerReportAddFile(ReportHandle, KVP_File.c_str(), WerFileTypeOther, WER_FILE_ANONYMOUS_DATA);

        // Submit
        WerReportSubmit(ReportHandle, WerConsentAlwaysPrompt, WER_SUBMIT_QUEUE | WER_SUBMIT_BYPASS_DATA_THROTTLING, &SubmitResult);

        // Cleanup
        WerReportCloseHandle(ReportHandle);
    }
    else
    {
        std::cout << "Failed to create WER report!" << std::endl;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::cout << "Done! please wait while the process dies" << std::endl;

    LONG lResult = EXCEPTION_CONTINUE_SEARCH;
    switch (SubmitResult)
    {
    //case WerReportQueued:
    //case WerReportUploaded: // To exit the process
    //    std::cout << "EXCEPTION_EXECUTE_HANDLER" << std::endl;
    //    lResult = EXCEPTION_EXECUTE_HANDLER;
    //    break;

    case WerReportDebug: // To end up in the debugger
        std::cout << "EXCEPTION_CONTINUE_SEARCH" << std::endl;
        lResult = EXCEPTION_CONTINUE_SEARCH;
        break;

    default: // Let the OS handle the exception
        std::cout << "EXCEPTION_CONTINUE_SEARCH" << std::endl;
        lResult = EXCEPTION_CONTINUE_SEARCH;
        break;
    }

    return lResult;
}



static void DereferenceNull()
{
    struct CrashHelper
    {
        int m_DoCrash;
    };

    static_cast<CrashHelper*>(nullptr)->m_DoCrash = 1;
}

static bool OptionExists(char** begin, char** end, const std::string& option)
{
    return std::find(begin, end, option) != end;
}

int main(int argc, char* argv[])
{
    InitializeCriticalSection(&m_cs);
    SetUnhandledExceptionFilter(BasicUnhandledExceptionHandler);

    if (OptionExists(argv, argv + argc, "-wait"))
    {
        s_ShouldWait = true;

        std::cout << "When crashing, this program will wait till a debugger is attached." << std::endl;
    }

    std::cout << "This program is about to crash!" << std::endl;

    DereferenceNull();

    return 0;
}
