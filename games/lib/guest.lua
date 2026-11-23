-- games/lib/guest.lua — generic guest-memory primitives.
--
-- These are the reusable building blocks the per-game lifecycle scripts
-- share. They take explicit parameters (offsets, sizes, addresses) so the
-- *mechanism* is app-agnostic; the per-game numbers live in the site table.
-- Nothing here names a game, an engine, or a fixed RVA.

local r8, r16, r32, r64 = armr.r8, armr.r16, armr.r32, armr.r64
local w8, w16, w32, w64 = armr.w8, armr.w16, armr.w32, armr.w64

local M = {}

-- Guest-heap pointer check. A real object pointer lives inside the heap
-- arena [heap_base, heap_base + heap_capacity); anything else (small ints,
-- module/import addresses, garbage) is not. The base is resolved lazily on
-- first use because the runtime is not yet bound when this file loads — and
-- it is NOT always 0x71... (a relocated heap sits in a low window the direct
-- page table can cover).
local heap_base, heap_end
local function heap_range()
    if not heap_base then
        heap_base = armr.heap_base() or 0x710000000000
        heap_end = heap_base + (armr.heap_capacity() or 0x800000000)
    end
    return heap_base, heap_end
end
function M.is_ptr(p)
    if p == 0 or p == nil then return false end
    local lo, hi = heap_range()
    return p >= lo and p < hi
end

-- Zero `size` bytes of guest memory at `addr`.
function M.zero(addr, size) armr.write(addr, string.rep("\0", size)) end

-- Read a pointer field, tolerating unmapped garbage (returns 0).
function M.field(addr, off) return r64(addr + off) or 0 end

-- Serve an eastl vector<T> triple {begin,end,cap} at `vec` with a freshly
-- allocated, zeroed block of `count` elements. init_elem(sb, i) is called
-- per element to fill fields; it may be nil for a plain zeroed array.
-- Returns the block address (0 on alloc failure).
function M.serve_vector(vec, elem_size, count, init_elem)
    local block = armr.heap_alloc_aligned(count * elem_size, 16)
    if block == 0 then block = armr.heap_alloc(count * elem_size) end
    if block == 0 then return 0 end
    M.zero(block, count * elem_size)
    if init_elem then
        for i = 0, count - 1 do init_elem(block + i * elem_size, i) end
    end
    local fin = block + count * elem_size
    w64(vec, block)
    w64(vec + 8, fin)
    w64(vec + 0x10, fin)
    return block
end

-- Re-assert one virtual-slot impl across a set of vtables: write `impl`
-- into vt+slot for each vtable that does not already point at it. Used to
-- repair abstract-class vtables whose slot is 0 (a blit through them would
-- jump to null).
function M.reassert_vtables(vtables, slot_off, impl)
    for _, vt in ipairs(vtables) do
        if r64(vt + slot_off) ~= impl then w64(vt + slot_off, impl) end
    end
end

-- OR a flag bit into a byte field once. Returns the flag value seen.
-- (The consent/startup-finish pattern: a completion flag the SDK path never
-- sets in our environment.)
function M.set_flag(obj, off, mask)
    if obj == 0 or obj == nil then return 0 end
    local f = r8(obj + off) or 0
    if armr.band(f, mask) == 0 then w8(obj + off, armr.bor(f, mask)) end
    return f
end

-- Null a field when it holds a non-heap-tagged (garbage) pointer — recycled
-- objects can carry a stale pointer a callee follows unconditionally.
-- Returns the sanitized-away value, or 0 if the field was already clean.
function M.sanitize_ptr(obj, off)
    if not M.is_ptr(obj) then return 0 end
    local p = r64(obj + off) or 0
    if p ~= 0 and not M.is_ptr(p) then
        w64(obj + off, 0)
        return p
    end
    return 0
end

-- Follow a chain of pointer fields: chase(obj, 0x80c0, 0x198) ==
-- r64(r64(obj+0x80c0)+0x198), returning 0 if any link is null/unmapped.
function M.chase(obj, ...)
    local cur = obj
    for _, off in ipairs({...}) do
        if cur == 0 or cur == nil then return 0 end
        cur = r64(cur + off) or 0
    end
    return cur
end

-- Bounded per-key log helper (matches the ISOMR_* diag style).
function M.make_dumper()
    local counts = {}
    return function(key, max, ...)
        local n = (counts[key] or 0) + 1
        if n > max then return false end
        counts[key] = n
        armr.log_err(string.format(...))
        return true
    end
end

return M
