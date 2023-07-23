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
local new_queue = require('pthread.thread').queue

--- define pthread.thread.queue metatable
--- @class pthread.thread.queue
--- @field nref fun(self: pthread.thread.queue):(nref:integer, err:any)
--- @field len fun(self: pthread.thread.queue):(len:integer, err:any)
--- @field size fun(self: pthread.thread.queue):(size:integer, err:any)
--- @field fd fun(self: pthread.thread.queue):(fd:integer, err:any)
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

--- fd
--- @return integer fd
--- @return any err
function Channel:fd()
    return self.queue:fd()
end

--- push
--- @param value boolean|number|string|lightuserdata
--- @return boolean ok
--- @return any err
--- @return boolean again
function Channel:push(value)
    return self.queue:push(value)
end

--- pop
--- @return any value
--- @return any err
--- @return boolean again
function Channel:pop()
    return self.queue:pop()
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

