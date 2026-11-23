// Host-side zlib inflate for the PvZ root image. The payload's statically
// linked zlib inflate runs under Dynarmic at roughly 1/100 host speed, which
// dominates the main.rsb resource-loading stall. This substitution redirects
// the guest `inflate` entry to a worker-safe SVC stub handled by the
// resumable DEFLATE decoder in host_inflate.h, preserving the LP64 z_stream
// streaming contract exactly.
#pragma once

#include <string>

namespace isomr {

struct Aarch64ProfileRuntimeConfig;

// Registers the inflate code substitution (module == root image, offset ==
// guest vaddr of inflate) so it is patched in during Load(). Returns false
// with *error set when the substitution cannot be configured.
bool ConfigurePvzInflateHost(Aarch64ProfileRuntimeConfig* config,
                             std::string* error);

}  // namespace isomr
