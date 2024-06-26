#include "CrashHandler.h"

#include <iostream>
#include <sstream>
#include <Windows.h>

#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include "WerReport.h"

namespace CrashHandler
{
    CRITICAL_SECTION s_CrashHandlerCritricalSection;
    DWORD s_ExceptionThreadID = 0;

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

    static void SetExceptionInProgress(const DWORD ticketTrackerThreadID)
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

    static __declspec(noinline) void SpinForever()
    {
        while (true)
        {
            SwitchToThread();
        }
    }

    static LONG WINAPI SpinWaitExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo)
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

    LONG WINAPI BasicUnhandledExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo)
    {
        ScopedCriticalSection scopedCriticalSection(&s_CrashHandlerCritricalSection);

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

            __debugbreak();
        }

        const auto result = WERReport::CreateReport(ExceptionInfo);
        std::cout << "Done! please wait while the process dies" << std::endl;

        LONG lResult = EXCEPTION_CONTINUE_SEARCH;
        switch (result)
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

    void InitializeCriticalSection()
    {
        InitializeCriticalSection(&s_CrashHandlerCritricalSection);
    }
} // namespace CrashHandler
