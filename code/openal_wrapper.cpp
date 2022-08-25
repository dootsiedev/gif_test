#include "global.h"
#include "SDL_wrapper.h"
#include "cvar.h"

#include "openal_wrapper.h"

/*
On windows (maybe linux) OpenAL will not switch the default audio device 
when the default is lost or a new default is found.
And the fix I found was to compile openAL using -DALSOFT_BACKEND_SDL2=ON (TODO: maybe another backend might work)
but for some reason openAL screwed up their SDL2 includes, 
inside OpenAL they use <SDL2/SDL.h> (correct) but the examples use "SDL.h" (not the cmake configured way), 
so set -DSDL2_INCLUDE_DIR=(path to the folder containing /SDL2/SDL.h)
because openal has it's own script to find SDL which will prefer the directory of "SDL.h" 
and set -DALSOFT_EXAMPLES=OFF
*/


// TODO (dootsie): implement the openal callback API
// because it lowers latency and only buffers as much as needed, and for SDL when I drag the window the IO thread freezes.
// TODO (dootsie): AL_OggStream could support positional playback
static cvar& cv_openal_buffercount = register_cvar_value(
	"cv_openal_buffercount", 8, "audio stream buffers", CVAR_DEFAULT);
static cvar& cv_openal_buffersize = register_cvar_value(
	"cv_openal_buffersize", 8192, "audio stream buffer size", CVAR_DEFAULT);

static size_t oggRWopsRead(void* ptr, size_t size, size_t nmemb, void* datasource)
{
	return static_cast<RWops*>(datasource)->read(ptr, size, nmemb);
}

static int oggRWopsSeek(void* datasource, ogg_int64_t offset, int whence)
{
	return static_cast<RWops*>(datasource)->seek(offset, whence);
}

static long oggRWopsTell(void* datasource)
{
	return static_cast<RWops*>(datasource)->tell();
}

static ov_callbacks g_oggRWopsCallbacks
{
	// read
	oggRWopsRead,
	// seek
	oggRWopsSeek,
	// close
	NULL,
	// tell
	oggRWopsTell
};

