#pragma once

#include <exception>
// SATURN: <iostream> instantiates std::locale, basic_streambuf and the
// iostream machinery -- hundreds of KB of libstdc++ in a program that must
// fit in 1 MB of HWRAM.  The only user is RedirectIoStream below, which
// points std::cout at a log window that does not exist on a console.
#ifndef TETHYS_SATURN
#include <iostream>
#endif
// printf/fputc/abort were reaching this header THROUGH <iostream>; removing it
// took them with it and broke all 204 engine translation units at once.  Named
// explicitly now, which is what the file always needed.
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "Types.hpp"

#if _MSC_VER
    #define FNAME __FUNCTION__
#else
    #define FNAME __PRETTY_FUNCTION__
#endif


#undef ERROR

// SATURN: every LOG_* call site embeds two strings in .rodata -- the format
// AND FNAME, which is __PRETTY_FUNCTION__: the whole demangled signature,
// "void Abe::Motion_0_Idle_44EEB0()" and a thousand more like it.  On this
// target they all end up in sat_console_putc, which does nothing: there is no
// console on a Saturn and diagnosis goes through SRL::Debug rows on the TV
// instead.  So the strings, and the calls that reference them, are paid for in
// a 1 MB budget and read by no one.
//
// ALIVE_FATAL is unaffected -- it is FatalError.hpp's, not this header's, and
// still stops visibly.
#if !TETHYS_SATURN
    #define LOGGING 1
#endif

enum class LogLevels
{
    Trace,
    Info,
    Warning,
    Error,
};

inline void log_impl(LogLevels logLevel, const char* funcName, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    switch (logLevel)
    {
        case LogLevels::Trace:
            printf("[T] ");
            break;
        case LogLevels::Info:
            printf("[I] ");
            break;
        case LogLevels::Warning:
            printf("[W] ");
            break;
        case LogLevels::Error:
            printf("[!] ");
            break;
    }
    printf("[%s] ", funcName);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

#ifdef LOGGING
    #define TRACE_ENTRYEXIT Logging::AutoLog __funcTrace(FNAME)
    #define LOG_TRACE(fmt, ...) log_impl(LogLevels::Trace, FNAME, fmt, ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...) log_impl(LogLevels::Info, FNAME, fmt, ##__VA_ARGS__)
    #define LOG_WARNING(fmt, ...) log_impl(LogLevels::Warning, FNAME, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) log_impl(LogLevels::Error, FNAME, fmt, ##__VA_ARGS__)
    #define LOG(fmt, ...) log_impl(LogLevels::Trace, FNAME, fmt, ##__VA_ARGS__)
#else
    #define TRACE_ENTRYEXIT
    #define LOG_TRACE(fmt, ...)
    #define LOG_INFO(fmt, ...)
    #define LOG_WARNING(fmt, ...)
    #define LOG_ERROR(fmt, ...)
    #define LOG(fmt, ...)
#endif

[[noreturn]] inline void HOOK_FATAL(const char_type* errMsg)
{
    LOG_ERROR(errMsg);
    abort();
}


#ifndef TETHYS_SATURN
class outbuf : public std::streambuf
{
public:
    outbuf()
    {
        setp(0, 0);
    }

    virtual int_type overflow(int_type c = traits_type::eof()) override
    {
        return fputc(c, stdout) == EOF ? traits_type::eof() : c;
    }
};

#else  // TETHYS_SATURN
inline void RedirectIoStream(bool)
{
    // No console, no window: nothing to redirect.
}
#endif

#ifndef TETHYS_SATURN
inline void RedirectIoStream(bool replace)
{
    static std::streambuf* sb = nullptr;
    if (replace)
    {
        if (!sb)
        {
            static outbuf ob;
            sb = std::cout.rdbuf(&ob);
        }
    }
    else
    {
        // make sure to restore the original so we don't get a crash on close!
        if (sb)
        {
            std::cout.rdbuf(sb);
        }
        sb = nullptr;
    }
}
#endif // TETHYS_SATURN

namespace Logging {
class AutoLog final
{
public:
    AutoLog(const AutoLog&) = delete;
    AutoLog& operator=(const AutoLog&) = delete;
    AutoLog(const char_type* funcName)
        : mFuncName(funcName)
    {
        log_impl(LogLevels::Trace, mFuncName, "[ENTER]");
    }

    ~AutoLog()
    {
        if (std::uncaught_exceptions())
        {
            log_impl(LogLevels::Trace, mFuncName, "[EXIT_EXCEPTION]");
        }
        else
        {
            log_impl(LogLevels::Trace, mFuncName, "[EXIT]");
        }
    }

private:
    const char_type* mFuncName;
};
} // namespace Logging
