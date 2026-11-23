# Plants vs. Zombies FREE — PopCap Sexy/Lawn engine + EA Nimble services.
# Select with: ArmrCtrEntryProbe.exe --game pvz1
id = pvz1
name = Plants vs. Zombies FREE
desc = Playable, slowly (first load ~6 min)
driver = pvz
payload_dir = apps/pvz1/payload
data_dir = apps/pvz1/data
root_lib = lib/arm64-v8a/libpvz.so
package_name = com.ea.game.pvzfree_row
package_version = 4.0.2
host_inflate = 1
render_timeout = 2400
script = pvz1.lua
# Import steps (windows\Import-ArmrGame.ps1). The engine memory-maps its OBB,
# so it is stored uncompressed, and ASSET:main.pak is served from a
# pre-inflated copy of main.rsb in the data directory.
import_repack_stored = assets/main.0.com.ea.game.pvzfree_row.obb
import_extract = assets/main.0.com.ea.game.pvzfree_row.obb|main.rsb|main.rsb.raw
