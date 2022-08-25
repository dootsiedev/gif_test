#pragma once

class RWops
{
public:
	const char* stream_info = 0;
	//follows the same error specifications as stdio functions.
	//if read returns 0, check serr_check_error()
	virtual size_t read(void *ptr, size_t size, size_t nmemb) = 0;
	virtual size_t write(const void *ptr, size_t size, size_t nmemb) = 0;
	//unlike SDL_RWops, this will not return tell(), 0 == success.
	virtual int seek(long offset, int whence) = 0;
	virtual long tell() = 0;
	//the one annoying quirk is that the destructor won't return an error, 
	//but you should still check serr.
	//if an error already occured, 
	virtual ~RWops() = default;
};

typedef std::unique_ptr<RWops> Unique_RWops;

enum{
    
};

Unique_RWops Unique_RWops_OpenFS(const char* path, const char* mode);
Unique_RWops Unique_RWops_FromFP(FILE* fp, bool autoclose = false, const char* name = "<unspecified>");

//writing to a FromMemory is janky, because it the size of the file cannot change.
//if you writing and reading the file, and you don't know the final size of the file, 
//you must reopen it (size = tell() + file.reset(Unique_RWops_FromMemory)).
Unique_RWops Unique_RWops_FromMemory(char* memory, size_t size, bool readonly = false, const char* name = "<unspecified>");
