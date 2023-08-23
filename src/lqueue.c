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
#include <lauxhlib.h>

typedef enum {
    QDATA_TRUE = 0,
    QDATA_FALSE,
    QDATA_LIGHTUSERDATA,
    QDATA_NUMBER,
    QDATA_INTEGER,
    QDATA_STRING,
} qdata_type_t;

typedef struct {
    size_t len;
    const char *data;
} qdata_str_t;

typedef struct {
    char type;
    union {
        char boolval;
        lua_Integer ival;
        lua_Number nval;
        uintptr_t p;
        qdata_str_t str;
    } value;
} qdata_t;

static void delete_queue_data(uintptr_t data, void *arg)
{
    (void)arg;
    free((void *)data);
}

static inline lpthread_queue_t *check_lpthread_queue(lua_State *L)
{
    lpthread_queue_t *q = luaL_checkudata(L, 1, LPTHREAD_THREAD_QUEUE_MT);
    luaL_argcheck(L, !q->closed, 1, "queue is closed");
    return q;
}

static void push_qdata(lua_State *L, qdata_t *data)
{
    switch (data->type) {
    case QDATA_TRUE:
        lua_pushboolean(L, 1);
        break;
    case QDATA_FALSE:
        lua_pushboolean(L, 0);
        break;

    case QDATA_NUMBER:
        lua_pushnumber(L, data->value.nval);
        break;

    case QDATA_INTEGER:
        lua_pushinteger(L, data->value.ival);
        break;

    case QDATA_STRING:
        lua_pushlstring(L, data->value.str.data, data->value.str.len);
        break;

    case QDATA_LIGHTUSERDATA:
        lua_pushlightuserdata(L, (void *)data->value.p);
        break;

    default:
        lua_pushnil(L);
        break;
    }
}

