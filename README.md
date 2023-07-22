# lua-pthread

[![test](https://github.com/mah0x211/lua-pthread/actions/workflows/test.yml/badge.svg)](https://github.com/mah0x211/lua-pthread/actions/workflows/test.yml)
[![codecov](https://codecov.io/gh/mah0x211/lua-pthread/branch/master/graph/badge.svg)](https://codecov.io/gh/mah0x211/lua-pthread)

pthread module.


## Installation

```
luarocks install pthread
```


---


## Error Handling

the functions/methods are return the error object created by https://github.com/mah0x211/lua-errno module.


## th, err, again = pthread.new( src [, ch, ...] )

executes a `src` script on a new posix thread and returns a `pthread` object. also, the script is passed a `pthread.self` object.

**Parameters**

- `src:string`: the script source to run on the created thread.
- `ch:pthread.channel`: `pthread.channel` arguments to pass to the script.

**Returns**

- `th:pthread`: `pthread` object.
- `err:any`: error object.
- `again:boolean`: `true` if `pthread_create` return `EAGAIN` error.
- `errno:number`: error number.

**NOTE**

When a `pthread` object is garbage collected, the thread is automatically canceled, as in the following code.

```c
if(pthread_cancel(tid) == 0){
    pthread_join(tid);
}
```

**Example**

```lua
local pthread = require('pthread')

-- create thread
local th, err, again = pthread.new([[
    print('arguments', ...) -- arguments pthread.self: 0x7fdc7a426c88
]])
if err then
    print(err)
    return
elseif again then
    print('too many threads are running')
    return
end

-- wait for thread termination
local ok, err, again = th:join()
while again do
    ok, err, again = th:join()
end
assert(ok, err)
print('done')
```


## th, err, again = pthread.new_with_file( filename [, ch, ...] )

executes a `filename` script on a new posix thread and returns a `pthread` object. also, the script is passed a `pthread.self` object.

**Parameters**

- `filename:string`: the script filename to run on the created thread.
- `ch:pthread.channel`: `pthread.channel` arguments to pass to the script.

**Returns**

same as `pthread.new` function.


## ok, err, again = pthread:join()

wait for thread termination.

**Returns**

- `ok:boolean`: `true` success or if already joined.
- `err:any`: error object.
- `again:boolean`: `true` if the thread has not yet terminated.


## ok, err = pthread:cancel()

cancel execution of a thread.

**Returns**

- `ok:boolean`: `true` on success.
- `err:any`: error object.


## status, errmsg = pthread:status()

get the thread status.

NOTE: the thread state is `running` until the `pthread:join()` method returns `true`.

**Returns**

- `status:string`: the following values are returned.
    - `running`: the thread is still running.
    - `terminated`: the thread was terminated.
    - `canceled`: the thread was canceled.
    - `failed`: the thread exited with an error.
- `errmsg:string`: if `status` is `failed`, then this value is the error message.

**Example**

```lua
local pthread = require('pthread')
local th, err, again = pthread.new('')
if err then
    print(err)
    return
elseif again then
    print('too many threads are running')
    return
end

-- get thread status
print('thread status:', th:status()) -- 'running'
-- wait for thread termination
local ok, err, again = th:join()
while again do
    print('thread status:', th:status()) -- 'running'
    ok, err, again = th:join()
end
if err then
    print(err)
    return
end
print('thread status after joined:', th:status()) -- 'terminated'
print('done')
```


## fd = pthread:fd()

get the file descriptor of the read end of the pipe that is used to signal the thread termination. you can use this file descriptor with `select` or `poll` function to wait for thread termination.

**Returns**

- `fd:integer`: file descriptor. if `-1` is returned, then the thread has already terminated.


## ch, err = pthread.channel( [maxitem [, maxsize]] )

create a `pthread.channel` object.

**Parameters**

- `maxitem:integer`: maximum number of items that can be stored in the channel. default is `0` (unlimited).
- `maxsize:integer`: maximum size of the item that can be stored in the channel. default is `0` (unlimited).

**Returns**

- `ch:pthread.channel`: `pthread.channel` object.
- `err:any`: error object.

**NOTE**

A `pthread.channel` object is managed by reference count mechanism. `pthread.channel` object is automatically closed when the reference count is `0`.

When a `pthread.channel` object passed to the `pthread.new` function, the reference count is incremented by `1`.

**Example**

the following example shows how to communicate between threads using `pthread.channel` object.

```lua
local pthread = require('pthread')

local ch = pthread.channel()
-- show the number of references to the channel
print(ch:nref()) -- 1


-- create a thread with channel argument
local th = pthread.new([[
    local th, ch = ...
    assert(ch:push('hello from thread'))
]], ch)
-- show the number of references to the channel
print(ch:nref()) -- 2

-- get data from channel until timeout
local data, err, again = ch:pop()
while again do
    data, err, again = ch:pop()
end
assert(data, err)
print(data) -- 'hello from thread'

-- wait for thread termination
local ok
ok, err, again = th:join()
while again do
    ok, err, again = th:join()
end
assert(ok, err)

-- show the number of references to the channel
print(ch:nref()) -- 1
```


## nref, err = pthread.channel:nref()

get the number of references to the channel.

**Returns**

- `nref:integer`: number of references.
- `err:any`: error object.


## len, err = pthread.channel:len()

get the number of items in the channel.

**Returns**

- `len:integer`: number of items.
- `err:any`: error object.


## size, err = pthread.channel:size()

get the used memory size of the channel.

**Returns**

- `size:integer`: used memory size.
- `err:any`: error object.


## fd, err = pthread.channel:fd()

get the file descriptor of the read end of the pipe. you can use this file descriptor with `select` or `poll` function to wait for the channel becomes readable.

**NOTE**

this file descriptor must not be used for other purposes. this file descriptor automatically read and write by the `pthread.channel` object.

**Returns**

- `fd:integer`: file descriptor.
- `err:any`: error object.


## ok, err, again = pthread.channel:push( value )

push the value to the channel.

**Parameters**

- `value:boolean|number|string|lightuserdata`: value to push.

**Returns**

- `ok:boolean`: `true` on success.
- `err:any`: error object.
- `again:boolean`: `true` if the channel is full.


## value, err, again = pthread.channel:pop()

pop the value from the channel.

**Returns**

- `value:any`: oldest value in the channel.
- `err:any`: error object.
- `again:boolean`: `true` if the channel is empty.

