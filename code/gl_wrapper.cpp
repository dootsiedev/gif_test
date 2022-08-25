#include "global.h"
#include "gl_wrapper.h"

GLES2_Context ctx;

bool LoadGLContext(GLES2_Context * data)
{

#if defined(SDL_VIDEO_DRIVER_UIKIT) || defined(SDL_VIDEO_DRIVER_ANDROID) || defined(SDL_VIDEO_DRIVER_PANDORA)
#define __SDL_NOGETPROCADDR__
#endif


#if defined __SDL_NOGETPROCADDR__
#define SDL_PROC(ret,func,params) data->func=func;
#define SDL_PROC_EXTENSION (ret,func,ext,params) data->func=func;
#else
#define SDL_PROC(ret,func,params) \
    do { \
        data->func = (decltype(data->func))SDL_GL_GetProcAddress(#func); \
        if ( ! data->func ) { \
			serrf("Couldn't load GL function %s: %s\n", #func, SDL_GetError());\
            return false; \
        } \
    } while ( 0 );
#define SDL_PROC_EXTENSION(ret,func,ext,params) data->func = (decltype(data->func))SDL_GL_GetProcAddress(#func #ext);
#endif /* __SDL_NOGETPROCADDR__ */

#include "opengles2/SDL_gles2funcs.h"

#undef SDL_PROC
#undef SDL_PROC_EXTENSION
    return true;
}


#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
//GCC errors
#pragma GCC diagnostic ignored "-Wduplicated-branches"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif // __GNUC__



//the one problem with stb_image is that it forces rgba, when win32 prefers bgra (and everyone uses windows).
//and I wouldn't want to convert all formats into bgra, but I would prefer loading the image at it's native order,
//and since stb doesn't use libpng/libjpeg (and libjpeg/libpng had CVE reports), I worry that stb is far more vulnerable...
//it's simple, but it's not smart. I wouldn't be using stb_image if it didn't support animated gifs.
//I would use giflib, it seems like the developer has never used windows before.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#include "stb/stb_image.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif // __GNUC__

// fill 'data' with 'size' bytes.  return number of bytes actually read
static int stbRWopsRead(void *user, char *data, int size)
{
    return static_cast<RWops*>(user)->read(data, 1, size);
}
// skip the next 'n' bytes, or 'unget' the last -n bytes if negative
static void stbRWopsSkip(void *user, int n)
{
    static_cast<RWops*>(user)->seek(n, SEEK_CUR);
}
// returns nonzero if we are at end of file/data
static int stbRWopsEOF(void *user)
{
    //yea stdio offers an EOF function, but why would you need it?
    int old_tell = static_cast<RWops*>(user)->tell();
    static_cast<RWops*>(user)->seek(0, SEEK_END);
    int new_tell = static_cast<RWops*>(user)->tell();
    if(old_tell != new_tell)
    {
        static_cast<RWops*>(user)->seek(old_tell, SEEK_SET);
    }
    return (old_tell == new_tell);
}

static stbi_io_callbacks g_stbRWopsCallbacks {stbRWopsRead,stbRWopsSkip, stbRWopsEOF};


//a scrappy alternative to gluErrorString (does gles support it?)
const char* gl_err_string(GLenum glError)
{
#define GLES2_ERROR(x)     \
	case(x): \
		return (#x);
	switch(glError)
	{
		GLES2_ERROR(GL_NO_ERROR)
        GLES2_ERROR(GL_INVALID_ENUM)
        GLES2_ERROR(GL_INVALID_VALUE)
        GLES2_ERROR(GL_INVALID_OPERATION)
        GLES2_ERROR(GL_INVALID_FRAMEBUFFER_OPERATION)
        GLES2_ERROR(GL_OUT_OF_MEMORY)
	}
#undef GLES2_ERROR
    return "UNKNOWN(GLES2)";
}


void STB_Image_Deleter::operator()(void* data){
    stbi_image_free(data);
}

struct SDL_Surface_Deleter
{
	void operator()(SDL_Surface* data)
    {
        SDL_FreeSurface(data);
    }
};

void STB_Array_Deleter::operator()(int* data)
{
    STBI_FREE(data);
};



Unique_StbImageData load_binary_texture(RWops* file, int* w, int* h, bool* rgba)
{
    int channels;
    Unique_StbImageData stb_data(stbi_load_from_callbacks(&g_stbRWopsCallbacks, file, w, h, &channels, 0));
    if(!stb_data)
    {
        serrf("Could not load image: %s in `%s`\n", stbi_failure_reason(), file->stream_info);
        return stb_data;
    }
    if(rgba != NULL){
        switch(channels)
        {
        case 3:
            *rgba = false;
            break;
        case 4:
            *rgba = true;
            break;
        default:
            serrf("Unsupported channel count: %d in `%s`\n", channels, file->stream_info);
            stb_data.reset();
        }
    }

    return stb_data;
}

GLuint load_texture(RWops* file, GLint filtering, int* w, int* h, bool* rgba)
{
    ASSERT(file != NULL);
    ASSERT(w != NULL);
    ASSERT(h != NULL);
    
    bool got_rgba = false;
    Unique_StbImageData data(load_binary_texture(file, w, h, &got_rgba));
    if(!data)
    {
        return 0;
    }

    if(rgba != NULL) *rgba = got_rgba;

    GLint internal_format = 0;
    //internal_format = params.gamma_correction ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    GLint format = 0;

    if(got_rgba)
    {
        internal_format = GL_RGBA;
        format = GL_RGBA;
    }
    else
    {
        internal_format = GL_RGB;
        format = GL_RGB;
    }
    
    if(format == 0)
    {
        serrf("Failed to find a gl format for: %s\n", file->stream_info);
        return 0;
    }

    GLuint tex_id;
    GL_CHECK_ERR_MSG( ctx.glGenTextures( 1, &tex_id ), return 0, file->stream_info );
    //tricky unwinding.
    do{
        GL_CHECK_ERR_MSG( ctx.glBindTexture( GL_TEXTURE_2D, tex_id ), break, file->stream_info );
        GL_CHECK_ERR_MSG( ctx.glTexImage2D( GL_TEXTURE_2D, 0, internal_format, *w, *h, 0, format, GL_UNSIGNED_BYTE, data.get() ), break, file->stream_info );
        //set parameters
        GL_CHECK_ERR_MSG( ctx.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering ), break, file->stream_info );
        GL_CHECK_ERR_MSG( ctx.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering ), break, file->stream_info );
        GL_CHECK_ERR_MSG( ctx.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE ), break, file->stream_info );
        GL_CHECK_ERR_MSG( ctx.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE ), break, file->stream_info );
        GL_SANITY( ctx.glBindTexture( GL_TEXTURE_2D, 0 ) );

        if(ctx.glGetError() != GL_NO_ERROR)
        {
            serrf("%s GL error in `%s`\n", __FUNCTION__, file->stream_info);
            break;
        }

        //success
        return tex_id;
    } while (false);

    //if an error occured.
    GL_CHECK_ERR_MSG( ctx.glDeleteTextures( 1, &tex_id ), return 0, file->stream_info );

    return 0;
}

