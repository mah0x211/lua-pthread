/*
 *  Copyright (C) 2017 Masatoshi Teruya
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 *  lpt.h
 *  lua-pthread
 *  Created by Masatoshi Teruya on 17/06/16.
 */


#ifndef lpt_h
#define lpt_h

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


void lpt_register_mt( lua_State *L, const char *tname, struct luaL_Reg mmethod[],
                      struct luaL_Reg method[] );

// shared memory
int lpt_shm_init( void );
void *lpt_shm_create( size_t bytes, int *ref );
int lpt_shm_retain( int ref );
int lpt_shm_release( int ref );
void *lpt_shm_get( int ref );


// inter-thread communication mechanism
typedef struct {
    int self;
    int peer;
    lua_State *inbox;
    int ref_inbox;
    pthread_mutex_t mutex;
} lpt_mbox_data_t;

typedef struct {
    lpt_mbox_data_t *data;
} lpt_mbox_t;


void lpt_mbox_init( lua_State *L );
lpt_mbox_t *lpt_mbox_alloc( lua_State *L, lpt_mbox_t *outbox );


#endif
