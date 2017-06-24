# lua-pthread

pthread module.

**NOTE: this module is under heavy development.**


## Dependencies

- luarocks-fetch-gitrec: <https://github.com/siffiejoe/luarocks-fetch-gitrec>
- lauxhlib: <https://github.com/mah0x211/lauxhlib>


---


## Create a Pthread Object.

### th = pthread.new( fn [, ...] )

returns a new `Pthread` object and run passed function in new posix thread.

**Parameters**

- `fn`: function or function string.
- `...`: arguments for fn except following data types;
    - `LUA_TFUNCTION`
    - `LUA_TUSERDATA`
    - `LUA_TTHREAD`

**Returns**

- `th:pthread`: pthread object.
- `err:string`: error message.


---


## Pthread Methods


### ok, err = th:join()

wait for thread termination.

**Returns**

- `ok:boolean`: true on success.
- `err:message`: error message.



### ok, err = th:kill( signo )

send a signal to thread.

**Parameters**

- `signo:number`: signal number.

**Returns**

- `ok:boolean`: true on success.
- `err:string`: error message.



## Example

```lua
local pthread = require('pthread')
local th

-- pass function
th = pthread.new(function( arg )
    print('hello', arg)
end, 'world!' );
th:join()

-- pass function string
th = pthread.new([[function foo( arg )
    print('hello', arg)
end
foo(...)]], 'world!' );
th:join()
```
