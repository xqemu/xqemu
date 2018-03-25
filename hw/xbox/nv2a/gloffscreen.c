/*
 *  Offscreen OpenGL abstraction layer - Common utilities
 *
 *  Copyright (c) 2010 Intel
 *  Written by:
 *    Gordon Williams <gordon.williams@collabora.co.uk>
 *    Ian Molton <ian.molton@collabora.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "gloffscreen.h"


void glo_readpixels(GLenum gl_format, GLenum gl_type,
                    unsigned int bytes_per_pixel, unsigned int stride,
                    unsigned int width, unsigned int height, void *data)
{
    /* TODO: weird strides */
    assert(stride % bytes_per_pixel == 0);

    /* Save guest processes GL state before we ReadPixels() */
    int rl, pa;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rl);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pa);
    glPixelStorei(GL_PACK_ROW_LENGTH, stride / bytes_per_pixel);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

#ifdef GETCONTENTS_INDIVIDUAL
    GLubyte *b = (GLubyte *) data;
    int irow;

    for (irow = height - 1; irow >= 0; irow--) {
        glReadPixels(0, irow, width, 1, gl_format, gl_type, b);
        b += stride;
    }
#else
    /* Faster buffer flip */
    GLubyte *b = (GLubyte *) data;
    GLubyte *c = &((GLubyte *) data)[stride * (height - 1)];
    GLubyte *tmp = (GLubyte *) malloc(width * bytes_per_pixel);
    int irow;

    glReadPixels(0, 0, width, height, gl_format, gl_type, data);

    for (irow = 0; irow < height / 2; irow++) {
        memcpy(tmp, b, width * bytes_per_pixel);
        memcpy(b, c, width * bytes_per_pixel);
        memcpy(c, tmp, width * bytes_per_pixel);
        b += stride;
        c -= stride;
    }
    free(tmp);
#endif

    /* Restore GL state */
    glPixelStorei(GL_PACK_ROW_LENGTH, rl);
    glPixelStorei(GL_PACK_ALIGNMENT, pa);
}


bool glo_check_extension(const char* ext_name)
{
    int i;
    int num_extensions = GL_NUM_EXTENSIONS;
    for (i=0; i<num_extensions; i++) {
      const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
      if (!ext) break;
      if (strcmp(ext, ext_name) == 0) return true;
    }
    return false;
}










/*
 *  Offscreen OpenGL abstraction layer - CGL (Apple) specific
 *
 *  Copyright (c) 2013 Wayo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>


#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLTypes.h>
#include <OpenGL/CGLCurrent.h>

#include "gloffscreen.h"

struct _GloContext {
  CGLContextObj     cglContext;
};

/* Create an OpenGL context for a certain pixel format. formatflags are from 
 * the GLO_ constants */
GloContext *glo_context_create(void)
{
    CGLError err;

    GloContext *context = (GloContext *)malloc(sizeof(GloContext));

    /* pixel format attributes */
    CGLPixelFormatAttribute attributes[] = {
        kCGLPFAAccelerated,
        kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL3_Core,
        (CGLPixelFormatAttribute)0
    };

    CGLPixelFormatObj pix;
    GLint num;
    err = CGLChoosePixelFormat(attributes, &pix, &num);
    if (err) return NULL;

    err = CGLCreateContext(pix, NULL, &context->cglContext);
    if (err) return NULL;

    CGLDestroyPixelFormat(pix);

    glo_set_current(context);

    return context;
}

void* glo_get_extension_proc(const char* ext_proc)
{
    return dlsym(RTLD_NEXT, ext_proc);
}

/* Set current context */
void glo_set_current(GloContext *context)
{
    if (context == NULL) {
        CGLSetCurrentContext(NULL);
    } else {
        CGLSetCurrentContext(context->cglContext);
    }
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context)
{
    if (!context) return;
    glo_set_current(NULL);
    CGLDestroyContext(context->cglContext);
}
