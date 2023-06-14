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

#include "lpthread.h"

static int join_lua(lua_State *L)
{
    lpthread_t *th = luaL_checkudata(L, 1, LPTHREAD_MT);
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
            // timeout
            lua_pushboolean(L, 0);
            lua_pushnil(L);
            lua_pushboolean(L, 1);
            return 3;
        } else if (errno == EBADF) {
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
        return luaL_error(
            L, "invalid thread termination message received: %d %c", len, *buf);
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
    lpthread_t *th = luaL_checkudata(L, 1, LPTHREAD_MT);
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
    lpthread_t *th = luaL_checkudata(L, 1, LPTHREAD_MT);

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
        // THREAD_FAILED
        lua_pushstring(L, "failed");
        lua_pushstring(L, th->errmsg);
        return 2;
    }
}

static int fd_lua(lua_State *L)
{
    lpthread_t *th = luaL_checkudata(L, 1, LPTHREAD_MT);
    lua_pushinteger(L, th->pipefd[0]);
    return 1;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, LPTHREAD_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int gc_lua(lua_State *L)
{
    lpthread_t *th = luaL_checkudata(L, 1, LPTHREAD_MT);

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

static int new_lua(lua_State *L)
{
    size_t len           = 0;
    const char *filename = luaL_checklstring(L, 1, &len);
    lpthread_t *th       = lua_newuserdata(L, sizeof(lpthread_t));

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

    // create thread
    errno = lpthread_self_start(th, filename);
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
    luaL_getmetatable(L, LPTHREAD_MT);
    lua_setmetatable(L, -2);
    return 1;

FAIL:
    for (int i = 0; i < 2; i++) {
        if (th->pipefd[i] > 0) {
            close(th->pipefd[i]);
        }
    }

    lua_pushnil(L);
    lua_errno_new_with_message(L, errno, NULL, th->errmsg);
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
    register_mt(L, LPTHREAD_MT, mmethods, methods);

    // return new function
    lua_pushcfunction(L, new_lua);
    return 1;
}
