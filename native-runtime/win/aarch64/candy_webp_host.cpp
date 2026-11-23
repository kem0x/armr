#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "candy_webp_host.h"

#include "aarch64_profile_runtime.h"
#include "bionic_runtime.h"
#include "guest_address_space.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <limits>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace isomr {
namespace {

constexpr int kExpectedDecoderVersion = 0x010003;
constexpr std::size_t kMaximumTransfer = 64U * 1024U * 1024U;
constexpr std::string_view kCandyModule = "libcandycrushsaga.so";
constexpr std::string_view kImportName =
    "__isomr_candy_webp_103_decode_rgba_into";
constexpr std::uint64_t kModuleOffset = 0x4e232dcULL;
constexpr std::array<std::uint32_t, 2> kExpectedInstructions{
    0xd10503ffU, 0xa90f7bfdU};

void SetError(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

bool RangesOverlap(std::uint64_t left, std::size_t left_size,
                   std::uint64_t right, std::size_t right_size) {
    if (left_size == 0 || right_size == 0) return false;
    const std::uint64_t left_length = static_cast<std::uint64_t>(left_size);
    const std::uint64_t right_length = static_cast<std::uint64_t>(right_size);
    if (left > (std::numeric_limits<std::uint64_t>::max)() - left_length ||
        right > (std::numeric_limits<std::uint64_t>::max)() - right_length)
        return true;
    return left < right + right_length && right < left + left_length;
}

bool ValidateApi(CandyWebP103Api api, std::string* error) {
    if (api.get_decoder_version == nullptr || api.decode_rgba_into == nullptr) {
        SetError(error, "Candy WebP 1.0.3 decoder exports are incomplete");
        return false;
    }
    const int version = api.get_decoder_version();
    if (version != kExpectedDecoderVersion) {
        SetError(error, "Candy WebP decoder version is not exact 0x010003");
        return false;
    }
    return true;
}

bool ConfigureValidatedApi(
    Aarch64ProfileRuntimeConfig* config,
    std::shared_ptr<CandyWebP103Host>* host, CandyWebP103Api api,
    std::shared_ptr<void> library_owner, std::string* error) {
    if (config == nullptr || host == nullptr || *host != nullptr) {
        SetError(error, "Candy WebP 1.0.3 host configuration is invalid");
        return false;
    }
    if (!ValidateApi(api, error)) return false;

    auto configured = std::shared_ptr<CandyWebP103Host>(
        new CandyWebP103Host(api, std::move(library_owner)));
    config->code_substitutions.push_back({
        std::string(kImportName), std::string(kCandyModule), kModuleOffset,
        kExpectedInstructions,
        [configured](BionicRuntime* runtime)
            -> Aarch64ImportRegistry::Handler {
            Aarch64ImportRegistry::Handler handler =
                configured->CreateHandler(runtime);
            if (!handler) return {};
            return [configured, handler = std::move(handler)](
                       Aarch64ImportRegistry::Registers* registers,
                       std::string* dispatch_error) mutable {
                (void)configured;
                return handler(registers, dispatch_error);
            };
        }});
    *host = std::move(configured);
    return true;
}

}  // namespace

struct CandyWebP103Host::Impl {
    Impl(CandyWebP103Api configured_api,
         std::shared_ptr<void> configured_library_owner)
        : api(configured_api),
          library_owner(std::move(configured_library_owner)) {}

    bool Bind(BionicRuntime* candidate) {
        if (candidate == nullptr) return false;
        BionicRuntime* expected = nullptr;
        return runtime.compare_exchange_strong(
                   expected, candidate, std::memory_order_acq_rel) ||
               expected == candidate;
    }

    bool Dispatch(Aarch64ImportRegistry::Registers* registers,
                  std::string* error) {
        attempts.fetch_add(1, std::memory_order_relaxed);
        BionicRuntime* const bound_runtime =
            runtime.load(std::memory_order_acquire);
        if (registers == nullptr || bound_runtime == nullptr) {
            return Refuse(error, "Candy WebP 1.0.3 host is not bound");
        }

        const std::uint64_t guest_input = (*registers)[0];
        const std::uint64_t input_size64 = (*registers)[1];
        const std::uint64_t guest_output = (*registers)[2];
        const std::uint64_t output_size64 = (*registers)[3];
        const std::uint64_t output_stride64 = (*registers)[4];
        if (guest_input == 0 || guest_output == 0 || input_size64 == 0 ||
            output_size64 == 0 || input_size64 > kMaximumTransfer ||
            output_size64 > kMaximumTransfer ||
            output_stride64 == 0 ||
            output_stride64 >
                static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) {
            return Refuse(error,
                          "Candy WebP 1.0.3 arguments are invalid or unbounded");
        }
        const std::size_t input_size =
            static_cast<std::size_t>(input_size64);
        const std::size_t output_size =
            static_cast<std::size_t>(output_size64);
        if (RangesOverlap(guest_input, input_size, guest_output,
                          output_size)) {
            return Refuse(error,
                          "Candy WebP 1.0.3 guest spans unexpectedly overlap");
        }
        GuestAddressSpace& address_space = bound_runtime->address_space();
        if (!address_space.IsWritableRange(guest_output, output_size)) {
            return Refuse(error,
                          "Candy WebP 1.0.3 guest output is not writable");
        }

        try {
            std::vector<std::uint8_t> input(input_size);
            std::vector<std::uint8_t> output(output_size);
            if (!address_space.Read(guest_input, input.data(), input.size()) ||
                !address_space.Read(guest_output, output.data(),
                                    output.size())) {
                return Refuse(error,
                              "Candy WebP 1.0.3 guest spans are not readable");
            }
            std::uint8_t* const decoded = api.decode_rgba_into(
                input.data(), input.size(), output.data(), output.size(),
                static_cast<int>(output_stride64));
            if (decoded != output.data()) {
                decoder_failures.fetch_add(1, std::memory_order_relaxed);
                SetError(error, "Candy WebP 1.0.3 exact decoder refused input");
                return false;
            }
            if (!address_space.Write(guest_output, output.data(),
                                     output.size())) {
                return Refuse(error,
                              "Candy WebP 1.0.3 guest output write failed");
            }
        } catch (const std::bad_alloc&) {
            return Refuse(error,
                          "Candy WebP 1.0.3 bounded host allocation failed");
        }

        (*registers)[0] = guest_output;
        successes.fetch_add(1, std::memory_order_relaxed);
        input_bytes.fetch_add(input_size64, std::memory_order_relaxed);
        output_bytes.fetch_add(output_size64, std::memory_order_relaxed);
        return true;
    }

