#include <iostream>
#include <Windows.h>

#include "../inc/CrashHandler.h"

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
}

int main(int argc, char* argv[])
{
    if (OptionExists(argv, argv + argc, "-wait"))
    {
        CrashApp::s_ShouldWait = true;
        std::cout << "When crashing, this program will wait till a debugger is attached." << std::endl;
    }

    CrashApp::InitializeCriticalSection();
    SetUnhandledExceptionFilter(CrashApp::BasicUnhandledExceptionHandler);

    DereferenceNull();
    return 0;
}
