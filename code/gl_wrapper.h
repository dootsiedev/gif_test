#pragma once

#define DESKTOP_GL

#include "SDL_wrapper.h"

//I copied this from the sdl2 test example.

#ifndef DESKTOP_GL
#include <SDL2/SDL_opengles2.h>
#else
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_opengl_glext.h>
#endif

struct GLES2_Context
{

#define SDL_PROC(ret,func,params) ret (APIENTRY *func) params;
#define SDL_PROC_EXTENSION(ret, func, func_ext, params) ret (APIENTRY *func) params;
#include "opengles2/SDL_gles2funcs.h"
#undef SDL_PROC
#undef SDL_PROC_EXTENSION

};



//global context holding functions.
extern GLES2_Context ctx;

//load this after creating the opengl context
MYNODISCARD bool LoadGLContext(GLES2_Context * data);


//never returns NULL
//opengl debug callbacks are vastly superior in helpfulness (actual messages with specific errors).
//this only returns the 5~ enums that you get from glGetError. 
//But im keeping it just in case you disable debug callbacks.
const char* gl_err_string(GLenum glError);


//note that you cannot treat GL_CHECK as a single expression on an if, while, and for statement, without braces.
//the second parameter could be used as an error action, like GL_CHECK(glBufferData(), return false);
#define GL_CHECK_ERR(x, err) \
    { \
        x; \
        GLenum glError = ctx.glGetError(); \
        if(glError != GL_NO_ERROR) { \
            serrf("GL_CHECK failed\nFuction: %s\nFile: %s\nLine: %i\nExpression: `%s`\n", __FUNCTION__, __FILE__, __LINE__, #x); \
            while(glError != GL_NO_ERROR) { \
                serrf("glGetError() = %s (0x%.8x)\n", gl_err_string(glError), glError); \
                glError = ctx.glGetError(); \
            } \
            err; \
        } \
    }

#define GL_CHECK_ERR_MSG(x, err, msg) \
    { \
        x; \
        GLenum glError = ctx.glGetError(); \
        if(glError != GL_NO_ERROR) { \
            serrf("GL_CHECK failed (hint: %s)\nFuction: %s\nFile: %s\nLine: %i\nExpression: `%s`\n", msg, __FUNCTION__, __FILE__, __LINE__, #x); \
            while(glError != GL_NO_ERROR) { \
                serrf("glGetError() = %s (0x%.8x)\n", gl_err_string(glError), glError); \
                glError = ctx.glGetError(); \
            } \
            err; \
        } \
    }

#define GL_CHECK_MSG(x, msg) GL_CHECK_ERR_MSG(x, (void)0, msg)

//check for errors, but instead use serr_check_errors to check for errors.
#define GL_CHECK(x) GL_CHECK_ERR(x, (void)0)

//CHECK_GL_RUNTIME is off by default because there exists debug callbacks on newer opengl versions
//debug callbacks could also replace the need of GL_CHECK, but atm I use an old version of gl.
#ifdef CHECK_GL_RUNTIME
//this is just to say "this is for sanity, this could be optimized away"
//like binding to 0 so that bad code will be more likely to give errors instead of using old state.
#define GL_SANITY(x) GL_CHECK_ERR(x, (void)0)

//this is a function that runs during runtime, not while loading, which means checking errors is slow.
//TODO: use a different error printing function so that you have a maximum gl error print count, like 50, and afterwards it's silenced.
#define GL_RUNTIME(x) GL_CHECK_ERR(x, (void)0)
#else
//optimized out since you shouldn't call glGet in the rendering loop.
#define GL_SANITY(x) x
#define GL_RUNTIME(x) x
#endif


struct STB_Image_Deleter
{
	void operator()(void* data);
};

typedef std::unique_ptr<void, STB_Image_Deleter> Unique_StbImageData;

struct STB_Array_Deleter
{
	void operator()(int* data);
};
typedef std::unique_ptr<int[], STB_Array_Deleter> Unique_StbArrayData;

//it is always in RGB or RGBA format, returns an empty ptr on error.
MYNODISCARD Unique_StbImageData load_binary_texture(RWops* file, int* w, int* h, bool* rgba);

//returns 0 if an error occurred.
MYNODISCARD GLuint load_texture(RWops* file, GLint filtering, int* w, int* h, bool* rgba = NULL);

//returns 0 if an error occurred
MYNODISCARD GLuint load_animated_gif(RWops* file, GLint filtering, int* w, int* h, int* column_size, int* frames, Unique_StbArrayData& delays, bool* rgba = NULL);

struct basic_shader_properties
{
    // Sampler locations
    GLint s_texture;

    // Attribute locations
    GLint  a_position;
    GLint  a_texCoord;
};

//returns 0 if an error occurred.
MYNODISCARD GLuint load_basic_shader_program(basic_shader_properties& data);

struct colorful_shader_properties
{
    // Sampler locations
    GLint s_texture;

    // Attribute locations
    GLint  a_position;
    GLint  a_texCoord;
    GLint  a_multColor;
    GLint  a_layerColor;

    struct color_vertex
	{
        GLubyte mult_rgba[4];
        GLubyte layer_rgba[4];
	};
    //just share the tex_coord from basic_shader_properties
};

MYNODISCARD GLuint load_colorful_shader_program(colorful_shader_properties& data);

struct palette_shader_properties
{
    // Sampler locations
    GLint s_texture;

    // uniforms
    GLint u_palette;    //256 vec4's note you only have 256-1 usable colors, because the last index (100% opaque alpha) is used for non-palette pixels.
    GLint u_palette_mode;   //a float, 0.0 = overwrite (with alpha mix), 1.0 = multiply, 2.0 = layer

    // Attribute locations
    GLint  a_position;
    GLint  a_texCoord;

    struct color_vertex
	{
        GLubyte mult_rgba[4];
        GLubyte layer_rgba[4];
	};
    //just share the tex_coord from basic_shader_properties
};
