#include "global.h"
#include "cvar.h"
#include "debug_tools.h"
#include "win32_crashrpt.h"

#define USING_ADDR2LINE
#define USE_LLVM_SYMBOLIZER

#ifdef USING_ADDR2LINE
#include "wai/whereami.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#if defined(__GNUC__) && !defined(_WIN32)

//TODO: win32_crashrtp also uses this macro, I should try to unify the macro location...
#ifndef MAX_STACK_DEPTH
#define MAX_STACK_DEPTH 30
#endif

//posix
#include <execinfo.h> //backtrace
#include <string.h>
#include <errno.h>

#endif //_WIN32

//not enabled by default even when pthreads are utilized because atm pthreads aren't as robust as win32 threads 
//due to lack of printing a stacktrace for a thread handle.
//which means a deadlock is more helpful for debugging than nothing.
#ifdef HAS_PTHREADS
//I have no idea what _GNU_SOURCE does
#define _GNU_SOURCE
#include <pthread.h>
#include <time.h>
#include <signal.h>
#endif // HAS_PTHREADS



#ifdef USE_WIN32_CRASHRTP_HANDLER

static void string_dump_stack_trace(void* ud, crashrpt_stack_info& data)
{
	std::string* output = reinterpret_cast<std::string*>(ud);
	char buffer[1024];
	int bytes_written;

	if(data.module == NULL)
	{
		bytes_written = snprintf(buffer, sizeof(buffer), "bad frame: 0x%p\n", data.addr);
	} else {
		if(data.function != NULL)
		{
			if(data.file != NULL)
			{
				bytes_written = snprintf(buffer, sizeof(buffer), "%s ! 0x%p in %s [%s @ %d]\n", data.module, data.addr, data.function, data.file, data.line);
			}
			else
			{
				bytes_written = snprintf(buffer, sizeof(buffer), "%s ! 0x%p in %s\n", data.module, data.addr, data.function);
			}
		}
		else
		{
			bytes_written = snprintf(buffer, sizeof(buffer), "%s ! 0x%p in (unknown function)\n", data.module, data.addr);
		}
	}
	output->append(buffer, bytes_written);
}

#endif

#ifdef USING_ADDR2LINE

//this is not signal handler safe.
//on linux set -no-pie
static bool addr2line(const char* program_name, void** addrs, int addr_count, std::string& output)
{
    //TODO: maybe implement vasprintf for msvc instead of using snprintf?
    char addr2line_cmd[512] = {0};
    int cmd_length = 0;

    int ret = snprintf(addr2line_cmd + cmd_length, sizeof(addr2line_cmd) - cmd_length,
    #ifdef USE_LLVM_SYMBOLIZER
        "llvm-symbolizer"
    #else
        "addr2line" 
    #endif //USE_LLVM_SYMBOLIZER
        
    #ifdef _WIN32
        ".exe"
    #endif //_WIN32
    
    #ifdef USE_LLVM_SYMBOLIZER
        //llvm includes an extra newline when doing a batch...
        " --output-style=GNU"
        //this prints the code which is cool
        " --print-source-context-lines=2"
        //this will take quite a bit longer, but it doesn't hurt!
        //" --verify-region-info"
        //does --inlines do anything?
        //" -i"
    #endif //USE_LLVM_SYMBOLIZER
        //f = demangle functions, p = pretty print, e = executable
        " -f -p -e %s", program_name);
    
    if(ret < 0)
    {
        output += "addr2line failed to allocate command for: ";
        output += program_name;
        return false;
    }
    cmd_length += ret;
  
    for(int i = 0; i < addr_count && cmd_length != 0; ++i)
    {
        ret = snprintf(addr2line_cmd + cmd_length, sizeof(addr2line_cmd) - cmd_length, " %p", addrs[i]);
        if(ret < 0)
        {
            output += "addr2line truncate call stack";
            //untested, but should work.
            addr2line_cmd[cmd_length] = '\0';
            break;
        }
        cmd_length += ret;
    }
    
    //fun fact: msvc pre-2015 does not support a conforming snprintf function (technically still exists as _snprintf), 
    //if size written == size of buffer, a null terminator will not be included, and length will be returned.
    //addr2line_cmd[sizeof(addr2line_cmd)-1] = '\0';
    
    FILE* fp = popen(addr2line_cmd, "r");
    if(fp == NULL)
    {
        output += "addr2line failed to call popen: ";
        output += strerror(errno);
        output += '\n';
        output += "command: ";
        output += addr2line_cmd;
        return false;
    }
    
    char read_buffer[512] = {0};
    int index = 0;
    while(true)
    {
        if(fgets(read_buffer, sizeof(read_buffer), fp) == NULL)
        {
            if(feof(fp) != 0)
            {
                break;
            }
            else if(ferror(fp) != 0)
            {
                output += "addr2line read error: ";
                output += strerror(errno);
                output += '\n';
                pclose(fp);
                return false;
            }
            ASSERT(false);
        }
        output.append(read_buffer);
        ++index;
    }
    ret = pclose(fp);
    if(ret < 0)
    {
        output += "addr2line pclose error: ";
        output += strerror(errno);
        output += '\n';
        return false;
    }
    
    return true;
}

