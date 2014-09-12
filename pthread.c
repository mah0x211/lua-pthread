//
//  pthread.c
//  lua-pthread
//
//  Created by Masatoshi Teruya on 2014/09/12.
//  Copyright (c) 2014 Masatoshi Teruya. All rights reserved.
//

#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <lua.h>
#include <lauxlib.h>


// helper macros
#define lstate_setmetatable(L,tname) do { \
    luaL_getmetatable( L, tname ); \
    lua_setmetatable( L, -2 ); \
}while(0)

#define lstate_ref(L) \
    (luaL_ref( L, LUA_REGISTRYINDEX ))

#define lstate_unref(L,ref) \
    luaL_unref( L, LUA_REGISTRYINDEX, (ref) )

#define lstate_fn2tbl(L,k,v) do{ \
    lua_pushstring(L,k); \
    lua_pushcfunction(L,v); \
    lua_rawset(L,-3); \
}while(0)


#define MODULE_MT   "pthread"

#define DEFAULT_TIMEWAIT    1


static pthread_mutex_t CREATION_MUTEX = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t CREATION_COND = PTHREAD_COND_INITIALIZER;


typedef struct {
    lua_State *L;
    int ref;
    pthread_mutex_t mutex;
} lpthread_task_t;


typedef struct {
    lua_State *L;
    pthread_t id;
    lpthread_task_t task;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    uint8_t join;
} lpthread_t;


#define lpth_lock(th)       pthread_mutex_lock( &th->mutex )
#define lpth_task_lock(th)  pthread_mutex_lock( &th->task.mutex )

#define lpth_unlock(th)         pthread_mutex_unlock( &th->mutex )
#define lpth_task_unlock(th)    pthread_mutex_unlock( &th->task.mutex )


static int create_co( lua_State *L, lpthread_t *th )
{
    if( ( th->task.L = lua_newthread( L ) ) ){
        // retain coroutine
        th->task.ref = lstate_ref( L );
        pthread_mutex_init( &th->mutex, NULL );
        pthread_cond_init( &th->cond, NULL );
        pthread_mutex_init( &th->task.mutex, NULL );
        return 0;
    }

    return -1;
}


static void remove_co( lua_State *L, lpthread_t *th ){
    lstate_unref( L, th->task.ref );
    th->task.ref = LUA_REFNIL;
}


static void *cothread( void *arg )
{
    lpthread_t *th = (lpthread_t*)arg;
    lua_State *L = lua_newthread( th->L );
    int ref = lstate_ref( th->L );
    lua_State *task = th->task.L;

    th->join = 0;
    // wakeup parent
    pthread_cond_signal( &CREATION_COND );

    lpth_lock( th );

WAIT_COTHREAD:
    pthread_cond_wait( &th->cond, &th->mutex );
    if( !th->join )
    {
        lua_State *co = NULL;
        lua_State *eco = NULL;
        int argc = 0;

CHECK_TASK:
        lpth_task_lock( th );
        // get task
        if( lua_gettop( task ) )
        {
            lua_pushvalue( task, 1 );
            lua_xmove( task, L, 1 );
            lua_remove( task, 1 );
            co = lua_tothread( L, -1 );
            argc = lua_gettop( co );
            // create error thread
            if( argc > 1 && lua_type( co, -1 ) == LUA_TFUNCTION )
            {
                if( ( eco = lua_newthread( L ) ) )
                {
                    int i = 2;

                    lua_xmove( co, eco, 1 );
                    for(; i < argc; i++ ){
                        lua_pushvalue( co, i );
                        lua_xmove( co, eco, 1 );
                    }
                }
            }
        }
        lpth_task_unlock( th );

        if( co )
        {
            switch( lua_resume( co, lua_gettop( co ) - 1 ) )
            {
                case LUA_YIELD:
                case LUA_ERRRUN:
                case LUA_ERRSYNTAX:
                case LUA_ERRMEM:
                case LUA_ERRERR:
                    if( eco ){
                        lua_xmove( co, eco, 1 );
                        lua_insert( eco, 2 );
                        lua_resume( eco, argc - 1 );
                    }
                break;
            }

            // remove coroutine
            lua_settop( L, 0 );
            if( !th->join ){
                co = eco = NULL;
                goto CHECK_TASK;
            }
        }
        else if( !th->join ){
            goto WAIT_COTHREAD;
        }
    }

    lpth_unlock( th );
    lstate_unref( th->L, ref );

    return NULL;
}


