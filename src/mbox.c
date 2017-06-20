/*
 *  Copyright (C) 2017 Masatoshi Teruya
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 *  mbox.c
 *  lua-pthread
 *  Created by Masatoshi Teruya on 17/06/16.
 *
 */

#include "lpt.h"

#define MODULE_MT   "pthread.mbox"

static int copy2mbox( lua_State *L, lua_State *mbox, int idx );

static int tbl2mbox( lua_State *L, lua_State *mbox, int idx )
{
    lua_newtable( mbox );
    lua_pushnil( L );

    while( lua_next( L, idx ) )
    {
        if( copy2mbox( L, mbox, idx + 1 ) )
        {
            if( copy2mbox( L, mbox, idx + 2 ) ){
                lua_rawset( mbox, -3 );
            }
            else {
                lua_pop( mbox, 1 );
            }
        }

        lua_pop( L, 1 );
    }

    return LUA_TTABLE;
}


static int copy2mbox( lua_State *L, lua_State *mbox, int idx )
{
    size_t len = 0;
    const char *str = NULL;

    switch( lua_type( L, idx ) )
    {
        case LUA_TNIL:
            lua_pushnil( mbox );
            return LUA_TNIL;

        case LUA_TBOOLEAN:
            lua_pushboolean( mbox, lua_toboolean( L, idx ) );
            return LUA_TBOOLEAN;

        case LUA_TLIGHTUSERDATA:
            lua_pushlightuserdata( mbox, lua_touserdata( L, idx ) );
            return LUA_TLIGHTUSERDATA;

        case LUA_TNUMBER:
            lua_pushnumber( mbox, lua_tonumber( L, idx ) );
            return LUA_TNUMBER;

        case LUA_TSTRING:
            str = lua_tolstring( L, idx, &len );
            lua_pushlstring( mbox, str, len );
            return LUA_TSTRING;

        case LUA_TTABLE:
            return tbl2mbox( L, mbox, idx );

        // ignore unsupported values
        // LUA_TNONE:
        // LUA_TFUNCTION
        // LUA_TUSERDATA
        // LUA_TTHREAD
        default:
            return 0;
    }
}


static int recv_lua( lua_State *L )
{
    lpt_mbox_t *mbox = luaL_checkudata( L, 1, MODULE_MT );
    int narg = 0;

    pthread_mutex_lock( &mbox->mutex );
    narg = lua_gettop( mbox->inbox );
    if( narg > 0 ){
        lua_xmove( mbox->inbox, L, narg );
    }
    pthread_mutex_unlock( &mbox->mutex );

    return narg;
}


static int send_lua( lua_State *L )
{
    int narg = lua_gettop( L );
    lpt_mbox_t *mbox = luaL_checkudata( L, 1, MODULE_MT );

    pthread_mutex_lock( &mbox->outbox->mutex );
    if( narg > 1 )
    {
        int idx = 2;

        for(; idx <= narg; idx++ ){
            copy2mbox( L, mbox->outbox->inbox, idx );
        }
    }
    pthread_mutex_unlock( &mbox->outbox->mutex );

    return 0;
}


static int tostring_lua( lua_State *L )
{
    lua_pushfstring( L, MODULE_MT ": %p", lua_touserdata( L, 1 ) );
    return 1;
}


lpt_mbox_t *lpt_mbox_alloc( lua_State *L, lpt_mbox_t *outbox )
{
    lpt_mbox_t *mbox = lua_newuserdata( L, sizeof( lpt_mbox_t ) );

    if( mbox && ( mbox->inbox = lua_newthread( L ) ) ){
        pthread_mutex_init( &mbox->mutex, NULL );
        mbox->outbox = outbox;
        lpt_weakref_set( L, -1 );
        lua_pop( L, 1 );
        lauxh_setmetatable( L, MODULE_MT );
        return mbox;
    }

    return NULL;
}


void lpt_mbox_init( lua_State *L )
{
    static struct luaL_Reg mmethod[] = {
        { "__tostring", tostring_lua },
        { NULL, NULL }
    };
    static struct luaL_Reg method[] = {
        { "send", send_lua },
        { "recv", recv_lua },
        { NULL, NULL }
    };

    // register metatable
    lpt_register_mt( L, MODULE_MT, mmethod, method );
}

