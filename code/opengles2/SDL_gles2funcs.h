/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

//you can lift this when I require gl 3
#define SDL_PROC_GL2_COMPAT(ret,func,params)

#ifdef DESKTOP_GL
//desktop GL will always choose non extensions, since angle es2.0 is better for extensions, due to dx9 -> es2 support
#define SDL_PROC_OES(ret,func,params) SDL_PROC(ret,func,params)
#define SDL_PROC_ANGLE(ret,func,params) SDL_PROC_EXTENSION(ret,func,func,params)
#define SDL_PROC_KHR(ret,func,params) SDL_PROC_EXTENSION(ret,func,func,params)
#define SDL_PROC_EXT(ret,func,params) SDL_PROC_EXTENSION(ret,func,func,params)

#define ROBUSTNESS_EXTENSION(x) x##_ARB

#else

#define SDL_PROC_OES(ret,func,params) SDL_PROC_EXTENSION(ret,func,OES,params)
#define SDL_PROC_ANGLE(ret,func,params) SDL_PROC_EXTENSION(ret,func,ANGLE,params)
#define SDL_PROC_KHR(ret,func,params) SDL_PROC_EXTENSION(ret,func,KHR,params)
#define SDL_PROC_EXT(ret,func,params) SDL_PROC_EXTENSION(ret,func,EXT,params)

#define ROBUSTNESS_EXTENSION(x) x##_EXT

#ifndef GL_DEBUG_TYPE_ERROR
#define GL_DEBUG_TYPE_ERROR GL_DEBUG_TYPE_ERROR_KHR
#endif
#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT GL_DEBUG_OUTPUT_KHR
#endif


typedef void (APIENTRY *GLDEBUGPROC)(GLenum source,
   GLenum type,
   GLuint id,
   GLenum severity,
   GLsizei length,
   const GLchar* message,
   const void* userParam);
#endif



#ifndef GL_GUILTY_CONTEXT_RESET
#define GL_GUILTY_CONTEXT_RESET ROBUSTNESS_EXTENSION(GL_GUILTY_CONTEXT_RESET)
#endif

#ifndef GL_INNOCENT_CONTEXT_RESET
#define GL_INNOCENT_CONTEXT_RESET ROBUSTNESS_EXTENSION(GL_INNOCENT_CONTEXT_RESET)
#endif

#ifndef GL_UNKNOWN_CONTEXT_RESET
#define GL_UNKNOWN_CONTEXT_RESET ROBUSTNESS_EXTENSION(GL_UNKNOWN_CONTEXT_RESET)
#endif

#ifndef GL_RESET_NOTIFICATION_STRATEGY
#define GL_RESET_NOTIFICATION_STRATEGY ROBUSTNESS_EXTENSION(GL_RESET_NOTIFICATION_STRATEGY)
#endif

#ifndef GL_LOSE_CONTEXT_ON_RESET
#define GL_LOSE_CONTEXT_ON_RESET ROBUSTNESS_EXTENSION(GL_LOSE_CONTEXT_ON_RESET)
#endif

#ifndef GL_NO_RESET_NOTIFICATION
#define GL_NO_RESET_NOTIFICATION ROBUSTNESS_EXTENSION(GL_NO_RESET_NOTIFICATION)
#endif


//dootsie: I had to add this in
SDL_PROC(void, glBlendFunc, (GLenum, GLenum))
SDL_PROC(void, glCullFace, (GLenum))

//GL_EXT_robustness
SDL_PROC_EXT(GLenum, glGetGraphicsResetStatus, (void))

//OES_vertex_array_object 
SDL_PROC_OES(void, glGenVertexArrays, (GLsizei, GLuint *))
SDL_PROC_OES(void, glBindVertexArray, (GLuint))
SDL_PROC_OES(void, glDeleteVertexArrays, (GLsizei, GLuint *))

//GL_KHR_debug (TODO: GL_ARB_debug is more supported on native opengl)
SDL_PROC_KHR(void, glDebugMessageControl, (GLenum source,
   GLenum type,
   GLenum severity,
   GLsizei count,
   const GLuint* ids,
   GLboolean enabled))
SDL_PROC_KHR(void, glDebugMessageCallback, (GLDEBUGPROC, const void*))