static int call_lua( lua_State *L )
{
    const int argc = lua_gettop( L );
    lpthread_t *th = (lpthread_t*)luaL_checkudata( L, 1, MODULE_MT );
    // create coroutine
    lua_State *co = NULL;

    // invalid argument
    if( lua_type( L, 2 ) != LUA_TFUNCTION ){
        lua_pushboolean( L, 0 );
        lua_pushstring( L, strerror( EINVAL ) );
        return 2;
    }

    // lock
    lpth_task_lock( th );
    // create coroutine
    if( ( co = lua_newthread( th->task.L ) ) ){
        lua_xmove( L, co, argc - 1 );
        pthread_cond_signal( &th->cond );
        lpth_task_unlock( th );
        lua_pushboolean( L, 1 );
        return 1;
    }
    // unlock
    lpth_task_unlock( th );

    // mem error
    lua_pushboolean( L, 0 );
    lua_pushstring( L, strerror( errno ) );
    
    return 2;
}


static int gc_lua( lua_State *L )
{
    lpthread_t *th = (lpthread_t*)lua_touserdata( L, 1 );

    if( th->join == 0 ){
        lpth_lock( th );
        th->join = 1;
        lpth_unlock( th );
        pthread_cond_signal( &th->cond );
        pthread_join( th->id, NULL );
        remove_co( th->L, th );
    }
    
    return 0;
}


static int tostring_lua( lua_State *L )
{
    lpthread_t *th = (lpthread_t*)lua_touserdata( L, 1 );
    
    lua_pushfstring( L, "<" MODULE_MT " %p>", th );
    
    return 1;
}


static int alloc_lua( lua_State *L )
{
    const int argc = lua_gettop( L );
    lpthread_t *th = NULL;
    lua_Integer sec = DEFAULT_TIMEWAIT;
    const char *errstr = NULL;

    // check cond timedwait
    if( argc && !lua_isnoneornil( L, 1 ) )
    {
        sec = luaL_checkinteger( L, 1 );
        if( sec < DEFAULT_TIMEWAIT ){
            sec = DEFAULT_TIMEWAIT;
        }
    }

    // create userdata and coroutine
    if( ( th = lua_newuserdata( L, sizeof( lpthread_t ) ) ) &&
        create_co( L, th ) == 0 )
    {
        struct timespec ts = {
            .tv_sec = time(NULL) + (time_t)sec,
            .tv_nsec = 0
        };
        int err = 0;
        pthread_t id = NULL;

        th->L = L;
        // for wait thread start
        pthread_mutex_lock( &CREATION_MUTEX );
        if( ( err = pthread_create( &id, NULL, cothread, (void*)th ) ) == 0 )
        {
            if( pthread_cond_timedwait( &CREATION_COND, &CREATION_MUTEX,
                                        &ts ) == 0 ){
                pthread_mutex_unlock( &CREATION_MUTEX );
                th->id = id;
                lstate_setmetatable( L, MODULE_MT );
                return 1;
            }
            errstr = strerror( errno );
            pthread_kill( id, SIGKILL );
        }
        else {
            errstr = strerror( err );
        }
        pthread_mutex_lock( &CREATION_MUTEX );
        remove_co( L, th );
    }
    else {
        errstr = strerror( errno );
    }

    // got error
    lua_pushnil( L );
    lua_pushstring( L, errstr );
    
    return 2;
}


LUALIB_API int luaopen_pthread( lua_State *L )
{
    struct luaL_Reg method_mt[] = {
        { "__gc", gc_lua },
        { "__tostring", tostring_lua },
        { "__call", call_lua },
        { NULL, NULL }
    };
    struct luaL_Reg *ptr = method_mt;

    // create metatable
    luaL_newmetatable( L, MODULE_MT );
    // add metamethods
    while( ptr->name ){
        lstate_fn2tbl( L, ptr->name, ptr->func );
        ptr++;
    }
    lua_pop( L, 1 );
    
    // add allocator
    lua_pushcfunction( L, alloc_lua );

    return 1;
}

