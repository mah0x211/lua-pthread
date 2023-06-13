/**
 * Copyright (C) 2014-present Masatoshi Fukunaga
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
 *
 * pthread.c
 * lua-pthread
 * Created by Masatoshi Teruya on 14/09/12.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
// lua
#include <lua_errno.h>

#define PTHREAD_MT      "pthread"
#define PTHREAD_SELF_MT "pthread.self"

#define DEFAULT_TIMEWAIT 1

typedef enum {
    THREAD_RUNNING,
    THREAD_TERMINATED,
    THREAD_FAILURE,
    THREAD_CANCELLED,
} lpthread_status_t;

typedef struct {
    pthread_t id;
    int pipefd[2];
    lpthread_status_t status;
    char errmsg[BUFSIZ];
} lpthread_t;

typedef struct {
    int lua_status;
    lua_State *L;
    lpthread_t *parent;
} lpthread_self_t;

static inline int tostring(lua_State *L, const char *tname)
{
    lua_pushfstring(L, "%s: %p", tname, lua_touserdata(L, 1));
    return 1;
}

static int self_tostring_lua(lua_State *L)
{
    return tostring(L, PTHREAD_SELF_MT);
    return 1;
}

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
        th->parent->status      = THREAD_FAILURE;
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

static void *on_start(void *arg)
{
    lpthread_self_t *th = (lpthread_self_t *)arg;

    pthread_cleanup_push(on_cleanup, arg);
    luaL_getmetatable(th->L, PTHREAD_SELF_MT);
    lua_setmetatable(th->L, -2);

    // set traceback function
    lua_pushcfunction(th->L, traceback);
    lua_insert(th->L, 1);

    // run state in thread
    th->lua_status = -1;
    th->lua_status = lua_pcall(th->L, 1, 0, 1);
    pthread_cleanup_pop(1);
    return NULL;
}

static int join_lua(lua_State *L)
{
    lpthread_t *th = luaL_checkudata(L, 1, PTHREAD_MT);
    int rc         = 0;

    if (th->pipefd[0] == -1) {
        // already joined
        lua_pushboolean(L, 1);
        return 1;
    }

    char buf[3] = {0};
    ssize_t len = read(th->pipefd[0], buf, sizeof(buf));
    switch (len) {
    case -1:
        // got error
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
TIMED_OUT:
            // timeout
            lua_pushboolean(L, 0);
            lua_pushnil(L);
            lua_pushboolean(L, 1);
            return 3;
        } else if (errno == EBADF) {
            if (th->status == THREAD_RUNNING) {
                // pipe was closed but thread is still running
                goto TIMED_OUT;
            }
            goto FORCE_JOIN;
        }
        lua_pushboolean(L, 0);
        lua_errno_new(L, errno, NULL);
        lua_pushnil(L);
        return 3;

    case 0:
        return luaL_error(
            L, "the pipe for inter-thread communication was closed for "
               "unknown reasons.");

    case 1:
        // received the termination message from thread
        if (*buf == '0') {
            break;
        }

    default:
        return luaL_error(L, "invalid thead termination message received.");
    }

    if (th->status == THREAD_RUNNING) {
        return luaL_error(L, "thread termination message received, but thread "
                             "status is still running.");
    }

FORCE_JOIN:
    rc = pthread_join(th->id, NULL);
    if (rc != 0) {
        return luaL_error(L,
                          "thread termination message received, but failed to "
                          "pthread_join(): %s",
                          strerror(rc));
    }
    // close pipe on the read side
    close(th->pipefd[0]);
    th->pipefd[0] = -1;

    lua_pushboolean(L, 1);
    return 1;
}

static int cancel_lua(lua_State *L)
{
    lpthread_t *th = luaL_checkudata(L, 1, PTHREAD_MT);
    int rc         = pthread_cancel(th->id);

    if (rc == 0) {
        lua_pushboolean(L, 1);
        return 1;
    }
    // got error
    lua_pushboolean(L, 0);
    lua_errno_new(L, rc, NULL);
    return 2;
}

static int status_lua(lua_State *L)
{
    lpthread_t *th = luaL_checkudata(L, 1, PTHREAD_MT);

    if (th->pipefd[0] != -1) {
        lua_pushstring(L, "running");
        return 1;
    }

    switch (th->status) {
    case THREAD_TERMINATED:
        lua_pushstring(L, "terminated");
        return 1;
    case THREAD_CANCELLED:
        lua_pushstring(L, "cancelled");
        return 1;

    default:
        // THREAD_FAILURE
        lua_pushstring(L, "failure");
        lua_pushstring(L, th->errmsg);
        return 2;
    }
}

static int fd_lua(lua_State *L)
{
    lpthread_t *th = luaL_checkudata(L, 1, PTHREAD_MT);
    lua_pushinteger(L, th->pipefd[0]);
    return 1;
}

static int tostring_lua(lua_State *L)
{
    return tostring(L, PTHREAD_MT);
    return 1;
}

static int gc_lua(lua_State *L)
{
    lpthread_t *th = luaL_checkudata(L, 1, PTHREAD_MT);

    if (pthread_cancel(th->id) == 0) {
        pthread_join(th->id, NULL);
    }
    // close pipe
    for (int i = 0; i < 2; i++) {
        if (th->pipefd[i] > 0) {
            close(th->pipefd[i]);
        }
    }

    return 0;
}

static void register_mt(lua_State *L, const char *tname,
                        struct luaL_Reg *mmethods, struct luaL_Reg *methods)
{
    // register metatable
    luaL_newmetatable(L, tname);
    // add metamethods
    for (struct luaL_Reg *ptr = mmethods; ptr->name; ptr++) {
        lua_pushcfunction(L, ptr->func);
        lua_setfield(L, -2, ptr->name);
    }
    // create method
    lua_newtable(L);
    // add methods
    for (struct luaL_Reg *ptr = methods; ptr->name; ptr++) {
        lua_pushcfunction(L, ptr->func);
        lua_setfield(L, -2, ptr->name);
    }
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

static int new_lua(lua_State *L)
{
    size_t len           = 0;
    const char *filename = luaL_checklstring(L, 1, &len);
    const char *errmsg   = NULL;
    lpthread_t *th       = lua_newuserdata(L, sizeof(lpthread_t));
    lua_State *thL       = NULL;

    // init properties
    memset(th, 0, sizeof(lpthread_t));
    th->status = THREAD_RUNNING;

    // create pipe
    if (pipe(th->pipefd) != 0) {
        goto FAIL;
    }
    // set o_cloexec and o_nonblock flags
    if (fcntl(th->pipefd[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(th->pipefd[0], F_SETFL, O_NONBLOCK) != 0 ||
        fcntl(th->pipefd[1], F_SETFD, FD_CLOEXEC) != 0) {
        goto FAIL;
    }

    // create thread state
    thL = luaL_newstate();
    if (!thL) {
        goto FAIL;
    }

    // open standard libraries and pthread module
    luaL_openlibs(thL);
    // open pthread module
    errno  = 0;
    int rc = luaL_dostring(thL, "require('pthread')");
    if (rc != 0) {
        errmsg = lua_tostring(thL, -1);
        if (errno == 0) {
            if (rc == LUA_ERRMEM) {
                errno = ENOMEM;
            } else {
                errno = EINVAL;
            }
        }
        goto FAIL;
    }

    // register pthead.self metatable
    struct luaL_Reg mmethods[] = {
        {"__tostring", self_tostring_lua},
        {NULL,         NULL             }
    };
    struct luaL_Reg methods[] = {
        {NULL, NULL}
    };
    register_mt(thL, PTHREAD_SELF_MT, mmethods, methods);

    // load script file that runs on thread
    rc = luaL_loadfile(thL, filename);
    if (rc != 0) {
        errmsg = lua_tostring(thL, -1);
        if (errno == 0) {
            if (rc == LUA_ERRMEM) {
                errno = ENOMEM;
            } else {
                errno = EINVAL;
            }
        }
        goto FAIL;
    }

    // create thread data
    lpthread_self_t *th_self = lua_newuserdata(thL, sizeof(lpthread_self_t));
    if (!th_self) {
        goto FAIL;
    }
    // reference thread properties
    th_self->lua_status = -1;
    th_self->parent     = th;
    th_self->L          = thL;

    // create thread
    errno = pthread_create(&th->id, NULL, on_start, (void *)th_self);
    if (errno != 0) {
        if (errno == EAGAIN) {
            // too many threads
            lua_pushnil(L);
            lua_pushnil(L);
            lua_pushboolean(L, 1);
            return 3;
        }
        goto FAIL;
    }

    // set metatable
    luaL_getmetatable(L, PTHREAD_MT);
    lua_setmetatable(L, -2);
    return 1;

FAIL:
    for (int i = 0; i < 2; i++) {
        if (th->pipefd[i] > 0) {
            close(th->pipefd[i]);
        }
    }
    if (thL) {
        lua_close(thL);
    }
    lua_pushnil(L);
    lua_errno_new_with_message(L, errno, NULL, errmsg);
    return 2;
}

LUALIB_API int luaopen_pthread(lua_State *L)
{
    struct luaL_Reg mmethods[] = {
        {"__gc",       gc_lua      },
        {"__tostring", tostring_lua},
        {NULL,         NULL        }
    };
    struct luaL_Reg methods[] = {
        {"fd",     fd_lua    },
        {"status", status_lua},
        {"cancel", cancel_lua},
        {"join",   join_lua  },
        {NULL,     NULL      }
    };

    lua_errno_loadlib(L);
    register_mt(L, PTHREAD_MT, mmethods, methods);

    // return new function
    lua_pushcfunction(L, new_lua);
    return 1;
}
