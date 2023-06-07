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
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
// lua
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#define MODULE_MT        "pthread"
#define DEFAULT_TIMEWAIT 1

typedef struct {
    pthread_t id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    lua_State *L;
    int running;
} lpt_t;

static void lpt_dealloc(lpt_t *th)
{
    if (th->L) {
        lua_close(th->L);
        th->L       = NULL;
        th->running = 0;
    }
}

static lpt_t *lpt_alloc(lua_State *L)
{
    lpt_t *th = lua_newuserdata(L, sizeof(lpt_t));

    // alloc
    if ((th->L = luaL_newstate())) {
        luaL_openlibs(th->L);
        pthread_mutex_init(&th->mutex, NULL);
        pthread_cond_init(&th->cond, NULL);
        th->running = 0;
        return th;
    }
    lpt_dealloc(th);

    return NULL;
}

static inline struct timespec *addabstime(struct timespec *ts)
{
    struct timeval tv = {0};

    gettimeofday(&tv, NULL);
    ts->tv_sec += tv.tv_sec;
    ts->tv_nsec += tv.tv_usec * 1000;

    return ts;
}

static void on_exit(void *arg)
{
    lpt_t *th = (lpt_t *)arg;

    pthread_mutex_unlock(&th->mutex);
}

static void *on_start(void *arg)
{
    lpt_t *th = (lpt_t *)arg;

    pthread_mutex_lock(&th->mutex);
    th->running = 1;
    pthread_cleanup_push(on_exit, th);
    pthread_cond_signal(&th->cond);
    pthread_cond_wait(&th->cond, &th->mutex);

    // run state in thread
    switch (lua_pcall(th->L, lua_gettop(th->L) - 1, 0, 0)) {
    case LUA_ERRRUN:
    case LUA_ERRMEM:
    case LUA_ERRERR:
        printf("got error: %s\n", lua_tostring(th->L, -1));
        break;
    }
    pthread_cleanup_pop(1);

    pthread_exit(NULL);
}

static int kill_lua(lua_State *L)
{
    lpt_t *th         = (lpt_t *)luaL_checkudata(L, 1, MODULE_MT);
    lua_Integer signo = luaL_checkinteger(L, 2);

    if (pthread_kill(th->id, signo) == 0) {
        lua_pushboolean(L, 1);
        return 1;
    }

    // got error
    lua_pushboolean(L, 0);
    lua_pushstring(L, strerror(errno));

    return 2;
}

static int join_lua(lua_State *L)
{
    lpt_t *th = (lpt_t *)luaL_checkudata(L, 1, MODULE_MT);

    pthread_mutex_lock(&th->mutex);
    if (th->running) {
        int rc = 0;

        pthread_cond_signal(&th->cond);
        pthread_mutex_unlock(&th->mutex);
        if ((rc = pthread_join(th->id, NULL))) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, strerror(rc));
            return 2;
        }
        lpt_dealloc(th);
    } else {
        pthread_mutex_unlock(&th->mutex);
    }

    lua_pushboolean(L, 1);

    return 1;
}