GLuint load_animated_gif(RWops* file, GLint filtering, int* w, int* h, int* column_size, int* frames, Unique_StbArrayData& delays, bool* rgba)
{
    ASSERT(file != NULL);
    ASSERT(w != NULL);
    ASSERT(h != NULL);
    ASSERT(column_size != NULL);
    ASSERT(frames != NULL);
    
#ifdef GIF_TIMER
    TIMER_U t2;
    TIMER_U t1 = timer_now();
#endif
    
    if(file->seek(0, SEEK_END) != 0)
    {
        serrf("SDL_RWseek: file stream can't seek\n");
        return 0;
    }
    int file_length = file->tell();
    if(file->seek(0, SEEK_SET) != 0)
    {
        serrf("SDL_RWseek: file stream can't seek\n");
        return 0;
    }
    std::unique_ptr<unsigned char[]> file_memory(new unsigned char[file_length]);

    if(static_cast<int>(file->read(file_memory.get(), 1, file_length)) != file_length)
    {
        serrf("SDL_RWread: could not read the file\n");
        return 0;
    }

#ifdef GIF_TIMER
    t2 = timer_now();
    slogf("file IO time: %f\n", timer_delta<TIMER_MS>(t1,t2));
    t1 = timer_now();
#endif

    int* delays_get = NULL;
    int channels;
    
    //a nitpick is that stb doesn't use callback IO for animated gifs
    //I also want to use giflib, but I would need to modify it a tiny bit just to get it to work on windows (I think <stdbool.h>)
    //the benefit of giflib is that I can avoid loading the gif on another thread (since it takes a very long time to load)
    //and I can get rid of the wasteful atlas because I can stream the individual frames into opengl 
    //and this would be very fast to load compared to loading the whole gif into frames.
    //also stb_image has a problem that some gifs will just cause a stack overflow due to stb using a bunch of recursion.
    //but giflib animation streaming is complicated (even just using it to get the same result as stb using slurp isn't simple).
    Unique_StbImageData stb_data(stbi_load_gif_from_memory(file_memory.get(), file_length, &delays_get, w, h, frames, &channels, 0));
    if(!stb_data)
    {
        serrf("Could not load image: %s in `%s`\n", stbi_failure_reason(), file->stream_info);
        return 0;
    }
    
    //give the memory to the output.
    delays.reset(delays_get);

#ifdef GIF_TIMER
    //this takes the most time
    t2 = timer_now();
    slogf("gif parsing time: %f\n", timer_delta<TIMER_MS>(t1,t2));
    t1 = timer_now();
#endif

    if(rgba != NULL){
        switch(channels)
        {
        case 3:
            *rgba = false;
            break;
        case 4:
            *rgba = true;
            break;
        default:
            serrf("Unsupported channel count: %d in `%s`\n", channels, file->stream_info);
            return 0;
        }
    }

    int max_texture = 2048;

    if(*w > max_texture)
    {
        serrf("%s: width (%d) larger than max texture size (%d)\n", __FUNCTION__, *w, max_texture);
        return 0;
    }

    if(*h > max_texture)
    {
        serrf("%s: height (%d) larger than max texture size (%d)\n", __FUNCTION__, *h, max_texture);
        return 0;
    }

    //stb stores the animation is a horizontal column (like a mipmap)
    //but if the animation is too long I need to loop it to the next row (I am targeting a minimum of 2024px max textures).
    //this calculation is not space efficient when there are 2 columns and 1 column is mostly empty, but it it's fine.
    *column_size = SDL_min(*frames, max_texture / *h);

    int atlas_columns = SDL_ceilf((float)(*frames) / (float)(*column_size));

    int atlas_width = atlas_columns * (*w);

    if(atlas_width > max_texture)
    {
        serrf("%s: height (%d) larger than max texture size (%d)\n", __FUNCTION__, atlas_width, max_texture);
        return 0;
    }

    std::unique_ptr<SDL_Surface, SDL_Surface_Deleter> atlas_texture(SDL_CreateRGBSurfaceWithFormat(0,
            atlas_width, (*column_size) * (*h), 
            (channels == 4 ? 32 : 24),
            //TODO: on modern opengl you can query the ideal format.
            (channels == 4 ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24)));
    if(!atlas_texture)
    {
        serrf("%s: Failed to create surface: %s\n", __FUNCTION__, SDL_GetError());
        return 0;
    }

    unsigned char* cursor = static_cast<unsigned char*>(stb_data.get());

    
    int frames_remaining = *frames;

    for(int x = 0; x < atlas_columns; ++x)
    {
        //I can't think right now, I know that there is probably a prettier way of doing the remainder.
        int frames_in_column = SDL_min((*column_size), frames_remaining);
        frames_remaining -= *column_size;

        std::unique_ptr<SDL_Surface, SDL_Surface_Deleter> temp_surf(SDL_CreateRGBSurfaceWithFormatFrom(
                cursor,
                *w, frames_in_column * (*h), 
                (channels == 4 ? 32 : 24),
                (*w) * channels,
                //TODO: on modern opengl you can query the ideal format.
                (channels == 4 ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24)));
        if(!temp_surf)
        {
            serrf("%s: Failed to create surface: %s\n", __FUNCTION__, SDL_GetError());
            return 0;
        }

        

        SDL_Rect dst_rect{x * (*w), 0, temp_surf->w, temp_surf->h};

        if(SDL_BlitSurface(temp_surf.get(), NULL, atlas_texture.get(), &dst_rect) < 0)
        {
            serrf("%s: Failed to call SDL_BlitSurface: %s\n", __FUNCTION__, SDL_GetError());
            return 0;
        }

        cursor += temp_surf->w * temp_surf->h * channels;
    }

    //SDL_SaveBMP(atlas_texture.get(), "out.bmp");
#ifdef GIF_TIMER
    t2 = timer_now();
    slogf("blitting time: %f\n", timer_delta<TIMER_MS>(t1,t2));
    t1 = timer_now();
#endif
    

    GLint internal_format = 0;
    //internal_format = params.gamma_correction ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    GLint format = 0;

    if(atlas_texture->format->BytesPerPixel == 4) {
        internal_format = GL_RGBA;
        format = GL_RGBA;
    } else if(atlas_texture->format->BytesPerPixel == 3) {
        internal_format = GL_RGB;
        format = GL_RGB;
    } else {
        serrf("%s: unexpected bytes per pixel (%d): %s\n", __FUNCTION__, atlas_texture->format->BytesPerPixel, file->stream_info);
        return 0;
    }

    GLuint tex_id = 0;
    GL_CHECK_ERR_MSG( ctx.glGenTextures( 1, &tex_id ), return 0, file->stream_info );
    //tricky unwinding.
    do{
        GL_CHECK_ERR_MSG( ctx.glBindTexture( GL_TEXTURE_2D, tex_id ), break, file->stream_info );
        GL_CHECK_ERR_MSG( ctx.glTexImage2D( GL_TEXTURE_2D, 0, internal_format, atlas_texture->w, atlas_texture->h, 0, format, GL_UNSIGNED_BYTE, atlas_texture->pixels ), break, file->stream_info );
        //set parameters
        GL_CHECK_ERR_MSG( ctx.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering ), break, file->stream_info );
        GL_CHECK_ERR_MSG( ctx.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering ), break, file->stream_info );
        GL_CHECK_ERR_MSG( ctx.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE ), break, file->stream_info );
        GL_CHECK_ERR_MSG( ctx.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE ), break, file->stream_info );
        GL_SANITY( ctx.glBindTexture( GL_TEXTURE_2D, 0 ) );

        if(ctx.glGetError() != GL_NO_ERROR)
        {
            serrf("%s GL error in `%s`\n", __FUNCTION__, file->stream_info);
            break;
        }
        