//ANGLE_instanced_arrays
SDL_PROC_ANGLE(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei,GLsizei))
SDL_PROC_ANGLE(void, glDrawElementsInstanced, (GLenum, GLsizei, GLenum, const void *, GLsizei))
SDL_PROC_ANGLE(void, glVertexAttribDivisor, (GLuint, GLuint))

SDL_PROC(void, glActiveTexture, (GLenum))
SDL_PROC(void, glAttachShader, (GLuint, GLuint))
SDL_PROC(void, glBindAttribLocation, (GLuint, GLuint, const char *))
SDL_PROC(void, glBindTexture, (GLenum, GLuint))
SDL_PROC(void, glBlendEquationSeparate, (GLenum, GLenum))
SDL_PROC(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))

SDL_PROC(void, glClear, (GLbitfield))
SDL_PROC(void, glClearColor, (GLclampf, GLclampf, GLclampf, GLclampf))
SDL_PROC(void, glCompileShader, (GLuint))
SDL_PROC(GLuint, glCreateProgram, (void))
SDL_PROC(GLuint, glCreateShader, (GLenum))
SDL_PROC(void, glDeleteProgram, (GLuint))
SDL_PROC(void, glDeleteShader, (GLuint))
SDL_PROC(void, glDeleteTextures, (GLsizei, const GLuint *))
SDL_PROC(void, glDisable, (GLenum))
SDL_PROC(void, glDisableVertexAttribArray, (GLuint))
SDL_PROC(void, glDrawArrays, (GLenum, GLint, GLsizei))
SDL_PROC(void, glEnable, (GLenum))
SDL_PROC(void, glEnableVertexAttribArray, (GLuint))
SDL_PROC(void, glFinish, (void))
SDL_PROC_GL2_COMPAT(void, glGenFramebuffers, (GLsizei, GLuint *))
SDL_PROC(void, glGenTextures, (GLsizei, GLuint *))
SDL_PROC(void, glGetBooleanv, (GLenum, GLboolean *))
SDL_PROC(const GLubyte *, glGetString, (GLenum))
SDL_PROC(GLenum, glGetError, (void))
SDL_PROC(void, glGetIntegerv, (GLenum, GLint *))
SDL_PROC(void, glGetProgramiv, (GLuint, GLenum, GLint *))
SDL_PROC(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *))
SDL_PROC(void, glGetShaderiv, (GLuint, GLenum, GLint *))
SDL_PROC(GLint, glGetUniformLocation, (GLuint, const char *))
SDL_PROC(void, glLinkProgram, (GLuint))
SDL_PROC(void, glPixelStorei, (GLenum, GLint))
SDL_PROC(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*))
SDL_PROC(void, glScissor, (GLint, GLint, GLsizei, GLsizei))
SDL_PROC_GL2_COMPAT(void, glShaderBinary, (GLsizei, const GLuint *, GLenum, const void *, GLsizei))
SDL_PROC(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint *))
SDL_PROC(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))
SDL_PROC(void, glTexParameteri, (GLenum, GLenum, GLint))
SDL_PROC(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *))
SDL_PROC(void, glUniform1i, (GLint, GLint))
SDL_PROC(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
SDL_PROC(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))
SDL_PROC(void, glUseProgram, (GLuint))
SDL_PROC(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))
SDL_PROC(void, glViewport, (GLint, GLint, GLsizei, GLsizei))
SDL_PROC_GL2_COMPAT(void, glBindFramebuffer, (GLenum, GLuint))
SDL_PROC_GL2_COMPAT(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
SDL_PROC_GL2_COMPAT(GLenum, glCheckFramebufferStatus, (GLenum))
SDL_PROC_GL2_COMPAT(void, glDeleteFramebuffers, (GLsizei, const GLuint *))
SDL_PROC(GLint, glGetAttribLocation, (GLuint, const GLchar *))
SDL_PROC(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
SDL_PROC(void, glGenBuffers, (GLsizei, GLuint *))
SDL_PROC(void, glDeleteBuffers, (GLsizei, const GLuint *))
SDL_PROC(void, glBindBuffer, (GLenum, GLuint))
SDL_PROC(void, glBufferData, (GLenum, GLsizeiptr, const GLvoid *, GLenum))
SDL_PROC(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const GLvoid *))

#undef SDL_PROC_OES
#undef SDL_PROC_ANGLE
#undef SDL_PROC_KHR
#undef SDL_PROC_EXT

#undef SDL_PROC_GL2_COMPAT
