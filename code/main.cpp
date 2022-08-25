#include "global.h"

#include "SDL_wrapper.h"
#include "openal_wrapper.h"

#include "cvar.h"

#ifdef USE_WIN32_CRASHRTP_HANDLER
#include "win32_crashrpt.h"
#define CRASHRTP_CVAR_STATE CVAR_STARTUP
#else
#define CRASHRTP_CVAR_STATE CVAR_DISABLED
#endif //USE_WIN32_CRASHRTP_HANDLER
static cvar& cv_win32_crashrpt = register_cvar_value(
	"cv_win32_crashrtp", 1.0, "captures system errors and prints a stacktrace, note: stack traces will still function if off.", CRASHRTP_CVAR_STATE);


#ifndef NO_THREADS
#include "debug_tools.h"

#if defined(__GNUC__) && !defined(_WIN32)
#include <X11/Xlib.h>
#endif

#define THREADS_CVAR_STATE CVAR_STARTUP
#else
#define THREADS_CVAR_STATE CVAR_DISABLED
#endif //NO_THREADS
static cvar& cv_disable_threads = register_cvar_value(
	"cv_disable_threads", 0.0, "disables the use of threads", THREADS_CVAR_STATE);
static cvar& cv_thread_timeout_ms = register_cvar_value(
	"cv_thread_timeout_ms", 10000, "the time a thread needs to deadlock to be considered an error, -1 for infinite", THREADS_CVAR_STATE);




#include "gl_wrapper.h"

enum{
	FULLSCREEN_MODE_FIT_TO_SCREEN  = 0,
	FULLSCREEN_MODE_NATIVE = 1,
	FULLSCREEN_MODE_STRETCH = 2,
	FULLSCREEN_MODE_LETTERBOX = 3
	//TODO: possibly a stretch by multiple of 2 mode, that works like letterbox.
};

static cvar& cv_vsync = register_cvar_value(
	"cv_vsync", 1, "vsync setting, 0 (off), 1 (on), -1 (adaptive?)", CVAR_CACHED);
static cvar& cv_fullscreen = register_cvar_value(
	"cv_fullscreen", 0, "0 = windowed, 1 = fullscreen", CVAR_CACHED);
static cvar& cv_fullscreen_mode = register_cvar_value(
	"cv_fullscreen_mode", 0, "0 = fit to screen, 1 = set hardware resolution, 2 = stretch, 3 = letterbox", CVAR_CACHED);
static cvar& cv_screen_width = register_cvar_value(
	"cv_screen_width", 640, "if fullscreen, this is ignored in \"fit to screen\" mode", CVAR_CACHED);
static cvar& cv_screen_height = register_cvar_value(
	"cv_screen_height", 480, "complements cv_screen_width", CVAR_CACHED);
static cvar& cv_opengl_debug = register_cvar_value(
	"cv_opengl_debug", 1, "0 = off, 1 = show detailed opengl errors, 2 = stacktrace per call", CVAR_STARTUP);

static SDL_GLContext gl_context;

//from https://github.com/nvMcJohn/apitest
//link isn't because this I copied this, but because I want to look at the rest of the code more.
// --------------------------------------------------------------------------------------------------------------------
static void ErrorCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char* message, const void* userParam)
{
    (void)userParam; (void)length; (void)source; (void)id;
    /*if (source == GL_DEBUG_SOURCE_API && type == GL_DEBUG_TYPE_OTHER && severity == GL_DEBUG_SEVERITY_LOW && strstr(message, "DRAW_INDIRECT_BUFFER") == nullptr) {
        return;
    }*/

	static bool only_once = false;

	if(only_once && cv_opengl_debug.get_value() != 2.0)
	{
		serrf("\nGL CALLBACK: (0x%.8x) type = %s, severity = %d, message = %s\n",
			type, ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "?" ), severity, message);
	} else {
		only_once = true;
		std::string stack_message;
		debug_stacktrace_string(stack_message, 1);

		//this is pretty lazy, but all I want is the message & stacktrace
		//if you are wondering why I am doing both GL_CHECK and callbacks, 
		//it is because I only print 1 stack trace, so knowing the file/line is still better.
		//yes printing a CHECK + callback takes up many lines of error message, 
		//but stack traces take much longer to retrieve than an error message.
		//reducing the amount of noise is a possible improvement though.
		serrf("\nGL CALLBACK: (0x%.8x) type = %s, severity = %d, message = %s\n"
			"\nStackTrace:\n"
			"%s\n", 
			type, ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "?" ), 
			severity, message, stack_message.c_str());
	}
}

bool test_json_1();
bool test_json_2();
bool test_json_3();
bool test_json_4();
bool test_json_5();