#ifdef GIF_TIMER
        t2 = timer_now();
        slogf("gl upload time: %f\n", timer_delta<TIMER_MS>(t1,t2));
#endif
        //success
        return tex_id;
    } while (false);

    //if an error occured.
    GL_CHECK_ERR_MSG( ctx.glDeleteTextures( 1, &tex_id ), return 0, file->stream_info );

    return 0;
}

static GLuint compile_shader(GLchar* shader_script, GLenum type, const char* file_info)
{
    ASSERT(shader_script != NULL);
    ASSERT(file_info != NULL);

    GLuint shader_id;
    GL_CHECK_ERR_MSG( shader_id = ctx.glCreateShader(type), return 0, file_info );
    do {
        GL_CHECK_ERR_MSG( ctx.glShaderSource(shader_id, 1, &shader_script, NULL), break, file_info );
        GL_CHECK_ERR_MSG( ctx.glCompileShader(shader_id), break, file_info );

        GLint compile_status;
        GL_CHECK_ERR_MSG( ctx.glGetShaderiv(shader_id, GL_COMPILE_STATUS, &compile_status), break, file_info );
        if (compile_status == 0)
        {
            GLint log_length;
            GLint infoLogLength;

            GL_CHECK_ERR_MSG( ctx.glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &log_length), break, file_info );
            std::unique_ptr<char[]> message(new char[log_length]);
            GL_CHECK_ERR_MSG( ctx.glGetShaderInfoLog(shader_id, log_length, &infoLogLength, message.get()), break, file_info );
            if(infoLogLength > 0)
            {
                serrf("%s:\n%s\n", file_info, message.get());
            }
            else
            {
                serrf("%s: infoLogLength returned %d\n", file_info, infoLogLength);
            }
            break;
        }
        //success
        return shader_id;
    } while(false);

    GL_CHECK_ERR_MSG( ctx.glDeleteShader(shader_id), return 0, file_info );

    //error
    return 0;
}

