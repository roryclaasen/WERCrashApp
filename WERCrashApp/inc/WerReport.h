#pragma once

#include <Windows.h>

#include <werapi.h>
#pragma comment(lib, "wer.lib")

namespace WERReport
{
    WER_SUBMIT_RESULT CreateReport(EXCEPTION_POINTERS* ExceptionInfo);
}
