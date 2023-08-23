/**
 * Copyright (C) 2023-present Masatoshi Fukunaga
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 **/

#ifndef queue_h
#define queue_h

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct queue_item_t {
    void *data;
    size_t size;
    struct queue_item_t *prev;
    struct queue_item_t *next;
} queue_item_t;

/**
 * @brief queue_delete_cb
 *  The callback function to be called when deleting each item.
 * @param data The data to be deleted.
 * @param arg The argument to be passed to the callback function.
 */
typedef void (*queue_delete_cb)(void *data, void *arg);

typedef struct queue_t {
    uintptr_t op;
    queue_delete_cb delete_cb;
    void *delete_cb_arg;
    ssize_t maxitem;
    ssize_t totalitem;
    int refcnt;

#define QUEUE_STATUS_READABLE 0x01
#define QUEUE_STATUS_WRITABLE 0x02
    // NOTE: status flag to indicate the queue is available to readable and
    // writable.
    // the QUEUE_STATUS_READABLE flag will be set when the queue is available to
    // readable.
    // the QUEUE_STATUS_WRITABLE flag will be set when the queue is available to
    // writable.
    int status;
    // NOTE: the following pipes are used to check whether the queue is readable
    // or writable by select(), poll(), epoll(), etc.
    int pipefd_readable[2];
    int pipefd_writable[2];
    queue_item_t *head;
    queue_item_t *tail;
} queue_t;

/**
 * @brief queue_new
 *  Create a new queue object.
 *  The queue object must be deleted by queue_delete().
 * @param maxitem The maximum number of items that can be stored in the queue.
 * <1 means 1.
 * @param cb The callback function to be called when deleting each item.
 * @param arg The argument to be passed to the callback function.
 * @return queue_t*
 */
queue_t *queue_new(ssize_t maxitem, queue_delete_cb cb, void *arg);

#define QUEUE_LOCK_ALREADY 0
#define QUEUE_LOCK_OK      1

/**
 * @brief queue_lock
 *  Lock the queue object with the specified operation ID.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @return int QUEUE_LOCK_OK on success, QUEUE_LOCK_ALREADY if the queue object
 * is already locked.
 */
int queue_lock(queue_t *queue, uintptr_t op);

/**
 * @brief queue_unlock
 *  Unlock the queue object with the specified operation ID. If the operation ID
 * is not matched, assert() is called and the program is terminated.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @param abort_on_fail if abort_on_fail is 1, abort() is called when the unlock
 * fails.
 * @return int 1 on success, otherwise failure.
 */
int queue_unlock(queue_t *q, uintptr_t op, int abort_on_fail);

/**
 * @brief queue_ref
 *  Increment the reference count of the queue object.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 */
void queue_ref(queue_t *queue, uintptr_t op);

/**
 * @brief queue_unref
 *  Decrement the reference count of the queue object.
 *  If the reference count becomes <1, the queue object is deleted.
 *  You must call this function when you no longer need the queue object.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 */
void queue_unref(queue_t *queue, uintptr_t op);

/**
 * @brief queue_nref
 *  Get the reference count of the queue object.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @return int The reference count of the queue object, <0 means error and errno
 * is set to indicate the error.
 */
int queue_nref(queue_t *queue, uintptr_t op);

/**
 * @brief queue_maxitem
 *  Get the maximum number of items that can be stored in the queue.
 * @param q The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @return ssize_t The maximum number of items that can be stored in the queue,
 */
ssize_t queue_maxitem(queue_t *q, uintptr_t op);

/**
 * @brief queue_len
 *  Get the number of items in the queue.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @return ssize_t The number of items in the queue
 */
ssize_t queue_len(queue_t *queue, uintptr_t op);

/**
 * @brief queue_size
 *  Get the used memory size of the queue.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @return size_t The used memory size of the queue
 */
ssize_t queue_size(queue_t *queue, uintptr_t op);

/**
 * @brief queue_fd_readable
 *  Get a file descriptor to wait for the queue to become available for reading.
 *  You can use this file descriptor with select(), poll(), epoll(), etc.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @return int The file descriptor
 */
int queue_fd_readable(queue_t *queue, uintptr_t op);

/**
 * @brief queue_fd_writable
 *  Get a file descriptor to wait for the queue to become available for writing.
 *  You can use this file descriptor with select(), poll(), epoll(), etc.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @return int The file descriptor
 */
int queue_fd_writable(queue_t *queue, uintptr_t op);

/**
 * @brief queue_push
 *  Push an item to the tail of the queue.
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @param data The data to be stored in the queue.
 * @param size The size of the data.
 * @return int 1 on success, 0 if the queue is full, otherwise errno is set to
 * indicate the error.
 */
int queue_push(queue_t *queue, uintptr_t op, void *data, size_t size);

/**
 * @brief queue_pop
 *  Pop an item from the head of the queue.
 *  If the queue is empty, NULL is returned.
 *  You can wait for the queue to become available for reading by using the file
 * descriptor returned by queue_fd().
 * @param queue The queue object.
 * @param op The operation ID to be performed on the queue object.
 * @param data The data to be returned.
 * @return int 0 on success, otherwise errno is set to indicate the error.
 */
int queue_pop(queue_t *queue, uintptr_t op, void **data);

// /**
//  * @brief queue_head
//  *  Peek an item from the head of the queue.
//  *  If the queue is empty, NULL is returned.
//  * @param queue The queue object.
//  * @return queue_item_t*
//  */
// queue_item_t *queue_head(queue_t *queue);

// /**
//  * @brief queue_tail
//  *  Peek an item from the tail of the queue.
//  *  If the queue is empty, NULL is returned.
//  * @param queue The queue object.
//  * @return queue_item_t*
//  */
// queue_item_t *queue_tail(queue_t *queue);

// /**
//  * @brief queue_item_delete
//  *  Delete an item from the queue.
//  * @param queue The queue object.
//  * @param item The item to be deleted.
//  * @param size The size of the data. If NULL, the size is not returned.
//  * @return void* The data.
//  */
// void *queue_item_delete(queue_t *queue, queue_item_t *item, size_t *size);

#endif
