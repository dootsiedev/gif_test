#include "global.h"
#include "SDL_wrapper.h"

#include <string.h> //strerror
#include <errno.h> //errno

// assumptions
static_assert(SEEK_SET == RW_SEEK_SET);
static_assert(SEEK_CUR == RW_SEEK_CUR);
static_assert(SEEK_END == RW_SEEK_END);

//I can't use SDL_RWops directly because I really want it to use the serr system
//(ex: SDL_RWRead is bad because on error you MUST check SDL_GetError for an empty string)
//by all means, my system is not BETTER, just more integrated (ex: I can print a stacktrace on serr)
//and when you open SDL_RWFromFile, it will insert the path of the file in the error.
//and I am uncertain if SDL errors are thread safe...
//TODO: create a SDL_RWFromMem replacement because SDL will not print errors for it (eg: read too much, write too much), and I can also use the stream_info to make better errors.

class RWops_Stdio_NoClose : public RWops
{
public:
    FILE* fp;
    RWops_Stdio_NoClose(FILE* stream, const char* file)
    : fp(stream)
    {
        ASSERT(file != NULL);
        ASSERT(stream != NULL);
        stream_info = file;
    }

	size_t read(void *ptr, size_t size, size_t nmemb) override
    {
        size_t bytes_read = fread(ptr, size, nmemb, fp);
        if(bytes_read != size && ferror(fp) != 0)
        {
            //errno is not set by fread, if I really wanted to I could use open() and read(), but that is too painful.
            serrf("Error reading from datastream: `%s` (return: %d)\n", stream_info, static_cast<int>(bytes_read));
        }
        return bytes_read;
    }
	size_t write(const void *ptr, size_t size, size_t nmemb) override
    {
        size_t bytes_written = fwrite(ptr, size, nmemb, fp);
        if(bytes_written != size && ferror(fp) != 0)
        {
            serrf("Error writing to datastream: `%s`, reason: %s (return: %d)\n", stream_info, strerror(errno), static_cast<int>(bytes_written));
        }
        return bytes_written;
    }
	int seek(long offset, int whence) override
    {
        int out = fseek(fp, offset, whence);
        if(out != 0)
        {
            serrf("Error seeking in datastream: `%s`, reason: %s (return: %d)\n", stream_info, strerror(errno), out);
        }
        return out;
    }
	long tell() override
    {
        //spec says this doesn't have any specific errno codes on error
        //not to say the error indicator could cause an error.
        return ftell(fp);
    }
	~RWops_Stdio_NoClose() override = default;
};

class RWops_Stdio_AutoClose : public RWops_Stdio_NoClose
{
public:
    RWops_Stdio_AutoClose(FILE* stream, const char* file) : RWops_Stdio_NoClose(stream, file)
    {
    }
    ~RWops_Stdio_AutoClose() override
    {
        //clearing the error because I don't want fclose to give an error just because the error indicator is on.
        clearerr(fp);
        int out = fclose(fp);
        if(out != 0){
            serrf("Failed to close: `%s`, reason: %s (return: %d)\n", stream_info, strerror(errno), out);
        }
    }
};

//this is how I implement the buffer API because I am too lazy to copy paste the code.
//not high performance by any means, but portable.
class RWops_SDL_NoClose : public RWops
{
public:
    SDL_RWops* sdl_ops;
    RWops_SDL_NoClose(SDL_RWops* stream, const char* file)
    : sdl_ops(stream)
    {
        ASSERT(file != NULL);
        ASSERT(stream != NULL);
        stream_info = file;
    }

	size_t read(void *ptr, size_t size, size_t nmemb) override
    {
        SDL_ClearError();   //must be done because SDL will never clear for you.
        size_t bytes_read = SDL_RWread(sdl_ops, ptr, size, nmemb);
        //(new SDL2 feature): SDL_GetErrorMsg is better because it fixes a dangerous race condition to a safe race condition,
        //but still gives the wrong result without a mutex, so just stick to one thread, ok?
        //char buffer[1024];
        //const char* error = SDL_GetErrorMsg(buffer, sizeof(buffer));
        //char buffer[1024];
        const char* error = SDL_GetError();//SDL_GetErrorMsg(buffer, sizeof(buffer));
        //SDL spec says that to check for non eof error you must compare GetError with an empty string to detect errors.
        if(bytes_read != size && error[0] != '\0')
        {
            //errno is not set by fread, if I really wanted to I could use open() and read(), but that is too painful.
            serrf("Error reading from datastream: `%s`, reason: %s (return: %d)\n", stream_info, error, static_cast<int>(bytes_read));
        }
        return bytes_read;
    }
	size_t write(const void *ptr, size_t size, size_t nmemb) override
    {
        SDL_ClearError();   //must be done because SDL will never clear for you.
        size_t bytes_written = SDL_RWwrite(sdl_ops, ptr, size, nmemb);
        //char buffer[1024];
        const char* error = SDL_GetError();//SDL_GetErrorMsg(buffer, sizeof(buffer));
        if(bytes_written != size && error[0] != '\0')
        {
            
            serrf("Error writing to datastream: `%s`, reason: %s (return: %d)\n", stream_info, error, static_cast<int>(bytes_written));
        }
        return bytes_written;
    }
	int seek(long offset, int whence) override
    {
        int out = SDL_RWseek(sdl_ops, offset, whence);
        if(out < 0)
        {
            //char buffer[1024];
            const char* error = SDL_GetError();//SDL_GetErrorMsg(buffer, sizeof(buffer));
            serrf("Error seeking in datastream: `%s`, reason: %s (return: %d)\n", stream_info, error, out);
            return -1;
        }
        //SDL returns tell(), but stdio uses zero as an error check.
        return 0;
    }
	long tell() override
    {
        int out = SDL_RWtell(sdl_ops);
        if(out < 0)
        {
            //char buffer[1024];
            const char* error = SDL_GetError();//SDL_GetErrorMsg(buffer, sizeof(buffer));
            serrf("Error calling tell in datastream: `%s`, reason: %s (return: %d)\n", stream_info, error, out);
        }
        return out;
    }
	~RWops_SDL_NoClose() override = default;
};

