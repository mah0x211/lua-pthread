local testcase = require('testcase')
local assert = require('assert')
local iowait = require('io.wait')
local new_pthread = require('pthread.thread').new
local new_queue = require('pthread.thread').queue

function testcase.create_queue()
    -- test that create a new pthread.thread.queue
    local q = new_queue()
    assert.match(q, '^pthread.thread.queue: 0x%x+', false)
    assert.equal(q:nref(), 1)

    -- test that can push values
    assert.equal(iowait.readable(q:fd_writable(), 10), q:fd_writable())
end

function testcase.maxitem()
    -- test that default maxitem is 1
    local q, err = assert(new_queue())
    assert.is_nil(err)
    assert.equal(q:maxitem(), 1)

    -- test that get maxitem of pthread.thread.queue
    collectgarbage('stop')
    for i = 1, 300 do
        q, err = new_queue(i)
        if not q then
            assert.re_match(err, 'too many open files', 'i')
            collectgarbage('restart')
        else
            assert.equal(q:maxitem(), i)
        end
    end
    collectgarbage('restart')
end

function testcase.close()
    -- test that close a pthread.thread.queue
    local q = new_queue()
    assert.is_nil(q:close())

    -- test that can be called multiple times
    assert.is_nil(q:close())

    -- test that throws an error if queue is closed
    local err = assert.throws(function()
        q:push('foo')
    end)
    assert.match(err, 'queue is closed')
end

function testcase.push()
    local q = new_queue()

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
        local id = assert(q:push(v))
        assert.is_uint(id)
        assert.equal(q:len(), 1)
        assert.equal(q:pop(), v)
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

function testcase.fd_writable()
    local q = new_queue(2)

    -- test that fd is used with iowait.readable to wait until queue is writable
    assert.is_uint(q:fd_writable())

    -- test that true if queue is writable
    local fd, err, again = iowait.readable(q:fd_writable(), 0.1)
    assert.equal(fd, q:fd_writable())
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that still be writable if queue is not full
    assert(q:push('hello'))
    fd, err, again = iowait.readable(q:fd_writable(), 0.1)
    assert.equal(fd, q:fd_writable())
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that false if queue is full
    assert(q:push('world'))
    fd, err, again = iowait.readable(q:fd_writable(), 0.1)
    assert.is_nil(fd)
    assert.is_nil(err)
    assert.is_true(again)

    -- test that can be writable again if pop a value
    assert.equal(q:pop(), 'hello')
    assert.equal(q:len(), 1)
    fd, err, again = iowait.readable(q:fd_writable(), 0.1)
    assert.equal(fd, q:fd_writable())
    assert.is_nil(err)
    assert.is_nil(again)
end

function testcase.fd_readable()
    local q = new_queue(2)

    -- test that return fd of queue that can be used with iowait.readable
    assert.is_uint(q:fd_readable())

    -- test that timeout if queue is empty
    local fd, err, again = iowait.readable(q:fd_readable(), 0.1)
    assert.is_nil(fd)
    assert.is_nil(err)
    assert.is_true(again)

    -- test that will be readable if push a value
    assert(q:push('hello'))
    fd, err, again = iowait.readable(q:fd_readable(), 0.1)
    assert.equal(fd, q:fd_readable())
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that readable until queue is empty
    assert(q:push('world'))
    fd, err, again = iowait.readable(q:fd_readable(), 0.1)
    assert.equal(fd, q:fd_readable())
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that still be readable if queue is not empty
    assert.equal(q:pop(), 'hello')
    assert.equal(q:len(), 1)
    fd, err, again = iowait.readable(q:fd_writable(), 0.1)
    assert.equal(fd, q:fd_writable())
    assert.is_nil(err)
    assert.is_nil(again)

    -- test that timeout after pop all values
    assert.equal(q:pop(), 'world')
    assert.equal(q:len(), 0)
    fd, err, again = iowait.readable(q:fd_readable(), 0.1)
    assert.is_nil(fd)
    assert.is_nil(err)
    assert.is_true(again)

    -- test that will be readable if push a value again
    assert(q:push('hello'))
    fd, err, again = iowait.readable(q:fd_writable(), 0.1)
    assert.equal(fd, q:fd_writable())
    assert.is_nil(err)
    assert.is_nil(again)
end

function testcase.push_maxitem()
    -- test that create a new pthread.thread.queue with maxitem
    local q = new_queue(2)
    for i = 1, 3 do
        local id, err, again = q:push(i)
        if i < 3 then
            assert(id, err)
            assert.is_nil(again)
        else
            assert.is_nil(id)
            assert.is_true(again)
            assert.is_nil(err)
        end
    end
end

function testcase.pop()
    local q = new_queue(8)
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
end

function testcase.pop_match()
    local q = new_queue(8)

    -- test that return nil if queue is empty
    assert.is_nil(q:pop_match(1))

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
    for _, v in ipairs(pushlist) do
        local id = assert(q:push(v))

        -- test that return nil if identifier is not matched
        assert.is_nil(q:pop_match(id + 1))

        -- test that pop values if match an identifier
        assert.equal(q:pop_match(id), v)
    end
end

function testcase.pass_channel_to_thread()
    -- test that communicate between threads via queue
    local q = new_queue()
    local th = assert(new_pthread([[
        local th, ch = ...
        assert(ch:push('hello'))
    ]], q))
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
