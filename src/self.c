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
    // stack:
    //  1: traceback function
    //  2: script function that passed to pthread.new
    //  3+: arguments
    th->lua_status = lua_pcall(th->L, lua_gettop(th->L) - 2, 0, 1);
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
    int nchan = lua_gettop(L);

    register_mt(L, PTHREAD_SELF_MT, mmethods, methods);

    // convert lightuserdata arguments to pthread.channel objects
    for (int i = 1; i <= nchan; i++) {
        luaL_checktype(L, i, LUA_TLIGHTUSERDATA);
        queue_t *q             = (queue_t *)lua_topointer(L, i);
        lpthread_channel_t *ch = lua_newuserdata(L, sizeof(lpthread_channel_t));
        if (queue_ref(q) != 0) {
            return luaL_error(L,
                              "failed to queue_ref() in new_pthread_self(): %s",
                              strerror(errno));
        }
        ch->queue = q;
        luaL_getmetatable(L, LPTHREAD_CHANNEL_MT);
        lua_setmetatable(L, -2);
        lua_replace(L, i);
    }

    // create pthread.self
    lua_newuserdata(L, sizeof(lpthread_self_t));
    luaL_getmetatable(L, PTHREAD_SELF_MT);
    lua_setmetatable(L, -2);
    // set pthread.self object as the first argument
    lua_insert(L, 1);
    return lua_gettop(L);
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

int lpthread_self_start(lua_State *L, lpthread_t *th, const char *src,
                        int with_file)
{
    // create thread state
    lua_State *thL = luaL_newstate();
    if (!thL) {
        return errno;
    }
    // open standard libraries
    luaL_openlibs(thL);
    // open pthread module
    int rc = luaL_dostring(thL, "require('pthread')");
    if (rc != 0) {
        errno = (rc == LUA_ERRMEM) ? ENOMEM : ECANCELED;
        goto FAIL_LUA;
    }

    // add traceback function
    lua_pushcfunction(thL, traceback);

    // load script or script file that runs on thread
    errno = 0;
    rc    = (with_file) ? luaL_loadfile(thL, src) : luaL_loadstring(thL, src);
    if (rc != 0) {
        errno = (rc == LUA_ERRMEM) ? ENOMEM : EINVAL;
        goto FAIL_LUA;
    }

    // create pthread.self and pthread.channel objects
    lua_pushcfunction(thL, new_pthread_self);
    // push queue_t as lightuserdata for passing to new_pthread_self
    int nchan = lua_gettop(L) - 2;
    for (int i = 0; i < nchan; i++) {
        lpthread_channel_t *ch = lua_touserdata(L, i + 2);
        lua_pushlightuserdata(thL, ch->queue);
    }

    rc = lua_pcall(thL, nchan, LUA_MULTRET, 1);
    if (rc != 0) {
        size_t len         = 0;
        const char *errmsg = NULL;

        errno = (rc == LUA_ERRMEM) ? ENOMEM : EINVAL;

FAIL_LUA:
        errmsg = lua_tolstring(thL, -1, &len);
        if (len > sizeof(th->errmsg) - 1) {
            len = sizeof(th->errmsg) - 1;
        }
        memcpy(th->errmsg, errmsg, len);
        lua_close(thL);
        return errno;
    }

    lpthread_self_t *self = luaL_checkudata(thL, -(1 + nchan), PTHREAD_SELF_MT);
    self->lua_status      = -1;
    self->L               = thL;
    self->parent          = th;
    errno = pthread_create(&th->id, NULL, on_start, (void *)self);
    if (errno != 0) {
        lua_close(thL);
    }
    return errno;
}