//could hold an error, but maybe not.
static const char* startup_settings(int argc, char** argv)
{
	const char* prog_name = NULL;
	const char* input_file = NULL;
	
	//note this takes in 1 printf argument for program name.
	const char* usage_message = "Usage: %s \"file.ogg\" [+cv_option \"0\"]\n"
		"Hint: pass --help to dump a list of possible options\n";

	if(argc == 0)
	{
		slog("program name missing?\n");
		return NULL;
	}
	prog_name = argv[0];
	++argv;
	--argc;

	if(argc == 0)
	{
		slog("No input file provided.\n");
		slogf(usage_message, prog_name);
		return NULL;
	}
	input_file = argv[0];
	++argv;
	--argc;
	

	//kinda ugly, but gets the job done.
	if(strcmp(input_file, "--help") == 0)
	{
		slogf(usage_message, prog_name);
		slog("option dump with defaults:\n");
		for(const auto &it : get_convars())
		{
			if(it.second.get_flags() == CVAR_DISABLED)
			{
				slogf("cvar disabled: %s\n", it.first.c_str());
			} else {
				slogf("%s: \"%s\"\n"
					"\t%s\n", it.first.c_str(), it.second.get_string().c_str(), it.second.get_comment());
				if(it.second.get_flags() == CVAR_STARTUP)
				{
					slog("\tnote: requires restart to take effect\n");
				}
			}
		}
		return NULL;
	}

	//load "+cv_XXX X +cv_YYY Y" arguments
	if(!cvar_args(argc, argv))
	{
		return NULL;
	}

	#ifdef USE_WIN32_CRASHRTP_HANDLER
	if(cv_win32_crashrpt.get_value() == 1.0)
	{
		crashrpt_setup();
	}
	#endif //USE_WIN32_CRASHRTP_HANDLER

	//this used to be part of cvar_args, but this is more likely to crash due to JSON.
	if(!cvar_config_file())
	{
		return NULL;
	}
	
    

	return input_file;
}

int main(int argc, char** argv)
{
	//initialize with the command arguments.
	const char* input_file = startup_settings(argc, argv);
	if(input_file == NULL)
	{
		//sometimes startup_settings might not hold an error (eg: --help)
		if(serr_check_error())
		{
			//TODO: In SDL I could make a custom messagebox which could offer a button to copy to clipboard.
            //and try to word wrap the error message and truncate the message if it has more than N lines.
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), NULL);
		}
	}

	slog("hellow openal!\n");
    
    //test json stuff
#if 0
    TIMER_U t1 = timer_now(), t2;
    
    
    //a lazy test suite
    test_json_1();
    test_json_2();
    test_json_3();
    test_json_4();
    test_json_5();
    
    t2 = timer_now();
    slogf("test time: %f\n", timer_delta<TIMER_MS>(t1,t2));
    
    if(serr_check_error())
	{
		serrf("\nUncaught Error Check, Function: %s, File: %s, Line: %d\n", __FUNCTION__, __FILE__, __LINE__);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error (Uncaptured)", serr_get_error().c_str(), NULL);
	}
