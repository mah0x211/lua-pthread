local testcase = require('testcase')
local iowait = require('io.wait')
local new_pthread = require('pthread.thread').new
local new_queue = require('pthread.thread').queue

function testcase.create_queue()
    -- test that create a new pthread.thread.queue
    local q = new_queue()
    assert.match(q, '^pthread.thread.queue: 0x%x+', false)
    assert.equal(q:nref(), 1)

    -- test that can push values
    assert.is_true(iowait.readable(q:fd_writable(), 10))
end

function testcase.close()
    -- test that close a pthread.thread.queue
    local q = new_queue()
    local ok, err = q:close()
    assert.is_true(ok)
    assert.is_nil(err)

    -- test that can be called multiple times
    ok, err = q:close()
    assert.is_true(ok)
    assert.is_nil(err)

    -- test that throws an error if queue is closed
    err = assert.throws(function()
        q:push('foo')
    end)
    assert.match(err, 'queue is closed')
end

function testcase.push()
    local q = new_queue()

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
        assert(q:push(v))
        assert.equal(q:len(), i)
    end

    -- test that throws an error if push an unsupported value
    local len = q:len()
    for _, v in ipairs({
        {},
        function()
        end,
        coroutine.create(function()
        end),
        q,
    }) do
        local err = assert.throws(q.push, q, v)
        assert.match(err, type(v))
        assert.equal(q:len(), len)
    end

    -- test that throws an error if push nil or no value
    local err = assert.throws(q.push, q, nil)
    assert.match(err, 'nil')
    assert.equal(q:len(), len)

    -- test that throws an error if push no value
    err = assert.throws(q.push, q)
    assert.match(err, 'no value')
    assert.equal(q:len(), len)
end

function testcase.fd_wait()
    local q = new_queue()

    -- test that return fd of queue that can be used with iowait.readable
    local fd = assert(q:fd_readable())
    assert.is_uint(fd)

    -- test that timeout if queue is empty
    local ok, err, again = iowait.readable(fd, 100)
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_true(again)

    -- test that will be readable if push a value
    assert(q:push('hello'))
    assert(q:push('world'))
    ok, err, again = iowait.readable(fd, 100)
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that still be readable if queue is not empty
    assert.equal(q:pop(), 'hello')
    ok, err, again = iowait.readable(fd, 100)
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that timeout after pop all values
    assert.equal(q:pop(), 'world')
    ok, err, again = iowait.readable(fd, 100)
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_true(again)

    -- test that it will be readable again if push a value
    assert(q:push('hello'))
    ok, err, again = iowait.readable(fd, 100)
    assert.is_true(ok)
    assert.is_nil(err)
    assert.is_nil(again)
end

function testcase.push_maxitem()
    -- test that create a new pthread.thread.queue with maxitem
    local q = new_queue(2)
    for i = 1, 3 do
        local ok, err, again = q:push(i)
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
    -- test that create a new pthread.thread.queue with maxsize
    local q = new_queue(nil, 100)
    local nitem = 0
    for _ = 1, 10 do
        local ok, err, again = q:push('hello')
        if ok then
            assert.is_true(ok)
            assert.is_nil(err)
            assert.is_nil(again)
            nitem = nitem + 1
        else
            assert.is_false(ok)
            assert.is_nil(err)
            assert.is_true(again)
            assert.equal(q:len(), nitem)
            assert.less_or_equal(q:size(), 100)
            break
        end
    end
end

function testcase.pop()
    local q = new_queue()
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
        assert(q:push(v))
        assert.equal(q:len(), i)
    end

    -- test that pop values
    local poplist = {}
    local v, err, again = q:pop()
    while v ~= nil do
        poplist[#poplist + 1] = v
        v, err, again = q:pop()
    end
    assert(not err, err)
    assert.is_true(again)
    assert.equal(poplist, pushlist)
    assert.equal(q:len(), 0)
    assert.equal(q:size(), 0)
end

function testcase.pass_channel_to_thread()
    -- test that communicate between threads via queue
    local q = new_queue()
    local th = new_pthread([[
        local th, ch = ...
        assert(ch:push('hello'))
    ]], q)
    -- confirm that reference count is increased
    assert.equal(q:nref(), 2)

    -- test that get value from queue that pushed by thread
    assert(iowait.readable(q:fd_readable()))
    local data = assert(q:pop())
    assert.equal(data, 'hello')

    -- test that reference count is decreased after thread is finished
    assert(iowait.readable(th:fd()))
    assert(th:join())
    assert.equal(q:nref(), 1)
end