#endif // USING_ADDR2LINE

void debug_stacktrace_string(std::string& output, int skip)
{
#ifdef USE_WIN32_CRASHRTP_HANDLER
	crashrpt_print_stack_trace(string_dump_stack_trace, &output, skip+1);
#else
#ifndef _WIN32
    //posix backtrace
    void *buffer[MAX_STACK_DEPTH];
    
    // get void*'s for all entries on the stack
    int nptrs = backtrace(buffer, MAX_STACK_DEPTH);
    
#ifdef USING_ADDR2LINE
    static std::string program_path;
    if(program_path.empty())
    {
        int length = wai_getExecutablePath(NULL, 0, NULL);
        program_path.resize(length,0);
        wai_getExecutablePath(program_path.data(), length, NULL);
    }
    //NOTE: I don't know why the top always has an invalid frame...
    //I need +1 because I want to skip THIS frame.
    skip += 2;
    if(addr2line(program_path.c_str(), buffer + skip, nptrs - skip, output))
    {
        //success
    }
    else
#endif //USING_ADDR2LINE
    {
        char **strings = NULL;
        
        if((strings = backtrace_symbols(buffer, nptrs)) == NULL)
        {
            output += "backtrace_symbols: ";
            output += strerror(errno);
            output += '\n';
        }
        else
        {
            for (int i = 0; i < MAX_STACK_DEPTH && i < nptrs; ++i)
            {
                if(i < skip)
                {
                    continue;
                }
                output += '[';
                output += std::to_string(i);
                output += "] ";
                output += strings[i];
                output += '\n';
            }
            free(strings);
        }
    }
#else
	output += "...Stacktrace Unsupported.\n";
#endif
#endif //USE_WIN32_CRASHRTP_HANDLER
}

#ifndef NO_THREADS

//this will always print to serr
#ifdef USE_WIN32_CRASHRTP_HANDLER
static void win32_debug_thread_stacktrace(std::thread& the_thread)
{
	if(::SuspendThread(the_thread.native_handle()) == (DWORD)-1)
	{
		serrf("win32_debug_thread_stacktrace: SuspendThread failed, reason: %s\n",
			WIN_GetFormattedGLE().c_str());
	} else {
		CONTEXT ctx;
		ctx.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL;
		if (!::GetThreadContext(the_thread.native_handle(), &ctx)) {
			serrf("win32_debug_thread_stacktrace: GetThreadContext failed, reason: %s\n",
				WIN_GetFormattedGLE().c_str());
		} else {
			serr("StackTrace:\n");
			
			std::string output;
			crashrpt_print_stack_trace(string_dump_stack_trace, &output, 0, &ctx);
			serr_raw(output.data(),output.size());

			serr("\n");

			if(::ResumeThread(the_thread.native_handle()) == (DWORD)-1)
			{
				serrf("win32_debug_thread_stacktrace: ResumeThread failed, reason: %s\n",
					WIN_GetFormattedGLE().c_str());
			}
		}
	}
}
#endif