static int gc_lua(lua_State *L)
{
    lpt_t *th = (lpt_t *)luaL_checkudata(L, 1, MODULE_MT);

    pthread_mutex_lock(&th->mutex);
    if (th->running) {
        pthread_cond_signal(&th->cond);
        pthread_mutex_unlock(&th->mutex);
        pthread_join(th->id, NULL);
    }
    // already joined
    else {
        pthread_mutex_unlock(&th->mutex);
    }

    lpt_dealloc(th);

    return 0;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, MODULE_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int dumpcb(lua_State *L, const void *chunk, size_t bytes, void *buf)
{
    (void)L;
    luaL_addlstring((luaL_Buffer *)buf, (const char *)chunk, bytes);
    return 0;
}

/* copy a value to different state */
static inline int xcopy(lua_State *from, lua_State *to, int idx,
                        const int allow_nil)
{
    switch (lua_type(from, idx)) {
    case LUA_TBOOLEAN:
        lua_pushboolean(to, lua_toboolean(from, idx));
        return LUA_TBOOLEAN;

    case LUA_TLIGHTUSERDATA:
        lua_pushlightuserdata(to, lua_touserdata(from, idx));
        return LUA_TLIGHTUSERDATA;

    case LUA_TNUMBER:
        lua_pushnumber(to, lua_tonumber(from, idx));
        return LUA_TNUMBER;

    case LUA_TSTRING: {
        size_t len      = 0;
        const char *str = lua_tolstring(from, idx, &len);
        lua_pushlstring(to, str, len);
        return LUA_TSTRING;
    }

    case LUA_TTABLE:
        lua_newtable(to);
        // to positive number
        if (idx < 0) {
            idx = lua_gettop(from) + idx + 1;
        }
        lua_pushnil(from);
        while (lua_next(from, idx)) {
            if (xcopy(from, to, -2, 0) != LUA_TNONE) {
                if (xcopy(from, to, -1, 0) != LUA_TNONE) {
                    lua_rawset(to, -3);
                } else {
                    lua_pop(to, 1);
                }
            }
            lua_pop(from, 1);
        }
        return LUA_TTABLE;

    case LUA_TNIL:
        if (allow_nil) {
            lua_pushnil(to);
            return LUA_TNIL;
        }

    // ignore unsupported values
    // LUA_TNONE
    // LUA_TFUNCTION
    // LUA_TUSERDATA
    // LUA_TTHREAD
    default:
        return LUA_TNONE;
    }
}

static int new_lua(lua_State *L)
{
    int narg       = lua_gettop(L);
    size_t len     = 0;
    const char *fn = NULL;
    lpt_t *th      = NULL;
    luaL_Buffer buf;
    struct timespec ts = {.tv_sec = DEFAULT_TIMEWAIT, .tv_nsec = 0};
    int rc             = 0;

    // check function argument
    switch (lua_type(L, 1)) {
    case LUA_TFUNCTION:
        lua_pushvalue(L, 1);
        luaL_buffinit(L, &buf);
        if (lua_dump(L, dumpcb, &buf) != 0) {
            return luaL_error(L, "unable to dump given function");
        }
        luaL_pushresult(&buf);
        lua_replace(L, 1);
        lua_pop(L, 1);
    case LUA_TSTRING:
        break;

    default:
        return luaL_error(L, "fn must be function or function string");
    }

    // get function string
    fn = lua_tolstring(L, 1, &len);

    // allocate
    if (!(th = lpt_alloc(L))) {
        lua_pushnil(L);
        lua_pushstring(L, strerror(rc));
        return 2;
    }
    // compile error
    else if ((rc = luaL_loadbuffer(th->L, fn, len, NULL))) {
        lua_pushnil(L);
        lua_pushstring(L, lua_tostring(th->L, -1));
        lpt_dealloc(th);
        return 2;
    }

    // copying passed arguments to thread state
    for (int i = 2; i <= narg; i++) {
        xcopy(L, th->L, i, 1);
    }

    pthread_mutex_lock(&th->mutex);
    // create thread
    if ((rc = pthread_create(&th->id, NULL, on_start, (void *)th))) {
        pthread_mutex_unlock(&th->mutex);
        lpt_dealloc(th);
        lua_pushnil(L);
        lua_pushstring(L, strerror(rc));
        return 2;
    }
    // wait suspend
    else if ((rc = pthread_cond_timedwait(&th->cond, &th->mutex,
                                          addabstime(&ts)))) {
        pthread_mutex_unlock(&th->mutex);
        pthread_cancel(th->id);
        lpt_dealloc(th);
        lua_pushnil(L);
        lua_pushstring(L, strerror(rc));
        return 2;
    }

    luaL_getmetatable(L, MODULE_MT);
    lua_setmetatable(L, -2);

    // resume thread
    pthread_cond_signal(&th->cond);
    pthread_mutex_unlock(&th->mutex);

    return 1;
}

LUALIB_API int luaopen_pthread(lua_State *L)
{
    struct luaL_Reg mmethod[] = {
        {"__gc",       gc_lua      },
        {"__tostring", tostring_lua},
        {NULL,         NULL        }
    };
    struct luaL_Reg method[] = {
        {"join", join_lua},
        {"kill", kill_lua},
        {NULL,   NULL    }
    };

    // register metatable
    luaL_newmetatable(L, MODULE_MT);
    // add metamethods
    for (struct luaL_Reg *ptr = mmethod; ptr->name; ptr++) {
        lua_pushcfunction(L, ptr->func);
        lua_setfield(L, -2, ptr->name);
    }
    // create method
    lua_newtable(L);
    // add methods
    for (struct luaL_Reg *ptr = method; ptr->name; ptr++) {
        lua_pushcfunction(L, ptr->func);
        lua_setfield(L, -2, ptr->name);
    }
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    // add new function
    lua_newtable(L);
    lua_pushcfunction(L, new_lua);
    lua_setfield(L, -2, "new");

    return 1;
}
