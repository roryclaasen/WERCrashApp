#pragma once

#include <Windows.h>

#include <werapi.h>
#pragma comment(lib, "wer.lib")

namespace CrashApp
{
    WER_SUBMIT_RESULT CreateReport(struct ::_EXCEPTION_POINTERS* ExceptionInfo);
}
