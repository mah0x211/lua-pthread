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
    -- arguments can also be obtained from the global variable _G.PTHREAD_ARG
    print('self', _G.PTHREAD_ARG.self) -- self pthread.self: 0x7fdc7a426c88
    print('channel', _G.PTHREAD_ARG.channel[1]) -- channel nil
]])
if err then
    print(err)
    return
elseif again then
    print('too many threads are running')
    return
end

-- wait for thread termination
assert(th:join())

print('done')
```


## th, err, again = pthread.new_with_func( fn [, ch, ...] )

executes a `fn` function on a new posix thread and returns a `pthread` object. also, the script is passed a `pthread.self` object.

**NOTE**

this function is equivalent to the following code.

```lua
local pthread = require('pthread')
local th, err, again = pthread.new(string.dump(function(...)
    print(...)
end))
```

**Parameters**

- `fn:function`: the function to run on the created thread.
- `ch:pthread.channel`: `pthread.channel` arguments to pass to the script.

**Returns**

same as `pthread.new` function.


## th, err, again = pthread.new_with_file( filename [, ch, ...] )

executes a `filename` script on a new posix thread and returns a `pthread` object. also, the script is passed a `pthread.self` object.

**NOTE**

this function is equivalent to the following code.

```lua
local pthread = require('pthread')
local th, err, again = pthread.new(assert(io.open(filename, 'r')):read('*a'))
```

**Parameters**

- `filename:string`: the script filename to run on the created thread.
- `ch:pthread.channel`: `pthread.channel` arguments to pass to the script.

**Returns**

same as `pthread.new` function.


## ok, err, timeout = pthread:join( [sec] )

wait for thread termination. if the thread has not yet terminated, wait until the thread terminates in the specified timeout `sec`.

**Parameters**

- `sec:number`: timeout in seconds. default is `nil` (wait forever).

**Returns**

- `ok:boolean`: `true` success or if already joined.
- `err:any`: error object.
- `timeout:boolean`: `true` if the thread has not yet terminated.


## ok, err = pthread:cancel( [notify] )

cancel execution of a thread.

**Parameters**

- `notify:boolean`: if `true`, the thread is not cancelled, only the cancellation request is sent. default is `false`.

**Returns**

- `ok:boolean`: `true` on success.
- `err:any`: error object.


**Example**

```lua
local sleep = require('time.sleep')
local pthread = require('pthread')
local th, err, again = pthread.new([[
    local sleep = require('time.sleep')
    local th = ...
    while not th:is_cancelled() do
        print('thread', th, 'running')
        sleep(1)
    end
    print('thread is cancelled')
]])
if err then
    print(err)
    return
elseif again then
    print('too many threads are running')
    return
end
sleep(1) -- wait a second for thread to start

-- notify cancellation request
assert(th:cancel(true))
-- wait for thread termination
assert(th:join())

print('thread status after joined:', th:status()) -- 'terminated'
print('done')
```


the generated thread `pthread.self` has the following methods.


### ok, err, again = pthread.self:is_cancelled()

check if the thread is cancelled.

**Returns**

- `ok:boolean`: `true` if the thread is cancelled.
- `err:any`: error object.
- `again:boolean`: `true` if the thread is not cancelled.


### fd = pthread.self:fd_cancel()

get the file descriptor of the cancellation request. this file descriptor can be used with `select`, `poll` or similar functions to wait for the cancellation request asynchronously.

**NOTE**: this file descriptor should only be used with `select`, `poll` or similar functions. do not use it with `read` or `close` functions.

**Returns**

- `fd:integer`: file descriptor of the cancellation request.


## status, errmsg = pthread:status()

get the thread status.

**NOTE**: the thread state is `running` until the `pthread:join()` method returns `true`.

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
assert(th:join())

print('thread status after joined:', th:status()) -- 'terminated'
print('done')
```


## ch, err = channel.new( [maxitem] )

create a `pthread.channel` object.

**Parameters**

- `maxitem:integer`: maximum number of items that can be stored in the channel. default is `1`. if `<1` is specified, it is treated as `1`.

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
local channel = require('pthread.channel')

local ch = channel.new()
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
local data = assert(ch:pop())
print(data) -- 'hello from thread'

-- wait for thread termination
assert(th:join())

-- show the number of references to the channel
print(ch:nref()) -- 1
```

## channel:close()

close the channel. after closing the channel, the channel can not be used.


## nref = channel:nref()

get the number of references to the channel.

**Returns**

- `nref:integer`: number of references.


## len = channel:len()

get the number of items in the channel.

**Returns**

- `len:integer`: number of items.


## ok, err, timeout = channel:push( value [, sec] )

push the value to the channel. if the channel is full, wait until the value is popped from the channel in the specified timeout `sec`.

**NOTE**

- if the maxitem is set to `1`, this method will be blocked until the value is popped from the channel.
- if the timeout `sec` is specified, the value is discarded when the timeout expires.

**Parameters**

- `value:boolean|number|string|lightuserdata`: value to push.
- `sec:number`: timeout in seconds. default is `nil` (wait forever).

**Returns**

- `ok:boolean`: `true` on success.
- `err:any`: error object.
- `timeout:boolean`: `true` if the channel is full and the timeout has expired.


## value, err, timeout = channel:pop( [sec] )

pop the value from the channel. if the channel is empty, wait until the value is pushed to the channel in the specified timeout `sec`.

**Parameters**

- `sec:number`: timeout in seconds. default is `nil` (wait forever).

**Returns**

- `value:any`: oldest value in the channel.
- `err:any`: error object.
- `timeout:boolean`: `true` if the channel is empty and the timeout has expired.

---

## License

MIT License
