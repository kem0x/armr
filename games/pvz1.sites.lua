-- games/pvz1.sites.lua — PvZ FREE site data (guest addresses into libpvz.so).
--
-- Pure data, no logic. Each entry names a guest site and says how to find
-- it: a bare number is an RVA into `module`; a table may instead carry
-- `sig`/`export`/`native` so the resolver (games/lib/sites.lua) can locate
-- the site without a fixed offset where a signature survives a rebuild.
-- Behavioral params (sp = prologue stack to emulate, stp = frame-pair store,
-- required = load-bearing vs diag-only) are per-site data and live here.
--
-- Reverse-engineered against lib/arm64-v8a/libpvz.so (pvzfree_row 4.0.2).
-- A new build of the same payload only needs this table regenerated.

return {
    module = "libpvz.so",

    -- .data/.bss globals and well-known objects ---------------------------
    globals = {
        gles20_if    = 0x3936c60, -- GetGLES20Interfaces() cached object ptr
        app_holder   = 0x2f23fe0, -- app singleton holder (resmgr at +0x940)
        atlas_impl   = 0x20885ec, -- SetupAtlasState implementation target
        statemgr_vt  = 0x2f2f400, -- EAMTGL20StateManager vtable object
        relro_lo     = 0x2e00000, -- .data.rel.ro vtable range low bound
        relro_hi     = 0x3100000, -- .data.rel.ro vtable range high bound
        todload_str  = 0xd9d000,  -- adrp target for the tod-load string
    },

    -- Device vtables whose SetupAtlasState slot (+0x2b0) must be re-asserted.
    vtables     = { 0x2e796a0, 0x2e79a00, 0x2e79d00 },
    vtable_slot = 0x2b0,

    -- Named struct field offsets the repair logic navigates. These describe
    -- the EAMTGL20/SexyApp object layout for this payload build; a recompile
    -- that shifts members only needs this table regenerated. Stack-frame
    -- arithmetic inside an instruction-emulating probe stays inline (it is
    -- positional to the named site, not an object-layout fact).
    fields = {
        device_statemgr  = 0x80c0, -- BaseOpenGLRenderDevice -> state manager*
        mgr_states       = 0x310,  -- state manager -> eastl vector<State>
        mgr_atlas_states = 0x2d8,  -- state manager -> atlas states vector
        image_atlas_info = 0x50,   -- image -> atlas-info pointer (sanitize)
        consent_flag     = 0x39,   -- consent object -> flow-completed flag
        nimble_finish    = 0x28,   -- nimble object -> startup-finish flag
        title_flag       = 0x126,  -- title -> progress/ready flag
        mgr_context      = 0x198,  -- state manager -> current context ptr
        mgr_context_obj  = 0x1a0,  -- state manager -> context object
        app_resmgr       = 0x940,  -- app -> resource manager
        gate_flag        = 0x190,  -- app -> display-link gate flag
        redraw_app       = 0x90,   -- driver -> app object
        redraw_vt_slot   = 0x70,   -- app vtable -> redraw slot
    },

    -- Breakpoints, grouped to match the install_* functions --------------
    bp = {
        startup = {
            { name = "init-app-driver",        rva = 0x206fa44, sp = 0x20, stp = true },
            { name = "driver-start",           rva = 0x20705a0, sp = 0x30 },
            { name = "launch-app",             rva = 0x2070624, sp = 0x20 },
            { name = "init-pre-app-launch",    rva = 0x2070648, sp = 0x90 },
            { name = "init-post-app-launch",   rva = 0x2070888, sp = 0x20 },
            { name = "create-app-windows",     rva = 0x2077634, sp = 0x10 },
            { name = "run-application",        rva = 0x2077658, sp = 0x20 },
            { name = "init-window-created",    rva = 0x20709a0, sp = 0x40 },
            { name = "set-gl20-backbuffer-images", rva = 0x2081134, sp = 0x80, required = true },
            { name = "get-glview-sys-fbo",     rva = 0x20776c0, sp = 0x10 },
            { name = "sysfbo-stub-return",     rva = 0x20776c8, sp = 0x0, required = true },
            { name = "render-state-manager-ctor", rva = 0x209a6a8, sp = 0x40 },
            { name = "is-3d-accelerated",      rva = 0x2072f04, sp = 0x10 },
        },

        input = {
            { name = "pvz-OnPointerDown",   rva = 0x161c794, sp = 256 },
            { name = "pvz-OnPointerUp",     rva = 0x161d2ec, sp = 304 },
            { name = "pvz-OnPointerMove",   rva = 0x161cea0, sp = 336 },
            { name = "pvz-OnCharacter",     rva = 0x161c738, sp = 48 },
            { name = "pvz-OnKeyDown",       rva = 0x161c1a8, sp = 80 },
            { name = "pvz-OnKeyUp",         rva = 0x161c634, sp = 48 },
            { name = "wm-MouseDown",        rva = 0x2055b90, sp = 80 },
            { name = "wm-MouseUp",          rva = 0x20559fc, sp = 80 },
            { name = "wm-MouseMove",        rva = 0x2055dc8, sp = 48 },
            { name = "wm-KeyChar",          rva = 0x20560e0, sp = 48 },
            { name = "wm-SetFocus",         rva = 0x2054ba4, sp = 48 },
            { name = "edit-GotFocus",       rva = 0x1dacce8, sp = 128 },
            { name = "lawn-KeyChar",        rva = 0x1b17ec4, sp = 48 },
            { name = "lawn-KeyDown",        rva = 0x1b17e68, sp = 48 },
            { name = "ts-HandleMessage",    rva = 0x26fe9cc, sp = 80 },
            { name = "ts-OnRawTouchDown",   rva = 0x26ff030, sp = 48 },
            { name = "ts-OnRawTouchMove",   rva = 0x26ff070, sp = 48 },
            { name = "ts-OnRawTouchUp",     rva = 0x26ff0b0, sp = 48 },
            { name = "ts-AndroidCtor",      rva = 0x2704698, sp = 112 },
            { name = "ts-RegisterModule",   rva = 0x26ff1c0, sp = 448 },
            { name = "ts-AllocModule",      rva = 0x26ff158, sp = 48 },
            { name = "ts-Init",             rva = 0x26fe5c0, sp = 96 },
            { name = "ts-AddPointerListener", rva = 0x26fec70, sp = 32 },
            { name = "kb-PhysKeyHandleMsg", rva = 0x26d4e6c, sp = 80 },
            { name = "mm-GetModule",        rva = 0x26ed974, sp = 112 },
            { name = "mm-GetModuleInfo",    rva = 0x26eda90, sp = 176 },
            { name = "lv-AddPointerListener", rva = 0x26ffe00, sp = 80 },
            { name = "lv-AddCharListener",  rva = 0x26d8a58, sp = 80 },
            { name = "lv-AddPhysKeyListener", rva = 0x26d5abc, sp = 80 },
            { name = "tsurf-AddPointerListener", rva = 0x26ffdd0, sp = 32 },
            { name = "pk-AddKeyListener",   rva = 0x26d59dc, sp = 32 },
            { name = "vk-AddKeyListener",   rva = 0x26dbea0, sp = 32 },
            { name = "kb-AddCharListener",  rva = 0x26d8a28, sp = 32 },
            { name = "kb-NotifyCharacter",  rva = 0x26d8e28, sp = 176 },
            { name = "kb-NotifyKey",        rva = 0x26d90f4, sp = 208 },
            { name = "vk-HandleMessage",    rva = 0x26db960, sp = 80 },
            { name = "pvz-PropogateSexyKey", rva = 0x161c40c, sp = 80 },
            { name = "msg-AddHandler",      rva = 0x270d304, sp = 64 },
            -- GetEntry name probe sits mid-function, emulates a sub-sp.
            { name = "get-entry",           rva = 0x26f1ee4, sp = 192, midfn = true },
        },

        chain = {
            { name = "display-link-update-app-step", rva = 0x20721cc, sp = 0x50 },
            { name = "driver-process",         rva = 0x20716b8, sp = 0x80 },
            { name = "update-app-step",        rva = 0x207243c, sp = 0x40 },
            { name = "draw-dirty-stuff",       rva = 0x2071e24, sp = 0xb0 },
            { name = "render-device-present",  rva = 0x20847d8, sp = 0x20 },
            { name = "present-glview",         rva = 0x2077734, sp = 0x10 },
            { name = "texture2d-update-with-data", rva = 0x24665cc, sp = 0x50 },
            { name = "blast-loop-locked",      rva = 0x26e5b28, sp = 0x40 },
            { name = "director-main-loop",     rva = 0x22b91a8, sp = 0x20 },
            { name = "director-draw-scene",    rva = 0x22b6994, sp = 0x20 },
            { name = "glview-render-scene",    rva = 0x23eb294, sp = 0x30 },
            { name = "do-update-frames",       rva = 0x20714d8, sp = 0x50 },
            { name = "do-update-frames-f",     rva = 0x2071474, sp = 0x30 },
            { name = "should-pause-updates",   rva = 0x2075740, sp = 0x10 },
            { name = "driver-redraw",          rva = 0x20723c8, sp = 0x20 },
            { name = "widget-manager-draw-screen", rva = 0x20552d8, sp = 0x20, required = true, stp = true },
            { name = "title-draw",             rva = 0x2052d38, sp = 0x90 },
            { name = "title-content-draw",     rva = 0x2053050, sp = 0x50 },
            { name = "flush-buffered-triangles", rva = 0x208885c, sp = 0x20 },
            { name = "bltswap-after-flush",    rva = 0x208194c, sp = 0x0 },
            { name = "title-update-exit-null-singleton", rva = 0x1c57484, sp = 0x0 },
            { name = "title-update-exit-consent-or-nimble", rva = 0x1c574c4, sp = 0x0 },
            { name = "title-update-proceed-todload", rva = 0x1c574c8, sp = 0x0 },
            { name = "loading-progress-poll",  rva = 0x1c579c4, sp = 0x0 },
            { name = "device-blt-entry",       rva = 0x2084010, sp = 0x40, required = true },
            { name = "device-bltnoclip-entry", rva = 0x208d26c, sp = 0x60, required = true },
            { name = "blt-atlas-target",       rva = 0x208d304, sp = 0x0 },
            { name = "statevalue-eq",          rva = 0x1e913cc, sp = 0x30 },
            { name = "state-find-entry",       rva = 0x1e912d4, sp = 0x80, required = true },
            { name = "states-vector-entry",    rva = 0x208c010, sp = 0x20 },
            { name = "set-render-state-wrapper", rva = 0x208bfd4, sp = 0x20, required = true },
            { name = "state-vector-ix",        rva = 0x208a16c, sp = 0x10, required = true },
            { name = "image-getrenderdata",    rva = 0x1d79780, sp = 0x10 },
            { name = "state-setvalue-ptr",     rva = 0x20992b4, sp = 0x60 },
            { name = "state-setvalue-uint",    rva = 0x208a1c0, sp = 0x60 },
            { name = "predraw-early-return-true", rva = 0x208b648, sp = 0x0 },
            { name = "buffered-draw-primitive", rva = 0x208b95c, sp = 0x60 },
            { name = "texturedata-blt-triangles", rva = 0x208e70c, sp = 0x20, required = true, stp = true },
            { name = "tiled-blit",             rva = 0x208d448, sp = 0x160 },
            { name = "atlas-result",           rva = 0x208d30c, sp = 0x0 },
            { name = "createimage-ok-continue", rva = 0x208d33c, sp = 0x0 },
            { name = "texturedata-blt-reached", rva = 0x208d348, sp = 0x0 },
        },

        lifecycle = {
            { name = "attach-gles20-barrier",  rva = 0x20aeed8, sp = 0x0 },
            { name = "setctx-branch",          rva = 0x20834d0, sp = 0x0, skip = false },
            { name = "consent-getter",         rva = 0x16a4200, sp = 0x10 },
            { name = "nimble-startup-finish",  rva = 0x1ab3a9c, sp = 0x10 },
        },

        diag = {
            { name = "drawnode-init",          rva = 0x2183f90 },
            { name = "ensure-capacity-point",  rva = 0x2183e38 },
            { name = "ensure-capacity-line",   rva = 0x2183ee4 },
            { name = "revert-state-journal",   rva = 0x1e92158 },
            { name = "journal-push-mark",      rva = 0x1e92258 },
            { name = "journal-pop-mark",       rva = 0x1e92318 },
            { name = "set-context-journal",    rva = 0x1e92b4c },
            { name = "netctx-capture",         rva = 0x256af80 },
            { name = "string-assign-entry",    rva = 0x1ba8e00 },
            { name = "rsb-new-post",           rva = 0x1eb0860 },
            { name = "load-manifest-after-mgr", rva = 0x1eb2000 },
            { name = "obb-post-lookup",        rva = 0x1ba8ebc },
            { name = "update-accept",          rva = 0x1eb1e28 },
            { name = "update-post-exec",       rva = 0x1eb1e4c },
            { name = "read-task-post-ctor",    rva = 0x20acb00 },
            { name = "decompress-a",           rva = 0x1ec0000 },
            { name = "decompress-b",           rva = 0x1ec1000 },
            { name = "readfile-state",         rva = 0x1ba9000 },
            { name = "rsb-callsite-result",    rva = 0x1eb0858 },
            { name = "resource-fail",          rva = 0x1e986c4 },
        },
    },

    -- cocos2d exports these VAO entry points as zero-initialized function
    -- pointers in .bss; the script points each at a callable trampoline.
    vao_slots = {
        { rva = 0x3942a08, sym = "glGenVertexArrays" },
        { rva = 0x3942a10, sym = "glBindVertexArray" },
        { rva = 0x3942a18, sym = "glDeleteVertexArrays" },
    },

    -- libNimble caches the JavaVM it was created with; JNI_OnLoad NPEs on an
    -- empty slot before any static initializer runs. (Different module.)
    nimble_vm_slot = { rva = 0x18d28, module = "libNimble.so" },

    -- EAMTGL20 State object layout the host-side ctor/serves replicate.
    state = {
        size       = 0xc8,
        count      = 64,
        mgr_ptr    = 0x0,             -- back-pointer to the state manager
        values     = {0x38, 0x50, 0x68, 0x80}, -- StateValues{type=2} slots
        self_links = {0x28, 0x30, 0x98, 0xa0}, -- intrusive list self-links
        mgr_block  = 0x400,           -- state manager allocation size
    },

    -- PopCap shared-platform input constants (action codes the touch/mouse
    -- and keyboard natives expect) and module ids for the key natives.
    input = {
        ptr_action  = { down = 0x6000f, move = 0x4000f, up = 0x8000f,
                        other = 0xf },
        mouse_action = { down = 0x2000c, move = 0xc, up = 0x4000c,
                         other = 0x4000c },
        mouse_button = 4,           -- press/release button code
        kmod_phys    = 600,         -- physical-key module id
        kmod_virt    = 700,         -- virtual-key module id
    },

    -- Branch targets the diag emulators jump to (control-flow repair).
    targets = {
        title_proceed   = 0x1c58478, -- `b` target for the title-update exits
        setctx_taken    = 0x20834dc, -- SetContext branch: taken path
        setctx_fallthru = 0x20834d4, -- SetContext branch: fallthrough path
    },

    -- PvZ2-era libPVZ2 surface natives (module-relative, engine-driven path).
    pvz2 = {
        module = "libPVZ2.so",
        fake_activity_size = 0x2000, -- stand-in activity object block
        create_lifecycle_observer = 0x23c98f8,
        game_app_initialize       = 0x23c98fc,
        will_finish_launching     = 0x23c9080,
        did_finish_launching      = 0x23c9084,
        did_become_active         = 0x23c9094,
        surface_changed           = 0x23cf3a4,
        on_draw_frame             = 0x23cf6c0,
    },
}