static GLuint create_program(const char* program_info, GLchar* vertex_shader, const char* vertex_info, GLchar* fragment_shader, const char* fragment_info)
{
    GLuint program_id;
    GL_CHECK_ERR( program_id = ctx.glCreateProgram(), return 0 );
    
    GLuint vertex_id = 0;
    GLuint fragment_id = 0;
    do {
        vertex_id = compile_shader(vertex_shader, GL_VERTEX_SHADER, vertex_info);
        if(vertex_id == 0)
        {
            break;
        }

        fragment_id = compile_shader(fragment_shader, GL_FRAGMENT_SHADER, fragment_info);
        if(fragment_id == 0){
            break;
        }

        GL_CHECK_ERR_MSG( ctx.glAttachShader(program_id, vertex_id), break, vertex_info );
	    GL_CHECK_ERR_MSG( ctx.glAttachShader(program_id, fragment_id), break, fragment_info );

        GL_CHECK_ERR( ctx.glLinkProgram(program_id), break );
        GL_CHECK_ERR_MSG( ctx.glDeleteShader(vertex_id), break, vertex_info );
        vertex_id = 0;
        GL_CHECK_ERR_MSG( ctx.glDeleteShader(fragment_id), break, fragment_info );
        fragment_id = 0;

        GLint link_status;
        GL_CHECK_ERR( ctx.glGetProgramiv(program_id, GL_LINK_STATUS, &link_status), break );
        if (link_status == 0) {
            GLint log_length;
            GLint infoLogLength;
            GL_CHECK_ERR( ctx.glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &log_length), break );
            std::unique_ptr<char[]> message(new char[log_length]);
            GL_CHECK_ERR( ctx.glGetProgramInfoLog(program_id, log_length, &infoLogLength, message.get()), break );
            if(infoLogLength > 0)
            {
                serrf("%s:\n%s\n", program_info, message.get());
            }
            else
            {
                serrf("%s: infoLogLength returned %d\n", program_info, infoLogLength);
            }
            break;
        }
        return program_id;
    } while (false);
    if(vertex_id != 0) GL_CHECK_MSG( ctx.glDeleteShader(vertex_id), vertex_info );
    if(fragment_id != 0) GL_CHECK_MSG( ctx.glDeleteShader(fragment_id), fragment_info );
    GL_CHECK( ctx.glDeleteProgram(program_id) );
    return 0;
}

