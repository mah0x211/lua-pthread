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

static void delete_queue_data(void *data, void *arg)
{
    (void)arg;
    qdata_t *qdata = (qdata_t *)data;

    if (qdata->type == QDATA_STRING) {
        free((void *)qdata->value.str.data);
    }
    free(data);
}

static int pop_lua(lua_State *L)
{
    lpthread_channel_t *ch = luaL_checkudata(L, 1, LPTHREAD_CHANNEL_MT);
    qdata_t *data          = NULL;

    errno = 0;
    if (queue_pop(ch->queue, (void *)&data, NULL) != 0) {
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
    delete_queue_data((void *)data, NULL);

    return 1;
}

static int push_lua(lua_State *L)
{
    lpthread_channel_t *ch = luaL_checkudata(L, 1, LPTHREAD_CHANNEL_MT);
    qdata_t data           = {0};
    size_t len             = 0;

    switch (lua_type(L, 2)) {
    case LUA_TBOOLEAN:
        if (lua_toboolean(L, 2)) {
            data.type = QDATA_TRUE;
        } else {
            data.type = QDATA_FALSE;
        }
        len = 1;
        break;

    case LUA_TNUMBER:
        if (lauxh_isinteger(L, 2)) {
            data.type       = QDATA_INTEGER;
            data.value.ival = lua_tointeger(L, 2);
            len             = 1 + sizeof(lua_Integer);
        } else {
            data.type       = QDATA_NUMBER;
            data.value.nval = lua_tonumber(L, 2);
            len             = 1 + sizeof(lua_Number);
        }
        break;

    case LUA_TSTRING:
        data.type           = QDATA_STRING;
        data.value.str.data = lua_tolstring(L, 2, &data.value.str.len);
        len                 = 1 + sizeof(size_t) + data.value.str.len;
        break;

    case LUA_TLIGHTUSERDATA:
        data.type    = QDATA_LIGHTUSERDATA;
        data.value.p = (uintptr_t)lua_touserdata(L, 2);
        len          = 1 + sizeof(uintptr_t);
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
    qdata_t *item = malloc(sizeof(qdata_t));
    if (!item) {
        lua_pushboolean(L, 0);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
    *item = data;
    // copy a string value
    if (data.type == QDATA_STRING) {
        item->value.str.data = malloc(data.value.str.len);
        if (!item->value.str.data) {
            free(item);
            lua_pushboolean(L, 0);
            lua_errno_new(L, errno, NULL);
            return 2;
        }
        memcpy((void *)item->value.str.data, data.value.str.data,
               data.value.str.len);
    }

    // push a value to queue
    if (queue_push(ch->queue, item, len) != 0) {
        // failed to push a value
        free(item);
        lua_pushboolean(L, 0);
        lua_errno_new(L, errno, NULL);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int fd_lua(lua_State *L)
{
    lpthread_channel_t *ch = luaL_checkudata(L, 1, LPTHREAD_CHANNEL_MT);
    int fd                 = queue_fd(ch->queue);

    if (fd < 0) {
        // got an error
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
    lua_pushinteger(L, fd);
    return 1;
}

static int size_lua(lua_State *L)
{
    lpthread_channel_t *ch = luaL_checkudata(L, 1, LPTHREAD_CHANNEL_MT);
    ssize_t size           = queue_size(ch->queue);

    if (size < 0) {
        // got an error
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
    lua_pushinteger(L, size);
    return 1;
}

static int len_lua(lua_State *L)
{
    lpthread_channel_t *ch = luaL_checkudata(L, 1, LPTHREAD_CHANNEL_MT);
    ssize_t len            = queue_len(ch->queue);

    if (len < 0) {
        // got an error
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
    lua_pushinteger(L, queue_len(ch->queue));
    return 1;
}

static int nref_lua(lua_State *L)
{
    lpthread_channel_t *ch = luaL_checkudata(L, 1, LPTHREAD_CHANNEL_MT);
    int nref               = queue_nref(ch->queue);

    if (nref < 0) {
        // got an error
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
    lua_pushinteger(L, nref);
    return 1;
}

static int tostring_lua(lua_State *L)
{
    lua_pushfstring(L, LPTHREAD_CHANNEL_MT ": %p", lua_touserdata(L, 1));
    return 1;
}

static int gc_lua(lua_State *L)
{
    lpthread_channel_t *ch = lua_touserdata(L, 1);
    if (queue_unref(ch->queue) != 0) {
        perror("failed to queue_unref() in pthread.channel.gc_lua");
    }
    return 0;
}

static int new_lua(lua_State *L)
{
    int maxitem = luaL_optinteger(L, 1, 0);
    int maxsize = luaL_optinteger(L, 2, 0);

    lpthread_channel_t *ch = lua_newuserdata(L, sizeof(lpthread_channel_t));
    ch->queue = queue_new(maxitem, maxsize, delete_queue_data, NULL);
    if (!ch->queue) {
        lua_pushnil(L);
        lua_errno_new(L, errno, NULL);
        return 2;
    }
    luaL_getmetatable(L, LPTHREAD_CHANNEL_MT);
    lua_setmetatable(L, -2);
    return 1;
}

void luaopen_pthread_channel(lua_State *L)
{
    struct luaL_Reg mmethods[] = {
        {"__gc",       gc_lua      },
        {"__tostring", tostring_lua},
        {NULL,         NULL        }
    };
    struct luaL_Reg methods[] = {
        {"nref", nref_lua},
        {"len",  len_lua },
        {"size", size_lua},
        {"fd",   fd_lua  },
        {"push", push_lua},
        {"pop",  pop_lua },
        {NULL,   NULL    }
    };

    lua_errno_loadlib(L);
    register_mt(L, LPTHREAD_CHANNEL_MT, mmethods, methods);

    // return new function
    lua_pushcfunction(L, new_lua);
}