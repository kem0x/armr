-- games/pvz1.lua -- Plants vs. Zombies FREE (EA, AArch64 libpvz.so)
--
-- App-agnostic structure: every guest address is resolved by name out of
-- games/pvz1.sites.lua (data) through games/lib/sites.lua (resolver), and
-- the reusable guest-memory / frame / input machinery lives in games/lib/.
-- This file now holds only PvZ *behavior*: which sites to hook and what each
-- callback does. The raw offsets are data in pvz1.sites.lua.
--
-- Boundaries preserved from the C++ version:
--   * `pre_initializers` runs BEFORE static initializers (the Nimble slot
--     must be nonzero or JNI_OnLoad throws an illegal-state NPE).
--   * `required` breakpoints repair guest state; the rest are diag
--     (ISOMR_PVZ_DIAG) and pass diag=true so exec_bp skips them.
--   * The frame loop is host-driven: a real title loop races lawngame
--     init, so the host calls NativeOnDrawFrame once per tick.

-- Resolve the games/ dir from this script's own path so the shared libs and
-- the per-game site table load regardless of the process CWD.
local SRC = (debug.getinfo(1, "S").source:gsub("^@", ""))
local GAMES_DIR = SRC:match("^(.*)[/\\][^/\\]-$") or "."
package.path = GAMES_DIR .. "\\?.lua;" .. GAMES_DIR .. "/?.lua;" ..
               package.path

local sites  = require("lib.sites")
local guest  = require("lib.guest")
local framelib = require("lib.frame")
local input  = require("lib.input")

local SITES  = dofile(GAMES_DIR .. "\\pvz1.sites.lua")
local MODULE = SITES.module
local F      = SITES.fields  -- named struct field offsets (object layout)
local ST     = SITES.state   -- EAMTGL20 State object layout
local IC     = SITES.input   -- PopCap input action codes + module ids

local BASE = armr.base()
local DIAG = armr.diag()

-- Resolve a named site to a guest address (nil when unresolvable).
local function G(name) return sites.addr(SITES.globals[name], MODULE) end
local function T(name) return sites.addr(SITES.targets[name], MODULE) end

-- module state --------------------------------------------------------

local ENV
local activity_ref = 0
local pvz_render_device = 0
local state_manager_served = false
local manager_built = false
local pvz_frame_returned = false
local keyboard_reported_visible = false

local function stage(name, result)
    io.write("ISOMR_CTR_ENTRY stage=" .. name .. " result=" .. result .. "\n")
    io.flush()
end

-- bounded per-name hit logger (diag output goes to stderr, like the C++)
local hit_counts = {}
local function hit(name, st)
    local n = (hit_counts[name] or 0) + 1
    hit_counts[name] = n
    if n <= 6 then
        armr.log_err(string.format(
            "ISOMR_BP_HIT %s #%d x0=%s x1=%s x2=%s",
            name, n, armr.hex(st:x(0)), armr.hex(st:x(1)),
            armr.hex(st:x(2))))
    end
end

local dump_limited = guest.make_dumper()

-- resolved-at-load site addresses -------------------------------------

local ATLAS_IMPL   = 0   -- SetupAtlasState impl target
local GLES20_IF    = 0   -- GetGLES20Interfaces cached object
local APP_HOLDER   = 0   -- app singleton holder
local STATEMGR_VT  = 0   -- EAMTGL20StateManager vtable object
local RELRO_LO     = 0   -- .data.rel.ro vtable range
local RELRO_HI     = 0
local TODLOAD_STR  = 0   -- adrp target for the tod-load string
local TITLE_PROCEED = 0  -- `b` target for the title-update exits
local SETCTX_TAKEN = 0
local SETCTX_FALL  = 0
local DEVICE_VTABLES = {}  -- resolved vtable addresses

