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
local new_readable_event = poll.new_readable_event
local dispose_event = poll.dispose_event
local wait_event = poll.wait_event

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
--- @field push fun(self: pthread.thread.queue, value:boolean|number|string|lightuserdata):(ok:boolean, err:any, again:boolean)
--- @field pop fun(self: pthread.thread.queue):(value:any, err:any, again:boolean)

--- @class pthread.channel
--- @field private queue pthread.thread.queue
--- @field private readable_evid any?
--- @field private writable_evid any?
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
    if self.readable_evid then
        dispose_event(self.readable_evid)
    end
    if self.writable_evid then
        dispose_event(self.writable_evid)
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
    local evid = self.readable_evid
    if not evid then
        if not pollable() then
            return io_wait_readable(self.queue:fd_readable(), msec)
        end

        local err
        evid, err = new_readable_event(self.queue:fd_readable())
        if not evid then
            return false, err
        end
        self.readable_evid = evid
    end

    -- wait until readable
    return wait_event(evid, msec)
end

--- wait_writable
--- @private
--- @param msec integer?
--- @return boolean ok
--- @return any err
--- @return boolean? timeout
function Channel:wait_writable(msec)
    local evid = self.writable_evid
    if not evid then
        if not pollable() then
            return io_wait_readable(self.queue:fd_writable(), msec)
        end

        local err
        evid, err = new_readable_event(self.queue:fd_writable())
        if not evid then
            return false, err
        end
        self.writable_evid = evid
    end

    -- wait until writable
    return wait_event(evid, msec)
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
    assert(msec == nil or is_uint(msec), 'msec must be integer or nil')

    local ok, err, again = self.queue:push(value)
    if again then
        local deadline = msec and gettime() + (msec > 0 and msec / 1000 or 0)

        while again do
            if deadline and gettime() >= deadline then
                return false, nil, true
            end

            -- wait for writable
            ok, err, again = self:wait_writable(msec)
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
--- @param msec integer?
--- @return any value
--- @return any err
--- @return boolean? timeout
function Channel:pop(msec)
    assert(msec == nil or is_uint(msec), 'msec must be integer or nil')

    local val, err, again = self.queue:pop()
    if again then
        local deadline = msec and gettime() + (msec > 0 and msec / 1000 or 0)

        while again do
            if deadline and gettime() >= deadline then
                return nil, nil, true
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

