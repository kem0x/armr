#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "pvz_inflate_host.h"

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"
#include "guest_address_space.h"
#include "host_inflate.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace isomr {
namespace {

// Guest vaddr of the payload's statically linked zlib `inflate` (confirmed via
// .eh_frame FDE + Capstone: entry loads strm->state@+0x38, next_out@+0x18).
constexpr std::uint64_t kInflateModuleOffset = 0x28fed74ULL;
// First two instructions of inflate: sub sp,sp,#0xd0 ; stp x29,x30,[sp,#0xc0].
constexpr std::array<std::uint32_t, 2> kExpectedInstructions{0xd10343ffU,
                                                             0xa90c7bfdU};
constexpr std::string_view kImportName = "__armr_pvz_inflate";
constexpr std::size_t kMaximumTransfer = 256U * 1024U * 1024U;

// zlib return codes.
constexpr int kZOk = 0;
constexpr int kZStreamEnd = 1;
constexpr int kZStreamError = -2;
constexpr int kZDataError = -3;
constexpr int kZBufError = -5;

// LP64 z_stream field offsets (Bytef*/uInt/uLong layout, confirmed by
// disassembly of the payload's inflate entry).
constexpr std::uint64_t kOffNextIn = 0x00;
constexpr std::uint64_t kOffAvailIn = 0x08;   // u32
constexpr std::uint64_t kOffTotalIn = 0x10;   // u64
constexpr std::uint64_t kOffNextOut = 0x18;
constexpr std::uint64_t kOffAvailOut = 0x20;  // u32
constexpr std::uint64_t kOffTotalOut = 0x28;  // u64
constexpr std::uint64_t kOffState = 0x38;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

struct StreamState {
    host_inflate::Inflater dec;
    bool started = false;
};

struct Shared {
    std::atomic<BionicRuntime*> runtime{nullptr};
    std::mutex mu;
    std::unordered_map<std::uint64_t, StreamState> streams;
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> produced{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> resets{0};
    std::atomic<std::uint64_t> ends{0};
};

// Dispatch one guest inflate(strm, flush) call natively. Registers: x0=strm,
// x1=flush; result returned in x0. Marshals the LP64 z_stream fields through
// the guest address space and runs the resumable decoder.
bool InflateDispatch(const std::shared_ptr<Shared>& shared,
                     Aarch64ImportRegistry::Registers* registers,
                     std::string* error) {
    shared->calls.fetch_add(1, std::memory_order_relaxed);
    BionicRuntime* const runtime = shared->runtime.load(std::memory_order_acquire);
    if (registers == nullptr || runtime == nullptr) {
        return SetError(error, "PvZ inflate host is not bound"), false;
    }
    const std::uint64_t strm = (*registers)[0];
    if (strm == 0) {
        (*registers)[0] = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(kZStreamError));
        return true;
    }
    GuestAddressSpace& as = runtime->address_space();
    std::uint64_t next_in = 0, total_in = 0, next_out = 0, total_out = 0,
                  state = 0;
    std::uint32_t avail_in = 0, avail_out = 0;
    if (!as.Read(strm + kOffNextIn, &next_in, sizeof(next_in)) ||
        !as.Read(strm + kOffAvailIn, &avail_in, sizeof(avail_in)) ||
        !as.Read(strm + kOffTotalIn, &total_in, sizeof(total_in)) ||
        !as.Read(strm + kOffNextOut, &next_out, sizeof(next_out)) ||
        !as.Read(strm + kOffAvailOut, &avail_out, sizeof(avail_out)) ||
        !as.Read(strm + kOffTotalOut, &total_out, sizeof(total_out)) ||
        !as.Read(strm + kOffState, &state, sizeof(state))) {
        (*registers)[0] = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(kZStreamError));
        return true;
    }
    if (avail_in > kMaximumTransfer || avail_out > kMaximumTransfer) {
        (*registers)[0] = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(kZBufError));
        return true;
    }

    // Key the host stream on the guest internal-state pointer (unique per
    // inflateInit); fall back to the z_stream address when state is unset.
    const std::uint64_t key = state != 0 ? state : strm;
    std::lock_guard<std::mutex> lock(shared->mu);
    StreamState& s = shared->streams[key];
    // A stream at offset 0 with no progress is a fresh init or an
    // inflateReset; start the decoder over so repeated use stays correct.
    if (total_in == 0 && total_out == 0) {
        if (s.started) shared->resets.fetch_add(1, std::memory_order_relaxed);
        s.dec = host_inflate::Inflater();
        s.started = true;
    }
    // A completed stream must not consume further input: any bytes the guest
    // still offers belong to whatever follows the zlib stream, so hand back
    // Z_STREAM_END without feeding them.
    if (s.dec.Finished()) {
        as.Write(strm + kOffAvailIn, &avail_in, sizeof(avail_in));
        (*registers)[0] = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(kZStreamEnd));
        return true;
    }

    // Feed only the bytes of this offer not already in the decoder's
    // append-only buffer. The guest offers the absolute input window
    // [total_in, total_in+avail_in); in_ already holds [0, fed_end), so the
    // fresh tail is [fed_end, offer_end). Deduplicating is required because
    // the guest re-offers any unconsumed tail on the next call.
    const std::uint64_t fed_end = s.dec.InputSize();
    const std::uint64_t offer_end = total_in + avail_in;
    if (avail_in > 0 && offer_end > fed_end) {
        if (next_in == 0 || avail_in > kMaximumTransfer) {
            (*registers)[0] = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(kZBufError));
            return true;
        }
        const std::uint64_t skip = fed_end - total_in;   // already-fed prefix
        const std::uint64_t fresh = offer_end - fed_end; // new bytes to take
        try {
            std::vector<std::uint8_t> buf(static_cast<std::size_t>(fresh));
            if (!as.Read(next_in + skip, buf.data(), buf.size())) {
                (*registers)[0] = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(kZStreamError));
                return true;
            }
            s.dec.Feed(buf.data(), buf.size());
        } catch (const std::bad_alloc&) {
            (*registers)[0] = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(kZBufError));
            return true;
        }
    }

    const std::size_t before_out = s.dec.Output().size();
    std::size_t produced = 0;
    const host_inflate::InflateResult res = s.dec.Decode(avail_out, &produced);
    // Guest-visible consumption = how far the decoder's input frontier
    // advanced this call (ConsumedInput is the monotonic high-water pull
    // mark, so a byte pulled then rolled back for retry still counts as
    // consumed — matching zlib's `hold`). Reporting the frontier (not the
    // whole offer) lets the guest keep feeding the buffered tail at the
    // real decode rate, which is what keeps libpng's IDAT supply aligned.
    const std::uint64_t consumed = s.dec.ConsumedInput() - total_in;
    {
        static std::atomic<std::uint64_t> dbg{0};
        const std::uint64_t n = dbg.fetch_add(1, std::memory_order_relaxed);
        // Log anomalies (non-OK results, ends, errors) plus a very sparse
        // heartbeat so a stream that produces wrong output or stops is
        // visible without flooding the diagnostic stream per call.
        if (res != host_inflate::InflateResult::kOk || (n % 8192) == 0) {
            std::fprintf(stderr,
                         "ISOMR_INFCALL n=%llu key=%llx flush=%llu "
                         "avail_in=%u avail_out=%u "
                         "produced=%llu res=%d totout=%llu\n",
                         (unsigned long long)n, (unsigned long long)key,
                         (unsigned long long)(*registers)[1],
                         (unsigned)avail_in, (unsigned)avail_out,
                         (unsigned long long)produced, (int)res,
                         (unsigned long long)s.dec.Output().size());
        }
    }

    if (produced > 0) {
        if (next_out == 0 ||
            !as.Write(next_out, s.dec.Output().data() + before_out, produced)) {
            (*registers)[0] = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(kZStreamError));
            return true;
        }
    }

    // Write back the streaming fields: next_in/avail_in/total_in and
    // next_out/avail_out/total_out advance by what this call moved.
    const std::uint64_t nin = next_in + consumed;
    const std::uint64_t tin = total_in + consumed;
    const std::uint64_t nout = next_out + produced;
    const std::uint64_t tout = total_out + produced;
    const std::uint32_t ain =
        avail_in - static_cast<std::uint32_t>(consumed);
    const std::uint32_t aout =
        avail_out - static_cast<std::uint32_t>(produced);
    as.Write(strm + kOffNextIn, &nin, sizeof(nin));
    as.Write(strm + kOffAvailIn, &ain, sizeof(ain));
    as.Write(strm + kOffTotalIn, &tin, sizeof(tin));
    as.Write(strm + kOffNextOut, &nout, sizeof(nout));
    as.Write(strm + kOffAvailOut, &aout, sizeof(aout));
    as.Write(strm + kOffTotalOut, &tout, sizeof(tout));

    int code;
    if (res == host_inflate::InflateResult::kStreamEnd) {
        code = kZStreamEnd;
        shared->ends.fetch_add(1, std::memory_order_relaxed);
    } else if (res == host_inflate::InflateResult::kDataError) {
        code = kZDataError;
    } else if (produced == 0 && consumed == 0) {
        code = kZBufError;
    } else {
        code = kZOk;
    }
    (*registers)[0] =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(code));
    shared->produced.fetch_add(produced, std::memory_order_relaxed);
    shared->consumed.fetch_add(consumed, std::memory_order_relaxed);
    // Data errors and stream ends are rare and diagnostic: log them so a
    // stream the decoder cannot handle is visible rather than a silent hang.
    if (res == host_inflate::InflateResult::kDataError) {
        std::fprintf(stderr,
                     "ISOMR_INFLATE dataerr key=0x%llx avail_in=%u "
                     "avail_out=%u total_in=%llu total_out=%llu\n",
                     (unsigned long long)key, (unsigned)avail_in,
                     (unsigned)avail_out, (unsigned long long)total_in,
                     (unsigned long long)total_out);
    }
    if (res == host_inflate::InflateResult::kStreamEnd) {
        std::fprintf(stderr,
                     "ISOMR_INFLATE end key=0x%llx total_in=%llu "
                     "total_out=%llu\n",
                     (unsigned long long)key, (unsigned long long)total_in,
                     (unsigned long long)total_out);
    }
    return true;
}

}  // namespace

bool ConfigurePvzInflateHost(Aarch64ProfileRuntimeConfig* config,
                             std::string* error) {
    if (config == nullptr) {
        SetError(error, "PvZ inflate host configuration is invalid");
        return false;
    }
    auto shared = std::make_shared<Shared>();
    config->code_substitutions.push_back({
        std::string(kImportName), /*module (root image)*/ "",
        kInflateModuleOffset, kExpectedInstructions,
        [shared](BionicRuntime* runtime) -> Aarch64ImportRegistry::Handler {
            shared->runtime.store(runtime, std::memory_order_release);
            return [shared](Aarch64ImportRegistry::Registers* registers,
                            std::string* dispatch_error) {
                return InflateDispatch(shared, registers, dispatch_error);
            };
        }});
    return true;
}

}  // namespace isomr