static const char* ov_err_string(int ov_error)
{
#define OV_ERROR(x)     \
	case(x): \
		return (#x);
	switch(ov_error)
	{
		// errors only for ov_open_callbacks
		OV_ERROR(OV_EREAD)
		OV_ERROR(OV_ENOTVORBIS)
		OV_ERROR(OV_EVERSION)
		OV_ERROR(OV_EBADHEADER)
		OV_ERROR(OV_EFAULT)

		// errors only for ov_read
		OV_ERROR(OV_HOLE)
		OV_ERROR(OV_EBADLINK)
		OV_ERROR(OV_EINVAL)

		// for ov_time_seek
		OV_ERROR(OV_ENOSEEK)
		// OV_EINVAL
		// OV_EREAD
		// OV_EFAULT
		// OV_EBADLINK
	}
#undef OV_ERROR
	return NULL;
}



bool internal_alerr(const char* msg, const char* func_name, const char* file, int line)
{
	ALenum al_error = alGetError();
	if(al_error == AL_NO_ERROR)
		return true;
	const char* error_msg = alGetString(al_error);
	ASSERT(error_msg != NULL);
	serrf("%s: openal error: %s, Func: %s, File: [%s @ %i]\n", msg, error_msg, func_name, file,
		  line);
	return false;
}

bool internal_alcerr(const char* msg, ALCdevice* device, const char* func_name, const char* file,
					 int line)
{
	ALCenum alc_error = alcGetError(device);
	if(alc_error == ALC_NO_ERROR)
		return true;
	const char* error_msg = alcGetString(device, alc_error);
	ASSERT(error_msg != NULL);
	serrf("%s: openal error: %s, Func: %s, File: [%s @ %i]\n", msg, error_msg, func_name, file,
		  line);
	return false;
}

bool InitAL(const char* device_name)
{
	bool success = true;
	ALCcontext* ctx = NULL;
	ALCdevice* device = alcOpenDevice(device_name);
	if(device == NULL)
	{
		//NOTE: can I use alcerr with a null context?
		serrf("InitAL: failed to open `%s` audio device.\n",
			  (device_name == NULL ? "(default)" : device_name));
		if(device_name != NULL)
		{
			// print availible devices (in openal 1.1 this extension is always supported)
			if(alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE)
			{
				serr("Possible Devices:\n");
				// a list of null terminated strings, ending with 2 nulls.
				const ALCchar* device_list = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
				while(*device_list != '\0')
				{
					serrf("%s\n", device_list);
					int dev_len = strlen(device_list);
					device_list = device_list + dev_len + 1;
				}
				serr("\n");
			}
			else
			{
				serr("(cannot print possible devices because this feature is not supported)\n");
			}
		}
		success = false;
	}
	else
	{
		ctx = alcCreateContext(device, NULL);
		if(ctx == NULL)
		{
			serr("InitAL: alcCreateContext failed.\n");
			success = false;
		}
		else
		{
			if(alcMakeContextCurrent(ctx) == ALC_FALSE)
			{
				serr("InitAL: alcMakeContextCurrent failed.\n");
				success = false;
			}
		}
	}

	if(!success)
	{
		if(ctx != NULL)
		{
			alcDestroyContext(ctx);
			ctx = NULL;
		}
		if(device != NULL){
			alcCloseDevice(device);
			device = NULL;
		}
	}

	return success;
}

void CloseAL()
{
	ALCcontext* ctx = alcGetCurrentContext();
	if(ctx != NULL)
	{
		ALCdevice* device = alcGetContextsDevice(ctx);
		alcDestroyContext(ctx);
		alcMakeContextCurrent(NULL);
		if(device != NULL)
			alcCloseDevice(device);
	}
}

bool AL_OggStream::open(Unique_RWops&& file_, int flags_)
{
	ASSERT(file_);

	file_info = file_->stream_info;

	ASSERT(file_info != NULL);

	flags = flags_;

	error_state = true;
	currently_playing = false;

	// initialize open al buffer and source id's ---------------

	if(!buffers)
	{
		buffer_count = static_cast<int>(cv_openal_buffercount.get_value());
		buffers.reset(new ALuint[buffer_count]);
		alGenBuffers(buffer_count, buffers.get());
		if(!internal_check_al_err(__FUNCTION__, "Could not create buffers"))
		{
			buffers.reset();
			return false;
		}
	}
	if(source_id == 0)
	{
		alGenSources(1, &source_id);
		if(!internal_check_al_err(__FUNCTION__, "Could not create source"))
		{
			return false;
		}
		// this makes the source follow the listener, because stereo is pointless in 3D space
		alSource3i(source_id, AL_POSITION, 0, 0, -1);
		alSourcei(source_id, AL_SOURCE_RELATIVE, AL_TRUE);
		// this shouldn't make a difference to the sound, but it is a hint that distance isn't used.
		alSourcei(source_id, AL_ROLLOFF_FACTOR, 0);
		if(!internal_check_al_err(__FUNCTION__, "Could not set source parameters"))
		{
			return false;
		}
	}
	else
	{
		// Rewind the source position, and stops playback.
		alSourceRewind(source_id);
		// zero is a valid buffer ID, and it is used to clear the queue.
		alSourcei(source_id, AL_BUFFER, 0);
		if(!internal_check_al_err(__FUNCTION__, "Could not rewind source"))
		{
			return false;
		}
	}

	if(!internal_check_al_err(__FUNCTION__, "Could not set looping"))
	{
		return false;
	}

	// initialize vorbis using the file -----------

	if(alformat != 0)
	{
		ov_clear(&vf);
		alformat = 0;
	}

	//don't use file, since I only move the file if opening is successful
	int ov_ret = ov_open_callbacks(file_.get(), &vf, NULL, 0, g_oggRWopsCallbacks);
	if(ov_ret != 0)
	{
		internal_print_ov_err(__FUNCTION__, ov_ret, "Could not initialize ogg stream");
		return false;
	}

	vorbis_info* vi = ov_info(&vf, -1);
	ASSERT(vi != NULL && "ov_info"); // should be impossible.

	// open AL supports more channel formats,
	// ambisonic b-format seems interesting since it can be converted to 5.1/7.1 but vorbis doesn't
	// support it, opus does (AL_EXT_BFORMAT & AL_SOFT_bformat_ex).
	if(vi->channels == 1)
		alformat = AL_FORMAT_MONO16;
	else if(vi->channels == 2)
		alformat = AL_FORMAT_STEREO16;
	else
	{
		serrf("AL_OggStream::%s Error: unsupported channel count in `%s` (got: %d)\n",
			  __FUNCTION__, file_info, vi->channels);
		//gotta clear it because alformat is used to check if it's allocated.
		ov_clear(&vf);
		return false;
	}

	// fill the buffers with sound --------

	// this is quite silly, since I ignore that the real size of the buffer if you open a smaller
	// channel stream. but mono channels are pretty rare, and the cost of allocating the buffer is
	// negligible.
	int new_buffer_size = static_cast<int>(cv_openal_buffersize.get_value()) * vi->channels;
	if(buffer_size < new_buffer_size)
	{
		temp_buf.reset(new short[new_buffer_size]);
	}
	buffer_size = new_buffer_size;

	if(!internal_fill_buffers(__FUNCTION__))
	{
		return false;
	}

	file = std::move(file_);

	// completed
	error_state = false;

	return true;
}

bool AL_OggStream::close()
{
	bool success = true;

	error_state = true;
	currently_playing = false;

	//never opened
	if(file_info == NULL)
	{
		return true;
	}

	if(alformat != 0)
	{
		ov_clear(&vf);
		alformat = 0;
	}

	if(source_id != 0)
	{
		alDeleteSources(1, &source_id);
		if(!internal_check_al_err(__FUNCTION__, "Failed to delete source ID"))
		{
			success = false;
		}
		source_id = 0;
	}

	if(buffers)
	{
		alDeleteBuffers(buffer_count, buffers.get());
		if(!internal_check_al_err(__FUNCTION__, "Failed to delete buffer IDs"))
		{
			success = false;
		}
		buffers.reset();
	}

	file.reset();
	//the destructor of file could print to serr.
	if(serr_check_error())
	{
		success = false;
	}

	file_info = NULL;

	return success;
}

bool AL_OggStream::update()
{
	ASSERT(!error_state);

	ALint processed;
	ALint state;
	alGetSourcei(source_id, AL_BUFFERS_PROCESSED, &processed);
	alGetSourcei(source_id, AL_SOURCE_STATE, &state);
	if(!internal_check_al_err(__FUNCTION__, "Error checking source state"))
	{
		return false;
	}

	vorbis_info* vi = ov_info(&vf, -1);

	// TODO (dootsie): looping AL_OggStream doesn't perfect loop
	while(processed-- > 0)
	{
		ALuint bufid;
		alSourceUnqueueBuffers(source_id, 1, &bufid);
		if(!internal_check_al_err(__FUNCTION__, "Error unqueue buffer data"))
		{
			return false;
		}

		int current_section; // unused
		int bytes_read;

		bytes_read = ov_read(&vf, (char*)temp_buf.get(), buffer_size * sizeof(short), 0, 2, 1,
							 &current_section);
		if(bytes_read == 0)
		{
			// eof
			break;
		}
		if(bytes_read < 0)
		{
			internal_print_ov_err(__FUNCTION__, bytes_read, "Could not read file");
			return false;
		}

		
		alBufferData(bufid, alformat, temp_buf.get(), (ALsizei)bytes_read, (ALsizei)vi->rate);
		alSourceQueueBuffers(source_id, 1, &bufid);

		if(!internal_check_al_err(__FUNCTION__, "Error buffering data"))
		{
			return false;
		}
	}
	if(state != AL_PLAYING && state != AL_PAUSED)
	{
		ALint queued;

		//If no buffers are queued, playback is finished
		alGetSourcei(source_id, AL_BUFFERS_QUEUED, &queued);

		if(!internal_check_al_err(__FUNCTION__, "Error getting buffer state"))
		{
			return false;
		}

		if(queued == 0)
		{
			if(flags & AL_STREAM_LOOPING)
			{
				//rewind the audio stream to the beggining.
				if(!seek_to_second(0))
				{
					return false;
				}

				//play again
				if(!play())
				{
					return false;
				}
			}
			else
			{
				// done playing by reading the end
				currently_playing = false;
			}
		}
		else
		{
			//the source under-run
			if(!play())
			{
				return false;
			}
		}
	}
	else
	{
		currently_playing = true;
	}

	return true;
}

bool AL_OggStream::play()
{
	ASSERT(!error_state);

	alSourcePlay(source_id);

	if(!internal_check_al_err(__FUNCTION__, "Error playing audio"))
	{
		return false;
	}
	currently_playing = true;

	return true;
}

bool AL_OggStream::pause()
{
	ASSERT(!error_state);

	alSourcePause(source_id);

	if(!internal_check_al_err(__FUNCTION__, "Error pausing audio"))
	{
		return false;
	}
	currently_playing = false;

	return true;
}

double AL_OggStream::get_progress_seconds()
{
	ASSERT(!error_state);
	double ret = ov_time_tell(&vf);
	ASSERT(ret != (double)OV_EINVAL && "ov_time_tell");
	return ret;
}

double AL_OggStream::get_total_seconds()
{
	ASSERT(!error_state);
	double ret = ov_time_total(&vf, -1);
	ASSERT(ret != (double)OV_EINVAL && "ov_time_total");
	return ret;
}

bool AL_OggStream::seek_to_second(double secs)
{
	ASSERT(!error_state);

	// rewinding should flush any queued buffers (but I haven't tested if this is right)
	alSourceRewind(source_id);
	alSourcei(source_id, AL_BUFFER, 0);

	if(!internal_check_al_err(__FUNCTION__, "Error rewinding audio"))
	{
		return false;
	}

	int ret = ov_time_seek(&vf, secs);
	if(ret != 0)
	{
		internal_print_ov_err(__FUNCTION__, ret, "Failed to seek");
		return false;
	}

	if(!internal_fill_buffers(__FUNCTION__))
	{
		return false;
	}

	return true;
}

bool AL_OggStream::internal_check_al_err(const char* function, const char* reason)
{
	ASSERT(file_info != NULL);
	ALenum err = alGetError();
	if(err != AL_NO_ERROR)
	{
		const char* error_msg = (char*)alGetString(err);
		ASSERT(error_msg != NULL && "alGetString");
		serrf("AL_OggStream::%s Error: %s in `%s` (openal: %s)\n", function,
			  reason, file_info, error_msg);
		return false;
	}
	return true;
}

void AL_OggStream::internal_print_ov_err(const char* function, int ov_error, const char* reason)
{
	ASSERT(file_info != NULL);
	const char* error_msg = ov_err_string(ov_error);
	ASSERT(error_msg != NULL && "ov_err_string");
	serrf("AL_OggStream::%s Error: %s in `%s` (vorbis: %s)\n", function, reason, file_info,
		  error_msg);
}

bool AL_OggStream::internal_fill_buffers(const char* func)
{
	vorbis_info* vi = ov_info(&vf, -1);
	int current_section; // unused

	int filled_buffers = 0;
	for(; filled_buffers < buffer_count; ++filled_buffers)
	{
		int bytes_read = ov_read(&vf, (char*)temp_buf.get(), buffer_size * sizeof(short), 0, 2, 1,
								 &current_section);
		if(bytes_read == 0)
		{
			// reached eof before buffers are filled
			break;
		}
		if(bytes_read < 0)
		{
			internal_print_ov_err(func, bytes_read, "Could not read file");
			return false;
		}

		alBufferData(buffers[filled_buffers], alformat, temp_buf.get(), (ALsizei)bytes_read,
					 (ALsizei)vi->rate);
	}
	if(!internal_check_al_err(func, "Could not fill buffers"))
	{
		return false;
	}

	alSourceQueueBuffers(source_id, filled_buffers, buffers.get());
	if(!internal_check_al_err(func, "Error queueing buffers"))
	{
		return false;
	}
	return true;
}
