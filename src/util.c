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


#define MODULE_MT   "pthread.weakref"

static int WEAKREF;

void lpt_weakref_init( lua_State *L )
{
    luaL_newmetatable( L, MODULE_MT );
    lauxh_pushstr2tbl( L, "__mode", "kv" );
    lua_pop( L, 1 );

    // create weak reference table
    lua_newtable( L );
    lauxh_setmetatable( L, MODULE_MT );
    WEAKREF = lauxh_ref( L );
}


void lpt_weakref_set( lua_State *L, int idx )
{
    lauxh_pushref( L, WEAKREF );
    lua_pushvalue( L, idx );
    lua_pushboolean( L, 1 );
    lua_rawset( L, -3 );
    lua_pop( L, 1 );
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

