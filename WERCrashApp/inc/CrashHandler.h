#pragma once

#include <Windows.h>

namespace CrashHandler
{
    static bool s_ShouldWait{ false };

    LONG WINAPI BasicUnhandledExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo);

    void InitializeCriticalSection();
}
