-- games/lib/frame.lua — generic host-driven frame loop.
--
-- A real guest title loop races init, so the host drives one frame per
-- tick: call the app's draw entry, present, pump the window queue, dispatch
-- input, drain queued callbacks, sleep a frame. Everything per-game is a
-- callback the caller supplies.

local M = {}

-- opts:
--   draw()        -> truthy to continue, falsy to fail out
--   on_events(ev) -> optional input dispatch (ev.pointers / ev.keys)
--   drain()       -> optional queued-callback delivery
--   on_tick(n)    -> optional per-frame hook (frame counter / telemetry)
--   present_tag   -> armr.present tag (default "frame")
--   tick_ms       -> inter-frame sleep (default 16)
--   timeout_s     -> cap (default ARMR_RENDER_TIMEOUT env or 600)
--   log           -> optional io sink for frame/close lines
-- Returns "closed" | "timeout" | "error" | "stopped".
function M.run(opts)
    local log = opts.log or function(s) io.write(s .. "\n"); io.flush() end
    local timeout_s = opts.timeout_s or armr.env_int("ARMR_RENDER_TIMEOUT", 600)
    local start = armr.now_ms()
    local frames = 0
    while not armr.stop_requested() do
        if not opts.draw() then return "error", frames end
        frames = frames + 1
        if opts.on_tick then opts.on_tick(frames) end
        armr.present(opts.present_tag or "frame")
        local ev = armr.pump()
        if ev.close then
            log("ISOMR_INPUT window-close-requested")
            return "closed", frames
        end
        if opts.on_events then opts.on_events(ev) end
        if opts.drain then opts.drain() end
        if (armr.now_ms() - start) / 1000 >= timeout_s then
            return "timeout", frames
        end
        armr.sleep(opts.tick_ms or 16)
    end
    return "stopped", frames
end

return M
