/*
 *  Copyright (C) 2017 Masatoshi Teruya. All rights reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 *  util.c
 *  lua-pthread
 *
 *  Created by Masatoshi Teruya on 2017/06/16.
 */

#include "lpt.h"


static lua_State *SHM_STATE = NULL;


int lpt_shm_init( void )
{
    if( SHM_STATE || ( SHM_STATE = luaL_newstate() ) ){
        return 0;
    }

    return -1;
}


void *lpt_shm_create( size_t bytes, int *ref )
{
    void *udata = NULL;

    if( !ref ){
        errno = EINVAL;
    }
    else if( ( udata = lua_newuserdata( SHM_STATE, bytes ) ) )
    {
        switch( ( *ref = lauxh_ref( SHM_STATE ) ) ){
            // failed to create reference
            case LUA_NOREF:
            case LUA_REFNIL:
                break;

            default:
                return udata;
        }
    }

    lua_settop( SHM_STATE, 0 );

    return NULL;
}


int lpt_shm_retain( int ref )
{
    lauxh_pushref( SHM_STATE, ref );
    if( lua_type( SHM_STATE, -1 ) == LUA_TUSERDATA ){
        ref = lauxh_ref( SHM_STATE );
    }
    else {
        ref = LUA_NOREF;
    }
    lua_settop( SHM_STATE, 0 );

    return ref;
}


int lpt_shm_release( int ref )
{
    lauxh_unref( SHM_STATE, ref );
    return LUA_NOREF;
}


void *lpt_shm_get( int ref )
{
    void *udata = NULL;

    lauxh_pushref( SHM_STATE, ref );
    if( lua_type( SHM_STATE, -1 ) == LUA_TUSERDATA ){
        udata = lua_touserdata( SHM_STATE, -1 );
    }
    lua_settop( SHM_STATE, 0 );

    return udata;
}


void lpt_register_mt( lua_State *L, const char *tname, struct luaL_Reg mmethod[],
                      struct luaL_Reg method[] )
{
    // create metatable
    if( luaL_newmetatable( L, tname ) )
    {
        struct luaL_Reg *ptr = mmethod;

        // add metamethods
        while( ptr->name ){
            lauxh_pushfn2tbl( L, ptr->name, ptr->func );
            ptr++;
        }
        // create method
        lua_pushstring( L, "__index" );
        lua_newtable( L );
        ptr = method;
        // add methods
        while( ptr->name ){
            lauxh_pushfn2tbl( L, ptr->name, ptr->func );
            ptr++;
        }
        lua_rawset( L, -3 );
        lua_pop( L, 1 );
    }
}

