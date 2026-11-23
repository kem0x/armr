# Candy Crush Saga (com.king.candycrushsaga) — the proven L1 profile.
#
# This game runs on the dedicated ArmrCandyEntryProbe, which takes a
# *positional* argument list (root library, payload root, data root, then the
# heap-watch triplet and option flags) rather than the generic `--game <id>`
# form. The `args` template below reproduces the accepted L1 configuration
# from Invoke-CandyNativeEntryProbe.ps1: 512 MiB guest heap, high-resolution
# waitable nanosleep, bionic-partition qsort, a 2B post-analytics session
# budget, the pinned vorbis/webp-1.0.3 host mirrors, bulk zero-stream, and
# null analytics-no-value.
#
# The payload is the user's staged install under
# %LOCALAPPDATA%\Armr\apps\com.king.candycrushsaga (see the Invoke script's
# AppRoot default); it is not duplicated into apps/.

id = candy-crush
name = Candy Crush Saga
desc = Playable, with hitches
exe = ArmrCandyEntryProbe.exe
payload_dir = {localappdata}/Armr/apps/com.king.candycrushsaga/payload
data_dir = {localappdata}/Armr/apps/com.king.candycrushsaga/data
root_lib = lib/arm64-v8a/libcandycrushsaga.so
package_name = com.king.candycrushsaga
package_version = 1.333.2.1
args = {rootlib} {payload} {data} 0 0 0 heap-capacity=536870912 instruction-budget=500000000 post-analytics-instruction-budget=2000000000 qsort-algorithm=bionic-partition candy-vorbis-host-mirror candy-webp-1.0.3-host="{build}ArmrWebP103.dll" candy-bulk-zero-stream analytics-no-value=null nanosleep-backend=high-resolution-waitable
