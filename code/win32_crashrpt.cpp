/*
 * win32_crashrpt.c : provides information after a crash
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

// dootsie: I found this at:
// http://web.mit.edu/freebsd/head/contrib/subversion/subversion/libsvn_subr/
// I have modified this quite a bit for my own purpose (the frame data showed the wrong values, so I removed that)
// the only problem I have is that StackWalk will stop walking if it reaches a bad frame (like a TCC function), 
// but libbacktrace will print the whole stack (it's made for mingw, but you can use the sym callback with codeview data)
// this is probably not related to the offical crashrtp (maybe just a very old version)
//TODO: it would make more sense to rename this mini-crashrtp
#include "global.h"
#ifdef USE_WIN32_CRASHRTP_HANDLER

/*** Includes. ***/

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <signal.h>

#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "win32_crashrpt.h"


#include "cvar.h"

//cv_win32_detect_debugger is cached because once setup is called, the hooks won't be removed.
static cvar& cv_win32_crashrpt_detect_debugger = register_cvar_value(
	"cv_win32_crashrpt_detect_debugger", 1.0, "crashrtp will disable itself if a debugger is detected", CVAR_STARTUP);

static cvar& cv_win32_crashrpt_minidump = register_cvar_value(
	"cv_win32_crashrpt_minidump", 1.0, "creates a dmp file you can open with visual studios", CVAR_DEFAULT);
static cvar& cv_win32_crashrpt_big_dump = register_cvar_value(
	"cv_win32_crashrpt_big_dump", 0.0, "big dumps include heap memory and globals (100mb and more in some projects)", CVAR_DEFAULT);
static cvar& cv_win32_crashrpt_wait_for_input = register_cvar_value(
	"cv_win32_crashrpt_wait_for_input", 0.0, "prompt \"Press Enter To Continue\" before exiting", CVAR_DEFAULT);
static cvar& cv_win32_crashrpt_append_stacktrace = register_cvar_value(
	"cv_win32_crashrpt_append_stacktrace", 0.0, "note: only functions if timestamp_files is off", CVAR_DEFAULT);
static cvar& cv_win32_crashrpt_timestamp_files = register_cvar_value(
	"cv_win32_crashrpt_timestamp_files", 0.0, "modifes the stacktrace and minidump files: filename.yymmddtime.dmp/txt", CVAR_DEFAULT);

static cvar& cv_win32_crashrpt_files_directory = register_cvar_string(
	"cv_win32_crashrpt_files_directory", "", "note: directory must end with a slash", CVAR_DEFAULT);
static cvar& cv_win32_crashrpt_stacktrace_file = register_cvar_string(
	"cv_win32_crashrpt_stacktrace_file", "stacktrace", "stacktrace filename (.txt added automatically)", CVAR_DEFAULT);
static cvar& cv_win32_crashrpt_minidump_file = register_cvar_string(
	"cv_win32_crashrpt_minidump_file", "coredump", "core dump filename (.dmp added automatically)", CVAR_DEFAULT);

/*** Global variables ***/
static HMODULE dbghelp_dll = NULL;

#ifndef MAX_STACK_DEPTH
#define MAX_STACK_DEPTH 30
#endif

// dbghelp is dynamically loaded, because it should be optional.
// because cmake wont copy dbghelp for you (does windows install with dbghelp?).
#define DBGHELP_DLL "dbghelp.dll"

#if defined(_M_IX86)
#define FORMAT_PTR "0x%08x"
#elif defined(_M_X64)
#define FORMAT_PTR "0x%016I64x"
#endif

