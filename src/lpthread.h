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
 **/

#ifndef lpthread_h
#define lpthread_h

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

#define LPTHREAD_MT         "pthread"
#define LPTHREAD_CHANNEL_MT "pthread.channel"

typedef enum {
    THREAD_RUNNING,
    THREAD_TERMINATED,
    THREAD_FAILED,
    THREAD_CANCELLED,
} lpthread_status_t;

typedef struct {
    pthread_t id;
    int pipefd[2];
    lpthread_status_t status;
    char errmsg[BUFSIZ];
} lpthread_t;

int lpthread_self_start(lpthread_t *th, const char *filename);

static inline void register_mt(lua_State *L, const char *tname,
                               struct luaL_Reg *mmethods,
                               struct luaL_Reg *methods)
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

static inline int tostring(lua_State *L, const char *tname)
{
    lua_pushfstring(L, "%s: %p", tname, lua_touserdata(L, 1));
    return 1;
}

#endif /* lpthread_h */