static int pop_match_lua(lua_State *L)
{
    lpthread_queue_t *q = check_lpthread_queue(L);
    uintptr_t id        = (uintptr_t)luaL_checkinteger(L, 2);

    errno = 0;
    switch (queue_pop_match(q->queue, (uintptr_t)q, id)) {
    case 0:
        // not found
        lua_pushnil(L);
        return 1;

    case 1:
        push_qdata(L, (qdata_t *)id);
        delete_queue_data(id, NULL);
        return 1;

    default:
        // got an error
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
}

static int pop_lua(lua_State *L)
{
    lpthread_queue_t *q = check_lpthread_queue(L);
    qdata_t *data       = NULL;

    errno = 0;
    if (queue_pop(q->queue, (uintptr_t)q, (uintptr_t *)&data) != 0) {
        // got an error
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;
    } else if (!data) {
        // queue is empty
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushboolean(L, 1);
        return 3;
    }

    push_qdata(L, data);
    delete_queue_data((uintptr_t)data, NULL);
    return 1;
}

static int push_lua(lua_State *L)
{
    lpthread_queue_t *q = check_lpthread_queue(L);
    qdata_t data        = {0};
    size_t size         = sizeof(qdata_t);

    switch (lua_type(L, 2)) {
    case LUA_TBOOLEAN:
        if (lua_toboolean(L, 2)) {
            data.type = QDATA_TRUE;
        } else {
            data.type = QDATA_FALSE;
        }
        break;

    case LUA_TNUMBER:
        if (lauxh_isinteger(L, 2)) {
            data.type       = QDATA_INTEGER;
            data.value.ival = lua_tointeger(L, 2);
        } else {
            data.type       = QDATA_NUMBER;
            data.value.nval = lua_tonumber(L, 2);
        }
        break;

    case LUA_TSTRING:
        data.type           = QDATA_STRING;
        data.value.str.data = lua_tolstring(L, 2, &data.value.str.len);
        size += data.value.str.len;
        break;

    case LUA_TLIGHTUSERDATA:
        data.type    = QDATA_LIGHTUSERDATA;
        data.value.p = (uintptr_t)lua_touserdata(L, 2);
        break;

    // ignore unsupported values
    // LUA_TTABLE
    // LUA_TNONE
    // LUA_TNIL
    // LUA_TFUNCTION
    // LUA_TUSERDATA
    // LUA_TTHREAD
    default:
        return luaL_error(L, "unsupported value type: %s", luaL_typename(L, 2));
    }

    // allocate memory for queue data
    qdata_t *item = malloc(size);
    if (!item) {
        lua_pushboolean(L, 0);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
    *item = data;
    // copy a string value
    if (data.type == QDATA_STRING) {
        item->value.str.data = (char *)(item + 1);
        memcpy((void *)item->value.str.data, data.value.str.data,
               data.value.str.len);
    }

    // push a value to queue
    switch (queue_push(q->queue, (uintptr_t)q, (uintptr_t)item, size)) {
    case -1:
        // failed to push a value
        delete_queue_data((uintptr_t)item, NULL);
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;

    case 0:
        // queue is full
        delete_queue_data((uintptr_t)item, NULL);
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushboolean(L, 1);
        return 3;

    default:
        lua_pushinteger(L, (uintptr_t)item);
        return 1;
    }
}

static int fd_readable_lua(lua_State *L)
{
    lpthread_queue_t *q = check_lpthread_queue(L);
    int fd              = queue_fd_readable(q->queue, (uintptr_t)q);
    lua_pushinteger(L, fd);
    return 1;
}

static int fd_writable_lua(lua_State *L)
{
    lpthread_queue_t *q = check_lpthread_queue(L);
    int fd              = queue_fd_writable(q->queue, (uintptr_t)q);
    lua_pushinteger(L, fd);
    return 1;
}

static int len_lua(lua_State *L)
{
    lpthread_queue_t *q = check_lpthread_queue(L);
    ssize_t len         = queue_len(q->queue, (uintptr_t)q);
    lua_pushinteger(L, len);
    return 1;
}

static int maxitem_lua(lua_State *L)
{
    lpthread_queue_t *q = check_lpthread_queue(L);
    ssize_t maxitem     = queue_maxitem(q->queue, (uintptr_t)q);
    lua_pushinteger(L, maxitem);
    return 1;
}

static int nref_lua(lua_State *L)
{
    lpthread_queue_t *q = check_lpthread_queue(L);
    int nref            = queue_nref(q->queue, (uintptr_t)q);
    lua_pushinteger(L, nref);
    return 1;
}

static int close_lua(lua_State *L)
{
    lpthread_queue_t *q = luaL_checkudata(L, 1, LPTHREAD_THREAD_QUEUE_MT);

    if (!q->closed) {
        queue_unref(q->queue, (uintptr_t)q);
        q->closed = 1;
    }
    return 0;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, LPTHREAD_THREAD_QUEUE_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int gc_lua(lua_State *L)
{
    lpthread_queue_t *q = lua_touserdata(L, 1);
    if (!q->closed) {
        queue_unref(q->queue, (uintptr_t)q);
    }
    return 0;
}

static int new_lua(lua_State *L)
{
    int maxitem = luaL_optinteger(L, 1, 0);

    lpthread_queue_t *q = lua_newuserdata(L, sizeof(lpthread_queue_t));
    q->queue            = queue_new(maxitem, delete_queue_data, NULL);
    if (!q->queue) {
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
    q->closed = 0;
    luaL_getmetatable(L, LPTHREAD_THREAD_QUEUE_MT);
    lua_setmetatable(L, -2);
    return 1;
}

void luaopen_pthread_queue(lua_State *L)
{
    struct luaL_Reg mmethods[] = {
        {"__gc",       gc_lua      },
        {"__tostring", tostring_lua},
        {NULL,         NULL        }
    };
    struct luaL_Reg methods[] = {
        {"close",       close_lua      },
        {"nref",        nref_lua       },
        {"maxitem",     maxitem_lua    },
        {"len",         len_lua        },
        {"fd_readable", fd_readable_lua},
        {"fd_writable", fd_writable_lua},
        {"push",        push_lua       },
        {"pop",         pop_lua        },
        {"pop_match",   pop_match_lua  },
        {NULL,          NULL           }
    };

    lua_errno_loadlib(L);
    register_mt(L, LPTHREAD_THREAD_QUEUE_MT, mmethods, methods);

    // return new function
    lua_pushcfunction(L, new_lua);
}
