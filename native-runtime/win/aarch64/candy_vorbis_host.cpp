#include "candy_vorbis_host.h"

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"
#include "guest_address_space.h"
#include "guest_heap.h"

#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <climits>
#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr std::size_t kMaximumAssetSize = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumReadSize = 16U * 1024U * 1024U;
constexpr std::string_view kCandyModule = "libcandycrushsaga.so";

enum class Operation : std::size_t {
    kClear,
    kTestCallbacks,
    kTestOpen,
    kStreams,
    kTimeTotal,
    kPcmTotal,
    kPcmSeek,
    kPcmTell,
    kInfo,
    kRead,
    kCount,
};

struct SubstitutionSpec {
    Operation operation;
    const char* import_name;
    std::uint64_t offset;
    std::array<std::uint32_t, 2> expected;
};

constexpr std::array<SubstitutionSpec,
                     static_cast<std::size_t>(Operation::kCount)>
    kSubstitutions{{
        {Operation::kClear, "__isomr_candy_vorbis_clear", 0x4d478b8ULL,
         {0xb40007e0U, 0xa9bd7bfdU}},
        {Operation::kTestCallbacks,
         "__isomr_candy_vorbis_test_callbacks", 0x4d48180ULL,
         {0xd10143ffU, 0xa9037bfdU}},
        {Operation::kTestOpen, "__isomr_candy_vorbis_test_open",
         0x4d481d0ULL, {0xb9408008U, 0x7100051fU}},
        {Operation::kStreams, "__isomr_candy_vorbis_streams",
         0x4d481e8ULL, {0xb9804000U, 0xd65f03c0U}},
        {Operation::kTimeTotal, "__isomr_candy_vorbis_time_total",
         0x4d481f0ULL, {0xfc1d0fe8U, 0xa900fbfdU}},
        {Operation::kPcmTotal, "__isomr_candy_vorbis_pcm_total",
         0x4d482acULL, {0xa9bd7bfdU, 0xa90157f6U}},
        {Operation::kPcmSeek, "__isomr_candy_vorbis_pcm_seek",
         0x4d47d30ULL, {0xd10303ffU, 0xa9067bfdU}},
        {Operation::kPcmTell, "__isomr_candy_vorbis_pcm_tell",
         0x4d49438ULL, {0xb9408008U, 0x7100091fU}},
        {Operation::kInfo, "__isomr_candy_vorbis_info", 0x4d49454ULL,
         {0xb9400808U, 0x34000148U}},
        {Operation::kRead, "__isomr_candy_vorbis_read", 0x4d4988cULL,
         {0xd10083ffU, 0xa9017bfdU}},
    }};

static_assert(kSubstitutions.size() ==
              static_cast<std::size_t>(Operation::kCount));

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::int32_t Int32Argument(std::uint64_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}

std::int64_t Int64Argument(std::uint64_t value) {
    return std::bit_cast<std::int64_t>(value);
}

std::uint64_t Int32Result(std::int32_t value) {
    return static_cast<std::uint32_t>(value);
}

std::uint64_t Int64Result(std::int64_t value) {
    return std::bit_cast<std::uint64_t>(value);
}

bool AddAddress(std::uint64_t base, std::uint64_t offset,
                std::uint64_t* result) {
    if (result == nullptr ||
        base > (std::numeric_limits<std::uint64_t>::max)() - offset)
        return false;
    *result = base + offset;
    return true;
}

struct AndroidVorbisInfo {
    std::int32_t version = 0;
    std::int32_t channels = 0;
    std::int64_t rate = 0;
    std::int64_t bitrate_upper = 0;
    std::int64_t bitrate_nominal = 0;
    std::int64_t bitrate_lower = 0;
    std::int64_t bitrate_window = 0;
    std::uint64_t codec_setup = 0;
};

static_assert(sizeof(AndroidVorbisInfo) == 56);
static_assert(offsetof(AndroidVorbisInfo, channels) == 4);
static_assert(offsetof(AndroidVorbisInfo, rate) == 8);

struct MemorySource {
    const std::vector<std::uint8_t>* bytes = nullptr;
    std::size_t cursor = 0;
};

