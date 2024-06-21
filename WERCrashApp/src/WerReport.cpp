#include "WerReport.h"

#include <sstream>
#include <strsafe.h>
#include <tchar.h>
#include <vector>

#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#pragma comment(lib, "version.lib")

namespace CrashApp
{
    constexpr int MAX_SPRINTF = 1024;

    static bool CreateTempDirectory(std::wstring& outDirectoryPath)
    {
        constexpr auto tempFolder = L"CrashAppExe";

        WCHAR tempPathBuffer[MAX_PATH];
        DWORD tempPathLength = GetTempPath(MAX_PATH, tempPathBuffer);
        if (tempPathLength == 0 || tempPathLength > MAX_PATH) {
            return false;
        }
        std::wstringstream ss;
        ss << tempPathBuffer << tempFolder;
        if (CreateDirectory(ss.str().c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
            outDirectoryPath = ss.str();
            return true;
        }

        return false;
    }

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
                VS_FIXEDFILEINFO* FixedFileInfo = nullptr;

                UINT InfoLength = 0;
                if (VerQueryValue(VersionInfo.data(), TEXT("\\"), (void**)&FixedFileInfo, &InfoLength))
                {
                    StringCchPrintf(StringBuffer, MaxSize, TEXT("%u.%u.%u.%u"),
                        HIWORD(FixedFileInfo->dwProductVersionMS), LOWORD(FixedFileInfo->dwProductVersionMS), HIWORD(FixedFileInfo->dwProductVersionLS), HIWORD(0));
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
    static HRESULT SetReportParameters(HREPORT reportHandle, EXCEPTION_POINTERS* exceptionInfo, const WCHAR* errorMessage, const WCHAR* applicationName)
    {
        HRESULT Result;
        TCHAR StringBuffer[MAX_SPRINTF] = { 0 };
        TCHAR LocalBuffer[MAX_SPRINTF] = { 0 };

        // Set the parameters for the standard problem signature
        HMODULE ModuleHandle = GetModuleHandle(NULL);

        StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%s"), applicationName);
        Result = WerReportSetParameter(reportHandle, WER_P0, TEXT("Application Name"), StringBuffer);

        GetModuleFileName(ModuleHandle, LocalBuffer, MAX_SPRINTF);
        PathStripPath(LocalBuffer);
        GetModuleVersion(LocalBuffer, StringBuffer, MAX_SPRINTF);
        Result = WerReportSetParameter(reportHandle, WER_P1, TEXT("Application Version"), StringBuffer);

        StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%08x"), GetTimestampForLoadedLibrary(ModuleHandle));
        // Result = WerReportSetParameter(reportHandle, WER_P2, TEXT("Application Timestamp"), StringBuffer);
        Result = WerReportSetParameter(reportHandle, WER_P2, TEXT("Application Timestamp"), TEXT("6672cbc2"));

        HMODULE FaultModuleHandle = NULL;
        GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)exceptionInfo->ExceptionRecord->ExceptionAddress, &FaultModuleHandle);

        GetModuleFileName(FaultModuleHandle, LocalBuffer, MAX_SPRINTF);
        PathStripPath(LocalBuffer);
        Result = WerReportSetParameter(reportHandle, WER_P3, TEXT("Fault Module Name"), LocalBuffer);

        GetModuleVersion(LocalBuffer, StringBuffer, MAX_SPRINTF);
        Result = WerReportSetParameter(reportHandle, WER_P4, TEXT("Fault Module Version"), StringBuffer);

        StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%08x"), GetTimestampForLoadedLibrary(FaultModuleHandle));
        // Result = WerReportSetParameter(reportHandle, WER_P5, TEXT("Fault Module Timestamp"), StringBuffer);
        Result = WerReportSetParameter(reportHandle, WER_P5, TEXT("Fault Module Timestamp"), TEXT("6672cbc2"));

        StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%08x"), exceptionInfo->ExceptionRecord->ExceptionCode);
        Result = WerReportSetParameter(reportHandle, WER_P6, TEXT("Exception Code"), StringBuffer);

        INT_PTR ExceptionOffset = (char*)(exceptionInfo->ExceptionRecord->ExceptionAddress) - (char*)FaultModuleHandle;
        StringCchPrintf(StringBuffer, MAX_SPRINTF, TEXT("%p"), (void*)ExceptionOffset);
        Result = WerReportSetParameter(reportHandle, WER_P7, TEXT("Exception Offset"), StringBuffer);

        // Set custom parameters

        // Result = WerReportSetParameter(ReportHandle, WER_P8, TEXT("Internal"), TEXT("This is a test"));
        // Result = WerReportSetParameter(ReportHandle, WER_P9, TEXT("BranchBaseDir"), TEXT("LOCAL"));

        return Result;
    }

    WER_SUBMIT_RESULT CreateReport(::_EXCEPTION_POINTERS* ExceptionInfo)
    {
        WER_REPORT_INFORMATION reportInfo = { sizeof(WER_REPORT_INFORMATION) };
        {
            TCHAR exePath[MAX_PATH];
            GetModuleFileName(NULL, exePath, MAX_PATH);

            StringCchCopy(reportInfo.wzConsentKey, ARRAYSIZE(reportInfo.wzConsentKey), TEXT(""));

            StringCchCopy(reportInfo.wzApplicationPath, ARRAYSIZE(reportInfo.wzApplicationPath), exePath);
            PathStripPath(exePath);
            StringCchCopy(reportInfo.wzApplicationName, ARRAYSIZE(reportInfo.wzApplicationName), exePath);

            StringCchCopy(reportInfo.wzDescription, ARRAYSIZE(reportInfo.wzDescription), TEXT("The application crashed while running the game"));
        }

        WER_SUBMIT_RESULT SubmitResult = WerReportFailed;
        HREPORT ReportHandle = NULL;
        std::wstring tempFolder;
        CreateTempDirectory(tempFolder);

        if (WerReportCreate(APPCRASH_EVENT, WerReportApplicationCrash, &reportInfo, &ReportHandle) == S_OK)
        {
            constexpr auto ErrorMessage = TEXT("Unhandled exception occurred!");

            SetReportParameters(ReportHandle, ExceptionInfo, ErrorMessage, reportInfo.wzApplicationName);

            const std::wstring MinidumpFileName = tempFolder + L"\\WERCrashApp.dmp";
            if (WriteMinidump(MinidumpFileName.c_str(), ExceptionInfo, true, GetCurrentThreadId()))
            {
                WerReportAddFile(ReportHandle, MinidumpFileName.c_str(), WerFileTypeMinidump, WER_FILE_ANONYMOUS_DATA);
            }

            const std::wstring KVP_File = tempFolder + L"\\dump_kvps.txt";
            // TODO: Check if this file exists before adding it
            WerReportAddFile(ReportHandle, KVP_File.c_str(), WerFileTypeOther, WER_FILE_ANONYMOUS_DATA);

            WerReportSubmit(ReportHandle, WerConsentAlwaysPrompt, WER_SUBMIT_QUEUE | WER_SUBMIT_BYPASS_DATA_THROTTLING, &SubmitResult);

            WerReportCloseHandle(ReportHandle);
        }

        return SubmitResult;
    }
}