class RWops_SDL_AutoClose : public RWops_SDL_NoClose
{
public:
    RWops_SDL_AutoClose(SDL_RWops* stream, const char* file) : RWops_SDL_NoClose(stream, file)
    {
    }
    ~RWops_SDL_AutoClose() override
    {
        int out = SDL_RWclose(sdl_ops);
        if(out != 0){
            //char buffer[1024];
            const char* error = SDL_GetError();//SDL_GetErrorMsg(buffer, sizeof(buffer));
            serrf("Failed to close: `%s`, reason: %s (return: %d)\n", stream_info, error, out);
        }
    }
};

Unique_RWops Unique_RWops_OpenFS(const char* path, const char* mode)
{
    FILE* fp = fopen(path, mode);
	if(fp == NULL)
	{
		serrf("Failed to open: `%s`, reason: %s\n", path, strerror(errno));
		return Unique_RWops();
	}
    return std::make_unique<RWops_Stdio_AutoClose>(fp, path);
}
Unique_RWops Unique_RWops_FromFP(FILE* fp, bool autoclose, const char* name)
{
    return (autoclose ? std::make_unique<RWops_Stdio_AutoClose>(fp, name) : std::make_unique<RWops_Stdio_NoClose>(fp, name));
}
Unique_RWops Unique_RWops_FromMemory(char* memory, size_t size, bool readonly, const char* name)
{
    SDL_RWops* sdl_rwop = (readonly ? SDL_RWFromConstMem(memory, size) : SDL_RWFromMem(memory, size));
	if(sdl_rwop == NULL)
	{
        //char buffer[1024];
        const char* error = SDL_GetError();//SDL_GetErrorMsg(buffer, sizeof(buffer));
		serrf("Failed to open: `%s`, reason: %s\n", name, error);
		return Unique_RWops();
	}
    return std::make_unique<RWops_SDL_AutoClose>(sdl_rwop, name);
}

/*
this is dead code, I might revive on day.

//The only problem with this is that you have no error information,
//so use Unique_RWops_Close() which will print an error and close.
struct RWops_Deleter
{
	void operator()(SDL_RWops* ops){
		if(SDL_RWclose(ops) < 0)
		{
			ASSERT(false && "SDL_RWclose");
		}
	}
};

typedef std::unique_ptr<SDL_RWops, RWops_Deleter> Unique_RWops;

MYNODISCARD Unique_RWops Unique_RWops_OpenFS(const char* path, const char* mode);

//closes the RWops with an error message.
//returns false on error.
MYNODISCARD bool Unique_RWops_Close(Unique_RWops& file, const char* msg = "<unspecified>");

Unique_RWops Unique_RWops_OpenFS(const char* path, const char* mode)
{
	// I prefer using SDL_RWFromFP instead of SDL_RWFromFile because on windows sdl will open a
	// win32 HANDLE, and SDL will print the path of the file inside of SDL_GerError, 
	// and the message excludes any specific error like errno/GLE.
	//
	// Note if you comple SDL2 on windows from source
	// by default the cmake flag LIBC is off by default
	// which will cause SDL_RWFromFP to fail due to missing stdio.h, 
	// but this means you must link to the correct CRT runtimes.
	//
	//TODO: I should just copy paste the SDL2 implementation of stdio, and split it between readonly and writeonly
	//and make it autoamtically print errno to serr for functions like RWread which only ruturn 0
	FILE* fp = fopen(path, mode);
	if(!fp)
	{
		serrf("Failed to open: `%s`, reason: %s\n", path, strerror(errno));
		return Unique_RWops();
	}
	// SDL_TRUE for closing the FILE* stream automatically.
	SDL_RWops* rwop = SDL_RWFromFP(fp, SDL_TRUE);
	if(!rwop)
	{
		serrf("SDL_RWFromFP %s: %s\n"
		"(there is only one way this could fail, which is if you compiled SDL2 without -DLIBC=ON)\n", path, SDL_GetError());
		return Unique_RWops();
	}
	return Unique_RWops(rwop);
}

bool Unique_RWops_Close(Unique_RWops& file, const char* msg)
{
	if(!file)
		return true;

	//release leaks the memory, unlike reset.
	SDL_RWops* sdl_ops = file.release();
	if(SDL_RWclose(sdl_ops) < 0)
	{
		//SDL_GetErrorMsg(buf, len) is thread safe, but a very recent addition to sdl
		serrf("Failed to close `%s`, reason: %s\n", msg, SDL_GetError());
		return false;
	}
	return true;
}
*/