std::size_t ReadMemory(void* destination, std::size_t size,
                       std::size_t count, void* opaque) {
    auto* source = static_cast<MemorySource*>(opaque);
    if (source == nullptr || source->bytes == nullptr ||
        destination == nullptr || size == 0 || count == 0 ||
        count > (std::numeric_limits<std::size_t>::max)() / size)
        return 0;
    const std::size_t requested = size * count;
    const std::size_t remaining =
        source->cursor <= source->bytes->size()
            ? source->bytes->size() - source->cursor
            : 0;
    const std::size_t copied =
        (std::min)(requested, remaining) / size * size;
    if (copied != 0) {
        std::memcpy(destination, source->bytes->data() + source->cursor,
                    copied);
        source->cursor += copied;
    }
    return copied / size;
}

int SeekMemory(void* opaque, ogg_int64_t offset, int origin) {
    auto* source = static_cast<MemorySource*>(opaque);
    if (source == nullptr || source->bytes == nullptr ||
        origin < SEEK_SET || origin > SEEK_END)
        return -1;
    const std::int64_t current =
        source->cursor <=
                static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)())
            ? static_cast<std::int64_t>(source->cursor)
            : -1;
    const std::int64_t end =
        source->bytes->size() <=
                static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)())
            ? static_cast<std::int64_t>(source->bytes->size())
            : -1;
    const std::int64_t base =
        origin == SEEK_SET ? 0 : origin == SEEK_CUR ? current : end;
    if (base < 0 ||
        (offset > 0 &&
         base > (std::numeric_limits<std::int64_t>::max)() - offset) ||
        (offset < 0 && offset < -base))
        return -1;
    const std::int64_t target = base + offset;
    if (target < 0 || target > end) return -1;
    source->cursor = static_cast<std::size_t>(target);
    return 0;
}

int CloseMemory(void*) { return 0; }

long TellMemory(void* opaque) {
    const auto* source = static_cast<const MemorySource*>(opaque);
    if (source == nullptr || source->bytes == nullptr ||
        source->cursor > static_cast<std::size_t>(LONG_MAX))
        return -1;
    return static_cast<long>(source->cursor);
}

constexpr ov_callbacks kMemoryCallbacks{ReadMemory, SeekMemory, CloseMemory,
                                        TellMemory};

struct HostState {
    ~HostState() {
        if (initialized) (void)ov_clear(&file);
    }

    std::mutex mutex;
    std::vector<std::uint8_t> bytes;
    MemorySource source;
    OggVorbis_File file{};
    std::unordered_map<std::int32_t, std::uint64_t> info_shadows;
    bool initialized = false;
};

bool RequireInitialized(const HostState& state, std::string_view operation,
                        std::string* error) {
    if (state.initialized) return true;
    SetError(error, "Candy Vorbis " + std::string(operation) +
                        " guest file has already been cleared");
    return false;
}

}  // namespace

struct CandyVorbisHostMirror::Impl {
    bool Bind(BionicRuntime* candidate) {
        const std::lock_guard lock(bind_mutex);
        if (candidate == nullptr) return false;
        if (runtime == nullptr) runtime = candidate;
        return runtime == candidate;
    }

    std::shared_ptr<HostState> FindState(std::uint64_t guest_file) const {
        const std::lock_guard lock(states_mutex);
        const auto found = states.find(guest_file);
        return found == states.end() ? nullptr : found->second;
    }

    bool ReadPointer(std::uint64_t address, std::uint64_t* value) const {
        return runtime != nullptr && value != nullptr && address != 0 &&
               runtime->address_space().Read(address, value, sizeof(*value));
    }

