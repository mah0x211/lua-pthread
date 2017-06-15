/*
 *  Copyright (C) 2014 Masatoshi Teruya
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
 *  pthread.c
 *  lua-pthread
 *  Created by Masatoshi Teruya on 14/09/12.
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>
#include <lua.h>
#include <lauxlib.h>
#include "lauxhlib.h"

#define LPTHREAD_MT         "pthread"
#define LPTHREAD_MBOX_MT    "pthread.mbox"
#define DEFAULT_TIMEWAIT    1


typedef struct {
    pthread_t id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_t co_mu;
    lua_State *L;
    lua_State *co;
    int running;
} lpthread_t;


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


static void register_mt( lua_State *L, const char *tname,
                         struct luaL_Reg mmethod[], struct luaL_Reg method[] )
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


static int recv_mbox_lua( lua_State *L )
{
    lpthread_t *th = *(lpthread_t**)luaL_checkudata( L, 1, LPTHREAD_MBOX_MT );
    lua_State *co = th->co;
    int narg = 0;

    pthread_mutex_lock( &th->co_mu );
    narg = lua_gettop( co );
    if( narg > 0 ){
        lua_xmove( co, L, narg );
    }
    pthread_mutex_unlock( &th->co_mu );

    return narg;
}


static int tostring_mbox_lua( lua_State *L )
{
    lua_pushfstring( L, LPTHREAD_MBOX_MT ": %p", lua_topointer( L, 1 ) );
    return 1;
}


static void lpthread_dealloc( lpthread_t *th )
{
    if( th->L ){
        lua_close( th->L );
    }
}


static lpthread_t *lpthread_alloc( lua_State *L )
{
    lpthread_t *th = lua_newuserdata( L, sizeof( lpthread_t ) );

    // alloc
    if( th )
    {
        if( ( th->L = luaL_newstate() ) )
        {
            static struct luaL_Reg mmethod[] = {
                { "__tostring", tostring_mbox_lua },
                { NULL, NULL }
            };
            static struct luaL_Reg method[] = {
                { "recv", recv_mbox_lua },
                { NULL, NULL }
            };

            luaL_openlibs( th->L );
            register_mt( th->L, LPTHREAD_MBOX_MT, mmethod, method );
            // create coroutine as a mbox
            if( ( th->co = lua_newthread( th->L ) ) ){
                // retain mbox
                lauxh_ref( th->L );
                pthread_mutex_init( &th->mutex, NULL );
                pthread_cond_init( &th->cond, NULL );
                pthread_mutex_init( &th->co_mu, NULL );
                th->running = 0;
                return th;
            }
        }
        lpthread_dealloc( th );
    }

    return NULL;
}


static inline struct timespec *addabstime( struct timespec *ts )
{
    struct timeval tv = { 0 };

    gettimeofday( &tv, NULL );
    ts->tv_sec += tv.tv_sec;
    ts->tv_nsec += tv.tv_usec * 1000;

    return ts;
}


static void lpthread_atexit( void *arg )
{
    lpthread_t *th = (lpthread_t*)arg;

    lua_close( th->L );
    th->L = NULL;
    th->running = 0;
    pthread_mutex_unlock( &th->mutex );
}


static void *lpthread_start( void *arg )
{
    lpthread_t *th = (lpthread_t*)arg;

    pthread_mutex_lock( &th->mutex );
    th->running = 1;
    pthread_cleanup_push( lpthread_atexit, th );
    pthread_cond_signal( &th->cond );
    pthread_cond_wait( &th->cond, &th->mutex );

    // run script in thread
    switch( lua_pcall( th->L, 1, 0, 0 ) ){
        case LUA_ERRRUN:
        case LUA_ERRMEM:
        case LUA_ERRERR:
            printf("got error: %s\n", lua_tostring( th->L, -1 ) );
            break;
    }
    pthread_cleanup_pop( 1 );

    pthread_exit( NULL );
}


static int kill_lua( lua_State *L )
{
    lpthread_t *th = (lpthread_t*)luaL_checkudata( L, 1, LPTHREAD_MT );
    lua_Integer signo = lauxh_checkinteger( L, 2 );

    if( pthread_kill( th->id, signo ) == 0 ){
        lua_pushboolean( L, 1 );
        return 1;
    }

    // got error
    lua_pushboolean( L, 0 );
    lua_pushstring( L, strerror( errno ) );

    return 2;
}


static int join_lua( lua_State *L )
{
    lpthread_t *th = (lpthread_t*)luaL_checkudata( L, 1, LPTHREAD_MT );

    pthread_mutex_lock( &th->mutex );
    if( th->running )
    {
        int rc = 0;

        pthread_cond_signal( &th->cond );
        pthread_mutex_unlock( &th->mutex );
        if( ( rc = pthread_join( th->id, NULL ) ) ){
            lua_pushboolean( L, 0 );
            lua_pushstring( L, strerror( rc ) );
            return 2;
        }
    }
    else {
        pthread_mutex_unlock( &th->mutex );
    }

    lua_pushboolean( L, 1 );

    return 1;
}


static int send_lua( lua_State *L )
{
    int narg = lua_gettop( L );
    lpthread_t *th = (lpthread_t*)luaL_checkudata( L, 1, LPTHREAD_MT );
    lua_State *mbox = th->co;

    pthread_mutex_lock( &th->co_mu );
    if( narg > 1 )
    {
        int idx = 2;

        for(; idx <= narg; idx++ ){
            copy2mbox( L, mbox, idx );
        }
    }
    pthread_mutex_unlock( &th->co_mu );

    return 0;
}


static int gc_lua( lua_State *L )
{
    lpthread_t *th = (lpthread_t*)luaL_checkudata( L, 1, LPTHREAD_MT );

    pthread_mutex_lock( &th->mutex );
    if( th->running ){
        pthread_cond_signal( &th->cond );
        pthread_mutex_unlock( &th->mutex );
        pthread_join( th->id, NULL );
    }
    // already joined
    else {
        pthread_mutex_unlock( &th->mutex );
    }

    lpthread_dealloc( th );

    return 0;
}


static int tostring_lua( lua_State *L )
{
    lua_pushfstring( L, LPTHREAD_MT ": %p", lua_touserdata( L, 1 ) );
    return 1;
}


static int new_lua( lua_State *L )
{
    size_t len = 0;
    const char *fn = lauxh_checklstring( L, 1, &len );
    lpthread_t *th = NULL;
    lpthread_t **mbox = NULL;
    struct timespec ts = {
        .tv_sec = DEFAULT_TIMEWAIT,
        .tv_nsec = 0
    };
    int rc = 0;

    lua_settop( L, 1 );

    // mem-error
    if( !( th = lpthread_alloc( L ) ) ){
        lua_pushnil( L );
        lua_pushstring( L, strerror( rc ) );
        return 2;
    }
    // compile error
    else if( ( rc = luaL_loadbuffer( th->L, fn, len, NULL ) ) ){
        lua_pushnil( L );
        lua_pushstring( L, lua_tostring( th->L, -1 ) );
        lpthread_dealloc( th );
        return 2;
    }
    else if( !( mbox = lua_newuserdata( th->L, sizeof( lpthread_t* ) ) ) ){
        lua_pushnil( L );
        lua_pushstring( L, strerror( errno ) );
        lpthread_dealloc( th );
        return 2;
    }
    *mbox = th;
    lauxh_setmetatable( th->L, LPTHREAD_MBOX_MT );

    pthread_mutex_lock( &th->mutex );
    // create thread
    if( ( rc = pthread_create( &th->id, NULL, lpthread_start, (void*)th ) ) ){
        pthread_mutex_unlock( &th->mutex );
        lpthread_dealloc( th );
        lua_pushnil( L );
        lua_pushstring( L, strerror( rc ) );
        return 2;
    }

    // wait suspend
    ts = (struct timespec){
        .tv_sec = DEFAULT_TIMEWAIT,
        .tv_nsec = 0
    };
    if( ( rc = pthread_cond_timedwait( &th->cond, &th->mutex,
                                       addabstime( &ts ) ) ) ){
        pthread_mutex_unlock( &th->mutex );
        pthread_cancel( th->id );
        lpthread_dealloc( th );
        lua_pushnil( L );
        lua_pushstring( L, strerror( rc ) );
        return 2;
    }

    lauxh_setmetatable( L, LPTHREAD_MT );

    // resume thread
    pthread_cond_signal( &th->cond );
    pthread_mutex_unlock( &th->mutex );

    return 1;
}


LUALIB_API int luaopen_pthread( lua_State *L )
{
    struct luaL_Reg mmethod[] = {
        { "__gc", gc_lua },
        { "__tostring", tostring_lua },
        { NULL, NULL }
    };
    struct luaL_Reg method[] = {
        { "send", send_lua },
        { "join", join_lua },
        { "kill", kill_lua },
        { NULL, NULL }
    };

    // register metatable
    register_mt( L, LPTHREAD_MT, mmethod, method );
    // add new function
    lua_newtable( L );
    lauxh_pushfn2tbl( L, "new", new_lua );

    return 1;
}

