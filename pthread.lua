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
local select = select
local tostring = tostring
local unpack = unpack or table.unpack
local new_thread = require('pthread.thread').new
local new_thread_with_file = require('pthread.thread').new_with_file
local instanceof = require('metamodule').instanceof

--- define pthread.thread metatable
--- @class pthread.thread
--- @field join fun(self: pthread.thread):(ok:boolean, err:any, again:boolean)
--- @field cancel fun(self: pthread.thread):(ok:boolean, err:any)
--- @field status fun(self: pthread.thread):(status:string, errmsg:string)
--- @field fd fun(self: pthread.thread):integer

--- @class pthread
--- @field private thread pthread.thread
local Pthread = {}

--- init
--- @param newfn fun(src:string, ...:pthread.channel):(pthread.thread?, any, boolean?)
--- @param src string
--- @param ... pthread.channel
--- @return pthread? self
--- @return any err
--- @return boolean? again
function Pthread:init(newfn, src, ...)
    --- @type pthread.channel[]|pthread.thread.queue[]
    local qs = {
        ...,
    }
    for i = 1, select('#', ...) do
        local ch = qs[i]
        if not instanceof(ch, 'pthread.channel') then
            error('invalid argument #' .. i + 1 ..
                      ': expected pthread.channel, got ' .. tostring(ch))
        end
        qs[i] = ch.queue
    end

    local thread, err, again = newfn(src, unpack(qs))
    if not thread then
        return nil, err, again
    end
    self.thread = thread
    return self
end

--- join
--- @return boolean ok
--- @return any err
--- @return boolean? again
function Pthread:join()
    local ok, err, again = self.thread:join()
    if not ok then
        return false, err, again
    end
    return true
end

--- cancel
--- @return boolean ok
--- @return any err
function Pthread:cancel()
    local ok, err = self.thread:cancel()
    if not ok then
        return false, err
    end
    return true
end

--- status
--- @return string status
--- @return string errmsg
function Pthread:status()
    return self.thread:status()
end

--- fd
--- @return integer fd
function Pthread:fd()
    return self.thread:fd()
end

Pthread = require('metamodule').new(Pthread)

--- new_with_file
--- @param filename string
--- @param ... pthread.channel
--- @return pthread? self
--- @return any err
--- @return boolean? again
local function new_with_file(filename, ...)
    return Pthread(new_thread_with_file, filename, ...)
end

--- new
--- @param src string
--- @param ... pthread.channel
--- @return pthread? self
--- @return any err
--- @return boolean? again
local function new(src, ...)
    return Pthread(new_thread, src, ...)
end

return {
    new = new,
    new_with_file = new_with_file,
}
