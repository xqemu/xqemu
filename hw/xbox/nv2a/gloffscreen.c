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


#ifdef __WINNT__
/*
 *  Offscreen OpenGL abstraction layer - WGL (windows) specific
 *
 *  Copyright (c) 2010 Intel
 *  Written by: 
 *    Gordon Williams <gordon.williams@collabora.co.uk>
 *    Ian Molton <ian.molton@collabora.co.uk>
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

#include <windows.h>
#include <wingdi.h>

#include <GL/glew.h>
#include <GL/wglew.h>
#include <GL/gl.h>
#include "wglext.h"

#include "gloffscreen.h"

/* In Windows, you must create a window *before* you can create a pbuffer or
 * get a context. So we create a hidden Window on startup(see glo_init/GloMain).
 *
 * Also, you can't share contexts that have different pixel formats, so we can't
 * just create a new context from the window. We must create a whole new PBuffer
 * just for a context :(
 */

struct GloMain {
    HINSTANCE             hInstance;
    HDC                   hDC;
    HWND                  hWnd; /* Our hidden window */
    HGLRC                 hContext;
};

struct GloMain glo;
int glo_inited = 0;

struct _GloContext {
    /* Pixel format returned by wglChoosePixelFormat */
    int                   wglPixelFormat;
    /* We need a pbuffer to make a context of the right pixelformat :( */
    HPBUFFERARB           hPBuffer;
    HDC                   hDC;
    HGLRC                 hContext;
};

#define GLO_WINDOW_CLASS "QEmuGLClass"

/* Initialise gloffscreen */
static void glo_init(void) {
    WNDCLASSEX wcx;
    PIXELFORMATDESCRIPTOR pfd;

    if (glo_inited) {
        fprintf(stderr, "gloffscreen already inited\n");
        abort();
    }

    /* Grab An Instance For Our Window */
    glo.hInstance = GetModuleHandle(NULL);
    wcx.cbSize = sizeof(wcx);
    wcx.style = 0;
    wcx.lpfnWndProc = DefWindowProc;
    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = 0;
    wcx.hInstance = glo.hInstance;
    wcx.hIcon = NULL;
    wcx.hCursor = NULL;
    wcx.hbrBackground = NULL;
    wcx.lpszMenuName =  NULL;
    wcx.lpszClassName = GLO_WINDOW_CLASS;
    wcx.hIconSm = NULL;
    RegisterClassEx(&wcx);
    glo.hWnd = CreateWindow(
        GLO_WINDOW_CLASS,
        "QEmuGL",
        0,0,0,0,0,
        (HWND)NULL, (HMENU)NULL,
        glo.hInstance,
        (LPVOID) NULL);

    if (!glo.hWnd) {
        fprintf(stderr, "Unable to create window\n");
        abort();
    }

    glo.hDC = GetDC(glo.hWnd);

    /* Create a pixel format */
    memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    unsigned int pixelFormat = ChoosePixelFormat(glo.hDC, &pfd);
    DescribePixelFormat(glo.hDC,
                    pixelFormat, sizeof(PIXELFORMATDESCRIPTOR), &pfd);
    if (!SetPixelFormat(glo.hDC, pixelFormat, &pfd))
        return;

    /* Create a tempoary OpenGL 2 context */
    glo.hContext = wglCreateContext(glo.hDC);
    if (glo.hContext == NULL) {
        fprintf(stderr, "Unable to create GL context\n");
        abort();
    }
    wglMakeCurrent(glo.hDC, glo.hContext);

    /* Initialize glew */
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Glew init failed.\n");
        abort();
    }
    
    if (!WGLEW_ARB_create_context
        || !WGLEW_ARB_pixel_format
        || !WGLEW_ARB_pbuffer) {
        fprintf(stderr, "Unable to load the required WGL extensions\n");
        abort();
    }

    glo_inited = 1;
}

/* Uninitialise gloffscreen */
static void glo_kill(void) {
    if (glo.hContext) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(glo.hContext);
        glo.hContext = NULL;
    }
    if (glo.hDC) {
        ReleaseDC(glo.hWnd, glo.hDC);
        glo.hDC = NULL;
    }
    if (glo.hWnd) {
        DestroyWindow(glo.hWnd);
        glo.hWnd = NULL;
    }
    UnregisterClass(GLO_WINDOW_CLASS, glo.hInstance);
}

GloContext *glo_context_create(void) {
    if (!glo_inited)
      glo_init();

    GloContext *context = (GloContext *)malloc(sizeof(GloContext));
    memset(context, 0, sizeof(GloContext));

    /* Create the context proper */
    const int ctx_attri[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    context->hDC = glo.hDC;
    context->hContext = wglCreateContextAttribsARB(context->hDC, 0, ctx_attri);
    if (context->hContext == NULL) {
        printf("Unable to create GL context\n");
        exit(EXIT_FAILURE);
    }

    glo_set_current(context);
    return context;
}

/* Set current context */
void glo_set_current(GloContext *context) {

    if (context == NULL) {
        wglMakeCurrent(NULL, NULL);
    } else {
        wglMakeCurrent(context->hDC, context->hContext);
    }
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context)
{
    if (!context) return;

    wglMakeCurrent(NULL, NULL);
    if (context->hPBuffer != NULL) {
        wglReleasePbufferDCARB(context->hPBuffer, context->hDC);
        wglDestroyPbufferARB(context->hPBuffer);
    }
    if (context->hDC != NULL) {
        ReleaseDC(glo.hWnd, context->hDC);
    }
    if (context->hContext) {
        wglDeleteContext(context->hContext);
    }
    free(context);
}


void glo_swap(GloContext *context)
{
    if (!context) { return; }

    SwapBuffers(context->hDC);

}
#else


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

#endif