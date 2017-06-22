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


typedef struct lpt_mbox_elm_st {
    int ref;
    int ref_co;
    lua_State *co;
    struct lpt_mbox_elm_st *prev;
    struct lpt_mbox_elm_st *next;
} lpt_que_elm_t;


typedef struct {
    lua_State *L;
    size_t len;
    lpt_que_elm_t *head;
    lpt_que_elm_t *tail;
} lpt_que_t;


static void que_pop( lpt_que_t *que )
{
    if( que->len )
    {
        lpt_que_elm_t *elm = que->tail;

        que->len--;
        if( que->len ){
            que->tail = elm->prev;
        }
        else {
            que->head = que->tail = NULL;
        }

        // release reference
        lauxh_unref( que->L, elm->ref_co );
        lauxh_unref( que->L, elm->ref );
    }
}


static lpt_que_elm_t *que_deq( lpt_que_t *que )
{
    if( que->len )
    {
        lpt_que_elm_t *elm = que->head;

        que->len--;
        que->head = elm->next;
        if( que->head ){
            que->head->prev = NULL;
        }
        else {
            que->tail = NULL;
        }
        elm->prev = elm->next = NULL;

        // release reference
        lauxh_unref( que->L, elm->ref_co );
        lauxh_unref( que->L, elm->ref );

        return elm;
    }

    return NULL;
}


static lpt_que_elm_t *que_enq( lpt_que_t *que )
{
    lpt_que_elm_t *elm = lua_newuserdata( que->L, sizeof( lpt_que_elm_t ) );

    if( elm )
    {
        if( ( elm->co = lua_newthread( que->L ) ) )
        {
            elm->next = NULL;
            elm->ref_co = lauxh_ref( que->L );
            elm->ref = lauxh_ref( que->L );

            que->len++;
            if( que->tail ){
                elm->prev = que->tail;
                que->tail->next = elm;
                que->tail = elm;
            }
            else {
                elm->prev = NULL;
                que->head = que->tail = elm;
            }

            return elm;
        }

        lua_pop( que->L, 1 );
    }

    return NULL;
}


static lpt_que_t *que_alloc( lua_State *L )
{
    lpt_que_t *que = lua_newuserdata( L, sizeof( lpt_que_t ) );

    if( que )
    {
        *que = (lpt_que_t){
            .L = L,
            .len = 0,
            .head = NULL,
            .tail = NULL
        };
    }

    return que;
}


static int copy2mbox( lua_State *L, lua_State *mbox, int idx, int allow_nil );

static int tbl2mbox( lua_State *L, lua_State *mbox, int idx )
{
    lua_newtable( mbox );
    lua_pushnil( L );

    while( lua_next( L, idx ) )
    {
        if( copy2mbox( L, mbox, idx + 1, 0 ) != LUA_TNONE )
        {
            if( copy2mbox( L, mbox, idx + 2, 0 ) != LUA_TNONE ){
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
    lpt_mbox_t *mbox = lua_touserdata( L, 1 );
    lpt_que_t *que = NULL;
    lpt_que_elm_t *elm = NULL;
    int narg = 0;

    pthread_mutex_lock( &mbox->data->mutex );
    luaL_checkudata( L, 1, MODULE_MT );

    que = lua_touserdata( mbox->data->inbox, 1 );
    if( ( elm = que_deq( que ) ) ){
        narg = lua_gettop( elm->co );
        lua_xmove( elm->co, L, narg );
    }
    pthread_mutex_unlock( &mbox->data->mutex );

    return narg;
}


static int send_lua( lua_State *L )
{
    lpt_mbox_t *mbox = lua_touserdata( L, 1 );
    lpt_mbox_data_t *outbox = NULL;
    int narg = 0;
    int ok = 0;

    pthread_mutex_lock( &mbox->data->mutex );
    luaL_checkudata( L, 1, MODULE_MT );
    narg = lua_gettop( L );

    if( narg > 1 && ( outbox = lpt_shm_get( mbox->data->peer ) ) )
    {
        pthread_mutex_lock( &outbox->mutex );
        if( outbox->self != LUA_NOREF )
        {
            lpt_que_t *que = lua_touserdata( outbox->inbox, 1 );
            lpt_que_elm_t *elm = que_enq( que );

            if( elm )
            {
                int xarg = 0;
                int idx = 2;

                for(; idx <= narg; idx++ ){
                    // copy arguments to queue element
                    copy2mbox( L, elm->co, idx, 1 );
                }

                ok = lua_gettop( elm->co );
                if( !ok ){
                    que_pop( que );
                }
            }
        }

        pthread_mutex_unlock( &outbox->mutex );
    }

    lua_pushboolean( L, ok );
    pthread_mutex_unlock( &mbox->data->mutex );

    return 1;
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

        if( mbox && ( data->inbox = lua_newthread( L ) ) &&
            que_alloc( data->inbox ) )
        {
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