    bool CopyDatasource(std::uint64_t datasource,
                        std::vector<std::uint8_t>* output,
                        std::string* error) const {
        std::uint64_t provider = 0;
        std::uint64_t provider_source_address = 0;
        std::uint64_t provider_source = 0;
        std::uint64_t source_object = 0;
        if (datasource == 0 || output == nullptr ||
            !ReadPointer(datasource, &provider) || provider == 0 ||
            !AddAddress(provider, 8, &provider_source_address) ||
            !ReadPointer(provider_source_address, &provider_source) ||
            provider_source == 0 ||
            !ReadPointer(provider_source, &source_object) ||
            source_object == 0) {
            SetError(error,
                      "Candy Vorbis datasource does not match the pinned provider layout");
            return false;
        }
        std::uint64_t source_address = 0;
        std::uint64_t length_address = 0;
        if (!AddAddress(source_object, 8, &source_address)) {
            SetError(error, "Candy Vorbis provider source address overflowed");
            return false;
        }
        std::uint64_t source = 0;
        if (!ReadPointer(source_address, &source) || source == 0) {
            SetError(error, "Candy Vorbis provider source is unavailable");
            return false;
        }
        std::uint64_t length = 0;
        if (AddAddress(source_object, 16, &length_address) &&
            ReadPointer(length_address, &length) &&
            length <= kMaximumAssetSize &&
            length <= (std::numeric_limits<std::size_t>::max)()) {
            output->resize(static_cast<std::size_t>(length));
            if (length == 0 || runtime->address_space().Read(
                                   source, output->data(), output->size())) {
                return true;
            }
            output->clear();
        }

        std::string asset_error;
        if (runtime->assets().CopyBufferLocalOnly(
                source, kMaximumAssetSize, output, &asset_error)) {
            return true;
        }
        output->clear();
        SetError(error,
                 "Candy Vorbis provider source is neither a bounded readable "
                 "memory span nor an owned asset");
        return false;
    }

    bool TestCallbacks(Aarch64ImportRegistry::Registers* registers,
                       std::string* error) {
        const std::uint64_t datasource = (*registers)[0];
        const std::uint64_t guest_file = (*registers)[1];
        const std::uint64_t initial = (*registers)[2];
        const std::int64_t initial_count = Int64Argument((*registers)[3]);
        if (guest_file == 0 || initial_count < 0 ||
            initial_count > LONG_MAX ||
            static_cast<std::uint64_t>(initial_count) > kMaximumAssetSize) {
            SetError(error, "Candy Vorbis test-callbacks arguments are invalid");
            return false;
        }
        {
            const std::lock_guard lock(states_mutex);
            if (states.contains(guest_file)) {
                SetError(error, "Candy Vorbis guest file is already mirrored");
                return false;
            }
        }
        auto state = std::make_shared<HostState>();
        if (!CopyDatasource(datasource, &state->bytes, error))
            return false;
        const std::size_t initial_size =
            static_cast<std::size_t>(initial_count);
        if (initial_size > state->bytes.size() ||
            (initial_size != 0 && initial == 0)) {
            SetError(error,
                     "Candy Vorbis initial byte span exceeds the owned asset");
            return false;
        }
        if (initial_size != 0) {
            std::vector<std::uint8_t> guest_initial(initial_size);
            if (!runtime->address_space().Read(
                    initial, guest_initial.data(), guest_initial.size()) ||
                !std::equal(guest_initial.begin(), guest_initial.end(),
                            state->bytes.begin())) {
                SetError(error,
                         "Candy Vorbis initial bytes do not match the owned asset");
                return false;
            }
        }
        state->source = {&state->bytes, initial_size};
        const int result = ov_test_callbacks(
            &state->source, &state->file,
            initial_size == 0
                ? nullptr
                : reinterpret_cast<const char*>(state->bytes.data()),
            static_cast<long>(initial_size), kMemoryCallbacks);
        state->initialized = true;
        {
            const std::lock_guard lock(states_mutex);
            if (!states.emplace(guest_file, state).second) {
                SetError(error, "Candy Vorbis guest file raced with mirroring");
                return false;
            }
        }
        (*registers)[0] = Int32Result(result);
        return true;
    }

    bool TestOpen(Aarch64ImportRegistry::Registers* registers,
                  std::string* error) {
        const auto state = FindState((*registers)[0]);
        if (!state) {
            SetError(error, "Candy Vorbis test-open guest file is not mirrored");
            return false;
        }
        const std::lock_guard lock(state->mutex);
        if (!RequireInitialized(*state, "test-open", error)) return false;
        (*registers)[0] = Int32Result(ov_test_open(&state->file));
        return true;
    }

    bool Streams(Aarch64ImportRegistry::Registers* registers,
                 std::string* error) {
        const auto state = FindState((*registers)[0]);
        if (!state) {
            SetError(error, "Candy Vorbis streams guest file is not mirrored");
            return false;
        }
        const std::lock_guard lock(state->mutex);
        if (!RequireInitialized(*state, "streams", error)) return false;
        (*registers)[0] = Int64Result(static_cast<std::int64_t>(
            ov_streams(&state->file)));
        return true;
    }

