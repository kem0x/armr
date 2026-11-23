-- games/ctr.lua -- Cut the Rope (ZeptoLab, Unity + il2cpp)
--
-- Unity nativeRender driver. Ported from the C++ CTR path: NativeLoader
-- loads libunity, a spawned render thread drives initJni/nativeResume/
-- nativeSendSurfaceChangedEvent/nativeRender, and the main thread drains
-- the pending-UI-task queue (JNIBridge Runnable proxies) that the render
-- thread posts into.
--
-- Known gap: JNIBridge.invoke hands run() to a
-- native impl that dispatches via CallVoidMethodA (JNI slot 63), which the
-- compat table does not implement; the concrete run() body is also DEX
-- bytecode this layer cannot execute. invoke_runnable is exercised here
-- so the latch behavior is observable from the scripted path.

local function stage(name, result)
    io.write("ISOMR_CTR_ENTRY stage=" .. name .. " result=" .. result .. "\n")
    io.flush()
end

local KLOADER = "com/unity3d/player/NativeLoader"
local KPLAYER = "com/unity3d/player/UnityPlayer"
local KBRIDGE = "bitter/jnibridge/JNIBridge"

local render_done = false
local render_ok = false
local render_err = nil
local render_tp = nil
local gclock_cleared = false
local gclock_held_since = nil
local gclock_orphan = 0
local gclock_breaks = 0
local render_halted = false
-- Handler-callback tasks (Message.sendToTarget) are forwarded to the
-- dedicated unity-handler thread: on Android they run on the game's
-- HandlerThread, and a handleMessage that blocks inside its queued callable
-- must not freeze the main drain (or the watchdog that watches it).
local handler_queue = {}
local usend_queue = {}
local usend_probed = false
local usend_sent_tick = nil
local tap_phase = 0
local tap_next_tick = 0
local tap_count = 0
local tap_last_pair_tick = 0
local ad_init_sent = false
local era_decoded = false
local era_decoded_at = 0
local sync_poll_armed = false

-- Arm the lost-wake poll cap (and with it armr.sync_sweep) once boot
-- milestones have passed: the boot catalog phase pays 4-5 cores of spurious
-- re-wakes if it is set from launch, while the load phase needs it to heal
-- the missed job-dispatch wakes that wedge the flask loader. The cap value
-- lives in arm_sync_poll (2000ms — a 100ms cap cost 6.6 cores in c11).
local function arm_sync_poll(why, tick)
    if sync_poll_armed or not armr.set_sync_poll then
        return
    end
    -- ARMR_CTR_ARM_POLL=0 disables arming: at cpu=1 jobs run inline, so
    -- there are no cross-lane lost wakes to heal and the 2000ms cap only
    -- slows every wait (c2 era uncapped: ~5.6/s vs capped ~0.5/s).
    if armr.env_int("ARMR_CTR_ARM_POLL", 1) == 0 then
        sync_poll_armed = true
        armr.log_err(string.format(
            "ISOMR_CTR_POLL disabled why=%s tick=%d", why, tick))
        return
    end
    sync_poll_armed = true
    -- 2000ms heals a lost wake within seconds while costing ~22 re-wakes/s
    -- across all waiters. c11 measured 6.6 cores with a 100ms cap: the
    -- untimed-wait cap must stay coarse. ARMR_CTR_POLL_MS overrides.
    local cap = armr.env_int("ARMR_CTR_POLL_MS", 2000)
    armr.set_sync_poll(cap)
    armr.log_err(string.format(
        "ISOMR_CTR_POLL armed=%dms why=%s tick=%d", cap, why, tick))
end

local function require_native(class, name, sig)
    if not armr.find_native(class, name, sig) then
        error(string.format("%s.%s%s is unavailable", class, name, sig))
    end
end

-- CtrEntryBridge::LoadUnity
local function load_unity()
    require_native(KLOADER, "load", "(Ljava/lang/String;)Z")
    require_native(KLOADER, "unload", "()Z")
    local loader = armr.new_class(KLOADER)
    local dir = armr.new_string("/app")
    local loaded, err = armr.call_native(KLOADER, "load",
                                         "(Ljava/lang/String;)Z",
                                         {loader, dir})
    armr.release_ref(dir)
    armr.release_ref(loader)
    if not loaded then
        error("NativeLoader.load failed: " .. tostring(err))
    end
    if loaded == 0 then
        error("NativeLoader.load returned false")
    end
    stage("native-loader-load", "pass")
end

-- CtrEntryBridge::RenderUnityOnce: full first-frame sequence on an
-- attached thread (initJni -> nativeResume -> surfaceChanged -> render).
local function render_unity_once()
    armr.log_err("ISOMR_CTR_RSTEP require-native")
    require_native(KPLAYER, "initJni", "(Landroid/content/Context;)V")
    require_native(KPLAYER, "nativeResume", "()V")
    require_native(KPLAYER, "nativeRender", "()Z")
    armr.log_err("ISOMR_CTR_RSTEP new-player")
    local player = armr.new_object("Lcom/unity3d/player/UnityPlayer;")
    armr.log_err("ISOMR_CTR_RSTEP new-activity")
    local context = armr.new_object("Landroid/app/Activity;")
    armr.log_err("ISOMR_CTR_RSTEP initJni-call")
    local ok, err = armr.call_native(KPLAYER, "initJni",
                                     "(Landroid/content/Context;)V",
                                     {player, context})
    armr.log_err(string.format("ISOMR_CTR_RSTEP initJni-ret ok=%s", tostring(ok)))
    if not ok then
        armr.release_ref(context)
        armr.release_ref(player)
        return nil, "initJni: " .. tostring(err)
    end
    armr.log_err("ISOMR_CTR_RSTEP nativeResume-call")
    ok, err = armr.call_native(KPLAYER, "nativeResume", "()V", {player})
    armr.log_err(string.format("ISOMR_CTR_RSTEP nativeResume-ret ok=%s", tostring(ok)))
    if not ok then
        armr.release_ref(context)
        armr.release_ref(player)
        return nil, "nativeResume: " .. tostring(err)
    end
    -- Activity.onWindowFocusChanged(true): Unity gates its main-loop
    -- tick/scene logic on window focus. Without it the render path runs but
    -- Update-driven transitions (splash -> menu) never fire.
    if armr.find_native(KPLAYER, "nativeFocusChanged", "(Z)V") then
        armr.log_err("ISOMR_CTR_RSTEP focus-call")
        local fok, ferr = pcall(function()
            return armr.call_native(KPLAYER, "nativeFocusChanged",
                                    "(Z)V", {player, 1})
        end)
        armr.log_err(string.format(
            "ISOMR_CTR_RSTEP focus-ret ok=%s err=%s",
            tostring(fok), tostring(ferr)))
    else
        armr.log_err("ISOMR_CTR_RSTEP nativeFocusChanged not found")
    end
    armr.log_err("ISOMR_CTR_RSTEP surfaceChanged-call")
    ok, err = armr.call_native(KPLAYER, "nativeSendSurfaceChangedEvent",
                               "()V", {player})
    armr.log_err(string.format("ISOMR_CTR_RSTEP surfaceChanged-ret ok=%s", tostring(ok)))
    if not ok then
        armr.release_ref(context)
        armr.release_ref(player)
        return nil, "nativeSendSurfaceChangedEvent: " .. tostring(err)
    end
    armr.release_ref(context)
    -- Deliver the graphics surface before the first nativeRender. Unity's
    -- nativeRender posts a device-init runnable to the UI thread and waits on
    -- a GfxDevice cond; that runnable only proceeds once a surface is stored
    -- in the player's surface slot. nativeRecreateGfxState(apiLevel, surface)
    -- is the real handoff: it resolves the ANativeWindow and stores it. Doing
    -- it here (before the render loop) means the runnable sees a valid
    -- surface when it runs.
    armr.log_err("ISOMR_CTR_RSTEP find-recreate")
    if armr.find_native(KPLAYER, "nativeRecreateGfxState",
                        "(ILandroid/view/Surface;)V") then
        armr.log_err("ISOMR_CTR_RSTEP new-surface")
        local surface = armr.new_object("Landroid/view/Surface;")
        armr.log_err("ISOMR_CTR_RSTEP recreate-call")
        local rok, rerr = pcall(function()
            return armr.call_native(KPLAYER, "nativeRecreateGfxState",
                                    "(ILandroid/view/Surface;)V",
                                    {player, 0, surface})
        end)
        armr.log_err(string.format(
            "ISOMR_CTR_RECREATE ok=%s err=%s journal=%s",
            tostring(rok), tostring(rerr), armr.journal(48)))
        if surface then armr.release_ref(surface) end
    else
        armr.log_err("ISOMR_CTR_RECREATE nativeRecreateGfxState not found")
    end
    -- Keep the player alive and visible to the drain loop.
    g_player = player
    -- Player stays alive for the frame loop.
    return player