bool debug_thread::check_pulse_ms(int timeout_ms)
{
	if(timeout_ms < 0)
	{
		return true;
	}

	#ifdef _WIN32
	if(IsDebuggerPresent())
	{
		return true;
	}
	#endif

	TIMER_RESULT time_spent = timer_delta<TIMER_SEC>(pulse_time.load(), timer_now());
	if(time_spent > (TIMER_RESULT)timeout_ms / 1000.0)
	{
		serrf("thread timed out: %s (%f seconds)\n", name, time_spent);
		return false;
	}
	return true;
}



bool debug_thread::timed_join(int timeout_ms)
{
	if(timeout_ms >= 0)
	{
		TIMER_RESULT left_over_time = SDL_max(0, (TIMER_RESULT)timeout_ms - timer_delta<TIMER_MS>(pulse_time.load(), timer_now()));
        (void)left_over_time;
#ifdef _WIN32
		switch(::WaitForSingleObject(current_thread.native_handle(), (int)left_over_time))
		{
			case WAIT_TIMEOUT:
				serrf("thread timed out: %s (%f seconds)\n", name, timer_delta<TIMER_SEC>(pulse_time.load(), timer_now()));
#ifdef USE_WIN32_CRASHRTP_HANDLER
				win32_debug_thread_stacktrace(current_thread);
#endif
				return false;
			case WAIT_FAILED:
				serrf("%s: WaitForSingleObject failed, comment: %s, reason: %s\n",
					__FUNCTION__, name, WIN_GetFormattedGLE().c_str());
				return false;
		}
#else
#ifdef HAS_PTHREADS
		//NOTE: untested
		struct timespec ts;

		if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
			serrf("debug_timed_join: clock_gettime failed, thread: %s, reason: %s\n", name, strerror(errno));
			return false;
		}

		ts.tv_nsec += (int)(left_over_time * 1000 * 1000);

		int s = pthread_timedjoin_np(current_thread.native_handle(), NULL, &ts);
		if(s == ETIMEDOUT)
		{
			serrf("thread timed out: %s (%f seconds)\n", name, timer_delta<TIMER_SEC>(pulse_time.load(), timer_now()));
			//this is super unsafe, and it is best to exit the application asap.
			//the purpose of throwing this signal is for gdb to hook onto.
			int s = pthread_kill(current_thread.native_handle(), SIG_TRAP);
			if(s != 0)
			{
				serrf("debug_timed_join: pthread_kill failed, thread: %s, reason: %s\n", name, strerror(s));
			}
			return false;
		}
		else if (s != 0) {
			serrf("debug_timed_join: pthread_timedjoin_np failed, thread: %s, reason: %s\n", name, strerror(s));
			return false;
		}
#endif //HAS_PTHREADS

#endif
	}
	//std::thread won't magically be joined by the native handle (tested on windows).
	current_thread.join();

	return true;
}

std::string debug_thread::get_errors()
{
	//ugly as hell, but performance isn't essential here (thankfully I have no need to convert numbers...)
	std::string buffer;
	
	//was context->end() called?
	//this will only occur if you have an improper debug_thread callback which doesn't call context->end().
	//note that exited == false does not mean context->start() was called, it is set by constructing debug_thread.
	if(!exited.load())
	{
		buffer += "Error thread exited unexpectedly (failed to set exit flag): ";
		buffer += name;
		buffer += '\n';
	}

	//print any errors.
	if(serr_buffer)
	{
		if(!serr_buffer->empty()) {
			buffer += "Errors in thread: ";
			buffer += name;
			buffer += '\n';
			buffer += *serr_buffer;
		}
	} else {
		//context->start() sets the serr buffer.
		buffer += "Error thread not initialized (serr buffer missing): ";
		buffer += name;
		buffer += '\n';
	}
	return buffer;
}


#endif // NO_THREADS
