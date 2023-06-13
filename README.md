# lua-pthread

[![test](https://github.com/mah0x211/lua-pthread/actions/workflows/test.yml/badge.svg)](https://github.com/mah0x211/lua-pthread/actions/workflows/test.yml)
[![codecov](https://codecov.io/gh/mah0x211/lua-pthread/branch/master/graph/badge.svg)](https://codecov.io/gh/mah0x211/lua-pthread)

pthread module.

**NOTE: this module is under heavy development.**


---


## Error Handling

the functions/methods are return the error object created by https://github.com/mah0x211/lua-errno module.


## th, err, again = pthread( pathname )

executes a `pathname` script on a new posix thread and returns a `pthread` object. also, the script is passed a `pthread.self` object.

**Parameters**

- `pathname:string`: filepath of the script to run on the created thread.

**Returns**

- `th:pthread`: `pthread` object.
- `err:any`: error object.
- `again:boolean`: `true` if `pthread_create` return `EAGAIN` error.
- `errno:number`: error number.

**Example**

```lua
local pthread = require('pthread')

-- create a script file
local tmpfile = os.tmpname()
local f = assert(io.open(tmpfile, 'w'))
f:write([[
    print('arguments', ...) -- arguments pthread.self: 0x7fdc7a426c88
]])
f:close()

-- create thread
local th, err, again = pthread(tmpfile)
os.remove(tmpfile)
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


## ok, err, again = pthread:join()

wait for thread termination.

**Returns**

- `ok:boolean`: `true` success or if already joined.
- `err:any`: error object.
- `again:boolean`: `true` if the thread has not yet terminated.


## ok, err, again = pthread:cancel()

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
    - `failure`: the thread exited with an error.
- `errmsg:string`: if `status` is `failure`, then this value is the error message.

**Example**

```lua
local pthread = require('pthread')
local th, err, again = pthread.new('./script.lua')
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