end

local function render_thread()
    armr.log_err("ISOMR_CTR_RSTEP attach-start")
    local attached, err = armr.attach_thread()
    armr.log_err(string.format("ISOMR_CTR_RSTEP attach-done attached=%s", tostring(attached)))
    if not attached then
        render_err = "render thread attach failed: " .. tostring(err)
        render_done = true
        return
    end
    render_tp = armr.thread_pointer()
    armr.log_err(string.format("ISOMR_CTR_RSTEP tp=%s", tostring(render_tp and armr.hex(render_tp) or "nil")))
    local player, perr = render_unity_once()
    if not player then
        render_err = perr
        render_done = true
        armr.detach_thread()
        return
    end
    local frames = 1
    local live = true
    local falses = 0
    while live and not armr.stop_requested() do
        armr.sleep(16)
        if frames == 1 then armr.log_err("ISOMR_CTR_RSTEP nativeRender-call") end
        local r, rerr = armr.call_native(KPLAYER, "nativeRender", "()Z",
                                       {player})
        if frames == 1 then armr.log_err(string.format("ISOMR_CTR_RSTEP nativeRender-ret r=%s", tostring(r))) end
        if r == nil then
            -- Hard dispatch failure (session aborted): stop.
            render_err = rerr
            armr.log_err(string.format(
                "ISOMR_CTR_RENDER_ERR err=%s journal=%s",
                tostring(rerr), armr.journal(40)))
            live = false
        else
            -- nativeRender returns false while the GfxDevice is still being
            -- created by the UI-thread runnable; a real Choreographer loop
            -- keeps pumping rather than treating the first false as fatal.
            frames = frames + 1
            if r == false then
                falses = falses + 1
                if falses % 60 == 0 then
                    io.write("ISOMR_CTR_RENDER_FALSE n=" .. falses .. "\n")
                    io.flush()
                end
            end
        end
        if frames % 120 == 0 then
            io.write("ISOMR_CTR_FRAMES count=" .. frames .. "\n")
            io.flush()
        end
    end
    io.write("ISOMR_CTR_FRAMES total=" .. frames .. "\n")
    io.flush()
    armr.release_ref(player)
    armr.detach_thread()
    render_ok = live
    render_done = true
end

