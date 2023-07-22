local testcase = require('testcase')
local iowait = require('io.wait')
local new_pthread = require('pthread').new
local new_channel = require('pthread').channel

function testcase.create_channel()
    -- test that create a new pthread.channel
    local ch = new_channel()
    assert.match(ch, '^pthread.channel: 0x%x+', false)
    assert.equal(ch:nref(), 1)
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
    assert.match(err, 'no value')
    assert.equal(ch:len(), len)
end

function testcase.fd_wait()
    local ch = new_channel()

    -- test that return fd of channel that can be used with iowait.readable
    local fd = assert(ch:fd())
    assert.is_uint(fd)

    -- test that timeout if channel is empty
    local ok, err, again = iowait.readable(fd, 100)
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_true(again)

    -- test that will be readable if push a value
    assert(ch:push('hello'))
    assert(ch:push('world'))
    ok, err, again = iowait.readable(fd, 100)
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that still be readable if queue is not empty
    assert.equal(ch:pop(), 'hello')
    ok, err, again = iowait.readable(fd, 100)
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that timeout after pop all values
    assert.equal(ch:pop(), 'world')
    ok, err, again = iowait.readable(fd, 100)
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_true(again)

    -- test that it will be readable again if push a value
    assert(ch:push('hello'))
    ok, err, again = iowait.readable(fd, 100)
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(again)
end

function testcase.push_maxitem()
    -- test that create a new pthread.channel with maxitem
    local ch = new_channel(2)
    for i = 1, 3 do
        local ok, err, again = ch:push(i)
        if i < 3 then
            assert(ok, err)
            assert.is_nil(again)
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
        local ok, err, again = ch:push('hello')
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
        v, err, again = ch:pop()
    end
    assert(not err, err)
    assert.is_true(again)
    assert.equal(poplist, pushlist)
    assert.equal(ch:len(), 0)
    assert.equal(ch:size(), 0)
end

function testcase.pass_channel_to_thread()
    -- test that communicate between threads via channel
    local ch = new_channel()
    local th = new_pthread([[
        local th, ch = ...
        assert(ch:push('hello'))
    ]], ch)
    -- confirm that reference count is increased
    assert.equal(ch:nref(), 2)

    -- test that get value from channel that pushed by thread
    assert(iowait.readable(ch:fd()))
    local data = assert(ch:pop())
    assert.equal(data, 'hello')

    -- test that reference count is decreased after thread is finished
    assert(iowait.readable(th:fd()))
    assert(th:join())
    assert.equal(ch:nref(), 1)
end

