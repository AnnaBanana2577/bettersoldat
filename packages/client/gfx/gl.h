#pragma once

// The slice of OpenGL 2.1 the client draws with, loaded at run time from the driver
// through SDL_GL_GetProcAddress (the original's dglOpenGL does the same). Nothing here
// comes from the platform's gl.h: the types, the tokens and the function pointers are
// declared below, so the build links no GL library and every platform sees the same
// names. Each function is a pointer with its usual name, so the code reads as plain GL.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(APIENTRY)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_DITHER 0x0BD0
#define GL_MULTISAMPLE 0x809D
#define GL_ONE 1
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_ALPHA 0x1906
#define GL_RGBA 0x1908
#define GL_TEXTURE_LOD_BIAS 0x8501
#define GL_RGBA8 0x8058
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_VERSION 0x1F02
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

// Every function the client calls, as (return type, name, argument list). gl.c turns
// the list into the pointer definitions and the loader; here into the declarations.
#define GL_FUNCTIONS(X)                                                                                              \
    X(void, glEnable, (GLenum cap))                                                                                  \
    X(void, glDisable, (GLenum cap))                                                                                 \
    X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor))                                                           \
    X(void, glPixelStorei, (GLenum pname, GLint param))                                                              \
    X(void, glViewport, (GLint x, GLint y, GLsizei width, GLsizei height))                                           \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                              \
    X(void, glClear, (GLbitfield mask))                                                                              \
    X(void, glGetIntegerv, (GLenum pname, GLint *params))                                                            \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)) \
    X(const GLubyte *, glGetString, (GLenum name))                                                                   \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))                                                 \
    X(void, glGenTextures, (GLsizei n, GLuint *textures))                                                            \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *textures))                                                   \
    X(void, glBindTexture, (GLenum target, GLuint texture))                                                          \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                                             \
    X(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param))                                           \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,          \
                           GLint border, GLenum format, GLenum type, const void *pixels))                            \
    X(void, glTexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,              \
                              GLsizei height, GLenum format, GLenum type, const void *pixels))                       \
    X(void, glGenerateMipmap, (GLenum target))                                                                       \
    X(void, glActiveTexture, (GLenum texture))                                                                       \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *framebuffers))                                                    \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers))                                           \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer))                                                  \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target))                                                             \
    X(void, glGenBuffers, (GLsizei n, GLuint *buffers))                                                              \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *buffers))                                                     \
    X(void, glBindBuffer, (GLenum target, GLuint buffer))                                                            \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage))                          \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data))                    \
    X(GLuint, glCreateShader, (GLenum type))                                                                         \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length))        \
    X(void, glCompileShader, (GLuint shader))                                                                        \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint *params))                                             \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog))                  \
    X(void, glDeleteShader, (GLuint shader))                                                                         \
    X(GLuint, glCreateProgram, (void))                                                                               \
    X(void, glAttachShader, (GLuint program, GLuint shader))                                                         \
    X(void, glDetachShader, (GLuint program, GLuint shader))                                                         \
    X(void, glLinkProgram, (GLuint program))                                                                         \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint *params))                                           \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog))                \
    X(void, glUseProgram, (GLuint program))                                                                          \
    X(void, glDeleteProgram, (GLuint program))                                                                       \
    X(void, glBindAttribLocation, (GLuint program, GLuint index, const GLchar *name))                                \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar *name))                                             \
    X(void, glUniform1i, (GLint location, GLint v0))                                                                 \
    X(void, glUniformMatrix3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))          \
    X(void, glEnableVertexAttribArray, (GLuint index))                                                               \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,     \
                                    const void *pointer))

#define GL_DECLARE(ret, name, args) extern ret(GLAPIENTRY *name) args;
GL_FUNCTIONS(GL_DECLARE)
#undef GL_DECLARE

// Loads every function above from the current GL context. False, with the first
// missing name on stderr, when the driver lacks one: OpenGL 2.1 is the floor.
bool gl_load(void);
