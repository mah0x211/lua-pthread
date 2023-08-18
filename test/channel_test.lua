require('luacov')
local testcase = require('testcase')
local nanotime = require('testcase.timer').nanotime
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
    local ok, err = ch:close()
    assert.is_true(ok)
    assert.is_nil(err)

    -- test that can be called multiple times
    ok, err = ch:close()
    assert.is_true(ok)
    assert.is_nil(err)

    -- test that throws an error if queue is closed
    err = assert.throws(function()
        ch:push('foo')
    end)
    assert.match(err, 'queue is closed')
end

function testcase.push()
    local ch = new_channel()

    -- test that push values
    for i, v in ipairs({
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
        assert.equal(ch:len(), i)
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
    -- test that calls gpoll.new_readable_event and gpoll.wait_event
    local ch = new_channel(1)
    assert(ch:push('foo'))
    local new_readable_called = false
    local wait_event_called = false
    gpoll.set_poller({
        pollable = function()
            return true
        end,
        new_readable_event = function(fd)
            new_readable_called = true
            return fd
        end,
        wait_event = function(evid)
            assert.equal(evid, ch.queue:fd_writable())
            wait_event_called = true
            return false, 'error wait_event', true
        end,
    })
    local ok, err, timeout = ch:push('foo')
    assert.is_false(ok)
    assert.match(err, 'error wait_event')
    assert.is_true(timeout)
    assert.is_true(new_readable_called)
    assert.is_true(wait_event_called)
    ch:close()

    -- test that gpoll.new_readable_event return error
    ch = new_channel(1)
    assert(ch:push('foo'))
    new_readable_called = false
    wait_event_called = false
    gpoll.set_poller({
        pollable = function()
            return true
        end,
        new_readable_event = function()
            new_readable_called = true
            return nil, 'error new_readable_event'
        end,
        wait_event = function(evid)
            assert.equal(evid, ch.queue:fd_writable())
            wait_event_called = true
            return false, 'error wait_event', true
        end,
    })
    ok, err, timeout = ch:push('foo')
    assert.is_false(ok)
    assert.match(err, 'error new_readable_event')
    assert.is_nil(timeout)
    assert.is_true(new_readable_called)
    assert.is_false(wait_event_called)
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

function testcase.push_maxsize()
    -- test that create a new pthread.channel with maxsize
    local ch = new_channel(nil, 100)
    local nitem = 0
    for _ = 1, 10 do
        local ok, err, again = ch:push('hello', 10)
        if ok then
            assert.is_true(ok)
            assert.is_nil(err)
            assert.is_nil(again)
            nitem = nitem + 1
        else
            assert.is_false(ok)
            assert.is_nil(err)
            assert.is_true(again)
            assert.equal(ch:len(), nitem)
            assert.less_or_equal(ch:size(), 100)
            break
        end
    end
end

function testcase.pop()
    local ch = new_channel()
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
        v, err, again = ch:pop(10)
    end
    assert(not err, err)
    assert.is_true(again)
    assert.equal(poplist, pushlist)
    assert.equal(ch:len(), 0)
    assert.equal(ch:size(), 0)

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
    -- test that calls gpoll.new_readable_event and gpoll.wait_event
    local ch = new_channel()
    local new_readable_called = false
    local wait_event_called = false
    gpoll.set_poller({
        pollable = function()
            return true
        end,
        new_readable_event = function(fd)
            new_readable_called = true
            return fd
        end,
        wait_event = function(evid)
            assert.equal(evid, ch.queue:fd_readable())
            wait_event_called = true
            return false, 'error wait_event', true
        end,
    })
    local val, err, timeout = ch:pop()
    assert.is_nil(val)
    assert.match(err, 'error wait_event')
    assert.is_true(timeout)
    assert.is_true(new_readable_called)
    assert.is_true(wait_event_called)
    ch:close()

    -- test that gpoll.new_readable_event return error
    ch = new_channel()
    new_readable_called = false
    wait_event_called = false
    gpoll.set_poller({
        pollable = function()
            return true
        end,
        new_readable_event = function()
            new_readable_called = true
            return nil, 'error new_readable_event'
        end,
        wait_event = function(evid)
            assert.equal(evid, ch.queue:fd_readable())
            wait_event_called = true
            return false, 'error wait_event', true
        end,
    })
    val, err, timeout = ch:pop()
    assert.is_nil(val)
    assert.match(err, 'error new_readable_event')
    assert.is_nil(timeout)
    assert.is_true(new_readable_called)
    assert.is_false(wait_event_called)
    ch:close()
end

function testcase.pass_channel_to_thread()
    -- test that pass channel to thread
    local ch = new_channel()
    local th = assert(new_pthread([[
        local th, ch = ...
        assert(ch:push('hello'))
        require('testcase.timer').sleep(.1)
    ]], ch))
    -- confirm that reference count is increased
    assert.equal(ch:nref(), 2)

    -- test that get value from channel that pushed by thread
    local data = assert(ch:pop())
    assert.equal(data, 'hello')

    -- test that reference count is decreased after thread is finished
    assert(th:join())
    assert.equal(ch:nref(), 1)
end

function testcase.cannot_pass_invalid_channel_to_thread()
    -- test that pass invalid channel to thread
    local err = assert.throws(new_pthread, [[]], {})
    assert.match(err, 'expected pthread.channel,')
end