    bool TimeTotal(Aarch64ImportRegistry::Registers* registers,
                   std::string* error) {
        const auto state = FindState((*registers)[0]);
        if (!state) {
            SetError(error, "Candy Vorbis time-total guest file is not mirrored");
            return false;
        }
        const std::lock_guard lock(state->mutex);
        if (!RequireInitialized(*state, "time-total", error)) return false;
        registers->vector[0] = {};
        registers->vector[0][0] = std::bit_cast<std::uint64_t>(
            ov_time_total(&state->file, Int32Argument((*registers)[1])));
        return true;
    }

    bool PcmTotal(Aarch64ImportRegistry::Registers* registers,
                  std::string* error) {
        const auto state = FindState((*registers)[0]);
        if (!state) {
            SetError(error, "Candy Vorbis pcm-total guest file is not mirrored");
            return false;
        }
        const std::lock_guard lock(state->mutex);
        if (!RequireInitialized(*state, "pcm-total", error)) return false;
        (*registers)[0] = Int64Result(ov_pcm_total(
            &state->file, Int32Argument((*registers)[1])));
        return true;
    }

    bool PcmSeek(Aarch64ImportRegistry::Registers* registers,
                 std::string* error) {
        const auto state = FindState((*registers)[0]);
        if (!state) {
            SetError(error, "Candy Vorbis pcm-seek guest file is not mirrored");
            return false;
        }
        const std::lock_guard lock(state->mutex);
        if (!RequireInitialized(*state, "pcm-seek", error)) return false;
        (*registers)[0] = Int32Result(ov_pcm_seek(
            &state->file, Int64Argument((*registers)[1])));
        return true;
    }

    bool PcmTell(Aarch64ImportRegistry::Registers* registers,
                 std::string* error) {
        const auto state = FindState((*registers)[0]);
        if (!state) {
            SetError(error, "Candy Vorbis pcm-tell guest file is not mirrored");
            return false;
        }
        const std::lock_guard lock(state->mutex);
        if (!RequireInitialized(*state, "pcm-tell", error)) return false;
        (*registers)[0] = Int64Result(ov_pcm_tell(&state->file));
        return true;
    }

    bool Info(Aarch64ImportRegistry::Registers* registers,
              std::string* error) {
        const std::uint64_t guest_file = (*registers)[0];
        const std::int32_t link = Int32Argument((*registers)[1]);
        const auto state = FindState(guest_file);
        if (!state) {
            SetError(error, "Candy Vorbis info guest file is not mirrored");
            return false;
        }
        const std::lock_guard lock(state->mutex);
        if (!RequireInitialized(*state, "info", error)) return false;
        const vorbis_info* host_info = ov_info(&state->file, link);
        if (host_info == nullptr) {
            (*registers)[0] = 0;
            return true;
        }
        AndroidVorbisInfo guest_info;
        guest_info.version = host_info->version;
        guest_info.channels = host_info->channels;
        guest_info.rate = host_info->rate;
        guest_info.bitrate_upper = host_info->bitrate_upper;
        guest_info.bitrate_nominal = host_info->bitrate_nominal;
        guest_info.bitrate_lower = host_info->bitrate_lower;
        guest_info.bitrate_window = host_info->bitrate_window;
        auto found = state->info_shadows.find(link);
        bool allocated = false;
        std::uint64_t shadow = 0;
        if (found == state->info_shadows.end()) {
            shadow = runtime->heap().Callocate(1, sizeof(guest_info));
            if (shadow == 0) {
                SetError(error,
                         "Candy Vorbis info guest shadow allocation failed");
                return false;
            }
            allocated = true;
        } else {
            shadow = found->second;
        }
        if (!runtime->heap().Write(shadow, &guest_info,
                                   sizeof(guest_info))) {
            if (allocated) {
                std::string ignored;
                (void)runtime->heap().Release(shadow, &ignored);
            }
            SetError(error, "Candy Vorbis info guest shadow is unwritable");
            return false;
        }
        if (allocated) state->info_shadows.emplace(link, shadow);
        (*registers)[0] = shadow;
        return true;
    }

