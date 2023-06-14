/**
 * Copyright (C) 2023-present Masatoshi Fukunaga
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "lpthread.h"

#define PTHREAD_SELF_MT "pthread.self"

typedef struct {
    int lua_status;
    lua_State *L;
    lpthread_t *parent;
} lpthread_self_t;

static void on_cleanup(void *arg)
{
    lpthread_self_t *th = (lpthread_self_t *)arg;

    switch (th->lua_status) {
    default: {
        size_t len      = 0;
        const char *str = lua_tolstring(th->L, -1, &len);
        if (len > sizeof(th->parent->errmsg) - 1) {
            len = sizeof(th->parent->errmsg) - 1;
        }
        memcpy(th->parent->errmsg, str, len);
        th->parent->errmsg[len] = 0;
        th->parent->status      = THREAD_FAILED;
    } break;

    case -1:
        // STILL_RUNNING
        th->parent->status = THREAD_CANCELLED;
        break;

    case 0:
        // LUA_OK
        th->parent->status = THREAD_TERMINATED;
        break;
    }
    write(th->parent->pipefd[1], "0", 1);
    lua_close(th->L);
}

static void *on_start(void *arg)
{
    lpthread_self_t *th = (lpthread_self_t *)arg;

    pthread_cleanup_push(on_cleanup, arg);
    // run state in thread
    th->lua_status = -1;
    th->lua_status = lua_pcall(th->L, 1, 0, 1);
    pthread_cleanup_pop(1);

    return NULL;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, PTHREAD_SELF_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int new_pthread_self(lua_State *L)
{
    // register pthead.self metatable
    struct luaL_Reg mmethods[] = {
        {"__tostring", tostring_lua},
        {NULL,         NULL        }
    };
    struct luaL_Reg methods[] = {
        {NULL, NULL}
    };
    register_mt(L, PTHREAD_SELF_MT, mmethods, methods);

    // create pthread.self
    lua_newuserdata(L, sizeof(lpthread_self_t));
    luaL_getmetatable(L, PTHREAD_SELF_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static int traceback(lua_State *L)
{
#if LUA_VERSION_NUM >= 502
    // push thread stack trace to dst state
    luaL_traceback(L, L, lua_tostring(L, -1), 1);
    return 1;

#else
    // get debug.traceback function
    lua_getglobal(L, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }

    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return 1;
    }

    // call debug.traceback function
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;

#endif
}

int lpthread_self_start(lpthread_t *th, const char *filename)
{
    // create thread state
    lua_State *L = luaL_newstate();
    if (!L) {
        return errno;
    }
    // open standard libraries
    luaL_openlibs(L);
    // add traceback function
    lua_pushcfunction(L, traceback);

    // load script file that runs on thread
    int rc = luaL_loadfile(L, filename);
    if (rc != 0) {
        size_t len         = 0;
        const char *errmsg = lua_tolstring(L, -1, &len);
        if (len > sizeof(th->errmsg) - 1) {
            len = sizeof(th->errmsg) - 1;
        }
        memcpy(th->errmsg, errmsg, len);
        if (errno == 0) {
            if (rc == LUA_ERRMEM) {
                errno = ENOMEM;
            } else {
                errno = EINVAL;
            }
        }
        lua_close(L);
        return errno;
    }

    // create thread data
    lua_pushcfunction(L, new_pthread_self);
    rc = lua_pcall(L, 0, 1, 1);
    if (rc != 0) {
        size_t len         = 0;
        const char *errmsg = lua_tolstring(L, -1, &len);
        if (len > sizeof(th->errmsg) - 1) {
            len = sizeof(th->errmsg) - 1;
        }
        memcpy(th->errmsg, errmsg, len);
        if (rc == LUA_ERRMEM) {
            errno = ENOMEM;
        } else {
            // LUA_ERRSYNTAX
            // LUA_ERRRUN
            // LUA_ERRERR
            errno = ECANCELED;
        }
        lua_close(L);
        return errno;
    }
    lpthread_self_t *self = luaL_checkudata(L, -1, PTHREAD_SELF_MT);
    self->lua_status      = -1;
    self->L               = L;
    self->parent          = th;
    errno = pthread_create(&th->id, NULL, on_start, (void *)self);
    if (errno != 0) {
        lua_close(L);
    }
    return errno;
}
