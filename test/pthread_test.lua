local testcase = require('testcase')
local sleep = require('testcase.timer').sleep
local close = require('testcase.close')
local iowait = require('io.wait')
local new_pthread = require('pthread').new

local TMPFILE
local function new_script(source)
    if TMPFILE then
        os.remove(TMPFILE)
    end

    TMPFILE = os.tmpname()
    local f = assert(io.open(TMPFILE, 'w'))
    assert(f:write(source))
    assert(f:close())
    return TMPFILE
end

function testcase.after_each()
    if TMPFILE then
        os.remove(TMPFILE)
        TMPFILE = nil
    end
end

function testcase.new()
    -- test that create a new thread
    local th, err = new_pthread(new_script(''))
    assert.match(th, 'pthread: 0x%x+', false)
    assert.is_nil(err)

    -- test that return error if failed to create a new thread
    th, err = new_pthread(new_script('function() do end'))
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
--     local th, err, again = new_pthread(new_script(source))
--     while th do
--         threads[#threads + 1] = th
--         th, err, again = new_pthread(new_script(source))
--     end
--     assert.is_nil(err)
--     assert.is_true(again)
-- end

function testcase.join_status()
    local th = new_pthread(new_script([[
        local assert = require('assert')
        local th = ...
        assert.match(th, '^pthread.self: ', false)
    ]]))

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
    th = new_pthread(new_script([[
        local function test()
            local foo = bar + 'foo'
        end
        test()
    ]]))
    assert(iowait.readable(th:fd()))
    assert(th:join())
    local status, errmsg = th:status()
    assert.equal(status, 'failed')
    assert.re_match(errmsg, 'attempt to', 'i')
end

function testcase.cancel()
    local th = new_pthread(new_script([[
        require('testcase.timer').sleep(1)
    ]]))
    -- wait for thread to load module
    sleep(.1)

    -- test that cancel a thread
    assert(th:cancel())
    assert(iowait.readable(th:fd()))
    assert(th:join())

    -- test that return 'canceled' when thread is canceled
    assert.equal(th:status(), 'cancelled')
end

function testcase.fd()
    -- test that return fd that can be used to wait for thread termination
    local th = new_pthread(new_script([[
        require('testcase.timer').sleep(0.1)
    ]]))
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
    local th = new_pthread(new_script([[
        require('testcase.timer').sleep(0.1)
    ]]))
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

