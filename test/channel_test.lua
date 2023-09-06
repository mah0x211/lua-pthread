require('luacov')
local testcase = require('testcase')
local nanotime = require('testcase.timer').nanotime
local sleep = require('testcase.timer').sleep
local gpoll = require('gpoll')
local new_pthread = require('pthread').new
local new_channel = require('pthread.channel').new

function testcase.before_each()
    gpoll.set_poller()
end

function testcase.create_channel()
    -- test that create a new pthread.channel
    local ch = new_channel()
    assert.match(ch, '^pthread.channel: 0x%x+', false)
    assert.equal(ch:nref(), 1)
end

function testcase.close()
    -- test that close a pthread.channel
    local ch = new_channel()
    assert.is_nil(ch:close())

    -- test that can be called multiple times
    assert.is_nil(ch:close())

    -- test that throws an error if queue is closed
    local err = assert.throws(function()
        ch:push('foo')
    end)
    assert.match(err, 'queue is closed')
end

function testcase.push()
    local ch = new_channel(2)

    -- test that push values
    for _, v in ipairs({
        true,
        false,
        1,
        0,
        -1,
        1.1,
        -1.1,
        'foo',
    }) do
        assert(ch:push(v))
        assert.equal(ch:len(), 1)
        assert.equal(ch:pop(), v)
    end

    -- test that throws an error if push an unsupported value
    local len = ch:len()
    for _, v in ipairs({
        {},
        function()
        end,
        coroutine.create(function()
        end),
        ch,
    }) do
        local err = assert.throws(ch.push, ch, v)
        assert.match(err, type(v))
        assert.equal(ch:len(), len)
    end

    -- test that throws an error if push nil or no value
    local err = assert.throws(ch.push, ch, nil)
    assert.match(err, 'nil')
    assert.equal(ch:len(), len)

    -- test that throws an error if push no value
    err = assert.throws(ch.push, ch)
    assert.match(err, 'unsupported value type: nil')
    assert.equal(ch:len(), len)
end

function testcase.push_async()
    -- test that calls gpoll.wait_readable
    local ch = new_channel(2)
    assert(ch:push('foo'))
    assert(ch:push('bar'))
    local wait_readable_called = false
    gpoll.set_poller({
        pollable = function()
            return true
        end,
        wait_readable = function()
            wait_readable_called = true
            return false, 'error wait_readable', true
        end,
    })
    local ok, err, timeout = ch:push('foo')
    assert.is_false(ok)
    assert.match(err, 'error wait_readable')
    assert.is_true(timeout)
    assert.is_true(wait_readable_called)
    ch:close()

    -- test that gpoll.wait_readable_event return error
    ch = new_channel(2)
    assert(ch:push('foo'))
    assert(ch:push('bar'))
    wait_readable_called = false
    gpoll.set_poller({
        pollable = function()
            return true
        end,
        wait_readable = function()
            wait_readable_called = true
            return false, 'error wait_readable'
        end,
    })
    ok, err, timeout = ch:push('foo')
    assert.is_false(ok)
    assert.match(err, 'error wait_readable')
    assert.is_nil(timeout)
    assert.is_true(wait_readable_called)
    ch:close()
end

function testcase.push_maxitem()
    -- test that create a new pthread.channel with maxitem
    local ch = new_channel(2)
    for i = 1, 3 do
        local elapsed = nanotime()
        local ok, err, again = ch:push(i, 0)
        elapsed = nanotime() - elapsed
        if i < 3 then
            assert(ok, err)
            assert.is_nil(again)
            assert.less(elapsed, 0.001)
        else
            assert.is_false(ok)
            assert.is_true(again)
            assert.is_nil(err)
        end
    end
end

function testcase.pop()
    local ch = new_channel(8)
    local pushlist = {
        true,
        false,
        1,
        0,
        -1,
        1.1,
        -1.1,
        'foo',
    }
    for i, v in ipairs(pushlist) do
        assert(ch:push(v))
        assert.equal(ch:len(), i)
    end

    -- test that pop values
    local poplist = {}
    local v, err, again = ch:pop()
    while v ~= nil do
        poplist[#poplist + 1] = v
        v, err, again = ch:pop(0.01)
    end
    assert(not err, err)
    assert.is_true(again)
    assert.equal(poplist, pushlist)
    assert.equal(ch:len(), 0)

    -- test that pop values with timeout
    local elapsed = nanotime()
    v, err, again = ch:pop(0)
    elapsed = nanotime() - elapsed
    assert.is_nil(v)
    assert.is_nil(err)
    assert.is_true(again)
    assert.less(elapsed, 0.001)
end

function testcase.pop_async()
    -- test that calls gpoll.wait_readable and gpoll.wait_event
    local ch = new_channel()
    local wait_readable_called = false
    gpoll.set_poller({
        pollable = function()
            return true
        end,
        wait_readable = function(fd)
            wait_readable_called = true
            assert.equal(fd, ch.queue:fd_readable())
            return false, 'error wait_readable', true
        end,
    })
    local val, err, timeout = ch:pop()
    assert.is_nil(val)
    assert.match(err, 'error wait_readable')
    assert.is_true(timeout)
    assert.is_true(wait_readable_called)
    ch:close()

    -- test that gpoll.wait_readable return error
    ch = new_channel()
    wait_readable_called = false
    gpoll.set_poller({
        pollable = function()
            return true
        end,
        wait_readable = function(fd)
            assert.equal(fd, ch.queue:fd_readable())
            wait_readable_called = true
            return false, 'error wait_readable'
        end,
    })
    val, err, timeout = ch:pop()
    assert.is_nil(val)
    assert.match(err, 'error wait_readable')
    assert.is_nil(timeout)
    assert.is_true(wait_readable_called)
    ch:close()
end

function testcase.pass_channel_to_thread()
    -- test that pass channel to thread
    local ch = new_channel()
    local th = assert(new_pthread([[
        local assert = require('assert')
        local th, ch = ...
        assert.match(ch, '^pthread.channel: 0x%x+', false)
        assert(ch:push('hello'))
    ]], ch))
    -- confirm that reference count is increased
    assert.equal(ch:nref(), 2)

    -- confirm that thread is blocked in ch:push() call
    sleep(.1)
    local ok, err, again = th:join(0.1)
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_true(again)

    -- test that get value from channel that pushed by thread
    local data = assert(ch:pop())
    assert.equal(data, 'hello')

    -- test that reference count is decreased after thread is finished
    assert(th:join())
    local status, errmsg = th:status()
    assert.equal(status, 'terminated')
    assert.is_nil(errmsg)
    assert.equal(ch:nref(), 1)

    -- test that value is discarded if timeout expires
    th = assert(new_pthread([[
            local assert = require('assert')
            local th, ch = ...
            assert.match(ch, '^pthread.channel: 0x%x+', false)
            local ok, err, again = ch:push('hello', 0.05)
            assert.is_false(ok)
            assert.is_nil(err)
            assert.is_true(again)
        ]], ch))
    assert.equal(ch:nref(), 2)

    -- wait until thread is finished
    sleep(.1)
    assert(th:join(0.1))
    status, errmsg = th:status()
    assert.equal(status, 'terminated')
    assert.is_nil(errmsg)
    assert.equal(ch:nref(), 1)

    -- test that return nil
    data, err, again = ch:pop(0.01)
    assert.is_nil(data)
    assert.is_nil(err)
    assert.is_true(again)
end

function testcase.cannot_pass_invalid_channel_to_thread()
    -- test that pass invalid channel to thread
    local err = assert.throws(new_pthread, [[]], {})
    assert.match(err, 'expected pthread.channel,')
end

