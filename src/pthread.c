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
#define DEFAULT_TIMEWAIT    1


typedef struct {
    pthread_t id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    lua_State *L;
    int running;
} lpthread_t;


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
        if( ( th->L = luaL_newstate() ) ){
            luaL_openlibs( th->L );
            pthread_mutex_init( &th->mutex, NULL );
            pthread_cond_init( &th->cond, NULL );
            th->running = 0;
            return th;
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
    switch( lua_pcall( th->L, 0, 0, 0 ) ){
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
    lpthread_t *th = (lpthread_t*)lua_touserdata( L, 1 );

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

