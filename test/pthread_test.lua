require('luacov')
local testcase = require('testcase')
local assert = require('assert')
local sleep = require('testcase.timer').sleep
local close = require('testcase.close')
local pthread = require('pthread')

function testcase.new()
    local srcfile = os.tmpname()
    do
        local f = assert(io.open(srcfile, 'w'))
        assert(f:write(''))
        assert(f:close())
    end

    -- test that create a new thread
    local th, err = pthread.new([[
        local assert = require('assert')
        local th = ...
        assert.match(th, '^pthread%.self: ', false)
        assert.equal(th, _G.PTHREAD_ARG.self)
    ]])
    assert.match(th, 'pthread: 0x%x+', false)
    assert.is_nil(err)
    assert(th:join())
    local status, errmsg = th:status()
    assert.equal(status, 'terminated')
    assert.is_nil(errmsg)

    -- test that return error if function is invalid
    th, err = pthread.new('function() do end')
    assert.is_nil(th)
    assert.re_match(err, 'invalid', 'i')
end

function testcase.new_with_func()
    -- test that create a new thread with function
    local th, err = pthread.new_with_func(function(th)
        -- this function is called in a new thread and shares nothing with the main thread
        local assert = require('assert')
        assert.match(th, '^pthread%.self: ', false)
    end)
    assert.match(th, 'pthread: 0x%x+', false)
    assert.is_nil(err)
    assert(th:join())
    local status, errmsg = th:status()
    assert.equal(status, 'terminated')
    assert.is_nil(errmsg)

    -- test that throws and error if argument is not a function
    err = assert.throws(pthread.new_with_func, {})
    assert.match(err, 'function expected,')
end

function testcase.new_with_file()
    local srcfile = os.tmpname()
    do
        local f = assert(io.open(srcfile, 'w'))
        assert(f:write([[
            local assert = require('assert')
            local th = ...
            assert.match(th, '^pthread%.self: ', false)
        ]]))
        assert(f:close())
    end

    -- test that create a new thread with file
    local th, err = pthread.new_with_file(srcfile)
    os.remove(srcfile)
    assert.match(th, 'pthread: 0x%x+', false)
    assert.is_nil(err)
    assert(th:join())
    local status, errmsg = th:status()
    assert.equal(status, 'terminated')
    assert.is_nil(errmsg)

    -- test that return error if file is invalid
    th, err = pthread.new_with_file('unknown_file')
    assert.is_nil(th)
    assert.match(err, 'unknown_file')
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
    local th = assert(pthread.new([[
        local assert = require('assert')
        local th = ...
        assert.match(th, '^pthread.self: ', false)
    ]]))

    -- test that return 'running' when thread is running
    assert.equal(th:status(), 'running')

    -- test that join a thread
    assert(th:join())

    -- test that return 'terminated' when thread is terminated
    assert.equal(th:status(), 'terminated')

    -- test that return true when thread is terminated
    assert.is_true(th:join(), true)

    -- test that return error messaage when thread is failed
    th = assert(pthread.new([[
        local function test()
            local foo = bar + 'foo'
        end
        test()
    ]]))
    assert(th:join())
    local status, errmsg = th:status()
    assert.equal(status, 'failed')
    assert.re_match(errmsg, 'attempt to', 'i')
end

function testcase.join_timeout()
    local th = assert(pthread.new([[
        require('testcase.timer').sleep(0.1)
    ]]))

    -- test that join a thread even if fd is closed
    local ok, err, timeout = th:join(0.02)
    assert.is_false(ok)
    assert.is_nil(err)
    assert.is_true(timeout)
    assert.equal(th:status(), 'running')

    assert(th:join(90))
    assert.equal(th:status(), 'terminated')
end

function testcase.join_even_fd_closed()
    local th = assert(pthread.new([[
        require('testcase.timer').sleep(0.1)
    ]]))
    close(th.thread:fd())
    sleep(0.2)

    -- test that join a thread even if fd is closed
    assert(th:join())
    assert.equal(th:status(), 'terminated')
end

function testcase.cancel()
    local th = assert(pthread.new([[
        require('testcase.timer').sleep(1)
    ]]))
    -- wait for thread to load module
    sleep(.1)

    -- test that cancel a thread
    local ok, err = th:cancel()
    assert.is_true(ok)
    assert.is_nil(err)
    assert(th:join())
    -- confirm that status is 'cancelled' when thread is canceled
    assert.equal(th:status(), 'cancelled')

    -- test that return true even if thread is already canceled
    ok, err = th:cancel()
    assert.is_true(ok)
    assert.is_nil(err)
end

function testcase.cancel_notify()
    local th = assert(pthread.new([[
        local sleep = require('testcase.timer').sleep
        local th = ...
        while not th:is_cancelled() do
            sleep(0.1)
        end
    ]]))
    -- wait for thread to load module
    sleep(.1)

    -- test that notify a thread to cancel
    local ok, err = th:cancel(true)
    assert.is_true(ok)
    assert.is_nil(err)
    assert(th:join())
    -- confirm that status is 'terminated'
    assert.equal(th:status(), 'terminated')

    -- test that return true even if thread is already canceled
    ok, err = th:cancel(true)
    assert.is_true(ok)
    assert.is_nil(err)
end