#endif
    
    


	if(SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		serrf("SDL_Init Error: %s", SDL_GetError());
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), NULL);
		return 1;
	}

	#define SDL_CHECK(x) do{ \
		if((x) < 0){ \
			serrf(#x " Error: %s", SDL_GetError()); \
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), NULL); \
		} }while(0)

	//attributes must be set before the window.
	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1));
	#ifdef DESKTOP_GL
	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2));
    SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1));
	#else 
	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2));
    SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0));
	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES));
	#endif // DESKTOP_GL

    int context_flags = 0;
	if(cv_opengl_debug.get_value() == 1.0){
        context_flags |= SDL_GL_CONTEXT_DEBUG_FLAG;
	}

    //TODO: should make a switch if this causes problems.
    //SDL_GL_CONTEXT_RESET_ISOLATION_FLAG could also be used, but I am not certain if it's useful.
    context_flags |= SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG;

    if(context_flags != 0)
    {
        SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, context_flags);
    }

	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16));
	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8));
	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8));
	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8));
	SDL_CHECK(SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8));

	//gamma
	//SDL_CHECK(SDL_GL_SetAttribute( SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1 ));

	#undef SDL_CHECK

	Uint32 sdl_window_flags = cv_fullscreen.get_value() == 0.0 ? 0 
		: (static_cast<int>(cv_fullscreen_mode.get_value()) == FULLSCREEN_MODE_NATIVE ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP);
        
    int sdl_window_width = cv_screen_width.get_value();
    int sdl_window_height = cv_screen_height.get_value();

	SDL_Window* window = NULL;
	window = SDL_CreateWindow("A Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
                           sdl_window_width, sdl_window_height, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL | sdl_window_flags);
	if(window == NULL)
	{
		serrf("SDL_CreateWindow Error: %s", SDL_GetError());
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), NULL);
		return 1;
	}

	

	if(!InitAL())
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), window);
		return 1;
	}

	
	AL_OggStream music_stream;
	
	if(input_file != NULL){
		Unique_RWops music = Unique_RWops_OpenFS(input_file, "rb");
		if(!music)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), window);
			return 1;
		}

		if(!music_stream.open(std::move(music), AL_STREAM_LOOPING))
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), window);
			return 1;
		}

		if(!music_stream.play())
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), window);
			return 1;
		}
	}

	

	float colors[3] = {0,0,0};

	//global data
	int texture_wh[2]{-1,-1};
	GLuint texture_id = 0;
	Unique_RWops image_file(Unique_RWops_OpenFS("test.png", "rb"));
	if(!image_file)
	{
		return 1;
	}

	Unique_RWops gif_file(Unique_RWops_OpenFS("sexy.gif", "rb"));
	if(!gif_file)
	{
		return 1;
	}
	

	#define VERTEX_COUNT 6

	//basic shader specific
	GLuint basic_program_id = 0;
	basic_shader_properties basic_shader;
	GLuint basic_position_vbo_id = 0;
	GLuint basic_texCoord_vbo_id = 0;
	GLuint basic_vao_id = 0;

	//color shader
	GLuint color_program_id = 0;
	colorful_shader_properties color_shader;
	GLuint color_position_vbo_id = 0;
	GLuint color_texCoord_vbo_id = 0;
	GLuint color_colors_vbo_id = 0;
	GLuint color_vao_id = 0;

	//gif stuff
	GLuint gif_tex_id = 0;
	Unique_StbArrayData gif_delays;
	int gif_wh[2]{0,0};
    int gif_column_size = 0;
    int gif_frame_count = 0;
	GLuint gif_position_vbo_id = 0;
	GLuint gif_texCoord_vbo_id = 0;
	GLuint gif_vao_id = 0;

	GLint check_device_reset = GL_NO_ERROR;

	auto initialize_renderer = [&]
	{
        //clear previous SDL errors because we depend on checking it.
        SDL_ClearError();
        
		gl_context = SDL_GL_CreateContext(window);
		if(gl_context == NULL)
		{
			serrf("SDL_GL_CreateContext(): %s\n", SDL_GetError());
			return false;
		}

		//this happens because bad context hints are made when the context is made.
		//TODO: SDL_GetError is not thread safe... I don't think polling SDL could cause an error, but it might.
		const char* possible_error = SDL_GetError();
		if(possible_error != NULL && possible_error[0] != '\0')
		{
			slogf("Warning: GL context error: %s\n", possible_error);
		}

		if(!LoadGLContext(&ctx))
		{
			return false;
		}

		//const char* extension_list = (const char*)ctx.glGetString(GL_EXTENSIONS);
		//slog(extension_list);

		if(cv_opengl_debug.get_value() == 1.0)
		{
			//this allows you to get stacktraces from gl errors and much better error messages
			if(SDL_GL_ExtensionSupported("GL_KHR_debug") == SDL_TRUE && ctx.glDebugMessageControl != NULL && ctx.glDebugMessageCallback != NULL)
			{
				GL_CHECK( ctx.glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE) );
				//ctx.glDebugMessageControl(GL_DEBUG_SOURCE_API_KHR, GL_DEBUG_TYPE_OTHER_KHR, GL_DEBUG_SEVERITY_LOW_KHR, 0, nullptr, GL_FALSE);
				GL_CHECK( ctx.glDebugMessageCallback(ErrorCallback, nullptr) );
				GL_CHECK( ctx.glEnable(GL_DEBUG_OUTPUT) );
			} else {
				slog("warning: gl debug callbacks unsupported\n");
			}
		}
		

		if(SDL_GL_ExtensionSupported("GL_EXT_robustness") == SDL_TRUE)
		{
			GL_CHECK( ctx.glGetIntegerv(GL_RESET_NOTIFICATION_STRATEGY, &check_device_reset) );
		}
		

		//vsync
		if(SDL_GL_SetSwapInterval(static_cast<int>(cv_vsync.get_value())) != 0)
		{
			slogf("Warning: SDL_GL_SetSwapInterval(): %s\n", SDL_GetError());
		}

		//Set blending to blend
  		GL_CHECK_ERR( ctx.glEnable( GL_BLEND ), return false);
		//basic blend function
		GL_CHECK_ERR( ctx.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA ), return false);

		GL_CHECK_ERR( ctx.glCullFace(GL_BACK), return false );
		GL_CHECK_ERR( ctx.glEnable(GL_CULL_FACE), return false);

		//load the image into the vram
		texture_id = load_texture(image_file.get(), GL_NEAREST, &texture_wh[0], &texture_wh[1]);
		if(texture_id == 0)
		{
			return false;
		}

		//I don't need the file anymore.
		image_file.reset();
		
		gif_tex_id = load_animated_gif(gif_file.get(),GL_NEAREST,&gif_wh[0],&gif_wh[1],&gif_column_size,&gif_frame_count,gif_delays);
		if(gif_tex_id == 0)
		{
			return false;
		}
		gif_file.reset();
		//slogf("w: %d, h: %d, col_n: %d, frames: %d\n",gif_wh[0], gif_wh[1], gif_column_size, gif_frame_count);


		//basic shader initialization
		basic_program_id = load_basic_shader_program(basic_shader);
		if(basic_program_id == 0)
		{
			return false;
		}

		enum
		{
			POSITION_XYZ_SIZE = 3,
			TEXCOORD_ST_SIZE = 2,
			COLOR_RGBA_SIZE = 4
		};
		GLfloat common_texCoord_data[VERTEX_COUNT * TEXCOORD_ST_SIZE]
		{
			0,0,
			1,0,
			0,1,
			0,1,
			1,0,
			1,1
		};

		GLfloat basic_position_data[VERTEX_COUNT * POSITION_XYZ_SIZE]
		{
			0,0,0,
			1,0,0,
			0,1,0,
			0,1,0,
			1,0,0,
			1,1,0,
		};
		//create the buffer for the basic shader
		GL_CHECK_ERR( ctx.glGenBuffers(1, &basic_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, basic_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBufferData(GL_ARRAY_BUFFER, sizeof(basic_position_data), basic_position_data, GL_STATIC_DRAW), return false);
		GL_SANITY( ctx.glBindBuffer(GL_ARRAY_BUFFER, 0) );

		GL_CHECK_ERR( ctx.glGenBuffers(1, &basic_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, basic_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBufferData(GL_ARRAY_BUFFER, sizeof(common_texCoord_data), common_texCoord_data, GL_STATIC_DRAW), return false);
		GL_SANITY( ctx.glBindBuffer(GL_ARRAY_BUFFER, 0) );

		//basic VAO
		GL_CHECK_ERR( ctx.glGenVertexArrays(1, &basic_vao_id), return false);
		GL_CHECK_ERR( ctx.glBindVertexArray(basic_vao_id), return false);

		//position vertex setup
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, basic_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glVertexAttribPointer(
			basic_shader.a_position,  // attribute
			POSITION_XYZ_SIZE,                                // size
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                // stride
			0                         // array buffer offset
		), return false);

		//texcoord vertex setup
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, basic_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glVertexAttribPointer(
			basic_shader.a_texCoord,  // attribute
			TEXCOORD_ST_SIZE,                                // size
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                // stride
			0                          // array buffer offset
		), return false);

		GL_CHECK_ERR( ctx.glEnableVertexAttribArray(basic_shader.a_position), return false);
		GL_CHECK_ERR( ctx.glEnableVertexAttribArray(basic_shader.a_texCoord), return false);

		//finish
		GL_SANITY( ctx.glBindVertexArray(0) );


		
		//color shader initialization
		color_program_id = load_colorful_shader_program(color_shader);
		if(color_program_id == 0)
		{
			return false;
		}
		
		GLfloat color_position_data[VERTEX_COUNT * POSITION_XYZ_SIZE]
		{
			0,0,0,
			-1,0,0,
			0,-1,0,
			0,-1,0,
			-1,0,0,
			-1,-1,0,
		};
		GLubyte color_colors_data[VERTEX_COUNT * COLOR_RGBA_SIZE * 2]
		{
			255,255,255,255,
			0,0,0,0,

			255,255,255,255,
			0,0,0,0,

			255,255,255,255,
			0,0,0,0,
			
			255,255,255,255,
			0,0,0,0,

			255,255,255,255,
			0,0,255,255,

			255,255,255,255,
			0,0,255,255,
		};
		
		

		//create the buffer for the shader
		GL_CHECK_ERR( ctx.glGenBuffers(1, &color_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, color_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBufferData(GL_ARRAY_BUFFER, sizeof(color_position_data), color_position_data, GL_STATIC_DRAW), return false);

		GL_CHECK_ERR( ctx.glGenBuffers(1, &color_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, color_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBufferData(GL_ARRAY_BUFFER, sizeof(common_texCoord_data), common_texCoord_data, GL_STATIC_DRAW), return false);

		GL_CHECK_ERR( ctx.glGenBuffers(1, &color_colors_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, color_colors_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBufferData(GL_ARRAY_BUFFER, sizeof(color_colors_data), color_colors_data, GL_STATIC_DRAW), return false);

		GL_SANITY( ctx.glBindBuffer(GL_ARRAY_BUFFER, 0) );

		//setup vao
		GL_CHECK_ERR( ctx.glGenVertexArrays(1, &color_vao_id), return false);
		GL_CHECK_ERR( ctx.glBindVertexArray(color_vao_id), return false);

		//setup position vertex
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, color_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glVertexAttribPointer(
			color_shader.a_position,  // attribute
			POSITION_XYZ_SIZE,                                // size
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                // stride
			0                         // array buffer offset
		), return false);

		//setup tex vertex
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, color_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glVertexAttribPointer(
			color_shader.a_texCoord,  // attribute
			TEXCOORD_ST_SIZE,                                // size
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                // stride
			0                          // array buffer offset
		), return false);
		
		//setup colors vertex
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, color_colors_vbo_id), return false);
		GL_CHECK_ERR( ctx.glVertexAttribPointer(
			color_shader.a_multColor,  // attribute
			COLOR_RGBA_SIZE,                                // size
			GL_UNSIGNED_BYTE,                         // type
			GL_TRUE,                         // normalized?
			COLOR_RGBA_SIZE * 2,                // stride
			(void*)(0)                         // array buffer offset
		), return false);

		GL_CHECK_ERR( ctx.glVertexAttribPointer(
			color_shader.a_layerColor,  // attribute
			COLOR_RGBA_SIZE,                                // size
			GL_UNSIGNED_BYTE,                         // type
			GL_TRUE,                         // normalized?
			COLOR_RGBA_SIZE * 2,                // stride
			(void*)(COLOR_RGBA_SIZE)                          // array buffer offset
		), return false);

		GL_CHECK_ERR( ctx.glEnableVertexAttribArray(color_shader.a_position), return false);
		GL_CHECK_ERR( ctx.glEnableVertexAttribArray(color_shader.a_texCoord), return false);
		GL_CHECK_ERR( ctx.glEnableVertexAttribArray(color_shader.a_multColor), return false);
		GL_CHECK_ERR( ctx.glEnableVertexAttribArray(color_shader.a_layerColor), return false);

		//finish
		GL_SANITY( ctx.glBindVertexArray(0) );


		//
		// GIF START
		//

		GLfloat gif_position_data[VERTEX_COUNT * POSITION_XYZ_SIZE]
		{
			0.5,0.5,0,
			-0.5,0.5,0,
			0.5,-0.5,0,
			0.5,-0.5,0,
			-0.5,0.5,0,
			-0.5,-0.5,0,
		};

		GL_CHECK_ERR( ctx.glGenBuffers(1, &gif_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, gif_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBufferData(GL_ARRAY_BUFFER, sizeof(gif_position_data), gif_position_data, GL_STATIC_DRAW), return false);
		GL_SANITY( ctx.glBindBuffer(GL_ARRAY_BUFFER, 0) );

		GL_CHECK_ERR( ctx.glGenBuffers(1, &gif_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, gif_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glBufferData(GL_ARRAY_BUFFER, sizeof(common_texCoord_data), common_texCoord_data, GL_DYNAMIC_DRAW), return false);
		GL_SANITY( ctx.glBindBuffer(GL_ARRAY_BUFFER, 0) );

		//basic VAO
		GL_CHECK_ERR( ctx.glGenVertexArrays(1, &gif_vao_id), return false);
		GL_CHECK_ERR( ctx.glBindVertexArray(gif_vao_id), return false);

		//position vertex setup
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, gif_position_vbo_id), return false);
		GL_CHECK_ERR( ctx.glVertexAttribPointer(
			basic_shader.a_position,  // attribute
			POSITION_XYZ_SIZE,                                // size
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                // stride
			0                         // array buffer offset
		), return false);

		//texcoord vertex setup
		GL_CHECK_ERR( ctx.glBindBuffer(GL_ARRAY_BUFFER, gif_texCoord_vbo_id), return false);
		GL_CHECK_ERR( ctx.glVertexAttribPointer(
			basic_shader.a_texCoord,  // attribute
			TEXCOORD_ST_SIZE,                                // size
			GL_FLOAT,                         // type
			GL_FALSE,                         // normalized?
			0,                // stride
			0                          // array buffer offset
		), return false);

		GL_CHECK_ERR( ctx.glEnableVertexAttribArray(basic_shader.a_position), return false);
		GL_CHECK_ERR( ctx.glEnableVertexAttribArray(basic_shader.a_texCoord), return false);

		//finish
		GL_SANITY( ctx.glBindVertexArray(0) );

		//
		// GIF END
		//

		if(ctx.glGetError() != GL_NO_ERROR)
		{
			//it's just a warning because most likely I am being sloppy with shaders, with missing attributes.
			slog("warning: GL error during initialization\n");
		}

		return !serr_check_error();
	};
	auto destroy_renderer = [&]
	{
		if(ctx.glGetError() != GL_NO_ERROR)
		{
			serr("GL error during runtime\n");
		}

		//macros are bad ok?
#define SAFE_GL_DELETE_VBO(id) do{\
    if(id != 0) {GL_CHECK(ctx.glDeleteBuffers(1, &id)); id = 0;}\
}while(0)
#define SAFE_GL_DELETE_VAO(id) do{\
    if(id != 0) {GL_CHECK(ctx.glDeleteVertexArrays(1, &id)); id = 0;}\
}while(0)
#define SAFE_GL_DELETE_PROGRAM(id) do{\
    if(id != 0) {GL_CHECK(ctx.glDeleteProgram(id)); id = 0;}\
}while(0)
#define SAFE_GL_DELETE_TEXTURE(id) do{\
    if(id != 0) {GL_CHECK(ctx.glDeleteTextures(1, &id)); id = 0;}\
}while(0)

		SAFE_GL_DELETE_VBO(color_position_vbo_id);
		SAFE_GL_DELETE_VBO(color_texCoord_vbo_id);
		SAFE_GL_DELETE_VBO(color_colors_vbo_id);
		SAFE_GL_DELETE_VAO(color_vao_id);

		SAFE_GL_DELETE_VBO(basic_position_vbo_id);
		SAFE_GL_DELETE_VBO(basic_texCoord_vbo_id);
		SAFE_GL_DELETE_VAO(basic_vao_id);

		SAFE_GL_DELETE_VBO(gif_position_vbo_id);
		SAFE_GL_DELETE_VBO(gif_texCoord_vbo_id);
		SAFE_GL_DELETE_VAO(gif_vao_id);
		

		SAFE_GL_DELETE_PROGRAM(color_program_id);
		SAFE_GL_DELETE_PROGRAM(basic_program_id);

		SAFE_GL_DELETE_TEXTURE(gif_tex_id);
		SAFE_GL_DELETE_TEXTURE(texture_id);