GLuint load_basic_shader_program(basic_shader_properties& data)
{
    static GLchar basic_vertex_shader_str[] =  
R"(attribute vec4 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
void main()
{
	gl_Position = a_position;
	v_texCoord = a_texCoord;
})";
    static GLchar basic_fragment_shader_str[] =  
    #ifndef DESKTOP_GL
    "precision mediump float;"
    #endif
R"(varying vec2 v_texCoord;
uniform sampler2D s_texture;
void main()
{
	gl_FragColor = texture2D( s_texture, v_texCoord );
})";
    
    GLuint program_id = create_program(__FUNCTION__, basic_vertex_shader_str, "basic_vertex_shader", basic_fragment_shader_str, "basic_fragment_shader");
    if(program_id == 0)
    {
        return 0;
    }
    
    const char* temp_string = "s_texture";
    data.s_texture = ctx.glGetUniformLocation ( program_id, temp_string );
    if(data.s_texture < 0)
    {
        slogf("%s warning: failed to set %s\n", __FUNCTION__, temp_string);
    }

    temp_string = "a_position";
    data.a_position = ctx.glGetAttribLocation ( program_id, temp_string );
    if(data.a_position < 0)
    {
        slogf("%s warning: failed to set %s\n", __FUNCTION__, temp_string);
    }
    
    temp_string = "a_texCoord";
    data.a_texCoord = ctx.glGetAttribLocation ( program_id, temp_string );
    if(data.a_texCoord < 0)
    {
        slogf("%s warning: failed to set %s\n", __FUNCTION__, temp_string);
    }

    //this shouldn't be activated from a missing attribute or uniform, this is just here for sanity.
    if(ctx.glGetError() != GL_NO_ERROR)
    {
        serrf("%s GL error\n", __FUNCTION__);
        GL_CHECK( ctx.glDeleteProgram(program_id) );
        return 0;
    }

    //success
    return program_id;
}

