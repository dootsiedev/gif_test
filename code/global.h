#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <chrono>

#ifndef NO_THREAD
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#endif // NO_THREAD


#include <assert.h>

//assert is bad, but sometimes an explosion is the only choice, eg: PTR IS NULL
#define ASSERT(x) assert(x)

#define RAPIDJSON_ASSERT ASSERT
#include <rapidjson/document.h>
namespace rj = rapidjson;

#include <SDL2/SDL.h>

// this is a vague signal to say "THIS FUNCTION PRINTS TO SERR, CAPTURE IT!"
// if a function return is not for serr, just use [[nodiscard]].
#define MYNODISCARD [[nodiscard]]

//CHECK is a soft assert, where the error is probably the programmers fault so the message is not user friendly, 
//but unlike ASSERT the program can still continue.
//returns the value of the condition, works the same way as assert, and prints a stack trace.
//WARNING: check prints into serr, which should be handled by serr_get_error.
#define CHECK(expr) implement_CHECK((expr), #expr, __FILE__, __LINE__)
MYNODISCARD bool implement_CHECK(bool cond, const char* expr, const char* file, int line);

// msvc doesn't support __attribute__, unless it is clang-cl.
#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(x)
#endif

#ifdef _WIN32
//get message from GetLastError()
std::string WIN_GetFormattedGLE();
//convenience conversion to/from win32 specific functions since only "wide" is unicode.
std::string WIN_WideToUTF8(const wchar_t* buffer, int size);
std::wstring WIN_UTF8ToWide(const char* buffer, int size);
#endif

typedef double TIMER_RESULT;
#define TIMER_MS 1000
#define TIMER_SEC 1


typedef std::chrono::steady_clock::time_point TIMER_U;

inline TIMER_U timer_now(){
  return std::chrono::steady_clock::now();
}

//resolution is based on how much to divide 1 seconds
//1 = print in seconds, 1000 = print in milliseconds
template<size_t resolution>
TIMER_RESULT timer_delta(const TIMER_U start, const TIMER_U end)
{
  return std::chrono::duration_cast< std::chrono::duration<TIMER_RESULT, std::ratio<1, resolution>>>(end-start).count();
}

//I want to use this, but I randomly got a negative number causing UBsan to trigger
//probably a problem with QPC with multiple threads?
#if 0
typedef Uint64 TIMER_U;

inline TIMER_U timer_now()
{
    return SDL_GetPerformanceCounter();
}

//resolution is based on how much to divide 1 seconds
//1 = print in seconds, 1000 = print in milliseconds
template<Uint64 resolution>
inline TIMER_RESULT timer_delta(TIMER_U start, TIMER_U end)
{
  Uint64 frequency = SDL_GetPerformanceFrequency();
  return (TIMER_RESULT)((end - start)*resolution) / (TIMER_RESULT)frequency;
}
#endif

//the main purpose of this logging system is
//to redirect the logs into both stdout and a log file, 
//and for being a thread safe logging system,
//but thread safety is only per call, you must always include a newline.

//serr will print into stdout and the log file like slog, 
//but the serr message will be buffered into serr_get_error.
//use serr if there is a fatal error that stops everything.
//mainly for initialization and for a GUI message to pop up.
//serr is not useful for a terminal.

//I don't use C++ streams because thread safety is overcomplicated 
//(and printf is technically thread safe on common OS's, and if not I can easily make a mutex).
//libfmt is better than printf, but also really weird in my opinion, 
//you don't need to think too hard about printf.

//this returns all previous errors into one message, and clears the buffer.
//but ideally there should only be one error at a time.
std::string serr_get_error();

//this will just check if anything was printed to serr.
bool serr_check_error();

//needed by the debug tools to get the serr buffer of a thread.
std::shared_ptr<std::string> internal_get_serr_buffer();

void slog_raw(const char * msg, size_t len);
void serr_raw(const char * msg, size_t len);

void slog(const char * msg);
void serr(const char * msg);

void slogf(const char * msg, ...)
__attribute__((format(printf, 1, 2)));

void serrf(const char * msg, ...)
__attribute__((format(printf, 1, 2)));