#undef SAFE_GL_DELETE_TEXTURE
#undef SAFE_GL_DELETE_PROGRAM
#undef SAFE_GL_DELETE_VAO
#undef SAFE_GL_DELETE_VBO


		if(ctx.glGetError() != GL_NO_ERROR)
		{
			serr("GL error during destruction\n");
		}
		
		if(gl_context != NULL)
		{
			ASSERT(SDL_GL_GetCurrentContext() == gl_context);
			SDL_GL_DeleteContext(gl_context);
			gl_context = NULL;
			//make sure that a crash happens when you call ctx
			memset(&ctx, 0, sizeof(ctx));
		}

		return !serr_check_error();
	};
	
    if(!initialize_renderer())
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), window);
        return 1;
    }
	
	enum{
		LOOP_RUNNING,
		LOOP_REQUEST_STOP,
		LOOP_ERROR
	};
    int loop_state = LOOP_RUNNING;

    //returns false if loop_state is not LOOP_RUNNING
    //not necessary an error.
	auto app_input = [&](SDL_Event &e)->bool
	{
		switch(e.type)
		{
		case SDL_QUIT:
			loop_state = LOOP_REQUEST_STOP;
            return false;
			break;
		case SDL_KEYDOWN:
			switch(e.key.keysym.sym)
			{
			//toggle fullscreen
			case SDLK_RETURN:
				if((SDL_GetModState() & KMOD_ALT))
				{
					cv_fullscreen.set_value((static_cast<int>(cv_fullscreen.get_value()) + 1) % 2);
					if( cv_fullscreen.get_value() == 1.0 )
					{
                        Uint32 window_flags = (static_cast<int>(cv_fullscreen_mode.get_value()) == FULLSCREEN_MODE_NATIVE)
								? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
						if( SDL_SetWindowFullscreen( window, window_flags ) < 0 )
						{
							serrf("Window could not be fullscreen'd! Error: %s\n", SDL_GetError());
							loop_state = LOOP_ERROR;
							return false;
						}
					}
					else 
					{
						if( SDL_SetWindowFullscreen(window, 0 ) < 0 )
						{
							serrf("Window could not be windowed! Error: %s\n", SDL_GetError());
							loop_state = LOOP_ERROR;
                            return false;
						}
					}
				}
            }
			break;
		}
		return true;
	};
	
    //the void* parameter is for emscripten.
	auto app_update = [&](void* = NULL)
	{
        
#ifdef __EMSCRIPTEN__
        if(loop_state != LOOP_RUNNING)
        {
            emscripten_cancel_main_loop();
            return;
        }
#endif

        SDL_Event e;
        while(SDL_PollEvent(&e) != 0)
        {
            if(!app_input(e))
                return;
        }

		if(input_file != NULL)
		{
			if(!music_stream.update())
			{
                loop_state = LOOP_ERROR;
				return;
			}
			if(!music_stream.is_playing())
			{
                loop_state = LOOP_ERROR;
				return;
			}
		}

        if(check_device_reset != GL_NO_ERROR && check_device_reset != GL_NO_RESET_NOTIFICATION)
		{
			GLenum status;
			GL_RUNTIME( status = ctx.glGetGraphicsResetStatus() );
			switch(status)
			{
			case GL_NO_ERROR:
				//good.
				break;
			case GL_GUILTY_CONTEXT_RESET:
				serr("GL_GUILTY_CONTEXT_RESET\n");
                loop_state = LOOP_ERROR;
				return;
			case GL_INNOCENT_CONTEXT_RESET:
				//NOTE: I could try to restore all the opengl context, but it is pure suffering.
				serr("GL_INNOCENT_CONTEXT_RESET\n");
                loop_state = LOOP_ERROR;
				return;
			case GL_UNKNOWN_CONTEXT_RESET:
				serr("GL_UNKNOWN_CONTEXT_RESET\n");
                loop_state = LOOP_ERROR;
				return;
			default:
				slogf("warning: glGetGraphicsResetStatus returned unknown: (0x%.8x)\n", status);
				check_device_reset = GL_NO_ERROR; //just in case of spam
			}
		}

		static TIMER_U color_time = timer_now();
		TIMER_U current_time = timer_now();
		TIMER_RESULT color_delta = timer_delta<TIMER_SEC>(color_time, current_time);
		color_time = current_time;

		colors[0] = colors[0] + (0.5 * color_delta);
		colors[1] = colors[1] + (0.7 * color_delta);
		colors[2] = colors[2] + (0.11 * color_delta);

		GL_RUNTIME( ctx.glClearColor(
			(SDL_sinf(colors[0]) + 1.0) / 2.0,  
			(SDL_sinf(colors[1]) + 1.0) / 2.0, 
			(SDL_sinf(colors[2]) + 1.0) / 2.0, 
			1));

#if 0
//I can test if vsync works with this, it causes rips because of sudden change of color.
		color_delta *= 10;
		
		GL_RUNTIME( ctx.glClearColor(
		SDL_fmodf(colors[0], 1),  
		SDL_fmodf(colors[1], 1), 
		SDL_fmodf(colors[2], 1), 
		1));
#endif

		
		GL_RUNTIME( ctx.glClear(GL_COLOR_BUFFER_BIT) );

		//use shader
		GL_RUNTIME( ctx.glUseProgram(basic_program_id) );
		GL_RUNTIME( ctx.glActiveTexture(GL_TEXTURE0) );
  		GL_RUNTIME( ctx.glBindTexture(GL_TEXTURE_2D, texture_id) );

		//shader uniforms
		GL_RUNTIME( ctx.glUniform1i(basic_shader.s_texture, 0) );

		//set attribues and draw
		GL_RUNTIME( ctx.glBindVertexArray(basic_vao_id) );
		GL_RUNTIME( ctx.glDrawArrays(GL_TRIANGLES, 0, VERTEX_COUNT) );
		
		//cleanup program (TODO: but you can cache these values for the next draw)
		GL_SANITY( ctx.glBindVertexArray(0) );
		GL_SANITY( ctx.glBindTexture(GL_TEXTURE_2D, 0) );

		//
		//render gif
		//
		if(gif_delays[0] != 0){	//if this is not an animated image
			static TIMER_U gif_animation_timer = current_time;
			static int gif_current_frame = 0;
            static TIMER_RESULT gif_loop_total_ms = 0;
            static TIMER_RESULT gif_accum = 0;
            
            //calculate the total loop length
            if(gif_loop_total_ms == 0)
            {
                for(int i = 0; i < gif_frame_count; ++i)
                {
                    ASSERT(gif_delays[i] >= 0);
                    gif_loop_total_ms += gif_delays[i];
                }
            }
            
            
            TIMER_RESULT gif_delta_time = timer_delta<TIMER_MS>(gif_animation_timer, current_time);
            gif_animation_timer = current_time;
            
            //skip full loops (in case of a really long hang)
            gif_delta_time -= static_cast<int>(gif_delta_time / gif_loop_total_ms) * gif_loop_total_ms;
            
            gif_accum += gif_delta_time;
            
            int gif_new_frame = gif_current_frame;
            
            //NOTE: I am unsure if I should use > or >=, it wouldn't cause a desync but it will cause an offset.
            for(;gif_accum > gif_delays[gif_new_frame]; gif_accum -= gif_delays[gif_new_frame])
            {
                gif_new_frame = (gif_new_frame+1) % gif_frame_count;
            }
            
            //upload changes
			if(gif_new_frame != gif_current_frame)
            {
                gif_current_frame = gif_new_frame;
                
				GLfloat atlas_x = gif_current_frame / gif_column_size;
				GLfloat atlas_y = gif_current_frame % gif_column_size;
				GLfloat atlas_width = SDL_ceilf((GLfloat)gif_frame_count / (GLfloat)gif_column_size);
				GLfloat atlas_height = gif_column_size;
				GLfloat minx = atlas_x / atlas_width;
				GLfloat miny = atlas_y / atlas_height;
				GLfloat maxx = (atlas_x + 1.0) / atlas_width;
				GLfloat maxy = (atlas_y + 1.0) / atlas_height;
                
				GLfloat common_texCoord_data[VERTEX_COUNT * 2];
				common_texCoord_data[0] = minx;
				common_texCoord_data[1] = miny;

				common_texCoord_data[2] = maxx;
				common_texCoord_data[3] = miny;

				common_texCoord_data[4] = minx;
				common_texCoord_data[5] = maxy;

				common_texCoord_data[6] = minx;
				common_texCoord_data[7] = maxy;

				common_texCoord_data[8] = maxx;
				common_texCoord_data[9] = miny;

				common_texCoord_data[10] = maxx;
				common_texCoord_data[11] = maxy;

				GL_RUNTIME( ctx.glBindBuffer(GL_ARRAY_BUFFER, gif_texCoord_vbo_id) );
				GL_RUNTIME( ctx.glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(common_texCoord_data), common_texCoord_data) );
                GL_SANITY( ctx.glBindBuffer(GL_ARRAY_BUFFER, 0) );
			}
		}
		


		GL_RUNTIME( ctx.glBindTexture(GL_TEXTURE_2D, gif_tex_id) );
		GL_RUNTIME( ctx.glBindVertexArray(gif_vao_id) );
		GL_RUNTIME( ctx.glDrawArrays(GL_TRIANGLES, 0, VERTEX_COUNT) );
		
		//cleanup program (TODO: but you can cache these values for the next draw)
		GL_SANITY( ctx.glBindVertexArray(0) );
		GL_SANITY( ctx.glBindTexture(GL_TEXTURE_2D, 0) );


		GL_SANITY( ctx.glUseProgram(0) );


		//
		// COLOR SHADER
		//

		//use shader
		GL_RUNTIME( ctx.glUseProgram(color_program_id) );
		GL_RUNTIME( ctx.glActiveTexture(GL_TEXTURE0) );
  		GL_RUNTIME( ctx.glBindTexture(GL_TEXTURE_2D, texture_id) );

		//shader uniforms
		GL_RUNTIME( ctx.glUniform1i(color_shader.s_texture, 0) );

		//set attribues and draw
		GL_RUNTIME( ctx.glBindVertexArray(color_vao_id) );
		GL_RUNTIME( ctx.glDrawArrays(GL_TRIANGLES, 0, VERTEX_COUNT) );
		GL_SANITY( ctx.glBindVertexArray(0) );

		//cleanup program
		GL_SANITY( ctx.glBindTexture(GL_TEXTURE_2D, 0) );
		GL_SANITY( ctx.glUseProgram(0) );
        
		SDL_GL_SwapWindow(window);
        
        //check if any GL_RUNTIME errors were made.
		if(serr_check_error())
        {
            serr("note: this should be an opengl error.'\n");
            loop_state = LOOP_ERROR;
            return;
        }

		return;
	};
    
    //before starting the loop, check for uncaught errors.
	if(serr_check_error())
	{
		serrf("\nUncaught Error Check, Function: %s, File: %s, Line: %d\n", __FUNCTION__, __FILE__, __LINE__);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error (Uncaptured)", serr_get_error().c_str(), NULL);
	}
	
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(app_update, 0, 1);
#else
    while(loop_state == LOOP_RUNNING)
    {
        app_update();
    }
#endif

	ASSERT(loop_state != LOOP_RUNNING);
	
	if(loop_state == LOOP_ERROR && !serr_check_error())
	{
		//LOOP_ERROR means THIS THREAD has an error.
		serrf("Exited unexpectedly without errors...\n");
	}
	else if(loop_state == LOOP_REQUEST_STOP && serr_check_error())
	{
		serrf("\nExited normally but errors were made...\n");
	}

	if(serr_check_error())
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), NULL);
	}
	
    if(!destroy_renderer())
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), window);
    }


	if(!music_stream.close())
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), window);
	}

	CloseAL();

	if(window != NULL)
	{
		SDL_DestroyWindow(window);
		window = NULL;
	}

	SDL_Quit();

	//last chance to catch any serr messages (hopefully there wasn't anything with static destructors....)
	if(serr_check_error())
	{
		serrf("\nUncaught Error Check, Function: %s, File: %s, Line: %d\n", __FUNCTION__, __FILE__, __LINE__);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error (Uncaptured)", serr_get_error().c_str(), NULL);
	}
	return 0;
}