    bool Read(Aarch64ImportRegistry::Registers* registers,
              std::string* error) {
        const std::uint64_t guest_output = (*registers)[1];
        const std::int32_t length = Int32Argument((*registers)[2]);
        const std::int32_t big_endian = Int32Argument((*registers)[3]);
        const std::int32_t word_size = Int32Argument((*registers)[4]);
        const std::int32_t signed_output = Int32Argument((*registers)[5]);
        const std::uint64_t guest_bitstream = (*registers)[6];
        if (length < 0 || static_cast<std::uint64_t>(length) >
                              kMaximumReadSize ||
            (length != 0 && guest_output == 0) ||
            (big_endian != 0 && big_endian != 1) ||
            (word_size != 1 && word_size != 2) ||
            (signed_output != 0 && signed_output != 1)) {
            SetError(error, "Candy Vorbis read arguments are invalid");
            return false;
        }
        const auto state = FindState((*registers)[0]);
        if (!state) {
            SetError(error, "Candy Vorbis read guest file is not mirrored");
            return false;
        }
        std::vector<char> output(
            length == 0 ? 1U : static_cast<std::size_t>(length));
        int bitstream = 0;
        long result = 0;
        {
            const std::lock_guard lock(state->mutex);
            if (!RequireInitialized(*state, "read", error)) return false;
            result = ov_read(&state->file, output.data(), length, big_endian,
                             word_size, signed_output, &bitstream);
        }
        if (result > length) {
            SetError(error, "Candy Vorbis read exceeded its requested span");
            return false;
        }
        if (result > 0 &&
            !runtime->address_space().Write(
                guest_output, output.data(), static_cast<std::size_t>(result))) {
            SetError(error, "Candy Vorbis decoded output is unwritable");
            return false;
        }
        if (guest_bitstream != 0 &&
            !runtime->address_space().Write(
                guest_bitstream, &bitstream, sizeof(bitstream))) {
            SetError(error, "Candy Vorbis bitstream output is unwritable");
            return false;
        }
        if (result > 0)
            read_bytes.fetch_add(static_cast<std::uint64_t>(result),
                                 std::memory_order_relaxed);
        (*registers)[0] = Int64Result(static_cast<std::int64_t>(result));
        return true;
    }

    bool Clear(Aarch64ImportRegistry::Registers* registers,
               std::string* error) {
        std::shared_ptr<HostState> state;
        {
            const std::lock_guard lock(states_mutex);
            const auto found = states.find((*registers)[0]);
            if (found == states.end()) {
                SetError(error, "Candy Vorbis clear guest file is not mirrored");
                return false;
            }
            state = found->second;
            states.erase(found);
        }
        const std::lock_guard lock(state->mutex);
        const int result = state->initialized ? ov_clear(&state->file) : 0;
        state->initialized = false;
        for (const auto& [link, shadow] : state->info_shadows) {
            (void)link;
            if (!runtime->heap().Release(shadow, error)) {
                SetError(error,
                         "Candy Vorbis info guest shadow cleanup failed");
                return false;
            }
        }
        state->info_shadows.clear();
        (*registers)[0] = Int32Result(result);
        return true;
    }