/* public functions in dbghelp.dll */
typedef BOOL(WINAPI* MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD ProcessId, HANDLE hFile,
										MINIDUMP_TYPE DumpType,
										CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
										CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
										CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
typedef BOOL(WINAPI* SYMINITIALIZE)(HANDLE hProcess, PSTR UserSearchPath, BOOL fInvadeProcess);
typedef DWORD(WINAPI* SYMSETOPTIONS)(DWORD SymOptions);
typedef DWORD(WINAPI* SYMGETOPTIONS)(VOID);
typedef BOOL(WINAPI* SYMCLEANUP)(HANDLE hProcess);
typedef BOOL(WINAPI* SYMGETLINEFROMADDR64)(HANDLE hProcess, DWORD64 dwAddr, PDWORD pdwDisplacement,
										   PIMAGEHLP_LINE64 Line);
typedef BOOL(WINAPI* SYMFROMADDR)(HANDLE hProcess, DWORD64 Address, PDWORD64 Displacement,
								  PSYMBOL_INFO Symbol);
typedef BOOL(WINAPI* STACKWALK64)(DWORD MachineType, HANDLE hProcess, HANDLE hThread,
								  LPSTACKFRAME64 StackFrame, PVOID ContextRecord,
								  PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
								  PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
								  PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
								  PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);
typedef PVOID(WINAPI* SYMFUNCTIONTABLEACCESS64)(HANDLE hProcess, DWORD64 AddrBase);
typedef DWORD64(WINAPI* SYMGETMODULEBASE64)(HANDLE hProcess, DWORD64 dwAddr);
typedef DWORD(WINAPI* UNDECORATESYMBOLNAME)(PCSTR name, PSTR outputString, DWORD maxStringLength,
											DWORD flags);
typedef BOOL (*SYMGETMODULEINFO)(HANDLE hProcess, DWORD64 qwAddr, PIMAGEHLP_MODULE64 ModuleInfo);

/* function pointers */
static MINIDUMPWRITEDUMP MiniDumpWriteDump_;
static SYMINITIALIZE SymInitialize_;
static SYMSETOPTIONS SymSetOptions_;
static SYMGETOPTIONS SymGetOptions_;
static SYMCLEANUP SymCleanup_;
static SYMGETLINEFROMADDR64 SymGetLineFromAddr64_;
static SYMFROMADDR SymFromAddr_;
static STACKWALK64 StackWalk64_;
static SYMFUNCTIONTABLEACCESS64 SymFunctionTableAccess64_;
static SYMGETMODULEBASE64 SymGetModuleBase64_;

static UNDECORATESYMBOLNAME UnDecorateSymbolName_;
static SYMGETMODULEINFO SymGetModuleInfo_;

// default print
void internal_print_stack_trace(void* ud, crashrpt_stack_info& data)
{
	FILE* file_stream = (FILE*)ud;
	if(data.module == NULL)
	{
		fprintf(file_stream, "bad frame: 0x%p\n", data.addr);
		return;
	}

	if(data.function != NULL)
	{
		if(data.file != NULL)
		{
			fprintf(file_stream, "%s ! 0x%p in %s [%s @ %d]\n", data.module, data.addr,
					data.function, data.file, data.line);
		}
		else
		{
			fprintf(file_stream, "%s ! 0x%p in %s\n", data.module, data.addr, data.function);
		}
	}
	else
	{
		fprintf(file_stream, "%s ! 0x%p in (unknown function)\n", data.module, data.addr);
	}
}

/*** Code. ***/

/* Convert the exception code to a string */
static const char* exception_string(long long exception)
{
#define EXCEPTION(x)     \
	case(EXCEPTION_##x): \
		return (#x);

	switch(exception)
	{
        case 0: return "Abort Signal"; //this is special from force_crash_dump
		EXCEPTION(ACCESS_VIOLATION)
		EXCEPTION(DATATYPE_MISALIGNMENT)
		EXCEPTION(BREAKPOINT)
		EXCEPTION(SINGLE_STEP)
		EXCEPTION(ARRAY_BOUNDS_EXCEEDED)
		EXCEPTION(FLT_DENORMAL_OPERAND)
		EXCEPTION(FLT_DIVIDE_BY_ZERO)
		EXCEPTION(FLT_INEXACT_RESULT)
		EXCEPTION(FLT_INVALID_OPERATION)
		EXCEPTION(FLT_OVERFLOW)
		EXCEPTION(FLT_STACK_CHECK)
		EXCEPTION(FLT_UNDERFLOW)
		EXCEPTION(INT_DIVIDE_BY_ZERO)
		EXCEPTION(INT_OVERFLOW)
		EXCEPTION(PRIV_INSTRUCTION)
		EXCEPTION(IN_PAGE_ERROR)
		EXCEPTION(ILLEGAL_INSTRUCTION)
		EXCEPTION(NONCONTINUABLE_EXCEPTION)
		EXCEPTION(STACK_OVERFLOW)
		EXCEPTION(INVALID_DISPOSITION)
		EXCEPTION(GUARD_PAGE)
		EXCEPTION(INVALID_HANDLE)

		default:
			return NULL;
	}
#undef EXCEPTION
}

/* Write the minidump to file. The callback function will at the same time
   write the list of modules to the log file. */
static BOOL write_minidump_file(const char* file, PEXCEPTION_POINTERS ptrs)
/*,
					MINIDUMP_CALLBACK_ROUTINE module_callback,
					void *data)*/
{
	/* open minidump file */
	HANDLE minidump_file =
		CreateFile(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if(minidump_file != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION expt_info;

		// dumping modules are cool and all, but it is very noisey, and the debugger can show the
		// modules too. MINIDUMP_CALLBACK_INFORMATION dump_cb_info;

		expt_info.ThreadId = GetCurrentThreadId();
		expt_info.ExceptionPointers = ptrs;
		expt_info.ClientPointers = FALSE;

		// dump_cb_info.CallbackRoutine = module_callback;
		// dump_cb_info.CallbackParam = data;

		MINIDUMP_TYPE mdt =
			(cv_win32_crashrpt_big_dump.get_value() == 1.0)
				? (MINIDUMP_TYPE)(MiniDumpWithPrivateReadWriteMemory | MiniDumpWithDataSegs |
								  MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo)
				: (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);

		MiniDumpWriteDump_(GetCurrentProcess(), GetCurrentProcessId(), minidump_file, mdt,
						   ptrs ? &expt_info : NULL, NULL, NULL);

		CloseHandle(minidump_file);
		return TRUE;
	}

	return FALSE;
}

/* Write the details of one function to the log file */
static void write_function_detail(DWORD64 stack_frame, int nr_of_frame,
								  crashrpt_stack_trace_callback callback, void* ud)
{
	crashrpt_stack_info data;
	::ZeroMemory(&data, sizeof(crashrpt_stack_info));

	nr_of_frame++; /* We need a 1 based index here */

	data.index = nr_of_frame;
	data.addr = (void*)stack_frame;

	HANDLE proc = GetCurrentProcess();

	IMAGEHLP_MODULE moduleInfo;
	::ZeroMemory(&moduleInfo, sizeof(moduleInfo));
	moduleInfo.SizeOfStruct = sizeof(moduleInfo);

	// If you want to get the absolute path, using GetModuleNameFromAddress + whereami
	// getModulePath_ but printing a full path for every line is very noisey.
	if(SymGetModuleInfo_(proc, stack_frame, &moduleInfo) == TRUE)
	{
		data.module = moduleInfo.ModuleName;
	}

	ULONG64
	symbolBuffer[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
	PSYMBOL_INFO pIHS = (PSYMBOL_INFO)symbolBuffer;
	DWORD64 func_disp = 0;

	/* log the function name */
	pIHS->SizeOfStruct = sizeof(SYMBOL_INFO);
	pIHS->MaxNameLen = MAX_SYM_NAME;
	if(SymFromAddr_(proc, stack_frame, &func_disp, pIHS))
	{
		data.function = pIHS->Name;
		/*char undecorate_buffer[500];//=_T("?");

		if(UnDecorateSymbolName_( pIHS->Name, undecorate_buffer, sizeof(undecorate_buffer),
			UNDNAME_NAME_ONLY ) == 0)
		{
		  data.function = pIHS->Name;
		} else {
		  data.function = undecorate_buffer;
		}*/
	}

	IMAGEHLP_LINE64 ih_line;
	DWORD line_disp = 0;

	/* find the source line for this function. */
	ih_line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
	if(SymGetLineFromAddr64_(proc, stack_frame, &line_disp, &ih_line) != 0)
	{
		data.file = ih_line.FileName;
		data.line = ih_line.LineNumber;
	}

	callback(ud, data);
}

/* Walk over the stack and log all relevant information to the log file */
static void write_stacktrace(CONTEXT* context, crashrpt_stack_trace_callback callback, void* ud,
							 int skip = 0)
{
	HANDLE proc = GetCurrentProcess();
	STACKFRAME64 stack_frame;
	DWORD machine;
	CONTEXT ctx;
	int i = 0;

	/* The thread information - if not supplied. */
	if(context == NULL)
	{
		/* If no context is supplied, skip 1 frame */
		skip += 1;

		ctx.ContextFlags = CONTEXT_FULL;
		// if (!GetThreadContext(GetCurrentThread(), &ctx))
		// I think this is 64 bit only, but GetThreadContext adds in an extra 2 frames (weird)
		RtlCaptureContext(&ctx);

		context = &ctx;
	}
	else
	{
		ctx = *context;
	}

	if(context == NULL)
		return;

	/* Write the stack trace */
	ZeroMemory(&stack_frame, sizeof(STACKFRAME64));
	stack_frame.AddrPC.Mode = AddrModeFlat;
	stack_frame.AddrStack.Mode = AddrModeFlat;
	stack_frame.AddrFrame.Mode = AddrModeFlat;

#if defined(_M_IX86)
	machine = IMAGE_FILE_MACHINE_I386;
	stack_frame.AddrPC.Offset = context->Eip;
	stack_frame.AddrStack.Offset = context->Esp;
	stack_frame.AddrFrame.Offset = context->Ebp;
#elif defined(_M_X64)
	machine = IMAGE_FILE_MACHINE_AMD64;
	stack_frame.AddrPC.Offset = context->Rip;
	stack_frame.AddrStack.Offset = context->Rsp;
	stack_frame.AddrFrame.Offset = context->Rbp;
#else
#error Unknown processortype, please disable SVN_USE_WIN32_CRASHHANDLER
#endif

	while(1)
	{
		if(i > MAX_STACK_DEPTH)
		{
			break;
		}
		if(!StackWalk64_(machine, proc, GetCurrentThread(), &stack_frame, &ctx, NULL,
						 SymFunctionTableAccess64_, SymGetModuleBase64_, NULL))
		{
			break;
		}

		if(i >= skip)
		{
			/* Try to include symbolic information.
			   Also check that the address is not zero. Sometimes StackWalk
			   returns TRUE with a frame of zero. */
			if(stack_frame.AddrPC.Offset != 0)
			{
				write_function_detail(stack_frame.AddrPC.Offset, i - skip, callback, ud);
			}
		}
		i++;
	}
}

/* Load the dbghelp.dll file, try to find a version that matches our
   requirements. */
static BOOL load_dbghelp_dll()
{
	dbghelp_dll = (HMODULE)LoadLibrary(DBGHELP_DLL);
	if(dbghelp_dll != NULL)
	{
		DWORD opts;

		/* load the functions */
		MiniDumpWriteDump_ = (MINIDUMPWRITEDUMP)GetProcAddress(dbghelp_dll, "MiniDumpWriteDump");
		SymInitialize_ = (SYMINITIALIZE)GetProcAddress(dbghelp_dll, "SymInitialize");
		SymSetOptions_ = (SYMSETOPTIONS)GetProcAddress(dbghelp_dll, "SymSetOptions");
		SymGetOptions_ = (SYMGETOPTIONS)GetProcAddress(dbghelp_dll, "SymGetOptions");
		SymCleanup_ = (SYMCLEANUP)GetProcAddress(dbghelp_dll, "SymCleanup");
		SymGetLineFromAddr64_ =
			(SYMGETLINEFROMADDR64)GetProcAddress(dbghelp_dll, "SymGetLineFromAddr64");
		SymFromAddr_ = (SYMFROMADDR)GetProcAddress(dbghelp_dll, "SymFromAddr");
		StackWalk64_ = (STACKWALK64)GetProcAddress(dbghelp_dll, "StackWalk64");
		SymFunctionTableAccess64_ =
			(SYMFUNCTIONTABLEACCESS64)GetProcAddress(dbghelp_dll, "SymFunctionTableAccess64");
		SymGetModuleBase64_ = (SYMGETMODULEBASE64)GetProcAddress(dbghelp_dll, "SymGetModuleBase64");

		UnDecorateSymbolName_ =
			(UNDECORATESYMBOLNAME)GetProcAddress(dbghelp_dll, "UnDecorateSymbolName");
		SymGetModuleInfo_ = (SYMGETMODULEINFO)GetProcAddress(dbghelp_dll, "SymGetModuleInfo");

		if(!(MiniDumpWriteDump_ && SymInitialize_ && SymSetOptions_ && SymGetOptions_ &&
			 SymCleanup_ && SymGetLineFromAddr64_ && SymFromAddr_ && SymGetModuleBase64_ &&
			 StackWalk64_ && SymFunctionTableAccess64_ && UnDecorateSymbolName_ &&
			 SymGetModuleInfo_))
			goto cleanup;

		/* initialize the symbol loading code */
		opts = SymGetOptions_();

		opts |= SYMOPT_LOAD_LINES;
		opts |= SYMOPT_DEFERRED_LOADS;

		/* Set the 'load lines' option to retrieve line number information;
		   set the Deferred Loads option to map the debug info in memory only
		   when needed. */
		SymSetOptions_(opts);

		/* Initialize the debughlp DLL with the default path and automatic
		   module enumeration (and loading of symbol tables) for this process.
		 */
		SymInitialize_(GetCurrentProcess(), NULL, TRUE);

		return TRUE;
	}

cleanup:
	if(dbghelp_dll)
		FreeLibrary(dbghelp_dll);

	return FALSE;
}

/* Cleanup the dbghelp.dll library */
static void cleanup_debughlp()
{
	SymCleanup_(GetCurrentProcess());

	FreeLibrary(dbghelp_dll);

	dbghelp_dll = NULL;
}

/* Create a filename based on a prefix, the timestamp and an extension.
   check if the filename was already taken, retry 3 times. */
bool create_filename(char* buffer, int size, const char* prefix, const char* ext)
{
	if(cv_win32_crashrpt_timestamp_files.get_value() == 1.0)
	{
        //this will attempt 3 times to write a unique file
        //since it is possible 2 files are created at the same time.
		for(int i = 0; i < 3; i++)
		{
			HANDLE file;
			time_t now;
			char time_str[64];

			time(&now);
			strftime(time_str, sizeof(time_str), "%Y%m%d%H%M%S", localtime(&now));
			snprintf(buffer, size, "%s%s%s.%s", cv_win32_crashrpt_files_directory.get_string().c_str(), prefix, time_str,
					 ext);

			file =
				CreateFile(buffer, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
			if(file != INVALID_HANDLE_VALUE)
			{
				CloseHandle(file);
				return true;
			}
            Sleep(1);
		}
	}
	else
	{
		snprintf(buffer, size, "%s%s.%s", cv_win32_crashrpt_files_directory.get_string().c_str(), prefix, ext);
		return true;
	}

	buffer[0] = '\0';
	return false;
}

void write_exception_info(FILE* file, PEXCEPTION_POINTERS ptrs)
{
	/* write the exception code */
	const char* exception_type = exception_string(ptrs->ExceptionRecord->ExceptionCode);
	if(exception_type == NULL)
	{
		fprintf(file, "\nException: (Unknown) 0x%lx\n\n", ptrs->ExceptionRecord->ExceptionCode);
	}
	else
	{
		fprintf(file, "\nException: %s\n\n", exception_type);
	}

	/* write information about the process */
	fprintf(file, "Version:  %s, compiled %s, %s\n", "N/A", __DATE__, __TIME__);

	/* write the stacktrace, if available */
	fprintf(file, "\nStacktrace:\n");
	write_stacktrace(ptrs ? ptrs->ContextRecord : NULL, internal_print_stack_trace, file);

	if(cv_win32_crashrpt_minidump.get_value() == 1.0)
	{
		/* write the minidump file and use the callback to write the list of modules
		   to the log file */
		fprintf(file, "\nCreating Minidump\n");
	}
}

/* Unhandled exception callback set with SetUnhandledExceptionFilter() */
LONG WINAPI crashrpt_unhandled_exception_filter(PEXCEPTION_POINTERS ptrs)
{
	char dmp_filename[MAX_PATH] = "n/a";
	char log_filename[MAX_PATH] = "n/a";
	FILE* log_file = NULL;

	// everything doesn't work in a stack overflow.
	if(ptrs->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
		return EXCEPTION_CONTINUE_SEARCH;

	/* Check if the crash handler was already loaded (crash while handling the
	   crash) */
	if(dbghelp_dll != NULL)
		return EXCEPTION_CONTINUE_SEARCH;

	/* don't log anything if we're running inside a debugger ... */
	if(cv_win32_crashrpt_detect_debugger.get_value() == 1.0 && IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

	/* If we can't load a recent version of the dbghelp.dll, pass on this
	   exception */
	if(!load_dbghelp_dll())
		return EXCEPTION_CONTINUE_SEARCH;

	/* ... or if we can't create the log files ... */
	if(!create_filename(dmp_filename, sizeof(dmp_filename), cv_win32_crashrpt_minidump_file.get_string().c_str(), "dmp") ||
	   !create_filename(log_filename, sizeof(log_filename), cv_win32_crashrpt_stacktrace_file.get_string().c_str(), "txt"))
		return EXCEPTION_CONTINUE_SEARCH;

	/* open log file */
	if(cv_win32_crashrpt_append_stacktrace.get_value() == 0.0)
	{
		log_file = fopen(log_filename, "w");
	}
	else
	{
		log_file = fopen(log_filename, "a");
	}

	if(log_file == NULL)
	{
		fprintf(stderr, "failed to write to - %s, error: %s", log_filename, strerror(errno));
	}
	else
	{
		if(cv_win32_crashrpt_append_stacktrace.get_value() == 0.0)
		{
			time_t now;
			char time_str[200];

			time(&now);
            //if you were to use this for something outside the stack printer,
            //I would reccomend you to use wcsftime.
			strftime(time_str, sizeof(time_str), "Date: %A, %B %d, %Y at %H:%M:%S %z\n",
					 localtime(&now));
			fputs(time_str, log_file);
		}
		write_exception_info(log_file, ptrs);

        fclose(log_file);
	}

	write_exception_info(stderr, ptrs);
	fprintf(stderr,
			"\nThis application has halted due to an unexpected error.\n"
			"A crash report and minidump file were saved to disk, you"
			" can find them here:\n"
			"%s\n%s\n",
			log_filename, dmp_filename);

	if(cv_win32_crashrpt_minidump.get_value() == 1.0)
	{
		write_minidump_file(dmp_filename, ptrs);
	}

	fflush(stderr);
	fflush(stdout);

	cleanup_debughlp();

	if(cv_win32_crashrpt_wait_for_input.get_value() == 1.0)
	{
		fprintf(stderr, "\nPress Enter To Continue\n");
		// this wont work if there is a newline leftover.
		char chunk[2];
		fgets(chunk, sizeof(chunk), stdin);
	}

	/* terminate the application */
	return EXCEPTION_EXECUTE_HANDLER;
}


static void force_crash_dump()
{
	__try
	{
		::RaiseException(0, // dwExceptionCode
						 EXCEPTION_NONCONTINUABLE, // dwExceptionFlags
						 0, // nNumberOfArguments,
						 NULL // const ULONG_PTR* lpArguments
		);
	}
	__except(crashrpt_unhandled_exception_filter(GetExceptionInformation()),
			 EXCEPTION_EXECUTE_HANDLER)
	{
	}
}


static void handle_abort(int sig)
{
    if(sig != SIGABRT) fprintf(stdout, "Not Abort, sig: %d\n", sig);

    //dootsie: most of the aborts will not be called
    //because of _set_error_mode(_OUT_TO_MSGBOX);
    //which works on the release runtime,
    //but just in case you accidentally press abort,
    //I would rather have it minidump.
    //also this prevents copy-paste.
    force_crash_dump();

    //this might be usefule for mingw code, but I don't think I will support mingw...
    #if 0

	char log_filename[MAX_PATH] = "n/a";
    FILE* log_file = NULL;

	/* Check if the crash handler was already loaded (crash while handling the
	   crash) */
	if(dbghelp_dll != NULL)
		return;

	if(IsDebuggerPresent())
		return;

	if(!load_dbghelp_dll())
		return;

	/* ... or if we can't create the log files ... */
	if(!create_filename(log_filename, sizeof(log_filename), g_crashsettings.stacktrace_file, "txt"))
		return;

	/* open log file */
	if(!g_crashsettings.append_stacktrace)
	{
		log_file = fopen(log_filename, "w");
	}
	else
	{
		log_file = fopen(log_filename, "a");
	}

    if(log_file == NULL)
    {
        fprintf(stderr, "failed to write to - %s, error: %s", log_filename, strerror(errno));
    }
    else 
    {
        if(sig == SIGABRT)
        {
            fprintf(log_file, "\nAbort Signal Caught\n");
        }
        else
        {
            fprintf(log_file, "\nSignal Caught: %d\n", sig);
        }

        fprintf(log_file, "\nStacktrace:\n");
        crashrpt_print_stack_trace(internal_print_stack_trace, (void*)log_file, 1);

        fclose(log_file);
        log_file = NULL;
    }

    //this is copy paste...

	if(sig == SIGABRT)
	{
        fprintf(stderr, "\nAbort Signal Caught\n");
	}
	else
	{
        fprintf(stderr, "\nSignal Caught: %d\n", sig);
	}

    fprintf(stderr, "\nStacktrace:\n");
    crashrpt_print_stack_trace(internal_print_stack_trace, (void*)stderr, 1);

    fflush(stderr);
    fflush(stdout);

	cleanup_debughlp();

	if(g_crashsettings.wait_for_input)
	{
		fprintf(stderr, "\nPress Enter To Continue\n");
		char chunk[2];
		fgets(chunk, sizeof(chunk), stdin);
	}
    #endif
}

void crashrpt_print_stack_trace(crashrpt_stack_trace_callback callback, void* ud, int skip, void* thread_context)
{
	if(!load_dbghelp_dll())
		return;

	write_stacktrace(static_cast<CONTEXT*>(thread_context), callback, ud, skip + 1);

	cleanup_debughlp();
}

void crashrpt_setup()
{
	if(cv_win32_crashrpt_detect_debugger.get_value() == 1.0 && IsDebuggerPresent())
		return;

    //if you build with /MT (relwithdebinfo)
    //C++ exceptions and asserts will not create the msgbox dialog.
    //but I don't like seeing "not responding", so I prefer this
    //unless this is a release build (with no debug info).
	_set_error_mode(_OUT_TO_MSGBOX);

	::SetUnhandledExceptionFilter(crashrpt_unhandled_exception_filter);

	signal(SIGABRT, handle_abort);
}

#endif /* USE_WIN32_CRASHRTP_HANDLER */