GLuint load_colorful_shader_program(colorful_shader_properties& data)
{
    static GLchar colorful_vertex_shader_str[] =  R"(attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_multColor;
attribute vec4 a_layerColor;
varying vec2 v_texCoord;
varying vec4 v_multColor;
varying vec4 v_layerColor;
void main()
{
	gl_Position = a_position;
	v_texCoord = a_texCoord;
	v_multColor = a_multColor;
    v_layerColor = a_layerColor;
})";

    //one note, if you plan on using premultiplied alpha, you must modify the layerColor code
    //to something like, and don't premultiply layerColor: mix(texel, vec4(layerColor.rgb * texel.a, texel.a), layerColor.a),
    //and make sure layerColor isn't premultiplied, and make sure multColor IS premultiplied.
    static GLchar colorful_fragment_shader_str[] =  
    #ifndef DESKTOP_GL
    "precision mediump float;"
    #endif
    R"(uniform sampler2D s_texture;
varying vec2 v_texCoord;
varying vec4 v_multColor;
varying vec4 v_layerColor;
void main()
{
	vec4 texel = texture2D( s_texture, v_texCoord );
	texel = texel * v_multColor;
    texel = mix(texel, vec4(v_layerColor.rgb, texel.a), v_layerColor.a);
	gl_FragColor = texel;
})";

    GLuint program_id = create_program(__FUNCTION__, colorful_vertex_shader_str, "colorful_vertex_shader", colorful_fragment_shader_str, "colorful_fragment_shader");
    if(program_id == 0)
    {
        return 0;
    }
    
    const char* temp_string = "s_texture";
    data.s_texture = ctx.glGetUniformLocation ( program_id, temp_string );
    if(data.s_texture < 0)
    {
        slogf("%s warning: failed to set %s\n", __FUNCTION__, temp_string);
    }

    temp_string = "a_position";
    data.a_position = ctx.glGetAttribLocation ( program_id, temp_string );
    if(data.a_position < 0)
    {
        slogf("%s warning: failed to set %s\n", __FUNCTION__, temp_string);
    }
    
    temp_string = "a_texCoord";
    data.a_texCoord = ctx.glGetAttribLocation ( program_id, temp_string );
    if(data.a_texCoord < 0)
    {
        slogf("%s warning: failed to set %s\n", __FUNCTION__, temp_string);
    }

    temp_string = "a_multColor";
    data.a_multColor = ctx.glGetAttribLocation ( program_id, temp_string );
    if(data.a_multColor < 0)
    {
        slogf("%s warning: failed to set %s\n", __FUNCTION__, temp_string);
    }

    temp_string = "a_layerColor";
    data.a_layerColor = ctx.glGetAttribLocation ( program_id, temp_string );
    if(data.a_layerColor < 0)
    {
        slogf("%s warning: failed to set %s\n", __FUNCTION__, temp_string);
    }

    if(ctx.glGetError() != GL_NO_ERROR)
    {
        serrf("%s GL error\n", __FUNCTION__);
        GL_CHECK( ctx.glDeleteProgram(program_id) );
        return 0;
    }

    //success
    return program_id;

    
    
    return 0;
}


GLuint load_palette_shader_program(palette_shader_properties& data)
{
    static GLchar vertex_shader_str[] =  R"(attribute vec4 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
void main()
{
	gl_Position = a_position;
	v_texCoord = a_texCoord;
})";

    static GLchar fragment_shader_str[] =  
    #ifndef DESKTOP_GL
    "precision mediump float;"
    #endif
    R"(
uniform sampler2D s_texture;
uniform vec4 u_palette[256];
//a float, 0.0 = overwrite (with alpha mix), 1.0 = multiply, 2.0 = layer
uniform float u_palette_mode;
varying vec2 v_texCoord;
void main()
{
    vec4 texel = texture2D( s_texture, v_texCoord );
    if(texel.a == 1.0)
    {
        gl_FragColor = texel;
    }
    else
    {
        //the only annoying part is that all transparent pixels have to calculate this, 
        //but on the bright side index 0 will color the transparent pixels (do you want that?).
        vec4 palette_color = u_palette[int(texel.a * 255.0)];
        
        //0 = over
        vec4 overwrite_value = palette_color;
        //1 = mod
        vec4 modulate_value = texel * palette_color;
        //2 = layer
        vec4 layer_value = mix(texel, vec4(palette_color.rgb, texel.a), palette_color.a);
        
        //weight the modes.
        gl_FragColor = mix(overwrite_value, mix(modulate_value, layer_value, u_palette_mode-1.0), u_palette_mode);
	}
})";
}
