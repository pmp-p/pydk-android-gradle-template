//
// Copyright 2011 Tero Saarni
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <android/native_window.h> // requires ndk r5 or newer
#include <EGL/egl.h> // requires ndk r5 or newer
#include <GLES/gl.h>

#include "logger.h"
#include "mod_egl.h"

static GLint vertices[][3] = {
    { -0x10000, -0x10000, -0x10000 },
    {  0x10000, -0x10000, -0x10000 },
    {  0x10000,  0x10000, -0x10000 },
    { -0x10000,  0x10000, -0x10000 },
    { -0x10000, -0x10000,  0x10000 },
    {  0x10000, -0x10000,  0x10000 },
    {  0x10000,  0x10000,  0x10000 },
    { -0x10000,  0x10000,  0x10000 }
};

static GLint colors[][4] = {
    { 0x00000, 0x00000, 0x00000, 0x10000 },
    { 0x10000, 0x00000, 0x00000, 0x10000 },
    { 0x10000, 0x10000, 0x00000, 0x10000 },
    { 0x00000, 0x10000, 0x00000, 0x10000 },
    { 0x00000, 0x00000, 0x10000, 0x10000 },
    { 0x10000, 0x00000, 0x10000, 0x10000 },
    { 0x10000, 0x10000, 0x10000, 0x10000 },
    { 0x00000, 0x10000, 0x10000, 0x10000 }
};

GLubyte indices[] = {
    0, 4, 5,    0, 5, 1,
    1, 5, 6,    1, 6, 2,
    2, 6, 7,    2, 7, 3,
    3, 7, 4,    3, 4, 0,
    4, 7, 6,    4, 6, 5,
    3, 0, 1,    3, 1, 2
};


Renderer::Renderer()
    : _msg(MSG_NONE), display(0), surface(0), context(0), _angle(0)
{
    LOG_I("Renderer instance created");
    pthread_mutex_init(&_mutex, 0);
    return;
}

Renderer::~Renderer()
{
    LOG_I("Renderer instance destroyed");
    pthread_mutex_destroy(&_mutex);
    return;
}

void Renderer::start()
{
    LOG_I("Creating renderer thread");
    pthread_create(&_threadId, 0, threadStartCallback, this);
    return;
}

void Renderer::stop()
{
    LOG_I("Stopping renderer thread");

    // send message to render thread to stop rendering
    pthread_mutex_lock(&_mutex);
    _msg = MSG_RENDER_LOOP_EXIT;
    pthread_mutex_unlock(&_mutex);

    pthread_join(_threadId, 0);
    LOG_I("Renderer thread stopped");

    return;
}

void Renderer::setWindow(ANativeWindow *_window)
{
    // notify render thread that window has changed
    pthread_mutex_lock(&_mutex);
    _msg = MSG_WINDOW_SET;
    window = _window;
    pthread_mutex_unlock(&_mutex);

    return;
}

void
Renderer::step() {
    if (display) {
        drawFrame();
        if (!eglSwapBuffers(display, surface)) {
            LOG_E("eglSwapBuffers() returned error %d", eglGetError());
        }
    }
}

void
Renderer::renderLoop() {

    LOG_I("renderLoop()");

    while (renderingEnabled) {

        pthread_mutex_lock(&_mutex);

        // process incoming messages
        switch (_msg) {

            case MSG_WINDOW_SET:
                initialize();
                break;

            case MSG_RENDER_LOOP_EXIT:
                renderingEnabled = false;
                destroy();
                break;

            default:
                break;
        }
        _msg = MSG_NONE;
        step();
        pthread_mutex_unlock(&_mutex);
    }

    LOG_I("Render test loop exiting");

    return;
}


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>


bool
Renderer::initialize() {

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_NONE
    };
    //EGLDisplay display;
    EGLConfig config;
    EGLint numConfigs;
    EGLint format;
    //EGLSurface surface;
    //EGLContext context;
    EGLint width;
    EGLint height;
    GLfloat ratio;

    //LOG_I("Initializing context");

    if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
        LOG_E("eglGetDisplay() returned error %d", eglGetError());
        return false;
    }
    if (!eglInitialize(display, 0, 0)) {
        LOG_E("eglInitialize() returned error %d", eglGetError());
        return false;
    }

    if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)) {
        LOG_E("eglChooseConfig() returned error %d", eglGetError());
        destroy();
        return false;
    }

    if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
        LOG_E("eglGetConfigAttrib() returned error %d", eglGetError());
        destroy();
        return false;
    }

    ANativeWindow_setBuffersGeometry(window, 0, 0, format);

    if (!(surface = eglCreateWindowSurface(display, config, window, 0))) {
        LOG_E("eglCreateWindowSurface() returned error %d", eglGetError());
        destroy();
        return false;
    }

    if (!(context = eglCreateContext(display, config, 0, 0))) {
        LOG_E("eglCreateContext() returned error %d", eglGetError());
        destroy();
        return false;
    }



    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOG_E("eglMakeCurrent() returned error %d", eglGetError());
        destroy();
        return false;
    }

    if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
        LOG_E("eglQuerySurface() returned error %d", eglGetError());
        destroy();
        return false;
    }

    xmit(display, surface, context, 0);

    glDisable(GL_DITHER);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    glClearColor(0, 0, 0, 0);
    glEnable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);

    glViewport(0, 0, width, height);

    ratio = (GLfloat) width / height;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustumf(-ratio, ratio, -1, 1, 1, 10);

    LOG_I("context initialized");

    return true;
}

void Renderer::destroy() {
    LOG_I("Destroying context");

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    display = EGL_NO_DISPLAY;
    surface = EGL_NO_SURFACE;
    context = EGL_NO_CONTEXT;

    return;
}

void Renderer::drawFrame()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -3.0f);
    glRotatef(_angle, 0, 1, 0);
    glRotatef(_angle*0.25f, 1, 0, 0);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glFrontFace(GL_CW);
    glVertexPointer(3, GL_FIXED, 0, vertices);
    glColorPointer(4, GL_FIXED, 0, colors);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, indices);

    _angle += 1.2f;
}

void* Renderer::threadStartCallback(void *myself)
{
    Renderer *renderer = (Renderer*)myself;

    renderer->renderLoop();
    pthread_exit(0);

    return 0;
}
