# Cut the Rope — ZeptoLab, Unity engine (libmain + libunity + libil2cpp).
# Uses the Unity nativeRender driver.
# Select with: ArmrCtrEntryProbe.exe --game ctr
id = ctr
name = Cut the Rope
desc = Unity/il2cpp engine - experimental
driver = unity
payload_dir = apps/ctr/payload
data_dir = apps/ctr/data
root_lib = lib/arm64-v8a/libmain.so
package_name = com.zeptolab.ctr.ads
package_version = 4.0.2
host_inflate = 0
render_timeout = 7200
