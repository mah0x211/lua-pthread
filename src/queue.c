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

#include "queue.h"
#include <fcntl.h>
#include <stdio.h>

queue_t *queue_new(ssize_t maxitem, ssize_t maxsize, queue_delete_cb cb,
                   void *arg)
{
    queue_t *q = (queue_t *)calloc(1, sizeof(queue_t));

    if (q == NULL) {
        return NULL;
    }

    // create pipe
    if (pipe(q->pipefd) != 0) {
        free(q);
        return NULL;
    }
    // set o_cloexec and o_nonblock flags
    if (fcntl(q->pipefd[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(q->pipefd[0], F_SETFL, O_NONBLOCK) != 0 ||
        fcntl(q->pipefd[1], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(q->pipefd[1], F_SETFL, O_NONBLOCK) != 0) {
        close(q->pipefd[0]);
        close(q->pipefd[1]);
        free(q);
        return NULL;
    }

    if (maxitem < 0) {
        maxitem = 0;
    }
    if (maxsize < 0) {
        maxsize = 0;
    }

    q->delete_cb     = cb;
    q->delete_cb_arg = arg;
    q->maxitem       = (size_t)maxitem;
    q->totalitem     = 0;
    q->maxsize       = (size_t)maxsize;
    q->totalsize     = 0;
    q->head = q->tail = NULL;
    q->refcnt         = 1;
    pthread_mutex_init(&q->mutex, NULL);

    return q;
}

int queue_ref(queue_t *q)
{
    if ((errno = pthread_mutex_lock(&q->mutex))) {
        return -1;
    }
    q->refcnt++;
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int queue_unref(queue_t *q)
{
    if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
        return -1;
    }

    // decrement reference count
    if (q->refcnt > 1) {
        // someone is using this queue
        q->refcnt--;
        pthread_mutex_unlock(&q->mutex);
        return 0;
    }

    // delete all items
    while (q->head) {
        void *data         = q->head->data;
        queue_item_t *next = q->head->next;

        if (q->delete_cb) {
            q->delete_cb(data, q->delete_cb_arg);
        }
        free(q->head);
        q->head = next;
    }

    // close pipe
    close(q->pipefd[0]);
    close(q->pipefd[1]);

    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
    free(q);
    return 0;
}

int queue_nref(queue_t *q)
{
    if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
        return -1;
    }
    int refcnt = q->refcnt;
    pthread_mutex_unlock(&q->mutex);
    return refcnt;
}

ssize_t queue_len(queue_t *q)
{
    if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
        return -1;
    }
    ssize_t len = q->totalitem;
    pthread_mutex_unlock(&q->mutex);
    return len;
}

ssize_t queue_size(queue_t *q)
{
    if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
        return -1;
    }
    ssize_t size = q->totalsize;
    pthread_mutex_unlock(&q->mutex);
    return size;
}

int queue_fd(queue_t *q)
{
    if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
        return -1;
    }
    int fd = q->pipefd[0];
    pthread_mutex_unlock(&q->mutex);
    return fd;
}

int queue_push(queue_t *q, void *data, size_t size)
{
    if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
        return -1;
    }

    if ((q->maxitem > 0 && q->totalitem >= q->maxitem) ||
        (q->maxsize > 0 &&
         (size > q->maxsize || sizeof(queue_item_t) > q->maxsize - size ||
          q->totalsize > q->maxsize - size - sizeof(queue_item_t)))) {
        errno = ENOBUFS;
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    queue_item_t *item = (queue_item_t *)calloc(1, sizeof(queue_item_t));
    if (item == NULL) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    item->data = data;
    item->size = size;
    q->totalitem++;
    q->totalsize += size + sizeof(queue_item_t);

    if (q->tail) {
        item->prev    = q->tail;
        q->tail->next = item;
        q->tail       = item;
    } else {
        q->head = q->tail = item;
    }

    // notify reader when the first item is pushed
    if (q->totalitem == 1) {
        int retry = 0;

RETRY:
        if (write(q->pipefd[1], "0", 1) == -1 && errno != EAGAIN &&
            errno != EWOULDBLOCK) {
            if (retry == 0 && errno == EINTR) {
                retry++;
                goto RETRY;
            }
            pthread_mutex_unlock(&q->mutex);
            return -1;
        }
    }

    pthread_mutex_unlock(&q->mutex);
    return 0;
}

static void *remove_head(queue_t *q, size_t *size)
{
    queue_item_t *item = q->head;
    char *data         = item->data;

    if (size) {
        *size = item->size;
    }
    q->totalitem--;
    q->totalsize -= item->size + sizeof(queue_item_t);

    q->head = item->next;
    if (q->head) {
        q->head->prev = NULL;
    } else {
        q->tail = NULL;
    }
    free(item);

    return data;
}

int queue_pop(queue_t *q, void **data, size_t *size)
{
    if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
        return -1;
    }
    while (q->head == NULL) {
        pthread_mutex_unlock(&q->mutex);
        return 0;
    }

    *data = remove_head(q, size);

    // consume notification when the last item is popped
    static char buf[1];
    if (q->totalitem == 0) {
        int retry = 0;

RETRY:
        if (read(q->pipefd[0], buf, sizeof(buf)) == -1 && errno != EAGAIN &&
            errno != EWOULDBLOCK) {
            if (retry == 0 && errno == EINTR) {
                retry++;
                goto RETRY;
            }
            // failed to read from pipe
            pthread_mutex_unlock(&q->mutex);
            return -1;
        }
    }

    pthread_mutex_unlock(&q->mutex);

    return 0;
}

// queue_item_t *queue_head(queue_t *q)
// {
//     if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
//         return NULL;
//     }
//     queue_item_t *item = q->head;
//     pthread_mutex_unlock(&q->mutex);
//     return item;
// }

// queue_item_t *queue_tail(queue_t *q)
// {
//     if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
//         return NULL;
//     }
//     queue_item_t *item = q->tail;
//     pthread_mutex_unlock(&q->mutex);
//     return item;
// }

// static void *remove_tail(queue_t *q, size_t *size)
// {
//     queue_item_t *item = q->tail;
//     char *data         = item->data;

//     if (size) {
//         *size = item->size;
//     }
//     q->totalitem--;
//     q->totalsize -= item->size + sizeof(queue_item_t);

//     q->tail = item->prev;
//     if (q->tail) {
//         q->tail->next = NULL;
//     } else {
//         q->head = NULL;
//     }
//     free(item);

//     return data;
// }

// void *queue_item_delete(queue_t *q, queue_item_t *item, size_t *size)
// {
//     if ((errno = pthread_mutex_lock(&q->mutex)) != 0) {
//         return NULL;
//     }

//     void *data = NULL;
//     if (item == q->head) {
//         data = remove_head(q, size);
//     } else if (item == q->tail) {
//         data = remove_tail(q, size);
//     } else {
//         data = item->data;
//         if (size) {
//             *size = item->size;
//         }
//         item->prev->next = item->next;
//         item->next->prev = item->prev;
//         q->totalitem--;
//         q->totalsize -= item->size + sizeof(queue_item_t);
//         free(item);
//     }
//     pthread_mutex_unlock(&q->mutex);

//     return data;
// }
