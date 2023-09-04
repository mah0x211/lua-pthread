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
local getmsec = require('time.clock').getmsec --- @type fun():integer
local io_wait_readable = require('io.wait').readable
local poll = require('gpoll')
local pollable = poll.pollable
local poll_wait_readable = poll.wait_readable
local poll_unwait_readable = poll.unwait_readable

--- @type fun(maxitem: integer?):(queue: pthread.thread.queue?, err: any)
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
--- @field close fun(self: pthread.thread.queue)
--- @field nref fun(self: pthread.thread.queue):integer
--- @field maxitem fun(self: pthread.thread.queue):integer
--- @field len fun(self: pthread.thread.queue):integer
--- @field fd_readable fun(self: pthread.thread.queue):integer
--- @field fd_writable fun(self: pthread.thread.queue):integer
--- @field push fun(self: pthread.thread.queue, value:boolean|number|string|lightuserdata):(id:integer, err:any, again:boolean)
--- @field pop fun(self: pthread.thread.queue):(value:any, err:any, again:boolean)
--- @field pop_match fun(self: pthread.thread.queue, id:integer):(value:any, err:any)

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

    self.queue = queue --[[@as pthread.thread.queue]]
    return self
end

--- close
function Channel:close()
    if pollable() then
        poll_unwait_readable(self.queue:fd_writable())
    end
    self.queue:close()
end

--- wait_readable
--- @private
--- @param msec integer?
--- @return boolean ok
--- @return any err
--- @return boolean? timeout
function Channel:wait_readable(msec)
    local wait_readable = pollable() and poll_wait_readable or io_wait_readable
    return wait_readable(self.queue:fd_readable(), msec)
end

--- wait_writable
--- @private
--- @param msec integer?
--- @return boolean ok
--- @return any err
--- @return boolean? timeout
function Channel:wait_writable(msec)
    local wait_readable = pollable() and poll_wait_readable or io_wait_readable
    return wait_readable(self.queue:fd_writable(), msec)
end

--- nref
--- @return integer nref
function Channel:nref()
    return self.queue:nref()
end

--- len
--- @return integer len
function Channel:len()
    return self.queue:len()
end

--- push
--- @param value boolean|number|string|lightuserdata
--- @param msec integer?
--- @return boolean ok
--- @return any err
--- @return boolean? again
function Channel:push(value, msec)
    assert(msec == nil or is_uint(msec), 'msec must be uint or nil')

    local deadline, mtime
    if msec then
        mtime = getmsec()
        deadline = mtime + msec
    end

    while true do
        local id, err, again = self.queue:push(value)
        if id then
            if self.queue:maxitem() ~= 1 then
                return true
            end

            -- act like the channel of the Go language
            -- wait until the value is taken by other thread
            local ok
            ok, err = self:wait_writable(msec)
            if ok then
                return true
            elseif err then
                return false, err
            elseif self:len() == 0 then
                -- timeout, but the value has already been taken by other thread
                return true
            end

            -- remove the pushed value
            ok, err = self.queue:pop_match(id)
            if ok then
                -- timeout
                return false, nil, true
            elseif err then
                return false, err
            end
            -- the value has already been taken by other thread
            return true
        end

        if not again then
            return false, err, again
        elseif deadline then
            -- get current time and check deadline
            mtime = getmsec()
            if mtime >= deadline then
                return false, nil, true
            end
            -- update remaining msec
            msec = deadline - mtime
        end

        -- wait for writable
        local ok
        ok, err, again = self:wait_writable(msec)
        if not ok then
            return false, err, again
        end
    end
end

--- pop
--- @param msec integer?
--- @return any value
--- @return any err
--- @return boolean? timeout
function Channel:pop(msec)
    assert(msec == nil or is_uint(msec), 'msec must be integer or nil')

    local val, err, again = self.queue:pop()
    if again then
        local deadline, mtime
        if msec then
            mtime = getmsec()
            deadline = mtime + msec
        end

        while again do
            if mtime then
                -- get current time and check deadline
                mtime = getmsec()
                if mtime >= deadline then
                    return nil, nil, true
                end
                -- update remaining msec
                msec = deadline - mtime
            end

            -- wait for readable
            local ok
            ok, err, again = self:wait_readable(msec)
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
--- @param maxitem integer?
--- @return pthread.channel? self
--- @return any err
local function new(maxitem)
    local queue, err = new_queue(maxitem)
    if not queue then
        return nil, err
    end
    return Channel(queue)
end

return {
    new = new,
    wrap = Channel,
}