    bool Dispatch(Operation operation,
                  Aarch64ImportRegistry::Registers* registers,
                  std::string* error) {
        if (registers == nullptr || runtime == nullptr) {
            refusals.fetch_add(1, std::memory_order_relaxed);
            SetError(error, "Candy Vorbis host mirror is not bound");
            return false;
        }
        bool succeeded = false;
        switch (operation) {
            case Operation::kClear: succeeded = Clear(registers, error); break;
            case Operation::kTestCallbacks:
                succeeded = TestCallbacks(registers, error);
                break;
            case Operation::kTestOpen:
                succeeded = TestOpen(registers, error);
                break;
            case Operation::kStreams:
                succeeded = Streams(registers, error);
                break;
            case Operation::kTimeTotal:
                succeeded = TimeTotal(registers, error);
                break;
            case Operation::kPcmTotal:
                succeeded = PcmTotal(registers, error);
                break;
            case Operation::kPcmSeek:
                succeeded = PcmSeek(registers, error);
                break;
            case Operation::kPcmTell:
                succeeded = PcmTell(registers, error);
                break;
            case Operation::kInfo: succeeded = Info(registers, error); break;
            case Operation::kRead: succeeded = Read(registers, error); break;
            case Operation::kCount: break;
        }
        if (!succeeded) {
            refusals.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        calls[static_cast<std::size_t>(operation)].fetch_add(
            1, std::memory_order_relaxed);
        return true;
    }

    CandyVorbisHostSnapshot Snapshot() const {
        CandyVorbisHostSnapshot result;
        result.clear = calls[static_cast<std::size_t>(Operation::kClear)]
                           .load(std::memory_order_relaxed);
        result.test_callbacks =
            calls[static_cast<std::size_t>(Operation::kTestCallbacks)]
                .load(std::memory_order_relaxed);
        result.test_open =
            calls[static_cast<std::size_t>(Operation::kTestOpen)]
                .load(std::memory_order_relaxed);
        result.streams = calls[static_cast<std::size_t>(Operation::kStreams)]
                             .load(std::memory_order_relaxed);
        result.time_total =
            calls[static_cast<std::size_t>(Operation::kTimeTotal)]
                .load(std::memory_order_relaxed);
        result.pcm_total =
            calls[static_cast<std::size_t>(Operation::kPcmTotal)]
                .load(std::memory_order_relaxed);
        result.pcm_seek =
            calls[static_cast<std::size_t>(Operation::kPcmSeek)]
                .load(std::memory_order_relaxed);
        result.pcm_tell =
            calls[static_cast<std::size_t>(Operation::kPcmTell)]
                .load(std::memory_order_relaxed);
        result.info = calls[static_cast<std::size_t>(Operation::kInfo)]
                          .load(std::memory_order_relaxed);
        result.read = calls[static_cast<std::size_t>(Operation::kRead)]
                          .load(std::memory_order_relaxed);
        result.read_bytes = read_bytes.load(std::memory_order_relaxed);
        result.refusals = refusals.load(std::memory_order_relaxed);
        {
            const std::lock_guard lock(states_mutex);
            result.active = states.size();
        }
        return result;
    }

    mutable std::mutex bind_mutex;
    BionicRuntime* runtime = nullptr;
    mutable std::mutex states_mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<HostState>> states;
    std::array<std::atomic<std::uint64_t>,
               static_cast<std::size_t>(Operation::kCount)>
        calls{};
    std::atomic<std::uint64_t> read_bytes{0};
    std::atomic<std::uint64_t> refusals{0};
};

CandyVorbisHostMirror::CandyVorbisHostMirror()
    : impl_(std::make_unique<Impl>()) {}

CandyVorbisHostMirror::~CandyVorbisHostMirror() = default;

CandyVorbisHostSnapshot CandyVorbisHostMirror::snapshot() const {
    return impl_->Snapshot();
}

Aarch64ImportRegistry::Handler CandyVorbisHostMirror::CreateHandler(
    std::size_t operation, BionicRuntime* runtime) {
    if (operation >= static_cast<std::size_t>(Operation::kCount) ||
        !impl_->Bind(runtime))
        return {};
    return [this, operation](Aarch64ImportRegistry::Registers* registers,
                             std::string* error) {
        return impl_->Dispatch(static_cast<Operation>(operation), registers,
                               error);
    };
}

bool ConfigureCandyVorbisHostMirror(
    Aarch64ProfileRuntimeConfig* config,
    std::shared_ptr<CandyVorbisHostMirror>* mirror,
    std::string* error) {
    if (config == nullptr || mirror == nullptr || *mirror != nullptr) {
        SetError(error, "Candy Vorbis host mirror configuration is invalid");
        return false;
    }
    auto configured = std::shared_ptr<CandyVorbisHostMirror>(
        new CandyVorbisHostMirror());
    for (const SubstitutionSpec& spec : kSubstitutions) {
        const std::size_t operation =
            static_cast<std::size_t>(spec.operation);
        config->code_substitutions.push_back({
            spec.import_name, std::string(kCandyModule), spec.offset,
            spec.expected,
            [configured, operation](BionicRuntime* runtime)
                -> Aarch64ImportRegistry::Handler {
                Aarch64ImportRegistry::Handler handler =
                    configured->CreateHandler(operation, runtime);
                if (!handler) return Aarch64ImportRegistry::Handler{};
                return [configured, handler = std::move(handler)](
                           Aarch64ImportRegistry::Registers* registers,
                           std::string* dispatch_error) mutable {
                    (void)configured;
                    return handler(registers, dispatch_error);
                };
            }});
    }
    *mirror = std::move(configured);
    return true;
}

}  // namespace isomr
