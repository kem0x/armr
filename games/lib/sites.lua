-- games/lib/sites.lua — named guest-site resolution.
--
-- The whole point of this layer is to keep hardcoded addresses out of the
-- lifecycle scripts. A per-game data file (games/<id>.sites.lua) maps a
-- semantic name to a *resolution spec*; this module turns a spec into a
-- guest address. Resolution order is chosen so a site survives a rebuild
-- where a bare RVA would not:
--
--   { export = "Java_..." }                  -> armr.find_export (stable name)
--   { native = {class,name,sig} }            -> armr.native_addr (registered JNI)
--   { sig    = "AA ?? BB ..." }              -> armr.sigscan (version-tolerant)
--   { rva    = 0x1234 }                      -> module base + rva (per-version)
--   { global = 0x1234 }                      -> module base + offset (a data
--                                               object rather than code)
--
-- `spec.module` (basename, e.g. "libpvz.so") selects the module; absent, the
-- root library is used. A spec may also be a bare number (an RVA into the
-- default module). Anything unresolvable returns nil so callers can decide
-- whether the site is load-bearing.

local M = {}

-- Resolve one spec to a guest address, or nil.
-- default_module is the module basename used when spec.module is absent.
function M.addr(spec, default_module)
    if spec == nil then return nil end
    if type(spec) == "number" then
        local base = default_module and armr.module_base(default_module)
                     or armr.base()
        return base and (base + spec) or nil
    end
    local module = spec.module or default_module
    if spec.export then
        return armr.find_export(spec.export)
    end
    if spec.native then
        local n = spec.native
        return armr.native_addr(n[1], n[2], n[3])
    end
    if spec.sig then
        local hit = armr.sigscan(module or "", spec.sig)
        if hit then
            if hit.matches > 1 then
                armr.log_err(string.format(
                    "ISOMR_SITE_AMBIGUOUS sig=%s matches=%d (using first)",
                    spec.sig, hit.matches))
            end
            return hit.addr
        end
        -- Fall through to rva when the signature does not match this build.
        if spec.rva == nil then return nil end
    end
    if spec.rva ~= nil or spec.global ~= nil then
        local base = module and armr.module_base(module) or armr.base()
        if not base then return nil end
        return base + (spec.rva or spec.global)
    end
    return nil
end

-- Resolve a whole {name -> spec} table to {name -> address}. Missing sites
-- resolve to nil (not an error) so a script can probe version-dependent
-- sites without aborting.
function M.all(specs, default_module)
    local out = {}
    for name, spec in pairs(specs or {}) do
        out[name] = M.addr(spec, default_module)
    end
    return out
end

return M
