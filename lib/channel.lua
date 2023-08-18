--
-- Copyright (C) 2023 Masatoshi Fukunaga
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
-- THE SOFTWARE.
--
--- assign to local
local find = string.find
local type = type
local tostring = tostring
local floor = math.floor
local rawequal = rawequal
local gettime = require('time.clock').gettime --- @type fun():number
local io_wait_readable = require('io.wait').readable
local poll = require('gpoll')
local pollable = poll.pollable
local poll_readable = poll.readable
local new_queue = require('pthread.thread').queue
--- constants
local INF_POS = math.huge
local INF_NEG = -math.huge

--- is_uint
--- @param v any
--- @return boolean
local function is_uint(v)
    return type(v) == 'number' and (v < INF_POS and v > INF_NEG) and
               rawequal(floor(v), v) and v >= 0
end

--- define pthread.thread.queue metatable
--- @class pthread.thread.queue
--- @field close fun(self: pthread.thread.queue):(ok:boolean, err:any)
--- @field nref fun(self: pthread.thread.queue):(nref:integer, err:any)
--- @field len fun(self: pthread.thread.queue):(len:integer, err:any)
--- @field size fun(self: pthread.thread.queue):(size:integer, err:any)
--- @field fd_readable fun(self: pthread.thread.queue):(fd:integer, err:any)
--- @field fd_writable fun(self: pthread.thread.queue):(fd:integer, err:any)
--- @field push fun(self: pthread.thread.queue, value:boolean|number|string|lightuserdata):(ok:boolean, err:any, again:boolean)
--- @field pop fun(self: pthread.thread.queue):(value:any, err:any, again:boolean)

--- @class pthread.channel
--- @field private queue pthread.thread.queue
local Channel = {}

--- init
--- @param queue pthread.thread.queue
--- @return pthread.channel? self
--- @return any err
function Channel:init(queue)
    if type(queue) ~= 'userdata' or
        not find(tostring(queue), '^pthread%.thread%.queue: ') then
        error('queue must be pthread.thread.queue')
    end

    self.queue = queue
    return self
end

--- close
--- @return boolean ok
--- @return any err
function Channel:close()
    return self.queue:close()
end

--- nref
--- @return integer nref
--- @return any err
function Channel:nref()
    return self.queue:nref()
end

--- len
--- @return integer len
--- @return any err
function Channel:len()
    return self.queue:len()
end

--- size
--- @return integer size
--- @return any err
function Channel:size()
    return self.queue:size()
end

--- push
--- @param value boolean|number|string|lightuserdata
--- @param msec? integer
--- @return boolean ok
--- @return any err
--- @return boolean again
function Channel:push(value, msec)
    assert(msec == nil or is_uint(msec), 'msec must be integer or nil')

    local ok, err, again = self.queue:push(value)
    if again then
        local wait_writable = pollable() and poll.readable or io_wait_readable
        local deadline = msec and gettime() + (msec > 0 and msec / 1000 or 0)

        while again do
            if deadline and gettime() >= deadline then
                return false, nil, true
            end

            -- wait for writable
            ok, err, again = wait_writable(self.queue:fd_writable(), msec)
            if not ok then
                return false, err, again
            end
            -- push value
            ok, err, again = self.queue:push(value)
        end
    end

    return ok, err, again
end

--- pop
--- @param msec? integer
--- @return any value
--- @return any err
--- @return boolean timeout
function Channel:pop(msec)
    assert(msec == nil or is_uint(msec), 'msec must be integer or nil')

    local val, err, again = self.queue:pop()
    if again then
        local wait_readable = pollable() and poll_readable or io_wait_readable
        local deadline = msec and gettime() + (msec > 0 and msec / 1000 or 0)

        while again do
            if deadline and gettime() >= deadline then
                return nil, nil, true
            end

            -- wait for readable
            local ok
            ok, err, again = wait_readable(self.queue:fd_readable(), msec)
            if not ok then
                return nil, err, again
            end
            -- pop value
            val, err, again = self.queue:pop()
        end
    end

    return val, err, again
end

Channel = require('metamodule').new(Channel)

--- new
--- @param maxitem? integer
--- @param maxsize? integer
--- @return pthread.channel? self
--- @return any err
local function new(maxitem, maxsize)
    local queue, err = new_queue(maxitem, maxsize)
    if not queue then
        return nil, err
    end
    return Channel(queue)
end

return {
    new = new,
    wrap = Channel,
}