local function resolve_sites()
    ATLAS_IMPL  = G("atlas_impl") or 0
    GLES20_IF   = G("gles20_if") or 0
    APP_HOLDER  = G("app_holder") or 0
    STATEMGR_VT = G("statemgr_vt") or 0
    RELRO_LO    = G("relro_lo") or 0
    RELRO_HI    = G("relro_hi") or 0
    TODLOAD_STR = G("todload_str") or 0
    TITLE_PROCEED = T("title_proceed") or 0
    SETCTX_TAKEN  = T("setctx_taken") or 0
    SETCTX_FALL   = T("setctx_fallthru") or 0
    DEVICE_VTABLES = {}
    for _, spec in ipairs(SITES.vtables) do
        DEVICE_VTABLES[#DEVICE_VTABLES + 1] = sites.addr(spec, MODULE)
    end
end

-- guest helpers --------------------------------------------------------

local KSTATE_SIZE  = ST.size
local KSTATE_COUNT = ST.count

-- Re-assert the SetupAtlasState virtual slot on the device vtables;
-- abstract-class vtables carry 0 here and a blit through them jumps to null.
local function reassert_atlas_vtables()
    guest.reassert_vtables(DEVICE_VTABLES, SITES.vtable_slot, ATLAS_IMPL)
end

-- Replicate State::State for one element: StateValues {type=2, value=0} at
-- the values slots, self-linked intrusive lists, manager back-pointer.
local function init_state(sb, manager)
    armr.w64(sb + ST.mgr_ptr, manager or 0)
    for _, off in ipairs(ST.values) do armr.w64(sb + off, 2) end
    for _, off in ipairs(ST.self_links) do armr.w64(sb + off, sb) end
end

-- Serve an eastl vector triple (begin/end/cap) at `vec` with a fresh
-- state block of `count` elements.
local function serve_state_vector(vec, count, manager)
    return guest.serve_vector(vec, KSTATE_SIZE, count,
                              function(sb) init_state(sb, manager) end)
end

-- vector<State>::operator[](index): serve the resize host-side when the
-- vector begin is null and the indexed State would be a tiny pointer.
local function state_vector_ix_serve(st)
    local vector_this = st:x(0)
    local index = st:x(1)
    local begin = vector_this ~= 0 and armr.r64(vector_this) or 0
    local result = begin + index * KSTATE_SIZE
    local tiny = begin == 0 or not guest.is_ptr(result)
    if tiny and dump_limited("ix_serve", 4,
        "ISOMR_STATE_IX_SERVED vec=%s index=%s newbegin pending",
        armr.hex(vector_this), armr.hex(index)) then
        serve_state_vector(vector_this, KSTATE_COUNT, 0)
    end
end

-- state-find-entry: the State::values walk hits garbage when the
-- manager's states vector base is null. Serve the states and atlas-states
-- vectors once.
local function state_find_entry_serve(st)
    local state_object = st:x(0)
    local suspicious = not guest.is_ptr(state_object)
    if suspicious and not state_manager_served then
        state_manager_served = true
        local manager = armr.r64(pvz_render_device + F.device_statemgr) or 0
        armr.log_err(string.format("ISOMR_STATES_SERVE mgr=%s",
                                   armr.hex(manager)))
        if manager ~= 0 then
            local begin = serve_state_vector(manager + F.mgr_states,
                                             KSTATE_COUNT, manager)
            -- atlas states vector at mgr+mgr_atlas_states (4 entries).
            local atlas = serve_state_vector(manager + F.mgr_atlas_states,
                                             4, manager)
            if begin ~= 0 then
                armr.log_err(string.format(
                    "ISOMR_STATES_VECTOR_SERVED begin=%s count=%d atlas=%s",
                    armr.hex(begin), KSTATE_COUNT, armr.hex(atlas)))
            else
                armr.log_err("ISOMR_STATES_ALLOC_FAIL")
            end
        end
    end
    if suspicious or (hit_counts["find"] or 0) < 4 then
        dump_limited("find", 4,
            "ISOMR_STATE_FIND suspicious=%d state=%s caller=%s",
            suspicious and 1 or 0, armr.hex(state_object), armr.hex(st:x(30)))
    end
end

-- Construct the EAMTGL20StateManager host-side (ctor layout): 0x400 block,
-- vptr from the guest vtable slot, context pointer, and the states/atlas
-- vectors.
local function build_state_manager(device)
    local vtable = armr.r64(STATEMGR_VT) or 0
    local mgr = armr.heap_alloc_aligned(ST.mgr_block, 16)
    if mgr == 0 or vtable == 0 then
        armr.log_err(string.format("ISOMR_MGR_BUILD_FAIL block=%s vt=%s",
                                   armr.hex(mgr), armr.hex(vtable)))
        return 0
    end
    armr.write(mgr, string.rep("\0", ST.mgr_block))
    local mgr_vptr = vtable + 16
    armr.w64(mgr, mgr_vptr)
    armr.w64(mgr + F.mgr_context, mgr + F.mgr_context_obj) -- context pointer
    armr.w64(device + F.device_statemgr, mgr)
    serve_state_vector(mgr + F.mgr_states, KSTATE_COUNT, mgr)
    serve_state_vector(mgr + F.mgr_atlas_states, 4, mgr)
    armr.log_err(string.format("ISOMR_MGR_BUILT mgr=%s vptr=%s",
                               armr.hex(mgr), armr.hex(mgr_vptr)))
    return mgr
end

-- BaseOpenGLRenderDevice::SetRenderState dereferences the state-manager
-- field; while it is still garbage early in frame 1 the call would fault.
-- while it is still garbage early in frame 1 the call would fault.
local function set_render_state_wrapper(st)
    local device = st:x(0)
    local manager = device ~= 0 and armr.r64(device + F.device_statemgr) or 0
    local valid = guest.is_ptr(manager)
    if not valid and not manager_built then
        manager_built = true
        build_state_manager(device)
    end
end

local function dump_gles20_if(tag)
    if not DIAG then return end
    local gles20 = armr.r64(GLES20_IF) or 0
    local vt = gles20 ~= 0 and armr.r64(gles20) or 0
    local app_holder = armr.r64(APP_HOLDER) or 0
    local app = app_holder ~= 0 and armr.r64(app_holder) or 0
    local resmgr = (app and app ~= 0) and armr.r64(app + F.app_resmgr) or 0
    local rvt = (resmgr and resmgr ~= 0) and armr.r64(resmgr) or 0
    armr.log_err(string.format(
        "ISOMR_GLES20IF stage=%s obj=%s vt=%s app=%s resmgr=%s rvt=%s",
        tag, armr.hex(gles20), armr.hex(vt), armr.hex(app),
        armr.hex(resmgr), armr.hex(rvt)))
end

local function fix_vao_slots()
    -- cocos2d exports the VAO entry points as zero-initialized
    -- function-pointer variables in .bss; point each at the runtime's
    -- callable trampoline.
    for _, s in ipairs(SITES.vao_slots) do
        local slot = sites.addr(s, MODULE)
        local callable = armr.resolve_callable(s.sym)
        if not slot then
            armr.log_err("ISOMR_VAO_FIX " .. s.sym .. " slot-unresolved")
        elseif not callable then
            armr.log_err("ISOMR_VAO_FIX " .. s.sym .. " trampoline-missing")
        elseif armr.w64(slot, callable) then
            armr.log_err(string.format("ISOMR_VAO_FIX %s fn=%s",
                                       s.sym, armr.hex(callable)))
        else
            armr.log_err("ISOMR_VAO_FIX " .. s.sym .. " write-failed")
        end
    end
end

-- breakpoint installers -----------------------------------------------
-- Each installer iterates a named group from SITES.bp and attaches the
-- run callback for that group; the addresses come from the site data.

-- force_diag: true -> always diag-gated (pure observation), false -> always
-- installed (load-bearing repair), nil -> per-site `diag = not required`.
local function install_group(group, run_for, force_diag)
    for _, s in ipairs(SITES.bp[group]) do
        local addr = sites.addr(s, MODULE)
        if not addr then
            armr.log_err("ISOMR_BP_UNRESOLVED " .. s.name)
        else
            local diag
            if force_diag ~= nil then diag = force_diag
            else diag = not s.required end
            armr.exec_bp(addr, {
                name = s.name, sp = s.sp or 0, stp = s.stp or false,
                skip = s.skip, diag = diag,
                run = run_for(s),
            })
        end
    end
end

local function install_startup_links()
    install_group("startup", function(s)
        return function(st)
            -- per-site diag: non-required startup links are observation-only
            if s.name == "set-gl20-backbuffer-images" then
                pvz_render_device = st:x(1)
                armr.log_err(string.format(
                    "ISOMR_DEVICE_CAPTURED device=%s",
                    armr.hex(pvz_render_device)))
                reassert_atlas_vtables()
            elseif s.name == "sysfbo-stub-return" then
                -- Serve 0: the screen image wraps the window framebuffer so
                -- widget batches draw directly.
                st:set_x(0, 0)
                armr.log_err("ISOMR_SYSFBO_SERVE w0=0")
            end
            hit(s.name, st)
        end
    end, nil)  -- per-site diag: non-required startup links are diag
end

local function install_input_links()
    -- All diag-only: pointer events arrive on the correct type ids but the
    -- TouchScreen module never registers a handler; these fire only when an
    -- event is dispatched.
    install_group("input", function(s)
        return function(st)
            if s.name == "get-entry" then
                local n = st:x(0) ~= 0 and armr.cstr(st:x(0), 96) or ""
                armr.log_err(string.format("ISOMR_GETENTRY x0=%s name=%s",
                                           armr.hex(st:x(0)), n or ""))
                return
            end
            armr.log_err(string.format(
                "ISOMR_INPUT_HIT %s x0=%s x1=%s w2=%s w3=%s w4=%s",
                s.name, armr.hex(st:x(0)), armr.hex(st:x(1)),
                armr.hex(armr.band(st:x(2), 0xffffffff)),
                armr.hex(armr.band(st:x(3), 0xffffffff)),
                armr.hex(armr.band(st:x(4), 0xffffffff))))
        end
    end, true)  -- every input link is diag-only
end

local function install_chain_links()
    -- required=true marks sites whose callback repairs guest state
    -- (state-manager/atlas serve, atlas-info sanitize, SetupAtlasState
    -- vtable re-assert); observation-only links gate behind pvz_diag.
    install_group("chain", function(s)
        local gate_probe = s.name == "display-link-update-app-step"
        local redraw_probe = s.name == "driver-redraw"
        return function(st)
            local name = s.name
            -- load-bearing repairs first -----------------------------
            if name == "state-find-entry" then
                state_find_entry_serve(st)
            elseif name == "state-vector-ix" then
                state_vector_ix_serve(st)
            elseif name == "set-render-state-wrapper" then
                set_render_state_wrapper(st)
            elseif name == "device-blt-entry" then
                -- Sanitize the image's atlas-info pointer: recycled heap
                -- images carry garbage there and SetupAtlasState follows it
                -- unconditionally.
                if guest.sanitize_ptr(st:x(1), F.image_atlas_info) ~= 0 then
                    armr.log_err(string.format(
                        "ISOMR_ATLAS_PTR_SANITIZED img=%s",
                        armr.hex(st:x(1))))
                end
            elseif name == "device-bltnoclip-entry" then
                reassert_atlas_vtables()
                local this_vptr = armr.r64(st:x(0)) or 0
                if this_vptr ~= 0 and
                   armr.r64(this_vptr + SITES.vtable_slot) ~= ATLAS_IMPL then
                    armr.w64(this_vptr + SITES.vtable_slot, ATLAS_IMPL)
                end
            elseif name == "texturedata-blt-triangles" or
                   name == "widget-manager-draw-screen" then
                reassert_atlas_vtables()
            end
            -- diag-only branches -------------------------------------
            if DIAG then
                if name == "title-update-exit-null-singleton" or
                   name == "title-update-exit-consent-or-nimble" then
                    -- sits on `b #proceed`; emulate or control falls
                    -- through to the wrong path.
                    st.pc = TITLE_PROCEED
                elseif name == "title-update-proceed-todload" then
                    -- sits on `adrp x1, todload_str`.
                    st:set_x(1, TODLOAD_STR)
                elseif name == "loading-progress-poll" then
                    local title = st:x(8)
                    dump_limited("poll", 6,
                        "ISOMR_LOAD_POLL title=%s flag=%s",
                        armr.hex(title),
                        armr.hex(armr.r8(title + F.title_flag) or 0))
                elseif name == "statevalue-eq" then
                    local a, b = st:x(0), st:x(1)
                    local bad = not guest.is_ptr(a) or not guest.is_ptr(b)
                    if bad or (hit_counts["eq"] or 0) < 4 then
                        dump_limited("eq", 4,
                            "ISOMR_STATEVALUE_EQ bad=%d a=%s b=%s c=%s",
                            bad and 1 or 0, armr.hex(a), armr.hex(b),
                            armr.hex(st:x(30)))
                    end
                elseif name == "flush-buffered-triangles" then
                    dump_limited("flush", 3, "ISOMR_FLUSH (guest target)")
                elseif name == "bltswap-after-flush" then
                    local devptr = armr.r64(st.sp + 0x40) or 0
                    st:set_x(0, devptr)
                    dump_limited("bind", 4,
                        "ISOMR_BLTSWAP_WINDOW_BIND dev=%s",
                        armr.hex(devptr))
                elseif name == "buffered-draw-primitive" then
                    local devptr, src = st:x(0), st:x(3)
                    dump_limited("bdp", 16,
                        "ISOMR_BDP dev=%s mode=%d count=%s src=%s " ..
                        "f30=%s f70=%s cnt=%s cap=%s",
                        armr.hex(devptr),
                        armr.band(st:x(1), 0xffffffff),
                        armr.hex(st:x(2)), armr.hex(src),
                        armr.hex(armr.r64(devptr + 0x30) or 0),
                        armr.hex(armr.r64(devptr + 0x70) or 0),
                        armr.hex(armr.r64(devptr + 0x78) or 0),
                        armr.hex(armr.r64(devptr + 0x80) or 0))
                elseif gate_probe then
                    local fw = armr.r64(st:x(0) + F.gate_flag) or 0
                    dump_limited("gate", 10,
                        "ISOMR_DL_GATE flag190=%s c8=%d",
                        armr.hex(fw), armr.band(fw, 1))
                elseif redraw_probe then
                    local app = armr.r64(st:x(0) + F.redraw_app) or 0
                    local vt = app ~= 0 and armr.r64(app) or 0
                    local slot = (vt and vt ~= 0) and
                                 armr.r64(vt + F.redraw_vt_slot) or 0
                    dump_limited("redraw", 6,
                        "ISOMR_REDRAW_VIRTUAL app=%s vt=%s slot70=%s",
                        armr.hex(app), armr.hex(vt or 0),
                        armr.hex(slot or 0))
                end
            end
            hit(name, st)
        end
    end, nil)  -- per-site diag: non-required chain links are diag
end

local function install_lifecycle_repairs()
    install_group("lifecycle", function(s)
        return function(st)
            local name = s.name
            if name == "attach-gles20-barrier" then
                -- GetGLES20Interfaces may return an object whose vtable is
                -- still being written; wait for a valid .data.rel.ro vptr,
                -- then emulate `str x0,[x29,#-0x30]`.
                local obj = st:x(0)
                local waited = 0
                while obj ~= 0 do
                    local vt = armr.r64(obj) or 0
                    if vt >= RELRO_LO and vt < RELRO_HI then break end
                    if waited >= 20000 then break end
                    armr.sleep(1)
                    waited = waited + 1
                    obj = armr.r64(GLES20_IF) or 0
                end
                st:set_x(0, obj)
                armr.w64(st:x(29) - 0x30, obj)
                armr.log_err(string.format(
                    "ISOMR_ATTACH_BARRIER obj=%s waited_ms=%d",
                    armr.hex(obj), waited))
            elseif name == "setctx-branch" then
                -- SetContext branch repair: taken -> SetContext path,
                -- fallthrough -> skip. When the manager's current context
                -- does not match the handle's target, force the SetContext
                -- branch so the device re-binds instead of rendering into a
                -- stale context.
                local w0 = st:x(0)
                local fp = st:x(29)
                local device, handle, ctxobj, mgr, curctx = 0, 0, 0, 0, 0
                if fp ~= 0 then
                    device = armr.r64(fp - 8) or 0
                    handle = armr.r64(fp - 0x10) or 0
                end
                if handle ~= 0 then ctxobj = armr.r64(handle) or 0 end
                if device ~= 0 then mgr = armr.r64(device + F.device_statemgr) or 0 end
                if mgr ~= 0 then curctx = armr.r64(mgr + F.mgr_context) or 0 end
                local expected = ctxobj ~= 0 and (ctxobj + 8) or 0
                local taken = armr.band(w0, 1) == 0
                local next_pc = taken and SETCTX_TAKEN or SETCTX_FALL
                if not taken and device ~= 0 and expected ~= 0 and
                   curctx ~= expected then
                    armr.log_err(string.format(
                        "ISOMR_CTXRESYNC device=%s handle=%s ctxobj=%s " ..
                        "curctx=%s want=%s",
                        armr.hex(device), armr.hex(handle), armr.hex(ctxobj),
                        armr.hex(curctx), armr.hex(expected)))
                    next_pc = SETCTX_TAKEN
                end
                st.pc = next_pc
            elseif name == "consent-getter" then
                -- GetConsentFlowCompleted returns the consent-flag bit. The
                -- CMP flow resolved "no consent applies" but the flag is
                -- never set; set it on the live singleton so the title
                -- 1->2 transition fires.
                guest.set_flag(st:x(0), F.consent_flag, 1)
                dump_limited("consent", 4,
                    "ISOMR_CONSENT_GETTER this=%s flag=%d",
                    armr.hex(st:x(0)),
                    armr.r8(st:x(0) + F.consent_flag) or 0)
            elseif name == "nimble-startup-finish" then
                -- GetNimbleStartupFinish returns the finish-flag bit; Nimble
                -- is loaded and its stubs serve every call, so "startup
                -- finished" is the honest state.
                guest.set_flag(st:x(0), F.nimble_finish, 1)
                dump_limited("nimble", 4,
                    "ISOMR_NIMBLE_FINISH this=%s flag=%d",
                    armr.hex(st:x(0)),
                    armr.r8(st:x(0) + F.nimble_finish) or 0)
            end
        end
    end, false)  -- lifecycle repairs are load-bearing, never diag-gated
end

local function install_diag_probes()
    -- Instruction-emulating research probes (diag-only). The callbacks
    -- emulate a specific instruction at each site; the site names carry the
    -- semantics.
    local function run_for(s)
        local name = s.name
        return function(st)
            if name == "netctx-capture" then
                armr.w64(st.sp + 0x8, st:x(8))
            elseif name == "string-assign-entry" then
                local fp = st:x(29)
                armr.w64(fp - 8, st:x(0))
                armr.w64(fp - 16, st:x(1))
            elseif name == "rsb-new-post" then
                st:set_x(8, armr.r64(st.sp + 0xe8) or 0)
            elseif name == "load-manifest-after-mgr" then
                st:set_x(8, armr.r64(st:x(29) - 0x30) or 0)
                st:set_x(1, TODLOAD_STR)
            elseif name == "obb-post-lookup" then
                st:set_x(8, armr.r8(st.sp + 0xd3) or 0)
            elseif name == "update-accept" or name == "update-post-exec" then
                st:set_x(8, armr.r64(st.sp + 0x30) or 0)
            elseif name == "read-task-post-ctor" then
                st:set_x(0, armr.r64(st.sp + 0x8) or 0)
            elseif name == "decompress-a" or name == "decompress-b" then
                local task = st:x(8)
                dump_limited("decomp", 8,
                    "ISOMR_DECOMP task=%s buf=%s", armr.hex(task),
                    armr.hex(st:x(0)))
            elseif name == "readfile-state" then
                armr.log_err(string.format(
                    "ISOMR_READFILE_STATE status=%s fs=%s",
                    armr.hex(armr.r32(st.sp + 0x94) or 0),
                    armr.hex(armr.r32(st:x(29) - 0xd4) or 0)))
            elseif name == "rsb-callsite-result" then
                local dp = armr.r64(st.sp + 0x160) or 0
                local dl = armr.r64(st.sp + 0x158) or 0
                st:set_x(0, dl)
                armr.log_err(string.format("ISOMR_RSB_RESULT data=%s len=%s",
                                           armr.hex(dp), armr.hex(dl)))
            elseif name == "resource-fail" then
                armr.log_err(string.format(
                    "ISOMR_RESOURCE_FAIL msg=%s caller=%s",
                    armr.hex(st:x(1)), armr.hex(st:x(30))))
            end
            hit(name, st)
        end
    end
    install_group("diag", run_for, true)  -- every diag probe is diag-gated
end

-- per-slice callback: pump the host window queue mid-guest-call and reap
-- faulted worker threads so a dead decompressor can't wedge the session.
local function on_slice()
    -- CheckCompletedWorkerFailure returns true when NO failure is
    -- recorded; false means a worker faulted and needs reaping.
    local healthy, werr = armr.threads_check_failure()
    if not healthy then
        local ack_ok, acked = pcall(armr.threads_ack_failure)
        if ack_ok then
            armr.log_err("ISOMR_WORKER_FAILURE_REAPED error=" ..
                         tostring(werr))
        else
            return false  -- in-flight failure the session cannot reap
        end
    end
    armr.pump_owner()
    return true
end

-- Java callbacks the compat layer queued for the launcher to deliver.
-- Static JNI natives take (env, jclass, ...); armr.call_export_entry
-- prepends env, so the class reference leads the arg list.
local android_platform_class = 0
local networker_class = 0
local function drain_guest_callbacks()
    for _, ev in ipairs(armr.take_scheduled_events()) do
        if android_platform_class == 0 then
            local ok, ref = pcall(armr.new_class,
                "csdk/glucentralservices/util/AndroidPlatform")
            if not ok then break end
            android_platform_class = ref
        end
        armr.call_export_entry(
            "Java_csdk_glucentralservices_util_AndroidPlatform_onTimerEvent",
            {android_platform_class, ev[2], ev[1]})
        io.write(string.format("ISOMR_TIMER_CALLBACK fired a0=%d a1=%d\n",
                               ev[1], ev[2]))
        io.flush()
    end
    for _, ev in ipairs(armr.take_consent()) do
        if android_platform_class == 0 then
            local ok, ref = pcall(armr.new_class,
                "csdk/glucentralservices/util/AndroidPlatform")
            if not ok then break end
            android_platform_class = ref
        end
        -- Completion exports read trailing jstring args (x4..x7); pass
        -- explicit nulls for deterministic empty consent data.
        armr.call_export_entry(
            ev.form and
            "Java_csdk_glucentralservices_util_AndroidPlatform_" ..
            "onShowCMPConsentFormComplete" or
            "Java_csdk_glucentralservices_util_AndroidPlatform_" ..
            "onRequestCMPConsentInfoComplete",
            {android_platform_class, ev.handle, 1, 0, 0, 0, 0})
        io.write(string.format("ISOMR_CMP_COMPLETE fired form=%d handle=%s\n",
                               ev.form and 1 or 0, armr.hex(ev.handle)))
        io.flush()
    end
    for _, ctx in ipairs(armr.take_networker()) do
        if networker_class == 0 then
            local ok, ref = pcall(armr.new_class, "com/popcap/pcsp/NetworkerJNI")
            if not ok then break end
            networker_class = ref
        end
        -- callback(int status, String body, long context): the
        -- std::function* rides in the final jlong slot.
        local ok, err = armr.call_export_entry(
            "Java_com_popcap_pcsp_NetworkerJNI_callback",
            {networker_class, 0, 0, ctx})
        if ok then
            io.write(string.format("ISOMR_NETWORKER_COMPLETE fired ctx=%s\n",
                                   armr.hex(ctx)))
        else
            armr.log_err(string.format(
                "ISOMR_NETWORKER_COMPLETE failed ctx=%s error=%s",
                armr.hex(ctx), tostring(err)))
        end
        io.flush()
    end
end

-- PvZ input-dispatch config: the engine wants the press mirrored through
-- both the touch and mouse natives with PopCap action codes. Codes and
-- module ids come from SITES.input keyed by the Win32 action index
-- (0=down, 1=up, 2=move, 3=other).
local PTR_ACTION = {[0] = IC.ptr_action.down, [2] = IC.ptr_action.move,
                    [1] = IC.ptr_action.up, [3] = IC.ptr_action.other}
local MOUSE_ACTION = {[0] = IC.mouse_action.down, [2] = IC.mouse_action.move,
                      [1] = IC.mouse_action.up, [3] = IC.mouse_action.other}
local KMOD_PHYS = IC.kmod_phys
local KMOD_VIRT = IC.kmod_virt

local function send_key(native, module, arg)
    local ok, err = armr.call_export_entry(native, {0, module, arg, 0, 0})
    if not ok then error("pvz-key-delivery: " .. tostring(err)) end
end

local function deliver_input(events)
    for _, ev in ipairs(events.pointers) do
        local x, y = ev.x, ev.y
        local ok, err = armr.call_export_entry(
            "Java_com_ea_blast_TouchSurfaceAndroid_NativeOnPointerEvent",
            {0, PTR_ACTION[ev.action] or 0, 1000, 0, 0,
             {f32 = x}, {f32 = y}})
        if not ok then error("pvz-input-delivery: " .. tostring(err)) end
        -- PvZ runs the PopCap shared platform in desktop/mouse mode, so
        -- mirror the press through MouseAndroid too.
        local mbutton = (ev.action == 0 or ev.action == 1)
                        and IC.mouse_button or 0
        ok, err = armr.call_export_entry(
            "Java_com_ea_blast_MouseAndroid_NativeOnMouseEvent",
            {0, MOUSE_ACTION[ev.action] or 0, mbutton, 0, 0, 0,
             {f32 = x}, {f32 = y}})
        if not ok then error("pvz-mouse-delivery: " .. tostring(err)) end
        armr.ack_pointer(ev)
        io.write(string.format(
            "ISOMR_CTR_INPUT delivered action=%x x=%d y=%d\n",
            PTR_ACTION[ev.action] or 0, x, y))
        io.flush()
    end
    local keys = events.keys
    if #keys > 0 and not keyboard_reported_visible then
        armr.call_export_entry(
            "Java_com_ea_blast_KeyboardAndroid_" ..
            "NativeOnKeyboardVisibilityChanged", {0, 0, 1})
        keyboard_reported_visible = true
    end
    for _, kev in ipairs(keys) do
        local code = kev.code
        if kev.action == 0 then
            -- kChar: committed text; derive an Android keycode for the
            -- accompanying down/up so the pair looks real.
            local keycode = input.char_to_android(code)
            if keycode ~= 0 then
                send_key(
                    "Java_com_ea_blast_KeyboardAndroid_NativeOnKeyDown",
                    KMOD_PHYS, keycode)
            end
            send_key("Java_com_ea_blast_KeyboardAndroid_NativeOnCharacter",
                     KMOD_VIRT, code)
            if keycode ~= 0 then
                send_key(
                    "Java_com_ea_blast_KeyboardAndroid_NativeOnKeyUp",
                    KMOD_PHYS, keycode)
            end
        else
            -- Win32 virtual-key -> Android keycode for dialog keys.
            local android_key = input.win32_to_android(code)
            if android_key ~= 0 then
                send_key(
                    kev.action == 1 and
                    "Java_com_ea_blast_KeyboardAndroid_NativeOnKeyDown" or
                    "Java_com_ea_blast_KeyboardAndroid_NativeOnKeyUp",
                    KMOD_PHYS, android_key)
            end
        end
        io.write(string.format("ISOMR_CTR_INPUT key action=%u code=%u\n",
                               kev.action, code))
        io.flush()
    end
end

local function watchdog_loop()
    while not armr.stop_requested() do
        armr.sleep(2000)
        if not pvz_frame_returned then
            armr.log_err("ISOMR_STALL_JOURNAL\n" .. armr.journal(64))
        end
    end
end

local function monitor_loop()
    while not armr.stop_requested() do
        armr.sleep(500)
        local healthy, werr = armr.threads_check_failure()
        if not healthy then
            armr.threads_ack_failure()
            armr.log_err("ISOMR_MONITOR_WORKER " .. tostring(werr))
        end
    end
end

local function pvz_frame_loop()
    -- The Java-driven frame export exists only on the pvzfree_row payload;
    -- the PvZ2-era libPVZ2 runs the engine on its own threads.
    local java_frame = armr.find_export(
        "Java_com_ea_blast_AndroidRenderer_NativeOnDrawFrame")
    if not java_frame then
        stage("pvz-frame-loop", "engine-driven")
        return pvz2_engine_loop()
    end
    local reason, frames = framelib.run({
        draw = function()
            local ok, err = armr.call_export(
                "Java_com_ea_blast_AndroidRenderer_NativeOnDrawFrame",
                {activity_ref})
            pvz_frame_returned = true
            if not ok then
                armr.log_err("ISOMR_CTR_PVZ_FULL_ERROR error=" ..
                             tostring(err))
                armr.log_err("ISOMR_CTR_PVZ_FAULT_JOURNAL recent=" ..
                             armr.journal(64))
                stage("pvz-draw-frame", "fail")
                return false
            end
            return true
        end,
        on_tick = function(n)
            io.write("ISOMR_CTR_FRAMES count=" .. n .. "\n")
            io.flush()
        end,
        on_events = deliver_input,
        drain = drain_guest_callbacks,
        present_tag = "ctr-frame",
    })
    return reason ~= "error"
end

-- PvZ2-era engine-driven path: the libPVZ2 RegisterNatives table exposes
-- the SexyAppFramework surface natives even though nothing registered
-- them; drive them from the host loop.
function pvz2_engine_loop()
    local P = SITES.pvz2
    local pvz2_base = armr.module_base(P.module)
    if not pvz2_base then
        stage("pvz-frame-loop", "fail reason=libPVZ2-not-loaded")
        return false
    end
    local function pvz2(off) return pvz2_base + off end
    local fake_activity = armr.heap_alloc(P.fake_activity_size)
    local function surface_native(off, name)
        armr.call_addr(pvz2(off), {ENV, fake_activity})
        stage(name, "pass")
        return true
    end
    surface_native(P.create_lifecycle_observer, "pvz-native-create-lifecycle-observer")
    -- GameAppInitialize: NewGlobalRefs each service object, so every arg
    -- must be a tracked compat reference, not a bare heap block.
    local init_args = {fake_activity}
    for _, t in ipairs({
        "Lcom/popcap/SexyAppFramework/AndroidSurfaceView;",
        "Lcom/popcap/SexyAppFramework/AndroidHttpProxy;",
        "Lcom/popcap/SexyAppFramework/cloud/Cloud;",
        "Lcom/popcap/SexyAppFramework/GooglePlay/GooglePlayConnect;",
        "Lcom/popcap/SexyAppFramework/GooglePlay/GooglePlayAchievements;",
        "Lcom/popcap/SexyAppFramework/GooglePlay/GooglePlayLeaderboard;",
        "Lcom/popcap/SexyAppFramework/AndroidNotification;",
        "Lcom/popcap/SexyAppFramework/SexyAppFrameworkActivity;",
    }) do
        init_args[#init_args + 1] = armr.new_object(t)
    end
    local init_args_full = {ENV}
    for _, a in ipairs(init_args) do init_args_full[#init_args_full + 1] = a end
    local ok = armr.call_addr(pvz2(P.game_app_initialize), init_args_full)
    stage("pvz-native-game-app-initialize",
          ok and "pass" or "fail")
    surface_native(P.will_finish_launching, "pvz-native-will-finish-launching")
    surface_native(P.did_finish_launching, "pvz-native-did-finish-launching")
    surface_native(P.did_become_active, "pvz-native-did-become-active")
    armr.call_addr(pvz2(P.surface_changed), {ENV, fake_activity})
    -- onSurfaceCreated/Changed read uninitialized C++ globals; go straight
    -- to onDrawFrame pumping.
    local kOnDrawFrame = pvz2(P.on_draw_frame)
    local ticks = 0
    while not armr.stop_requested() do
        local ok, err = armr.call_addr(kOnDrawFrame, {ENV, fake_activity})
        if not ok then
            stage("pvz-engine-draw-frame", "fail " .. tostring(err))
            return false
        end
        ticks = ticks + 1
        if ticks % 64 == 0 then armr.present("ctr-frame") end
        if ticks % 600 == 0 then
            io.write("ISOMR_CTR_ENGINE_TICKS total=" .. ticks .. "\n")
            io.flush()
        end
        local events = armr.pump()
        if events.close then
            io.write("ISOMR_CTR_INPUT window-close-requested\n")
            io.flush()
            return true
        end
        deliver_input(events)
        drain_guest_callbacks()
    end
    return true
end

-- hooks --------------------------------------------------------------

function pre_initializers()
    -- The Nimble core caches the JavaVM it was created with at a fixed
    -- slot in libNimble.so; JNI_OnLoad NPEs on an empty slot before any
    -- static initializer could run. Write the live VM up front.
    local vm = armr.vm()
    local slot = sites.addr(SITES.nimble_vm_slot)
    if slot then
        armr.w64(slot, vm)
        stage("pvz-vm-cache", "pass")
    else
        stage("pvz-vm-cache", "warn-skipped")
    end
end

function main()
    resolve_sites()
    ENV = armr.env()
    if not ENV then error("pvz-env: current JNI environment unavailable") end
    activity_ref = armr.new_object("Lcom/ea/game/pvzfree_row/PvZActivity;")
    local classloader_ref = armr.new_class("java/lang/ClassLoader")

    -- call a lifecycle stage export; tolerate missing exports on the
    -- PvZ2-era payload (skipped, not fatal).
    local function call_stage(name, export_name, args)
        local ok, err = armr.call_export(export_name, args or {})
        if not ok then
            if err and string.find(err, "export is unavailable", 1, true) then
                stage(name, "skip reason=export-unavailable")
                return true
            end
            stage(name, "fail " .. tostring(err))
            error(err or (name .. " failed"))
        end
        stage(name, "pass")
        return true
    end

    local pvz_create = armr.find_export(
        "Java_com_ea_game_pvzfree_1row_PvZActivity_onPVZCreate")
    if not pvz_create then
        -- PvZ2-era libPVZ2: only EAIO's startup native is callable. Serve
        -- compat stand-ins and let the engine spawn its own threads.
        stage("pvz-create-export", "fallback-eaio")
        local files_dir = armr.new_string("/data")
        local apk_path = armr.new_string("/app")
        local r = armr.call_export("Java_com_ea_EAIO_EAIO_StartupNativeImpl",
                                 {activity_ref, files_dir, apk_path})
        stage("pvz-eaio-startup",
              r and "pass" or "fail")
        local registrar = armr.new_class(
            "com/ea/nimble/bridge/NimbleCppComponentRegistrar$NimbleCppComponent")
        call_stage("pvz-nimble-component-setup",
            "Java_com_ea_nimble_bridge_NimbleCppComponentRegistrar_" ..
            "00024NimbleCppComponent_setup", {registrar})
    else
        call_stage("on-pvz-create",
            "Java_com_ea_game_pvzfree_1row_PvZActivity_onPVZCreate",
            {activity_ref, classloader_ref})
    end
    local on_load = armr.find_export("JNI_OnLoad")
    io.write(string.format(
        "ISOMR_CTR_ENTRY stage=jni-on-load-lookup address=%s\n",
        on_load and armr.hex(on_load) or "unresolved"))
    io.flush()

    -- pvzfree-only lifecycle exports.
    if pvz_create then
        local obb = armr.new_string(
            "/app/assets/main.0.com.ea.game.pvzfree_row.obb")
        call_stage("pvz-obb-path",
            "Java_com_ea_game_pvzfree_1row_PvZActivity_obbFilePath",
            {activity_ref, obb})
        call_stage("pvz-is-tablet",
            "Java_com_ea_game_pvzfree_1row_PvZActivity_isDeviceTablet",
            {activity_ref})
        call_stage("pvz-main-activity-on-create",
            "Java_com_ea_blast_MainActivity_NativeOnCreate",
            {activity_ref})
    end

    -- Nimble bridge lifecycle.
    local lifecycle_ref = armr.new_object(
        "Lcom/ea/nimble/bridge/NimbleCppApplicationLifeCycle;")
    call_stage("pvz-application-launch",
        "Java_com_ea_nimble_bridge_NimbleCppApplicationLifeCycle_" ..
        "onApplicationLaunch", {lifecycle_ref})

    -- EA portability layers bind their JNI delegates here on device.
    local eathread = armr.new_class("com/ea/EAThread/EAThread")
    call_stage("pvz-ea-thread-init",
               "Java_com_ea_EAThread_EAThread_Init", {eathread})
    local eamio = armr.new_class("com/ea/EAMIO/StorageDirectory")
    call_stage("pvz-ea-storage-startup",
        "Java_com_ea_EAMIO_StorageDirectory_StartupNativeImpl", {eamio})
    local netmon = armr.new_class(
        "com/ea/NetworkUtil/ConnectionStatusMonitorAndroid")
    call_stage("pvz-network-monitor-startup",
        "Java_com_ea_NetworkUtil_ConnectionStatusMonitorAndroid_" ..
        "StartupNativeImpl", {netmon})
    local eaaudio = armr.new_class("com/ea/EAAudioCore/AndroidEAAudioCore")
    local audio_track = armr.new_object("Landroid/media/AudioTrack;")
    call_stage("pvz-ea-audio-init",
        "Java_com_ea_EAAudioCore_AndroidEAAudioCore_Init",
        {eaaudio, audio_track, 44100, 4096, 2, 0})
    call_stage("pvz-init-obb-reader", "_ZN7LawnApp13InitObbReaderEv", {})

    -- Surface creation + geometry, then show the host window.
    call_stage("pvz-surface-created",
        "Java_com_ea_blast_AndroidRenderer_NativeOnSurfaceCreated",
        {activity_ref})
    dump_gles20_if("after-surface-created")
    call_stage("pvz-surface-changed",
        "Java_com_ea_blast_AndroidRenderer_NativeOnSurfaceChanged",
        {activity_ref, 1280, 720})
    dump_gles20_if("after-surface-changed")
    armr.show_window()
    stage("pvz-window-show", "pass")
    dump_gles20_if("after-window-show")

    fix_vao_slots()

    -- Breakpoints must be live before the first frame runs.
    install_startup_links()
    install_input_links()
    install_chain_links()
    install_lifecycle_repairs()
    install_diag_probes()
    armr.set_slice_callback(200000, on_slice)

    armr.spawn("pvz-watchdog", watchdog_loop)
    armr.spawn("pvz-monitor", monitor_loop)

    -- If the render device was captured but its state manager is still
    -- null (ctor path never ran), construct it before the first frame.
    if pvz_render_device ~= 0 and
       (armr.r64(pvz_render_device + F.device_statemgr) or 0) == 0 then
        build_state_manager(pvz_render_device)
    end

    pvz_frame_loop()
    stage("pvz-frame-loop", "exit")
    -- Keep the process alive for captures, like the C++ epilogue.
    while not armr.stop_requested() do armr.sleep(1000) end
end
