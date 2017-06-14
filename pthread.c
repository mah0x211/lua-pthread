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

#define MODULE_MT           "pthread"
#define DEFAULT_TIMEWAIT    1


typedef struct {
    pthread_t id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    lua_State *L;
    int running;
} thctx_t;


static void thctx_dealloc( thctx_t *ctx )
{
    if( ctx->L ){
        lua_close( ctx->L );
    }
}


static thctx_t *thctx_alloc( lua_State *L )
{
    thctx_t *ctx = lua_newuserdata( L, sizeof( thctx_t ) );

    // alloc
    if( ctx )
    {
        if( ( ctx->L = luaL_newstate() ) ){
            luaL_openlibs( ctx->L );
            pthread_mutex_init( &ctx->mutex, NULL );
            pthread_cond_init( &ctx->cond, NULL );
            ctx->running = 0;
            return ctx;
        }
        thctx_dealloc( ctx );
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


static void th_atexit( void *arg )
{
    thctx_t *ctx = (thctx_t*)arg;

    lua_close( ctx->L );
    ctx->L = NULL;
    ctx->running = 0;
    pthread_mutex_unlock( &ctx->mutex );
}


static void *th_start( void *arg )
{
    thctx_t *ctx = (thctx_t*)arg;

    pthread_mutex_lock( &ctx->mutex );
    ctx->running = 1;
    pthread_cleanup_push( th_atexit, ctx );
    pthread_cond_signal( &ctx->cond );
    pthread_cond_wait( &ctx->cond, &ctx->mutex );

    // run script in thread
    switch( lua_pcall( ctx->L, 0, 0, 0 ) ){
        case LUA_ERRRUN:
        case LUA_ERRMEM:
        case LUA_ERRERR:
            printf("got error: %s\n", lua_tostring( ctx->L, -1 ) );
        break;
    }
    pthread_cleanup_pop( 1 );

    pthread_exit( NULL );
}


static int kill_lua( lua_State *L )
{
    thctx_t *ctx = (thctx_t*)luaL_checkudata( L, 1, MODULE_MT );
    lua_Integer signo = lauxh_checkinteger( L, 2 );

    if( pthread_kill( ctx->id, signo ) == 0 ){
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
    thctx_t *ctx = (thctx_t*)lua_touserdata( L, 1 );

    pthread_mutex_lock( &ctx->mutex );
    if( ctx->running )
    {
        int rc = 0;

        pthread_cond_signal( &ctx->cond );
        pthread_mutex_unlock( &ctx->mutex );
        if( ( rc = pthread_join( ctx->id, NULL ) ) ){
            lua_pushboolean( L, 0 );
            lua_pushstring( L, strerror( rc ) );
            return 2;
        }
    }
    else {
        pthread_mutex_unlock( &ctx->mutex );
    }

    lua_pushboolean( L, 1 );

    return 1;
}


static int gc_lua( lua_State *L )
{
    thctx_t *ctx = (thctx_t*)luaL_checkudata( L, 1, MODULE_MT );

    pthread_mutex_lock( &ctx->mutex );
    if( ctx->running ){
        pthread_cond_signal( &ctx->cond );
        pthread_mutex_unlock( &ctx->mutex );
        pthread_join( ctx->id, NULL );
    }
    // already joined
    else {
        pthread_mutex_unlock( &ctx->mutex );
    }

    thctx_dealloc( ctx );

    return 0;
}


static int tostring_lua( lua_State *L )
{
    lua_pushfstring( L, MODULE_MT ": %p", lua_touserdata( L, 1 ) );
    return 1;
}



static int new_lua( lua_State *L )
{
    size_t len = 0;
    const char *fn = lauxh_checklstring( L, 1, &len );
    thctx_t *ctx = NULL;
    struct timespec ts = {
        .tv_sec = DEFAULT_TIMEWAIT,
        .tv_nsec = 0
    };
    int rc = 0;

    lua_settop( L, 1 );

    // mem-error
    if( !( ctx = thctx_alloc( L ) ) ){
        lua_pushnil( L );
        lua_pushstring( L, strerror( rc ) );
        return 2;
    }
    // compile error
    else if( ( rc = luaL_loadbuffer( ctx->L, fn, len, NULL ) ) ){
        lua_pushnil( L );
        lua_pushstring( L, lua_tostring( ctx->L, -1 ) );
        thctx_dealloc( ctx );
        return 2;
    }

    pthread_mutex_lock( &ctx->mutex );
    // create thread
    if( ( rc = pthread_create( &ctx->id, NULL, th_start, (void*)ctx ) ) ){
        pthread_mutex_unlock( &ctx->mutex );
        thctx_dealloc( ctx );
        lua_pushnil( L );
        lua_pushstring( L, strerror( rc ) );
        return 2;
    }

    // wait suspend
    ts = (struct timespec){
        .tv_sec = DEFAULT_TIMEWAIT,
        .tv_nsec = 0
    };
    if( ( rc = pthread_cond_timedwait( &ctx->cond, &ctx->mutex,
                                       addabstime( &ts ) ) ) ){
        pthread_mutex_unlock( &ctx->mutex );
        pthread_cancel( ctx->id );
        thctx_dealloc( ctx );
        lua_pushnil( L );
        lua_pushstring( L, strerror( rc ) );
        return 2;
    }

    lauxh_setmetatable( L, MODULE_MT );

    // resume thread
    pthread_cond_signal( &ctx->cond );
    pthread_mutex_unlock( &ctx->mutex );

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
    struct luaL_Reg *ptr = mmethod;

    // create metatable
    luaL_newmetatable( L, MODULE_MT );
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

    // add new function
    lua_newtable( L );
    lauxh_pushfn2tbl( L, "new", new_lua );

    return 1;
}

