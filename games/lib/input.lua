-- games/lib/input.lua — app-agnostic Win32 -> Android input helpers.
--
-- The Win32 virtual-key -> Android keycode and ASCII -> Android keycode maps
-- are fixed Android constants, not game data. The export names, action-code
-- tables, and module ids a given engine wants are supplied per game via the
-- `cfg` table passed to dispatch().

local M = {}

-- Win32 VK -> Android AKEYCODE for the dialog/navigation keys a game
-- surface actually sees (back, enter, escape, delete, dpad).
local VK_TO_ANDROID = {
    [8] = 67,    -- VK_BACK    -> AKEYCODE_DEL
    [13] = 66,   -- VK_RETURN  -> AKEYCODE_ENTER
    [27] = 4,    -- VK_ESCAPE  -> AKEYCODE_BACK
    [46] = 112,  -- VK_DELETE  -> AKEYCODE_FORWARD_DEL
    [37] = 21,   -- VK_LEFT    -> AKEYCODE_DPAD_LEFT
    [38] = 19,   -- VK_UP      -> AKEYCODE_DPAD_UP
    [39] = 22,   -- VK_RIGHT   -> AKEYCODE_DPAD_RIGHT
    [40] = 20,   -- VK_DOWN    -> AKEYCODE_DPAD_DOWN
}
function M.win32_to_android(vk) return VK_TO_ANDROID[vk] or 0 end

-- ASCII char -> Android AKEYCODE (used to give a text commit a plausible
-- accompanying down/up pair). a-z -> 29+, A-Z -> 29+, 0-9 -> 7+, space -> 62.
function M.char_to_android(code)
    if code >= 0x61 and code <= 0x7a then return 29 + (code - 0x61) end
    if code >= 0x41 and code <= 0x5a then return 29 + (code - 0x41) end
    if code >= 0x30 and code <= 0x39 then return 7 + (code - 0x30) end
    if code == 0x20 then return 62 end
    return 0
end

return M
