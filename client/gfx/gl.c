#include "gfx/gl.h"

#include <SDL.h>
#include <stdio.h>

#define GL_DEFINE(ret, name, args) ret(GLAPIENTRY *name) args = NULL;
GL_FUNCTIONS(GL_DEFINE)
#undef GL_DEFINE

bool gl_load(void)
{
    bool ok = true;
#define GL_LOAD(ret, name, args)                                                                                     \
    {                                                                                                                \
        void *p = SDL_GL_GetProcAddress(#name);                                                                      \
        if (!p) {                                                                                                    \
            if (ok) fprintf(stderr, "OpenGL 2.1 is required: %s is missing\n", #name);                               \
            ok = false;                                                                                              \
        }                                                                                                            \
        *(void **)&name = p;                                                                                         \
    }
    GL_FUNCTIONS(GL_LOAD)
#undef GL_LOAD
    return ok;
}
