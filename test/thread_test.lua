local testcase = require('testcase')
local sleep = require('testcase.timer').sleep
local close = require('testcase.close')
local iowait = require('io.wait')
local pthread = require('pthread.thread')

function testcase.new()
    local srcfile = os.tmpname()
    do
        local f = assert(io.open(srcfile, 'w'))
        assert(f:write(''))
        assert(f:close())
    end

    -- test that create a new thread
    local th, err = pthread.new('')
    assert.match(th, 'pthread.thread: 0x%x+', false)
    assert.is_nil(err)

    -- test that return error if failed to create a new thread
    th, err = pthread.new('function() do end')
    assert.is_nil(th)
    assert.re_match(err, 'invalid', 'i')
end

--
-- NOTE: the following commented-out test case cannot be run because the maximum
--       number of threads cannot be changed in the test environment.
--
-- function testcase.failed_to_create_new_thread()
--     -- luacheck: ignore threads
--     local threads = {}
--     local source = [[
--         require('testcase.timer').sleep(100)
--     ]]

--     -- test that create a new thread
--     local th, err, again = pthread.new(source)
--     while th do
--         threads[#threads + 1] = th
--         th, err, again = pthread.new(source)
--     end
--     assert.is_nil(err)
--     assert.is_true(again)
-- end

function testcase.join_status()
    local th = pthread.new([[
        local assert = require('assert')
        local th = ...
        assert.match(th, '^pthread.self: ', false)
    ]])

    -- test that return 'running' when thread is running
    assert.equal(th:status(), 'running')

    -- test that join a thread
    assert(iowait.readable(th:fd()))
    assert(th:join())

    -- test that return 'terminated' when thread is terminated
    assert.equal(th:status(), 'terminated')

    -- test that return true when thread is terminated
    assert.is_true(th:join(), true)

    -- test that return error messaage when thread is failed
    th = pthread.new([[
        local function test()
            local foo = bar + 'foo'
        end
        test()
    ]])
    assert(iowait.readable(th:fd()))
    assert(th:join())
    local status, errmsg = th:status()
    assert.equal(status, 'failed')
    assert.re_match(errmsg, 'attempt to', 'i')
end

function testcase.cancel()
    local th = pthread.new([[
        require('testcase.timer').sleep(1)
    ]])
    -- wait for thread to load module
    sleep(.1)

    -- test that cancel a thread
    assert(th:cancel())
    assert(iowait.readable(th:fd()))
    assert(th:join())

    -- test that return 'canceled' when thread is canceled
    assert.equal(th:status(), 'cancelled')
end

function testcase.cancel_notify()
    local th = pthread.new([[
        local sleep = require('testcase.timer').sleep
        local th = ...
        while not th:is_cancelled() do
            sleep(.1)
        end
        -- test that is_cancelled method can be called multiple times
        sleep(.1)
        assert(th:is_cancelled())
    ]])
    -- wait for thread to load module
    sleep(.1)

    -- test that notify a thread to cancel
    assert(th:cancel(true))

    -- test that cancel method can be called multiple times
    assert(th:cancel(true))

    -- confirm that thread is canceled
    assert(iowait.readable(th:fd()))
    assert(th:join())

    -- test that return 'terminated' when thread is soft canceled
    assert.equal(th:status(), 'terminated')
end

function testcase.fd_cancel()
    local th = pthread.new([[
        local assert = require('assert')
        local io_wait_readable = require('io.wait').readable
        local sleep = require('testcase.timer').sleep
        local th = ...

        -- test that return fd that can be used to wait for thread termination
        local fd = th:fd_cancel()
        assert.is_uint(fd)
        assert(io_wait_readable(fd))
        assert(th:is_cancelled())
    ]])
    -- wait for thread to load module
    sleep(.1)

    -- test that notify a thread to cancel
    assert(th:cancel(true))
    -- confirm that thread is canceled
    assert(iowait.readable(th:fd()))
    assert(th:join())
    -- test that return 'terminated' when thread is soft canceled
    assert.equal(th:status(), 'terminated')
end

function testcase.fd()
    -- test that return fd that can be used to wait for thread termination
    local th = pthread.new([[
        require('testcase.timer').sleep(0.1)
    ]])
    local fd = th:fd()
    assert.is_uint(fd)

    -- test that fd can be used to monitor thread termination
    assert(iowait.readable(fd))
    assert(th:join())
    assert.equal(th:status(), 'terminated')

    -- test that return -1 after thread is terminated
    assert.equal(th:fd(), -1)
end

function testcase.join_even_fd_closed()
    local th = pthread.new([[
        require('testcase.timer').sleep(0.1)
    ]])
    close(th:fd())
    sleep(0.2)

    -- test that join a thread even if fd is closed
    local ok, err, again = th:join()
    while again do
        ok, err, again = th:join()
    end
    assert(ok, err)
    assert.equal(th:status(), 'terminated')
end
