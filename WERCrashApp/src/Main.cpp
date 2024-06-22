#include <iostream>
#include <Windows.h>

#include "CrashHandler.h"

namespace
{
    static bool OptionExists(char** begin, char** end, const std::string& option)
    {
        return std::find(begin, end, option) != end;
    }

    static void DereferenceNull()
    {
        struct CrashHelper
        {
            int m_DoCrash;
        };

        static_cast<CrashHelper*>(nullptr)->m_DoCrash = 1;
    }
} // namespace

int main(int argc, char* argv[])
{
    if (OptionExists(argv, argv + argc, "-wait"))
    {
        CrashHandler::s_ShouldWait = true;
        std::cout << "When crashing, this program will wait till a debugger is attached." << std::endl;
    }

    CrashHandler::InitializeCriticalSection();
    SetUnhandledExceptionFilter(CrashHandler::BasicUnhandledExceptionHandler);

    DereferenceNull();
    return 0;
}
