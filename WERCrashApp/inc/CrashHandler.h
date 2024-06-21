#pragma once

#include <Windows.h>

namespace CrashApp
{
    static bool s_ShouldWait{ false };

    LONG WINAPI BasicUnhandledExceptionHandler(struct ::_EXCEPTION_POINTERS* ExceptionInfo);

    void InitializeCriticalSection();
}