    bool Refuse(std::string* error, std::string message) {
        refusals.fetch_add(1, std::memory_order_relaxed);
        SetError(error, std::move(message));
        return false;
    }

    CandyWebP103HostSnapshot Snapshot() const {
        CandyWebP103HostSnapshot result;
        result.attempts = attempts.load(std::memory_order_relaxed);
        result.successes = successes.load(std::memory_order_relaxed);
        result.decoder_failures =
            decoder_failures.load(std::memory_order_relaxed);
        result.refusals = refusals.load(std::memory_order_relaxed);
        result.input_bytes = input_bytes.load(std::memory_order_relaxed);
        result.output_bytes = output_bytes.load(std::memory_order_relaxed);
        return result;
    }

    CandyWebP103Api api;
    std::shared_ptr<void> library_owner;
    std::atomic<BionicRuntime*> runtime{nullptr};
    std::atomic<std::uint64_t> attempts{0};
    std::atomic<std::uint64_t> successes{0};
    std::atomic<std::uint64_t> decoder_failures{0};
    std::atomic<std::uint64_t> refusals{0};
    std::atomic<std::uint64_t> input_bytes{0};
    std::atomic<std::uint64_t> output_bytes{0};
};

CandyWebP103Host::CandyWebP103Host(
    CandyWebP103Api api, std::shared_ptr<void> library_owner)
    : impl_(std::make_unique<Impl>(api, std::move(library_owner))) {}

CandyWebP103Host::~CandyWebP103Host() = default;

CandyWebP103HostSnapshot CandyWebP103Host::snapshot() const {
    return impl_->Snapshot();
}

Aarch64ImportRegistry::Handler CandyWebP103Host::CreateHandler(
    BionicRuntime* runtime) {
    if (!impl_->Bind(runtime)) return {};
    return [this](Aarch64ImportRegistry::Registers* registers,
                  std::string* error) {
        return impl_->Dispatch(registers, error);
    };
}

bool ConfigureCandyWebP103Host(
    Aarch64ProfileRuntimeConfig* config,
    std::shared_ptr<CandyWebP103Host>* host,
    const std::wstring& library_path, std::string* error) {
    const std::filesystem::path path(library_path);
    if (library_path.empty() || !path.is_absolute()) {
        SetError(error, "Candy WebP 1.0.3 library path must be absolute");
        return false;
    }
    const DWORD attributes = GetFileAttributesW(library_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        SetError(error, "Candy WebP 1.0.3 library is unavailable");
        return false;
    }
    HMODULE const module = LoadLibraryExW(
        library_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        SetError(error, "Candy WebP 1.0.3 library load failed with Win32 " +
                            std::to_string(GetLastError()));
        return false;
    }
    std::shared_ptr<void> owner(
        module, [](void* value) {
            if (value != nullptr)
                (void)FreeLibrary(static_cast<HMODULE>(value));
        });
    CandyWebP103Api api;
    api.get_decoder_version = reinterpret_cast<
        CandyWebP103Api::GetDecoderVersion>(GetProcAddress(
        module, "ArmrWebP103GetDecoderVersion"));
    api.decode_rgba_into = reinterpret_cast<
        CandyWebP103Api::DecodeRgbaInto>(GetProcAddress(
        module, "ArmrWebP103DecodeRGBAInto"));
    return ConfigureValidatedApi(config, host, api, std::move(owner), error);
}

bool ConfigureCandyWebP103HostForTesting(
    Aarch64ProfileRuntimeConfig* config,
    std::shared_ptr<CandyWebP103Host>* host, CandyWebP103Api api,
    std::string* error) {
    return ConfigureValidatedApi(config, host, api, {}, error);
}

}  // namespace isomr
