#pragma once

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <vorbis/vorbisfile.h>
#include <vorbis/codec.h>


// insert msg here: openal error: AL_XXX, Func: ..., File [... @ ...]
// returns false on error.
MYNODISCARD bool internal_alerr(const char* msg, const char* func_name, const char* file, int line);
MYNODISCARD bool internal_alcerr(const char* msg, ALCdevice* device, const char* func_name, const char* file, int line);
#define alerr(msg) internal_alerr(msg, __FUNCTION__, __FILE__, __LINE__)
#define alcerr(msg, device) internal_alcerr(msg, device, __FUNCTION__, __FILE__, __LINE__)

// openal should be used with SDL2 to grab the list of devices,
// and you should listen to SDL_AUDIODEVICEADDED/REMOVED, and load the new default if the default
// changed. you must call CloseAL before calling InitAL again.
MYNODISCARD bool InitAL(const char* device_name = NULL);
void CloseAL();

enum AL_STREAM_FLAGS{
	AL_STREAM_NONE = 0,
	AL_STREAM_LOOPING = 1,
	MAX_AL_STREAM_FLAGS
};

class AL_OggStream
{
public:
	
	~AL_OggStream()
	{
		//the destructor cannot capture serr, so I can only resort to ASSERT.
		//you should never rely on the destructor.
		if(!close())
		{
			ASSERT(false && "close");
		}
	}
	
	//starts off paused
	//the file will not be moved if an error occurs.
	MYNODISCARD bool open(Unique_RWops&& file_, int flags_ = AL_STREAM_NONE);
	
	MYNODISCARD bool close();
	
	MYNODISCARD bool update();
	
	MYNODISCARD bool play();
	
	MYNODISCARD bool pause();
	
	explicit operator bool() const { return !error_state; }
	
	double get_progress_seconds();
	
	double get_total_seconds();
	
	MYNODISCARD bool seek_to_second(double secs);

	bool is_playing() const
	{
		return currently_playing;
	}

	// could be null.
	const char* get_info() const
	{
		ASSERT(file);
		return file->stream_info;
	}

	// before you close the stream, you can reuse the file before it's closed.
	// the stream cannot continue without the file.
	// note that the file's cursor might not be at the start.
	Unique_RWops release_file()
	{
		return std::move(file);
	}
	
	void set_looping(bool on)
	{
		flags = (on ? (flags | AL_STREAM_LOOPING) : (flags & ~AL_STREAM_LOOPING));
	}
	
private:
	Unique_RWops file; // the file that this holds the compressed stream of audio data
	const char* file_info = NULL; //this is copied from file->stream_info.

	// OggVorbis_File is a large structure, I would make it a unique_ptr if this was a movable
	// object. I use alformat as a signal to check if this is allocated or not.
	OggVorbis_File vf;

	// I don't know the size because I expect to change the size with config options.
	std::unique_ptr<short[]> temp_buf; // used to decompress audio into, vorbis only supports 16bit
	std::unique_ptr<ALuint[]> buffers; // the openal buffer id's
	int buffer_count = 0;
	int buffer_size = 0;
	ALuint source_id = 0;
	ALenum alformat = 0;
	int flags = AL_STREAM_NONE;
	bool error_state = true;
	bool currently_playing = false;
	
	//the reason why I don't use alerr is because __FUNCTION__ doesn't show AL_OggStream
	
	MYNODISCARD bool internal_check_al_err(const char* function, const char* reason);
	
	void internal_print_ov_err(const char* function, int ov_error, const char* reason);
	
	MYNODISCARD bool internal_fill_buffers(const char* func);
};