function main()
    -- Seed a large Boehm initial heap before libil2cpp's GC_init reads env
    -- during load_unity. The deserialize expands the heap ~1MB at a time under
    -- GC_allocate_ml; a big initial reservation lets Boehm reuse hblocks
    -- instead of churning expands.
    if armr.guest_setenv then
        local heap_mb = armr.env_int("ARMR_CTR_INITIAL_HEAP_MB", 4096)
        armr.guest_setenv("GC_INITIAL_HEAP_SIZE",
                          string.format("%d", heap_mb * 1024 * 1024))
        armr.log_err(string.format(
            "ISOMR_CTR_GC_ENV GC_INITIAL_HEAP_SIZE=%dMB", heap_mb))
    end
    load_unity()
    -- Probe: disable the Boehm GC collector before the render worker spawns.
    -- IL2CPP metadata init deadlocks all attached threads on the byte-lock at
    -- libil2cpp+0x6138498 once a collection starts and cannot complete its
    -- stop-the-world handshake under emulation; if the stall vanishes with
    -- collection disabled that confirms the GC lock as the starvation source.
    local gc_disable = "skipped"
    local gc_err = "disabled by ARMR_CTR_GC_DISABLE=0"
    if armr.env_int("ARMR_CTR_GC_DISABLE", 1) == 1 then
        gc_disable, gc_err = armr.call_export("il2cpp_gc_disable", {})
    end
    armr.log_err(string.format("ISOMR_CTR_GC_DISABLE ok=%s err=%s",
        tostring(gc_disable), tostring(gc_err)))
    -- Probe which Boehm/GC symbols are exported so a heap pre-expand can be
    -- attempted to cut the deserialize's per-alloc expand churn.
    for _, sym in ipairs({
        "GC_expand_hp", "GC_expand_hp_inner", "GC_get_heap_usage_safe",
        "GC_set_free_space_divisor", "GC_init", "GC_dont_expand",
        "GC_get_total_bytes", "GC_enable_incremental", "GC_gcollect",
        "GC_set_dont_precollect", "GC_free_space_divisor",
        "GC_set_full_freq", "GC_get_prof_stats"}) do
        local addr = armr.find_export(sym)
        armr.log_err(string.format("ISOMR_CTR_GCSYM %s addr=%s",
            sym, addr and armr.hex(addr) or "nil"))
    end
    for _, m in ipairs(armr.registered_natives(KPLAYER)) do
        io.write(string.format(
            "ISOMR_CTR_NATIVE class=%s name=%s signature=%s\n",
            m.class, m.name, m.sig))
    end
    io.flush()
    require_native(KPLAYER, "nativeRender", "()Z")

    -- Unity presents through the host window; ANGLE blocks on vsync for
    -- hidden windows, so surface it before rendering.
    armr.show_window()
    stage("window-show", "pass")

    armr.spawn("unity-render", render_thread)

    -- Watchdog: if the first render call never returns, dump the journal
    -- and exit like the C++ watchdog did.
    local timeout_s = armr.env_int("ARMR_RENDER_TIMEOUT", 600)
    armr.log_err(string.format(
        "ISOMR_CTR_WATCHDOG armed timeout_s=%d", timeout_s))
    armr.spawn("unity-watchdog", function()
        local deadline = armr.now_ms() + timeout_s * 1000
        local next_pcs_report = armr.now_ms() + 30000
        while not armr.stop_requested() and not render_done do
            if armr.now_ms() >= next_pcs_report then
                next_pcs_report = next_pcs_report + 30000
                armr.log_err(string.format(
                    "ISOMR_CTR_WATCHDOG_PCS pcs=%s", armr.guest_pcs()))
            end
            if armr.now_ms() >= deadline then
                armr.log_err(string.format(
                    "ISOMR_CTR_WATCHDOG stage=native-render result=fail " ..
                    "pending-ui=%d pcs=%s",
                    armr.pending_ui_count(), armr.guest_pcs()))
                armr.log_err(string.format(
                    "ISOMR_CTR_WATCHDOG_THREADS threads=%s imports=%s",
                    armr.threads_describe(), armr.journal(32)))
                armr.exit(8)
            end
            armr.sleep(1000)
        end
    end)

    -- Handler-callback worker: tasks queued by Message.sendToTarget carry
    -- the handler's handleMessage dispatch; run them on their own attached
    -- thread so a blocking callable can't wedge the main drain.
    armr.spawn("unity-handler", function()
        local attached, aerr = armr.attach_thread()
        if not attached then
            armr.log_err(string.format(
                "ISOMR_CTR_HANDLER attach-fail err=%s", tostring(aerr)))
            return
        end
        while not armr.stop_requested() and not render_done do
            local task = table.remove(handler_queue, 1)
            if task then
                armr.log_err(string.format(
                    "ISOMR_CTR_HINVOKE begin handle=%s class=%s " ..
                    "name=%s sig=%s arg=%s",
                    armr.hex(task.handle), tostring(task.class),
                    tostring(task.name), tostring(task.sig),
                    armr.hex(task.arg or 0)))
                local ok, err = armr.invoke_peer(task.handle, task.class,
                                                 task.name, task.sig,
                                                 task.arg)
                armr.log_err(string.format(
                    "ISOMR_CTR_HINVOKE end handle=%s ok=%s err=%s exc=%s",
                    armr.hex(task.handle), tostring(ok), tostring(err),
                    tostring(armr.has_exception())))
            else
                armr.sleep(1)
            end
        end
        armr.detach_thread()
    end)

    -- Vsync worker: Swappy's frame pacer parks on a frame counter that
    -- Android's Choreographer advances via FrameCallback.doFrame(J)V each
    -- vsync. No real Choreographer runs, so deliver a synthetic ~60Hz tick
    -- on a dedicated attached thread — a blocking doFrame must not wedge
    -- the drain, mirroring the handler worker's isolation.
    armr.spawn("unity-vsync", function()
        local attached, aerr = armr.attach_thread()
        if not attached then
            armr.log_err(string.format(
                "ISOMR_CTR_VSYNC attach-fail err=%s", tostring(aerr)))
            return
        end
        local vsync_handle = nil
        local tick_nanos = armr.now_ms() * 1000000
        local vsync_ticks = 0
        while not armr.stop_requested() and not render_done do
            -- The mode-1 period getter gates on a flag BYTE at prov+0
            -- (ldrb). The ctor clears it when its display-service init
            -- checks fail; other bytes of the qword hold live data, so
            -- test only the byte and re-poke every iteration so a pacer
            -- wait never enters with a cleared flag.
            do
                local prov_now = armr.r64(0x410137ca50) or 0
                if prov_now ~= 0 and
                    (armr.r8(prov_now) or 0) == 0 then
                    armr.w8(prov_now, 1)
                    armr.log_err(string.format(
                        "ISOMR_CTR_VSYNC prov-flag poked prov=%s",
                        armr.hex(prov_now)))
                end
                -- Present gate 0x735b04 returns cached_flag && !disabled.
                -- Swappy's swap path needs the display-manager instance that
                -- the broken onDisplayAdded chain never built, so the gate
                -- flag is cleared: present() then takes the direct
                -- eglSwapBuffers branch at libunity+0x735d2c.
                if (armr.r8(0x41013474f0) or 0) ~= 0 then
                    armr.w8(0x41013474f0, 0)
                    armr.log_err("ISOMR_CTR_VSYNC swappy-gate cleared")
                end
            end
            if not vsync_handle then
                local p = armr.find_interface_proxy(
                    "android/view/Choreographer$FrameCallback")
                if p then
                    vsync_handle = p.handle
                    armr.log_err(string.format(
                        "ISOMR_CTR_VSYNC proxy ref=%s handle=%s",
                        armr.hex(p.ref), armr.hex(p.handle)))
                else
                    armr.sleep(50)
                end
            else
                tick_nanos = tick_nanos + 16666666
                local ref = armr.new_long(tick_nanos)
                local ok, err = armr.invoke_peer(
                    vsync_handle,
                    "android/view/Choreographer$FrameCallback",
                    "doFrame", "(J)V", ref)
                -- Choreographer.postFrameCallback queues one-shot callbacks;
                -- fire doFrame once per post so the guest's re-post loop is
                -- fed by real consumption, not a dropped stub.
                local posted = armr.take_frame_callbacks()
                for _, c in ipairs(posted) do
                    local cbref = armr.new_long(tick_nanos)
                    armr.invoke_peer(
                        c.handle,
                        "android/view/Choreographer$FrameCallback",
                        "doFrame", "(J)V", cbref)
                    if cbref and cbref ~= 0 then
                        armr.release_ref(cbref)
                    end
                end
                if #posted > 0 and vsync_ticks <= 3 then
                    armr.log_err(string.format(
                        "ISOMR_CTR_VSYNC posted=%d drained", #posted))
                end
                if ref and ref ~= 0 then armr.release_ref(ref) end
                vsync_ticks = vsync_ticks + 1
                if not ok or vsync_ticks <= 3 or vsync_ticks % 300 == 0 then
                    -- Probe the pacer's frame counter trio: mutex at
                    -- +0xc50, condvar at +0xc78, counter at +0xca8
                    -- (libunity 0x134b000 base; seen in the parked wait).
                    local cbase = 0x410134b000
                    local r8 = armr.r64(0x41013474f0) or 0
                    local prov = armr.r64(0x410137ca50) or 0
                    local provq0, provq48, prov70, prov1a0 = 0, 0, 0, 0
                    if prov ~= 0 then
                        provq0 = armr.r64(prov) or 0
                        provq48 = armr.r64(prov + 0x48) or 0
                        prov70 = armr.r64(prov + 0x70) or 0
                        prov1a0 = armr.r64(prov + 0x1a0) or 0
                    end
                    armr.log_err(string.format(
                        "ISOMR_CTR_VSYNC tick=%d ok=%s err=%s exc=%s " ..
                        "ctr=%s cb0=%s cc8=%s fbase=%s lastd=%s " ..
                        "mode=%s flags=%s prov=%s pq0=%s p70=%s p1a0=%s",
                        vsync_ticks, tostring(ok), tostring(err),
                        tostring(armr.has_exception()),
                        armr.hex(armr.r64(cbase + 0xca8) or 0),
                        armr.hex(armr.r64(cbase + 0xcb0) or 0),
                        armr.hex(armr.r64(cbase + 0xcc8) or 0),
                        armr.hex(armr.r64(0x4101346000) or 0),
                        armr.hex(armr.r64(0x41012c1550) or 0),
                        armr.hex(armr.r64(0x41013474fc) or 0),
                        armr.hex(r8), armr.hex(prov),
                        armr.hex(provq0), armr.hex(prov70),
                        armr.hex(prov1a0)))
                end
                armr.sleep(16)
            end
        end
        armr.detach_thread()
    end)

    -- Heap bounds are dynamic: ARMR_GUEST_HEAP_BASE relocates the arena into
    -- the Dynarmic direct page-table window, so the JobQueue/job-group arena
    -- floor must be computed from the live base rather than hardcoding the old
    -- 0x7100… layout.
    local hbase = (armr.heap_base and armr.heap_base()) or 0x710000000000
    local jq_floor = hbase + 0x30000000
    -- Dump a job-group / lane region that lives in the PROT_NONE-reserved
    -- JobQueue arena (hbase+0x3000… region) which address_space can now read
    -- via the ReadPhys committed-page fallback. hex-dumps `words` u64s.
    local function jobq_hex(addr, words)
        local out = {}
        for i = 0, words - 1 do
            local ok, v = pcall(armr.r64, addr + i * 8)
            out[#out + 1] = (ok and v) and string.format("%x", v) or "??"
        end
        return table.concat(out, " ")
    end
    -- Discover candidate job-group addresses: the shared pointer that several
    -- parked threads carry in x20 (the JobGroupID they WaitForJobGroupID on),
    -- then dump the group header and each member's wait word.
    local function dump_jobq(pcs)
        local groups = {}
        for entry in string.gmatch(pcs, "handle0x[^|]+") do
            local x20 = entry:match("%-x20=0x(%x+)")
            -- Job-group pointers live in the heap+0x20000000..heap+0x50000000
            -- arena and are shared across joiners in x20 (the JobGroupID they
            -- wait on). Heap-relative so ARMR_GUEST_HEAP_BASE relocation holds.
            local gv = x20 and tonumber(x20, 16)
            if gv and gv >= hbase + 0x20000000 and gv < hbase + 0x50000000 then
                groups[x20] = (groups[x20] or 0) + 1
            end
        end
        for g, n in pairs(groups) do
            if n >= 2 then
                local ga = tonumber(g, 16)
                armr.log_err(string.format(
                    "ISOMR_CTR_JOBGROUP grp=0x%s waiters=%d hdr=%s",
                    g, n, jobq_hex(ga, 16)))
                -- Decode the group state word (grp+0): byte+4 is the pending
                -- count, byte+5 the done count. pend>0 frozen across dumps =
                -- deadlock; pend decreasing = a slow-but-progressing drain.
                local okp, pv = pcall(armr.r8, ga + 4)
                local okd, dv = pcall(armr.r8, ga + 5)
                if okp and pv then
                    armr.log_err(string.format(
                        "ISOMR_CTR_GSTATE grp=0x%s pend=%d done=%d",
                        g, pv, (okd and dv) or -1))
                else
                    armr.log_err(string.format(
                        "ISOMR_CTR_GSTATE grp=0x%s gstate=unreadable " ..
                        "(jobgroupid handle?)", g))
                end
                -- Dump the region around the group so head/tail/pending
                -- fields and any queued job pointers are visible.
                armr.log_err(string.format(
                    "ISOMR_CTR_JOBGROUPX grp=0x%s body=%s",
                    g, jobq_hex(ga + 0x40, 24)))
            end
        end
    end
    -- UI drain: the render thread posts JNIBridge Runnable proxies; the
    -- launcher thread invokes them so synchronous runOnUiThread callers
    -- can complete.
    local bridge_natives_dumped = false
    local drain_ticks = 0
    local swappy_cookie = nil
    local display_ref = nil
    -- Frame-completion release: the render thread paces on a heap futex flag
    -- (expected=0, ~10ms timed retry) that the stubbed present path never
    -- publishes — the producer broadcasts the Swappy condvar to zero waiters
    -- instead. With no real display/vsync, satisfy the flag directly: a futex
    -- parked with a live waiter on *f==0 in the regular heap (below the
    -- PROT_NONE JobQueue arena 0x710030+) is an expected-0 completion wait —
    -- an unlocked mutex has no blocked waiters — so writing non-zero releases
    -- exactly that waiter. Require the same address to stay stuck across
    -- consecutive scans so transient waits are never released early.
    local stuck_scans = {}
    local gc_cleared = 0
    -- The render thread polls the frame-completion flag every ~10ms; releasing
    -- it after just a couple scans (~one guest poll interval) restores real
    -- frame pacing instead of parking ~384ms/frame. The *f==0 + heap filter +
    -- live-waiter + persistence checks still keep transient/job-completion
    -- waits from being released prematurely.
    local FP_RELEASE_AFTER = 2
    local function release_stuck_frame_waits()
        if not armr.futex_waiters then return end
        local desc = armr.futex_waiters()
        if not desc or desc == "none" then return end
        local seen = {}
        for addr, w, word in desc:gmatch(
            "f=0x(%x+) w=(%d+) n=%d+ %*f=0x(%x+)") do
            local a = tonumber(addr, 16)
            if a and a >= hbase and a < jq_floor and tonumber(w) >= 1 and
                tonumber(word, 16) == 0 then
                seen[a] = true
                local n = (stuck_scans[a] or 0) + 1
                stuck_scans[a] = n
                if n >= FP_RELEASE_AFTER then
                    armr.w32(a, 1)
                    armr.log_err(string.format(
                        "ISOMR_CTR_FP release f=%s scans=%d",
                        armr.hex(a), n))
                    stuck_scans[a] = 0
                end
            end
        end
        -- Decay counters for futexes that resolved or left the heap filter.
        for a in pairs(stuck_scans) do
            if not seen[a] then stuck_scans[a] = nil end
        end
    end
    armr.log_err("ISOMR_CTR_DRAIN loop-enter")
    while true do
        if drain_ticks <= 60 then
            armr.log_err(string.format(
                "ISOMR_CTR_DRAIN tick=%d stage=pre-stop", drain_ticks))
        end
        if armr.stop_requested() then break end
        if render_done then break end
        drain_ticks = drain_ticks + 1
        -- Opt-in lost-wake sweep (active only under ISOMR_SYNC_POLL_MS).
        -- Bounded futex waits already self-heal via the poll clamp, so the
        -- sweep's unique job is waking a mutex or untimed-cond waiter whose
        -- release signal was dropped. ~1/sec is enough and keeps the
        -- thread-heavy boot phase from drowning in spurious wakeups.
        -- Work-steal heal: waking parked JobQueue workers lets them re-scan
        -- the work deques and steal an orphaned job -> real completion, not a
        -- faked flag. The 2000ms wait cap is the primary self-heal; this
        -- thundering-herd wake is coarse insurance only — every 64 ticks,
        -- not 8 (c11 measured the 8-tick rate pinning extra cores).
        if drain_ticks % 64 == 0 and armr.sync_sweep then
            armr.sync_sweep()
            -- Boot-floor fallback: if the tap gate never armed the poll
            -- (no g_player, slow boot), the load phase still needs it.
            if drain_ticks >= 45000 then
                arm_sync_poll("boot-floor", drain_ticks)
            end
        end
        -- Event-driven gate: serve #1 is catalog.json, #2 the first asset
        -- bundle — boot's catalog phase is done and the load phase (with its
        -- lost-wake wedges) is beginning. Tick counts vary 30x between runs,
        -- so watch serves, not ticks.
        if not sync_poll_armed and armr.served_count and
            armr.served_count() >= 2 then
            arm_sync_poll("served-2", drain_ticks)
        end
        if drain_ticks % 4 == 0 then
            release_stuck_frame_waits()
        end
        -- GC-lock diagnostic: the deserialize backs off in Boehm's
        -- GC_generic_lock (libil2cpp+0x269b6c4) whenever the collection /
        -- world-stop flag byte at il2cpp+0x6138994 is set. Read the flags the
        -- lock path consults plus a thread-handle window around the lock byte
        -- 0x6138498 to identify whether a collection is stuck in-flight (a
        -- suspend we don't deliver) vs merely contended. libil2cpp loads at
        -- guest base 0x4200000000.
        -- gcf8c (0x5f0cf8c) forces the slow nanosleep backoff path in
        -- GC_generic_lock while the lock byte is actually free. If it is a
        -- stale "collection in progress" flag (collector deadlocked on a
        -- suspend we never deliver), clearing it lets allocs fast-spin.
        -- Clear aggressively; the %750 read then reports whether a live
        -- collector re-set it (still 1) or it stayed clear (stuck flag).
        do
            local b0 = 0x4200000000
            local ok, g = pcall(armr.r32, b0 + 0x5f0cf8c)
            if ok and g == 1 then
                pcall(armr.w32, b0 + 0x5f0cf8c, 0)
                gc_cleared = gc_cleared + 1
            end
        end
        if drain_ticks % 750 == 0 then
            local b = 0x4200000000
            local function rb(o) local ok,v = pcall(armr.r8, b + o); return ok and v or -1 end
            local function rq(o) local ok,v = pcall(armr.r64, b + o); return ok and v or 0 end
            local function rdw(o) local ok,v = pcall(armr.r32, b + o); return ok and v or -1 end
            armr.log_err(string.format(
                "ISOMR_CTR_GC tick=%d needlock=%d collectflag=%d " ..
                "gcf8c=%d gcf90=%d mlbyte=%d holderw=%s cleared=%d",
                drain_ticks, rdw(0x6138490), rb(0x6138994),
                rdw(0x5f0cf8c), rdw(0x5f0cf90), rb(0x6138498),
                armr.hex(rq(0x61384a0)), gc_cleared))
        end
        if drain_ticks <= 500 then
            armr.log_err(string.format(
                "ISOMR_CTR_DRAIN tick=%d stage=pre-take", drain_ticks))
        elseif drain_ticks % 250 == 0 then
            armr.log_err(string.format(
                "ISOMR_CTR_DRAIN tick=%d pending=%d render_done=%s",
                drain_ticks, armr.pending_ui_count(),
                tostring(render_done)))
        end
        if drain_ticks % 3000 == 0 then
            local pcs = armr.guest_pcs()
            armr.log_err(string.format(
                "ISOMR_CTR_PCS tick=%d pcs=%s",
                drain_ticks, pcs))
            -- Inspect each futex-parked worker's wait word. x0=0x62 is the
            -- futex syscall number, x1 the futex uaddr (the real wait word),
            -- x3 the expected value the guest compares against. If *x1 has
            -- already diverged from x3 while the waiter stays parked, the
            -- producer updated the word but its FUTEX_WAKE was lost (lost
            -- wake the poll/sweep heals). If *x1 == x3, the producer never
            -- published the sequence — a structural stall, not a dropped
            -- signal.
            local fwt = 0
            for entry in string.gmatch(pcs, "handle0x[^|]+") do
                local pc = entry:match("%-pc0x(%x+)")
                local x0 = entry:match("%-x0=0x(%x+)")
                local x1 = entry:match("%-x1=0x(%x+)")
                local x3 = entry:match("%-x3=0x(%x+)")
                local x20 = entry:match("%-x20=0x(%x+)")
                if pc == "68000000074c" and x0 == "62" and x1 then
                    fwt = fwt + 1
                    if fwt <= 14 then
                        local wa = tonumber(x1, 16)
                        local ok, wv = pcall(armr.r32, wa)
                        local cur = (ok and wv) and string.format("0x%x", wv)
                                    or "unreadable"
                        local verdict = "?"
                        if ok and wv and x3 then
                            local exp = tonumber(x3, 16)
                            verdict = (wv ~= exp) and "LOSTWAKE"
                                      or "structural"
                        end
                        armr.log_err(string.format(
                            "ISOMR_CTR_FW uaddr=0x%s *uaddr=%s exp=0x%s " ..
                            "grp=0x%s %s",
                            x1, cur, x3 or "?", x20 or "?", verdict))
                    end
                end
            end
            armr.log_err(string.format("ISOMR_CTR_FW waiters=%d", fwt))
            -- Authoritative futex view: the sync layer's own registry dumps
            -- each futex with live waiters plus the word re-read via ReadWord
            -- (the exact path the wait used). A word already advanced past
            -- the guest's expected value is a lost wake; a word still at the
            -- pre-signal value is a structural stall. "unreadable" means the
            -- word's page was decommitted out from under a parked waiter.
            if armr.futex_waiters then
                armr.log_err(string.format(
                    "ISOMR_CTR_FUTEX tick=%d %s",
                    drain_ticks, armr.futex_waiters()))
            end
            -- Dump the shared job-group headers + wait counters: reveals
            -- whether pending jobs sit undispatched on a lane (structural
            -- stall) vs the group simply idling between batches.
            dump_jobq(pcs)
            -- Era decoder: the render thread parks in the libunity+0xec00c0
            -- JobQueue wait helper with x19=group base, x20=0xc280-style
            -- timeout ms, x21=timeout<<32|lane-mask (0x5dc000000c0 =
            -- 1500ms, lanes 6-7). Dump the group head words + the decoded
            -- mask so the wait's true target (which lanes it needs drained)
            -- is on record instead of theorized.
            if (not era_decoded or drain_ticks - (era_decoded_at or 0) > 30000) and pcs then
                era_decoded_at = drain_ticks
                for entry in string.gmatch(pcs, "handle0x[^|]+") do
                    local x19 = entry:match("%-x19=0x(%x+)")
                    local x21 = entry:match("%-x21=0x(%x+)")
                    local lr = entry:match("caller=libunity%.so%+0xec00c0")
                    if lr and x19 and x21 then
                        local g = tonumber(x19, 16)
                        local m = tonumber(x21, 16)
                        if g and g > 0x100000000000 and m and m > 0 then
                            local okd, body = pcall(jobq_hex, g, 16)
                            era_decoded = true
                            armr.log_err(string.format(
                                "ISOMR_CTR_ERADEC grp=%s x21=%s to=%dms mask=0x%x body=%s",
                                armr.hex(g), x21, math.floor(m / 0x100000000),
                                m % 0x100000000,
                                okd and body or "read-fail"))
                            break
                        end
                    end
                end
            end
            -- Per-thread residency + start routine: names the parked
            -- participants when the job-group wedges (jit = running guest,
            -- host = blocked inside a host primitive call).
            armr.log_err(string.format(
                "ISOMR_CTR_THREADS tick=%d %s",
                drain_ticks, armr.threads_describe()))
            if armr.locks_describe then
                armr.log_err(string.format(
                    "ISOMR_CTR_LOCKS tick=%d %s",
                    drain_ticks, armr.locks_describe()))
            end
        end
        local ui_tasks = armr.take_ui_tasks()
        if drain_ticks <= 500 then
            armr.log_err(string.format(
                "ISOMR_CTR_DRAIN tick=%d stage=post-take n=%d",
                drain_ticks, #ui_tasks))
        end
        -- UnitySendMessage deliveries queued by compat stubs (the
        -- CtrAndroidCloudReader Play-Games-absent path). The script host
        -- forwards them through the bound nativeUnitySendMessage so the
        -- managed message pump dispatches them on the next Update.
        if not usend_probed and drain_ticks >= 50 then
            usend_probed = true
            local ok, res, cerr = pcall(function()
                local go = armr.new_string("IsomrProbeSink")
                local meth = armr.new_string("OnProbe")
                local bytes = armr.new_byte_array("")
                return armr.call_native(
                    KPLAYER, "nativeUnitySendMessage",
                    "(Ljava/lang/String;Ljava/lang/String;[B)V",
                    {0, go, meth, bytes})
            end)
            armr.log_err(string.format(
                "ISOMR_CTR_USEND_PROBE ok=%s res=%s err=%s",
                tostring(ok), tostring(res), tostring(cerr)))
        end
        for _, msg in ipairs(armr.take_unity_messages()) do
            usend_queue[#usend_queue + 1] = msg
        end
        local usend_retry = {}
        for _, msg in ipairs(usend_queue) do
            msg.tries = (msg.tries or 0) + 1
            local ok, res, cerr = pcall(function()
                local go = armr.new_string(msg.go)
                local meth = armr.new_string(msg.method)
                local bytes = armr.new_byte_array(msg.data)
                return armr.call_native(
                    KPLAYER, "nativeUnitySendMessage",
                    "(Ljava/lang/String;Ljava/lang/String;[B)V",
                    {0, go, meth, bytes})
            end)
            if ok and res ~= nil then
                armr.log_err(string.format(
                    "ISOMR_CTR_USEND go=%s method=%s SENT tries=%d",
                    msg.go, msg.method, msg.tries))
                usend_sent_tick = usend_sent_tick or drain_ticks
            else
                if msg.tries <= 3 or msg.tries % 500 == 0 then
                    armr.log_err(string.format(
                        "ISOMR_CTR_USEND go=%s method=%s " ..
                        "try=%d ok=%s res=%s err=%s",
                        msg.go, msg.method, msg.tries, tostring(ok),
                        tostring(res), tostring(cerr)))
                end
                if msg.tries < 5000 then
                    usend_retry[#usend_retry + 1] = msg
                else
                    armr.log_err(string.format(
                        "ISOMR_CTR_USEND go=%s DROPPED after %d tries",
                        msg.go, msg.tries))
                end
            end
        end
        usend_queue = usend_retry
        -- Tap-to-continue: the ZeptoLab splash parks on tapTcs until a
        -- touch arrives. Once the cloud callback has been delivered (or
        -- the boot has clearly stalled), drive ACTION_DOWN/UP pairs at
        -- screen center through the bound nativeInjectEvent.
        if g_player then
            if tap_phase == 0 and
                ((usend_sent_tick and
                  drain_ticks >= usend_sent_tick + 240) or
                 drain_ticks >= 30000) then
                tap_phase = 1
                tap_next_tick = drain_ticks
                -- Splash/cloud milestone reached: boot is over, the load
                -- phase (and its lost-wake wedges) is next.
                arm_sync_poll("tap-gate", drain_ticks)
                -- AppLovin MAX init burst: the Java-side SDK init is
                -- JNI-stubbed to nothing (guest log: "Listener has been
                -- added to callback: OnSdkInitializedEvent" then silence),
                -- so the C# AppLovinCallback never receives
                -- OnSdkInitialized and the post-tap loader waits for ad
                -- init forever (the flask era). Fire the MAX plugin's
                -- public callback shape directly; C# dispatch logs which
                -- receivers exist ("has no receivers" otherwise).
                if not ad_init_sent then
                    ad_init_sent = true
                    usend_queue[#usend_queue + 1] = {
                        go = "AppLovinCallback",
                        method = "OnSdkConfigurationReady",
                        data = '{"sdkKey":"CTR-ARMR-STUB"}' }
                    usend_queue[#usend_queue + 1] = {
                        go = "AppLovinCallback",
                        method = "OnSdkInitialized",
                        data = '{"sdkKey":"CTR-ARMR-STUB"}' }
                    usend_queue[#usend_queue + 1] = {
                        go = "AppLovinCallback",
                        method = "OnMediationReady",
                        data = "true" }
                end
            elseif tap_phase == 3 and drain_ticks >= tap_next_tick then
                tap_phase = 1
            end
            -- Forced re-arm: on c19 the phase machine stalled with
            -- tap_next_tick parked ahead of the (slow, wait-capped) tick
            -- clock, yielding only ~5 tap pairs across the whole load era.
            -- Guarantee one fresh pair every ~3000 ticks regardless of
            -- how phase 3 parked.
            if tap_phase == 3 and drain_ticks - (tap_last_pair_tick or 0) > 3000 then
                tap_phase = 1
                tap_next_tick = drain_ticks
            end
            if tap_phase >= 3 and drain_ticks % 500 == 0 then
                armr.log_err(string.format(
                    "ISOMR_CTR_TAPSTATE phase=%d count=%d next=%d tick=%d",
                    tap_phase, tap_count, tap_next_tick, drain_ticks))
            end
            if (tap_phase == 1 or tap_phase == 2) and
                drain_ticks >= tap_next_tick then
                local action = tap_phase == 1 and 0 or 1
                -- Cycle candidate play-control coordinates: the main menu's
                -- Play button is not always dead-center, so sweep the spots
                -- CTR actually uses (center, then the lower-center band where
                -- the big play arrow sits, then the corners of the central
                -- play area) so one of them lands on the control.
                local tap_targets = {
                    {640.0, 360.0},  -- dead center / Om Nom
                    {640.0, 560.0},  -- lower-center play button
                    {640.0, 620.0},  -- bottom-center
                    {640.0, 480.0},  -- mid-lower
                    {980.0, 560.0},  -- lower-right play arrow
                    {640.0, 240.0},  -- upper-center
                }
                local tp = tap_targets[(tap_count % #tap_targets) + 1]
                local ev = armr.new_motion_event(action, tp[1], tp[2])
                local ok, res, cerr = pcall(function()
                    return armr.call_native(
                        KPLAYER, "nativeInjectEvent",
                        "(Landroid/view/InputEvent;I)Z",
                        {g_player, ev, 0})
                end)
                armr.log_err(string.format(
                    "ISOMR_CTR_TAP action=%d n=%d ok=%s res=%s err=%s",
                    action, tap_count, tostring(ok), tostring(res),
                    tostring(cerr)))
                if tap_phase == 1 then
                    tap_phase = 2
                    tap_next_tick = drain_ticks + 3
                else
                    -- Re-arm: later taps cover additional prompts and any
                    -- first-tap race before the gate opens. Keep tapping
                    -- periodically — a post-menu consent/age dialog may
                    -- need a tap that only lands after the menu exists.
                    tap_last_pair_tick = drain_ticks
                    tap_count = tap_count + 1
                    tap_phase = tap_count < 40 and 3 or 4
                    tap_next_tick = drain_ticks + 2400
                end
            end
        end
        for _, task in ipairs(ui_tasks) do
            local handle = type(task) == "table" and task.handle or task
            -- Diagnostics are best-effort: a nil/failed read logs and the
            -- Runnable is still invoked so the latch path stays live.
            local diag_ok, diag_err = pcall(function()
                local fields = {}
                for off = 0, 0x48, 8 do
                    fields[#fields + 1] = string.format(
                        "+%02x=%s", off,
                        armr.hex(armr.r64(handle + off) or 0))
                end
                local invoke_addr = armr.native_addr(
                    KBRIDGE, "invoke",
                    "(JLjava/lang/Class;Ljava/lang/reflect/Method;" ..
                    "[Ljava/lang/Object;)Ljava/lang/Object;")
                armr.log_err(string.format(
                    "ISOMR_CTR_PROXY handle=%s invoke=%s %s",
                    armr.hex(handle),
                    invoke_addr and armr.hex(invoke_addr) or "nil",
                    table.concat(fields, " ")))
                local vt = armr.r64(handle)
                -- Runnable subobject is handle+0x28; h6 calls its vt[4].
                local vt6 = armr.r64(handle + 0x28)
                local run_impl = vt6 and armr.r64(vt6 + 0x20) or nil
                if run_impl then
                    armr.log_err(string.format(
                        "ISOMR_CTR_RUNIMPL vt6=%s run=%s",
                        armr.hex(vt6), armr.hex(run_impl)))
                end
                if vt then
                    local impl = armr.r64(vt + 0x10)
                    armr.log_err(string.format(
                        "ISOMR_CTR_VTABLE vt=%s impl=%s",
                        armr.hex(vt), impl and armr.hex(impl) or "nil"))
                    -- Dump live code bytes: invoke, impl, and the impl's
                    -- own vt[8] dispatch target (file bytes are relocated
                    -- and do not match the loaded image).
                    local vt8 = armr.r64(vt + 0x40)
                    for _, a in ipairs({invoke_addr, impl, vt8, run_impl,
                                        0x4100ee7da4, 0x4100ecbdc4,
                                        0x4100ecc060, 0x4100ecbbb8,
                                        0x4100ee1a38, 0x4100ee2b7c,
                                        0x4100ee8038, 0x4100ee6768,
                                        0x4100ec839c, 0x4100ee6868,
                                        0x4100ee5730, 0x4100ee5838,
                                        0x4100ee5a08, 0x4100ee5eec,
                                        0x4100ee7404, 0x4100ee62c8,
                                        0x41005c0f24}) do
                        if a then
                            local code = armr.read(a, 0x100)
                            if code then
                                local w = {}
                                for i = 1, #code, 4 do
                                    local b1, b2, b3, b4 =
                                        code:byte(i, i + 3)
                                    w[#w + 1] = string.format(
                                        "%02x%02x%02x%02x",
                                        b4, b3, b2, b1)
                                end
                                armr.log_err(string.format(
                                    "ISOMR_CTR_CODE addr=%s %s",
                                    armr.hex(a), table.concat(w, " ")))
                            end
                        end
                    end
                end
                -- Helper1 (Runnable) scans a static mid table at
                -- 0x41013823c0/+0x3c8/+0x3d0 once its guard byte at
                -- 0x4101382398 is set (lazy init resolves the mids).
                local function midtbl(tag)
                    local guard = armr.r8(0x4101382398)
                    local mids = {}
                    for i = 0, 3 do
                        mids[#mids + 1] = string.format(
                            "[%d]=%s", i,
                            armr.hex(armr.r64(0x41013823c0 + i * 8) or 0))
                    end
                    armr.log_err(string.format(
                        "ISOMR_CTR_MIDTBL %s guard=%s %s", tag,
                        armr.hex(guard or 0), table.concat(mids, " ")))
                end
                midtbl("pre")
                -- Each helper resolves its interface method mid(s) via
                -- wrapper 0xee6868 with these name/sig pairs.
                local helper_pairs = {
                    {"h1-obj-a", 0x4100121f1f, 0x41001624ef},
                    {"h1-obj-b", 0x410012a65a, 0x4100121ed8},
                    {"h1-obj-c", 0x41000f0231, 0x41000e7ee2},
                    {"h2", 0x410016a103, 0x41000f5631},
                    {"h3", 0x410014f296, 0x410016a10b},
                    {"h4", 0x41001647d8, 0x41000c9321},
                    {"h5", 0x41001648d1, 0x41000f57c8},
                    {"h6", 0x4100149afa, 0x4100109b94},
                    {"h6-cls", 0x410014f3e6, 0},
                }
                for _, pr in ipairs(helper_pairs) do
                    armr.log_err(string.format(
                        "ISOMR_CTR_HNAME %s name=%s sig=%s", pr[1],
                        armr.cstr(pr[2]) or "?",
                        pr[3] ~= 0 and (armr.cstr(pr[3]) or "?") or "-"))
                end
                -- What mid does our invoke path produce for Runnable.run?
                local rc = armr.new_class("java/lang/Runnable")
                if rc then
                    local rm = armr.new_method(rc, "run", "()V")
                    armr.log_err(string.format(
                        "ISOMR_CTR_OURMID class=%s method-ref=%s",
                        armr.hex(rc), rm and armr.hex(rm) or "nil"))
                end
                local tbl = armr.r64(handle + 0x30)
                local cnt = armr.r64(handle + 0x40)
                armr.log_err(string.format(
                    "ISOMR_CTR_MTBL handle=%s table=%s count=%s",
                    armr.hex(handle),
                    tbl and armr.hex(tbl) or "nil",
                    cnt and armr.hex(cnt) or "nil"))
                if tbl and tbl ~= 0 and cnt and cnt < 64 then
                    for i = 0, math.min(cnt, 8) - 1 do
                        local f0 = armr.r64(tbl + i * 0x10)
                        local f8 = armr.r64(tbl + i * 0x10 + 8)
                        armr.log_err(string.format(
                            "ISOMR_CTR_MTBL_ENTRY i=%d f0=%s f8=%s", i,
                            f0 and armr.hex(f0) or "nil",
                            f8 and armr.hex(f8) or "nil"))
                    end
                end
                if not bridge_natives_dumped then
                    bridge_natives_dumped = true
                    for _, m in ipairs(armr.registered_natives()) do
                        armr.log_err(string.format(
                            "ISOMR_CTR_BNATIVE class=%s name=%s sig=%s " ..
                            "addr=%s", m.class, m.name, m.sig,
                            armr.hex(armr.native_addr(m.class, m.name,
                                                      m.sig) or 0)))
                    end
                end
            end)
            if not diag_ok then
                armr.log_err(string.format(
                    "ISOMR_CTR_TASK handle=%s diag-error=%s",
                    armr.hex(handle), tostring(diag_err)))
            end
            if type(task) == "table" and task.class then
                -- Handler-callback dispatch: forward to the unity-handler
                -- thread (see handler_queue); invoking handleMessage here
                -- can block inside the queued callable.
                handler_queue[#handler_queue + 1] = task
                armr.log_err(string.format(
                    "ISOMR_CTR_HQ push handle=%s name=%s depth=%d",
                    armr.hex(handle), tostring(task.name), #handler_queue))
            else
                armr.log_err(string.format(
                    "ISOMR_CTR_INVOKE begin handle=%s kind=runnable",
                    armr.hex(handle)))
                local ok, err = armr.invoke_runnable(handle)
                armr.log_err(string.format(
                    "ISOMR_CTR_INVOKE end handle=%s ok=%s",
                    armr.hex(handle), tostring(ok)))
                local has_exc = armr.has_exception()
                if ok then
                    io.write(string.format(
                        "ISOMR_CTR_UI_TASK result=pass handle=%s exc=%s\n",
                        armr.hex(handle), tostring(has_exc)))
                else
                    armr.log_err(string.format(
                        "ISOMR_CTR_UI_TASK result=fail handle=%s " ..
                        "error=%s exc=%s",
                        armr.hex(handle), tostring(err), tostring(has_exc)))
                    armr.log_err(string.format(
                        "ISOMR_CTR_UI_TRACE handle=%s recent=%s",
                        armr.hex(handle), armr.journal(32)))
                end
            end
            -- Post-invoke state: all six helpers' guard + mid slots.
            pcall(function()
                local slots = {
                    {"h1", 0x4101382398, 0x41013823c0},
                    {"h2", 0x410137fd88, 0x410137fd90},
                    {"h3", 0x410137fda8, 0x410137fdb0},
                    {"h4", 0x410137fd68, 0x410137fd70},
                    {"h5", 0x4101381d40, 0x4101381d48},
                    {"h6", 0x4101381f40, 0x4101381f48},
                }
                local parts = {}
                for _, s in ipairs(slots) do
                    parts[#parts + 1] = string.format(
                        "%s(g=%s mid=%s)", s[1],
                        armr.hex(armr.r8(s[2]) or 0),
                        armr.hex(armr.r64(s[3]) or 0))
                end
                armr.log_err(string.format(
                    "ISOMR_CTR_MIDSLOTS %s", table.concat(parts, " ")))
            end)
            io.flush()
        end
        -- Swappy's SwappyDisplayManager registers a DisplayListener and
        -- expects Android to fire onDisplayAdded so it can learn the refresh
        -- period. Our DisplayManager has no real display service, so the
        -- listener is captured at registration and we deliver the period
        -- straight into nOnRefreshPeriodChanged. Without a period the frame
        -- pacer's deadline catch-up spins on a zero interval and the render
        -- thread never reaches eglSwapBuffers.
        for _, l in ipairs(armr.take_display_listeners()) do
            -- nSetSupportedRefreshPeriods builds the Swappy display-manager
            -- instance that [provider+0x30] publishes; it previously died on
            -- phantom locks, which are now tolerated. Deliver the real
            -- onDisplayAdded payload so the swap path has a live instance.
            local periods = armr.new_long_array({16666666})
            local ints = armr.new_int_array({1})
            local sp_ok, sp_err = armr.notify_supported_periods(
                l.ref, l.handle, periods, ints)
            armr.log_err(string.format(
                "ISOMR_CTR_DISPLAY ref=%s cookie=%s setperiods=%s err=%s",
                armr.hex(l.ref), armr.hex(l.handle), tostring(sp_ok),
                tostring(sp_err)))
            local ok, err = armr.notify_refresh_period(
                l.ref, l.handle, 16666666, 0, 0)
            armr.log_err(string.format(
                "ISOMR_CTR_DISPLAY ref=%s cookie=%s notify=%s err=%s",
                armr.hex(l.ref), armr.hex(l.handle), tostring(ok),
                tostring(err)))
            display_ref = l.ref
            -- Diagnostic only: dump the listener handle's vtable head. Its
            -- vt[0x20] resolved to a destructor/release slot, not
            -- onChoreographer, so this object is the wrong cookie for
            -- nOnChoreographer; do not call it.
            local vt = armr.r64(l.handle)
            armr.log_err(string.format(
                "ISOMR_CTR_CHOR dhandle=%s vt=%s vt20=%s",
                armr.hex(l.handle), armr.hex(vt),
                armr.hex(vt and armr.r64(vt + 0x20))))
        end
        -- Swappy's frame pacer sleeps on a completion flag until the Android
        -- Choreographer delivers a per-frame vsync via
        -- ChoreographerCallback.nOnChoreographer(JJ)V. With no real
        -- Choreographer running, deliver a synthetic 60Hz tick ourselves.
        if not choreo then
            local p = armr.find_interface_proxy(
                "com/google/androidgamesdk/ChoreographerCallback")
            if p then
                choreo = p
                armr.log_err(string.format(
                    "ISOMR_CTR_CHOR proxy ref=%s handle=%s",
                    armr.hex(p.ref), armr.hex(p.handle)))
            end
        end
        -- The callback may also arrive as a concrete
        -- ChoreographerCallback(J)V object rather than a JNIBridge proxy.
        for _, c in ipairs(armr.take_choreographer_callbacks()) do
            choreo = {ref = c.ref, handle = c.handle}
            armr.log_err(string.format(
                "ISOMR_CTR_CHOR_CTOR ref=%s handle=%s",
                armr.hex(c.ref), armr.hex(c.handle)))
        end
        if choreo and armr.now_ms() - (choreo_last_ms or 0) >= 16 then
            choreo_last_ms = armr.now_ms()
            local ok, err = armr.notify_choreographer(
                choreo.ref, choreo.handle, choreo_last_ms * 1000000)
            if not ok then
                armr.log_err(string.format(
                    "ISOMR_CTR_CHOR_NOTIFY err=%s", tostring(err)))
                choreo = nil
            end
        end
        if drain_ticks <= 60 then
            armr.log_err(string.format(
                "ISOMR_CTR_DRAIN tick=%d stage=pre-pump", drain_ticks))
        end
        local pumped = armr.pump()
        -- Real Win32 pointer input arrives queued on the pump result. It must
        -- be delivered to Unity through nativeInjectEvent (the registered
        -- UnityPlayer native) or clicks are silently dropped. MotionEvent
        -- action codes match NativePointerAction (0=down,1=up,2=move).
        if g_player and pumped and pumped.pointers then
            for _, pev in ipairs(pumped.pointers) do
                local ok, res, cerr = pcall(function()
                    return armr.call_native(
                        KPLAYER, "nativeInjectEvent",
                        "(Landroid/view/InputEvent;I)Z",
                        {g_player,
                         armr.new_motion_event(pev.action, pev.x, pev.y),
                         0})
                end)
                armr.log_err(string.format(
                    "ISOMR_CTR_REALINPUT action=%d x=%s y=%s ok=%s res=%s err=%s",
                    pev.action, tostring(pev.x), tostring(pev.y),
                    tostring(ok), tostring(res), tostring(cerr)))
                if armr.ack_pointer then armr.ack_pointer(pev) end
            end
        end
        if drain_ticks <= 500 then
            armr.log_err(string.format(
                "ISOMR_CTR_DRAIN tick=%d stage=pre-sleep", drain_ticks))
        end
        armr.sleep(4)
        if drain_ticks <= 60 or drain_ticks % 100 == 0 then
            armr.log_err(string.format(
                "ISOMR_CTR_DRAIN tick=%d stage=post-sleep", drain_ticks))
        end
        -- While nativeRender #2 is parked, surface the JNI tail so the
        -- dropped Java-side post (void stub) that the guest waits on shows
        -- its class.method target.
        if drain_ticks % 4000 == 0 then
            armr.log_err(string.format(
                "ISOMR_CTR_JNITAIL tick=%d notes=%s proxies=%s",
                drain_ticks, armr.jni_notes(64), armr.proxies()))
        end
        -- Stall capture: if the render worker attached but produced no
        -- frame/error, force its CallEntry session to halt so the error path
        -- dumps the stalled guest PC + import tail. Disabled while probing
        -- whether UnityMain's init loop is bounded (it needs minutes under
        -- emulation); re-enable once the loop is understood.
        if not render_halted and render_tp and not render_done and
            drain_ticks > 300000 then
            render_halted = true
            local ok = armr.halt_session(render_tp)
            armr.log_err(string.format(
                "ISOMR_CTR_HALT tp=%s ok=%s", armr.hex(render_tp),
                tostring(ok)))
        end
        if drain_ticks % 2000 == 0 then
            armr.log_err(string.format(
                "ISOMR_CTR_PROGRESS tick=%d pcs=%s",
                drain_ticks, armr.guest_pcs()))
            -- GC_generic_lock state (libil2cpp statics; base 0x4200000000):
            -- 0x6138498 lock byte, 0x61396f0 holder thread id (id<<1),
            -- 0x6138994 collecting flag, 0x5f0cf8c/0x5f0cf90 spin counters.
            armr.log_err(string.format(
                "ISOMR_CTR_GCLOCK tick=%d byte=%s holder=%s collect=%s c1=%s c2=%s",
                drain_ticks,
                armr.hex(armr.r8(0x4206138498)),
                armr.hex(armr.r64(0x42061396f0)),
                armr.hex(armr.r8(0x4206138994)),
                armr.hex(armr.r64(0x4205f0cf8c)),
                armr.hex(armr.r64(0x4205f0cf90))))
        end
        -- Deadlock probe: if the GC lock byte stays held with no collection
        -- active across a sustained window, the holder's jit stopped
        -- mid-call. Require consecutive held sightings (~4s) so a
        -- legitimately-held lock is never cleared mid-section; each clear
        -- releases one abandoned hold.
        local byte_now = armr.r8(0x4206138498)
        if byte_now == 1 then
            gclock_held_since = gclock_held_since or drain_ticks
        else
            gclock_held_since = nil
            gclock_orphan = 0
        end
        -- Orphaned Boehm lock recovery. A thread can acquire GC_allocate_ml and
        -- then get stranded blocked inside the critical section on a guest wait
        -- that is never signalled (missed wake / undelivered suspend). The byte
        -- stays 1 while every other allocator spins forever on backoff.
        --
        -- Detection is purely the sustained continuous hold: a Boehm alloc
        -- critical section is microseconds, so the byte staying 1 for a long
        -- drain span while no stop-the-world collection is in progress
        -- (collect flag clear) is itself proof the holder is dead. The earlier
        -- "no il2cpp PC" test was confounded — the detached GC marker/finalizer
        -- threads always resolve inside libil2cpp, so it almost never fired
        -- during deserialize. Require a long continuous hold plus periodic
        -- re-samples so a legit brief section is never cleared.
        -- Stuck stop-the-world: a collection that DID start (collect=1)
        -- can never finish its suspend handshake under emulation (the
        -- suspended-waiters never report safepoints), so every allocator
        -- including the render thread backoffs on nanosleep forever — the
        -- era's terminal deadlock. Break those too after a long sustained
        -- hold, clear the collect flag + holder word, and re-disable GC so
        -- the collector restart does not immediately re-arm the trap.
        local collect_now = armr.r8(0x4206138994)
        if gclock_held_since and drain_ticks % 500 == 0
            and gclock_breaks < 200
            and ((drain_ticks - gclock_held_since >= 1500 and collect_now == 0)
                 or (drain_ticks - gclock_held_since >= 4000
                     and collect_now == 1)) then
            gclock_breaks = gclock_breaks + 1
            gclock_held_since = nil
            armr.w8(0x4206138498, 0)
            local kind = "orphan-clear"
            if collect_now == 1 then
                armr.w8(0x4206138994, 0)
                if armr.w64 then
                    pcall(armr.w64, 0x42061396f0, 0)
                end
                pcall(function()
                    armr.call_export("il2cpp_gc_disable", {})
                end)
                kind = "stw-break+redisable"
            end
            armr.log_err(string.format(
                "ISOMR_CTR_GCLOCK_BREAK tick=%d n=%d %s byte was=%s collect=%d",
                drain_ticks, gclock_breaks, kind, armr.hex(byte_now),
                collect_now))
        end
    end

    if render_err then error("unity render: " .. render_err) end

    -- UnloadUnity on the way out.
    local loader = armr.new_class(KLOADER)
    armr.call_native(KLOADER, "unload", "()Z", {loader})
    armr.release_ref(loader)
    stage("unity-unload", "pass")
end
