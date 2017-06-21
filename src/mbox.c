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

static int copy2mbox( lua_State *L, lua_State *mbox, int idx, int allow_nil );

static int tbl2mbox( lua_State *L, lua_State *mbox, int idx )
{
    lua_newtable( mbox );
    lua_pushnil( L );

    while( lua_next( L, idx ) )
    {
        if( copy2mbox( L, mbox, idx + 1, 0 ) != LUA_TNONE &&
            copy2mbox( L, mbox, idx + 2, 0 ) != LUA_TNONE ){
            lua_rawset( mbox, -3 );
        }
        lua_pop( mbox, 1 );
    }

    return LUA_TTABLE;
}


static int copy2mbox( lua_State *L, lua_State *mbox, int idx, int allow_nil )
{
    size_t len = 0;
    const char *str = NULL;

    switch( lua_type( L, idx ) )
    {
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

        case LUA_TNIL:
            if( allow_nil ){
                lua_pushnil( mbox );
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


static int recv_lua( lua_State *L )
{
    lpt_mbox_t *mbox = luaL_checkudata( L, 1, MODULE_MT );
    int narg = 0;

    pthread_mutex_lock( &mbox->data->mutex );
    narg = lua_gettop( mbox->data->inbox );
    if( narg > 0 ){
        lua_xmove( mbox->data->inbox, L, narg );
    }
    pthread_mutex_unlock( &mbox->data->mutex );

    return narg;
}


static int send_lua( lua_State *L )
{
    int narg = lua_gettop( L );
    lpt_mbox_t *mbox = luaL_checkudata( L, 1, MODULE_MT );
    lpt_mbox_data_t *outbox = NULL;

    if( narg > 1 && ( outbox = lpt_shm_get( mbox->data->peer ) ) )
    {
        pthread_mutex_lock( &outbox->mutex );
        if( outbox->self != LUA_NOREF )
        {
            int idx = 2;

            for(; idx <= narg; idx++ ){
                copy2mbox( L, outbox->inbox, idx, 1 );
            }
        }
        pthread_mutex_unlock( &outbox->mutex );
    }

    return 0;
}


static int tostring_lua( lua_State *L )
{
    lua_pushfstring( L, MODULE_MT ": %p", lua_touserdata( L, 1 ) );
    return 1;
}


static int gc_lua( lua_State *L )
{
    lpt_mbox_t *mbox = luaL_checkudata( L, 1, MODULE_MT );

    pthread_mutex_lock( &mbox->data->mutex );
    mbox->data->self = lpt_shm_release( mbox->data->self );
    mbox->data->peer = lpt_shm_release( mbox->data->peer );
    lauxh_unref( L, mbox->data->ref_inbox );
    pthread_mutex_unlock( &mbox->data->mutex );

    return 0;
}


lpt_mbox_t *lpt_mbox_alloc( lua_State *L, lpt_mbox_t *outbox )
{
    int ref = LUA_NOREF;
    lpt_mbox_data_t *data = lpt_shm_create( sizeof( lpt_mbox_data_t ), &ref );

    if( data )
    {
        lpt_mbox_t *mbox = lua_newuserdata( L, sizeof( lpt_mbox_t ) );

        if( mbox && ( data->inbox = lua_newthread( L ) ) ){
            data->ref_inbox = lauxh_ref( L );
            lauxh_setmetatable( L, MODULE_MT );
            data->self = ref;
            if( outbox ){
                data->peer = lpt_shm_retain( outbox->data->self );
                outbox->data->peer = lpt_shm_retain( data->self );
            }
            else {
                data->peer = LUA_NOREF;
            }
            pthread_mutex_init( &data->mutex, NULL );

            mbox->data = data;
            return mbox;
        }

        lpt_shm_release( ref );
    }

    return NULL;
}


void lpt_mbox_init( lua_State *L )
{
    static struct luaL_Reg mmethod[] = {
        { "__gc", gc_lua },
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

