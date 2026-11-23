// Resumable DEFLATE (RFC1951) + zlib (RFC1950) decoder used to satisfy guest
// zlib inflate calls natively. The decoder buffers all input it has been fed
// and all output it has produced so a call can be resumed exactly where it
// stopped when more input or output space becomes available.
//
// This is a from-scratch minimal implementation. It produces byte-identical
// output to zlib inflate for well-formed streams.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace host_inflate {

// Result codes mirror the zlib results the caller maps back.
enum class InflateResult {
    kOk,         // produced output / made progress, more may follow
    kStreamEnd,  // reached the end of the stream
    kNeedInput,  // consumed all available input; stream not finished
    kDataError,  // malformed stream
};

// Canonical Huffman decode table (counts per length + symbols sorted by
// (length, symbol)). Enough for DEFLATE's max 288 symbols.
struct Huffman {
    std::uint16_t counts[16]{};
    std::uint16_t symbols[288]{};
};

class Inflater {
  public:
    Inflater() = default;

    // Append more compressed input bytes (called when the guest supplies them).
    void Feed(const std::uint8_t* data, std::size_t size) {
        in_.insert(in_.end(), data, data + size);
    }

    // All output produced so far (also the LZ77 back-reference window).
    const std::vector<std::uint8_t>& Output() const { return out_; }
    std::size_t InputSize() const { return in_.size(); }
    // Bytes pulled into the bit accumulator = guest-visible consumed input.
    std::size_t PulledInput() const { return in_pos_; }
    // Furthest input position ever reached (monotonic; ignores rollback).
    // Mirrors zlib's `hold`: once a byte is pulled into the bit accumulator it
    // counts as consumed even if the in-progress symbol is later retried.
    std::size_t ConsumedInput() const { return in_high_; }

    // Decode until `max_produce` more output bytes are emitted, input runs out,
    // the stream finishes, or an error occurs. `*produced` receives the number
    // of NEW bytes appended to Output() during this call.
    InflateResult Decode(std::size_t max_produce, std::size_t* produced) {
        const std::size_t start = out_.size();
        *produced = 0;
        InflateResult result = InflateResult::kNeedInput;
        while (out_.size() - start < max_produce) {
            const Step st = StepOnce();
            if (st == Step::kNeedInput) {
                result = InflateResult::kNeedInput;
                break;
            }
            if (st == Step::kDataError) {
                result = InflateResult::kDataError;
                break;
            }
            if (st == Step::kStreamEnd) {
                result = InflateResult::kStreamEnd;
                break;
            }
            // Step::kProgress: a unit of work completed; loop for more.
        }
        *produced = out_.size() - start;
        if (*produced > 0 && result == InflateResult::kNeedInput)
            result = InflateResult::kOk;
        return result;
    }

    bool Finished() const { return state_ == State::kDone; }
    // Debug helpers for offline validation only.
    int DebugState() const { return static_cast<int>(state_); }
    std::size_t DebugInPos() const { return in_pos_; }
    int DebugBitCnt() const { return bitcnt_; }

  private:
    enum class State {
        kZlibHeader,
        kBlockHeader,
        kStoredHeader,
        kStoredData,
        kDynCounts,
        kDynCodeLengths,
        kDynReadLengths,
        kSymbols,
        kLenExtra,
        kDistSym,
        kDistExtra,
        kCopy,
        kTrailer,
        kDone,
    };
    enum class Step { kProgress, kNeedInput, kDataError, kStreamEnd };
    enum class Sym { kOk, kNeedInput, kError };

    // Pull input bytes into the bit accumulator until `n` bits are buffered
    // or input runs out, then report whether `n` bits are available. Pulling
    // eagerly mirrors zlib filling `hold`: offered bytes are claimed (they
    // advance the consumed frontier) even when the unit still needs more, so
    // the guest drains avail_in and refills instead of stalling on a
    // partially-consumed tail.
    bool HaveBits(int n) {
        while (bitcnt_ < n && in_pos_ < in_.size()) {
            bitbuf_ |= static_cast<std::uint64_t>(PullByte()) << bitcnt_;
            bitcnt_ += 8;
        }
        return bitcnt_ >= n;
    }
    void AlignToByte() {
        // Drop the partial byte's remaining bits.
        const int drop = bitcnt_ % 8;
        bitbuf_ >>= drop;
        bitcnt_ -= drop;
    }
    // Pull one input byte into the bit accumulator. Records the high-water
    // frontier so guest-visible consumption reflects bytes claimed even when a
    // symbol decode is rolled back for retry (zlib `hold` semantics).
    std::uint8_t PullByte() {
        if (in_pos_ + 1 > in_high_) in_high_ = in_pos_ + 1;
        return in_[in_pos_++];
    }
    int GetBits(int n) {
        while (bitcnt_ < n && in_pos_ < in_.size()) {
            bitbuf_ |= static_cast<std::uint64_t>(PullByte()) << bitcnt_;
            bitcnt_ += 8;
        }
        const std::uint64_t mask = (1ull << n) - 1;
        const int value = static_cast<int>(bitbuf_ & mask);
        bitbuf_ >>= n;
        bitcnt_ -= n;
        return value;
    }

    // Decode exactly one unit of work. Bit-level input exhaustion inside a
    // unit rolls back so the unit can be retried once more input arrives.
    Step StepOnce() {
        switch (state_) {
        case State::kZlibHeader: {
            if (!HaveBits(16)) return Step::kNeedInput;
            const int cmf = GetBits(8);
            const int flg = GetBits(8);
            if ((cmf & 0x0f) != 8 || (cmf * 256 + flg) % 31 != 0)
                return Step::kDataError;
            if (flg & 0x20) {  // FDICT
                if (!HaveBits(32)) return Step::kNeedInput;
                GetBits(32);
            }
            state_ = State::kBlockHeader;
            return Step::kProgress;
        }
        case State::kBlockHeader: {
            if (!HaveBits(3)) return Step::kNeedInput;
            last_block_ = GetBits(1) != 0;
            const int type = GetBits(2);
            if (type == 0) {
                AlignToByte();
                state_ = State::kStoredHeader;
            } else if (type == 1) {
                BuildFixed();
                state_ = State::kSymbols;
            } else if (type == 2) {
                state_ = State::kDynCounts;
            } else {
                return Step::kDataError;
            }
            return Step::kProgress;
        }
        case State::kStoredHeader: {
            if (!HaveBits(32)) return Step::kNeedInput;
            stored_len_ = GetBits(16);
            if ((GetBits(16) ^ 0xffff) != stored_len_) return Step::kDataError;
            state_ = State::kStoredData;
            return Step::kProgress;
        }
        case State::kStoredData:
            if (stored_len_ == 0) {
                state_ = last_block_ ? State::kTrailer : State::kBlockHeader;
                return Step::kProgress;
            }
            if (in_pos_ >= in_.size()) return Step::kNeedInput;
            out_.push_back(PullByte());
            --stored_len_;
            return Step::kProgress;
        case State::kDynCounts: {
            if (!HaveBits(14)) return Step::kNeedInput;
            hlit_ = GetBits(5) + 257;
            hdist_ = GetBits(5) + 1;
            hclen_ = GetBits(4) + 4;
            cl_index_ = 0;
            std::memset(cl_lengths_, 0, sizeof(cl_lengths_));
            state_ = State::kDynCodeLengths;
            return Step::kProgress;
        }
        case State::kDynCodeLengths:
            while (cl_index_ < hclen_) {
                if (!HaveBits(3)) return Step::kNeedInput;
                cl_lengths_[kClOrder[cl_index_++]] =
                    static_cast<std::uint8_t>(GetBits(3));
            }
            if (BuildHuffman(&clcode_, cl_lengths_, 19) != 0)
                return Step::kDataError;
            lens_index_ = 0;
            lengths_.assign(hlit_ + hdist_, 0);
            state_ = State::kDynReadLengths;
            return Step::kProgress;
        case State::kDynReadLengths:
            while (lens_index_ < hlit_ + hdist_) {
                // Snapshot so a symbol+repeat unit is atomic across input
                // boundaries; otherwise a consumed symbol would be lost.
                const std::size_t sp = in_pos_;
                const std::uint64_t sb = bitbuf_;
                const int sc = bitcnt_;
                int sym = -1;
                const Sym s = DecodeSymbol(&clcode_, &sym);
                if (s == Sym::kNeedInput) {
                    in_pos_ = sp; bitbuf_ = sb; bitcnt_ = sc;
                    return Step::kNeedInput;
                }
                if (s == Sym::kError) return Step::kDataError;
                if (sym < 16) {
                    lengths_[lens_index_++] = static_cast<std::uint8_t>(sym);
                } else {
                    int rep = 0;
                    std::uint8_t fill = 0;
                    if (sym == 16) {
                        if (lens_index_ == 0) return Step::kDataError;
                        if (!HaveBits(2)) { in_pos_ = sp; bitbuf_ = sb; bitcnt_ = sc; return Step::kNeedInput; }
                        rep = GetBits(2) + 3;
                        fill = lengths_[lens_index_ - 1];
                    } else if (sym == 17) {
                        if (!HaveBits(3)) { in_pos_ = sp; bitbuf_ = sb; bitcnt_ = sc; return Step::kNeedInput; }
                        rep = GetBits(3) + 3;
                    } else if (sym == 18) {
                        if (!HaveBits(7)) { in_pos_ = sp; bitbuf_ = sb; bitcnt_ = sc; return Step::kNeedInput; }
                        rep = GetBits(7) + 11;
                    } else {
                        return Step::kDataError;
                    }
                    if (lens_index_ + rep > hlit_ + hdist_)
                        return Step::kDataError;
                    while (rep--) lengths_[lens_index_++] = fill;
                }
            }
            if (lengths_[256] == 0) return Step::kDataError;
            if (BuildHuffman(&litcode_, lengths_.data(), hlit_) != 0 ||
                BuildHuffman(&distcode_, lengths_.data() + hlit_, hdist_) != 0)
                return Step::kDataError;
            state_ = State::kSymbols;
            return Step::kProgress;
        case State::kSymbols: {
            int sym = -1;
            const Sym s = DecodeSymbol(&litcode_, &sym);
            if (s == Sym::kNeedInput) return Step::kNeedInput;
            if (s == Sym::kError) return Step::kDataError;
            if (sym < 256) {
                out_.push_back(static_cast<std::uint8_t>(sym));
                return Step::kProgress;
            }
            if (sym == 256) {
                state_ = last_block_ ? State::kTrailer : State::kBlockHeader;
                return Step::kProgress;
            }
            if (sym > 285) return Step::kDataError;
            len_sym_ = sym;
            state_ = State::kLenExtra;
            return Step::kProgress;
        }
        case State::kLenExtra: {
            const int extra = kLenExtra[len_sym_ - 257];
            if (!HaveBits(extra)) return Step::kNeedInput;
            copy_len_ = kLenBase[len_sym_ - 257] + GetBits(extra);
            state_ = State::kDistSym;
            return Step::kProgress;
        }
        case State::kDistSym: {
            int dsym = -1;
            const Sym ds = DecodeSymbol(&distcode_, &dsym);
            if (ds == Sym::kNeedInput) return Step::kNeedInput;
            if (ds == Sym::kError || dsym > 29) return Step::kDataError;
            dist_sym_ = dsym;
            state_ = State::kDistExtra;
            return Step::kProgress;
        }
        case State::kDistExtra: {
            const int extra = kDistExtra[dist_sym_];
            if (!HaveBits(extra)) return Step::kNeedInput;
            copy_dist_ = kDistBase[dist_sym_] + GetBits(extra);
            if (copy_dist_ <= 0 || copy_dist_ > out_.size())
                return Step::kDataError;
            copy_left_ = copy_len_;
            state_ = State::kCopy;
            return Step::kProgress;
        }
        case State::kCopy:
            // Emit one byte per Step so the output quota is respected.
            if (copy_left_ == 0) {
                state_ = State::kSymbols;
                return Step::kProgress;
            }
            out_.push_back(out_[out_.size() - copy_dist_]);
            --copy_left_;
            return Step::kProgress;
        case State::kTrailer:
            AlignToByte();
            if (!HaveBits(32)) return Step::kNeedInput;
            GetBits(32);  // adler32; not verified (guest only needs the data)
            state_ = State::kDone;
            return Step::kStreamEnd;
        case State::kDone:
            return Step::kStreamEnd;
        }
        return Step::kDataError;
    }

    // Decode one Huffman symbol. On input exhaustion the bit position is rolled
    // back so the same call can be retried after more input arrives.
    Sym DecodeSymbol(const Huffman* h, int* sym) {
        const std::size_t save_pos = in_pos_;
        const std::uint64_t save_buf = bitbuf_;
        const int save_cnt = bitcnt_;
        std::uint32_t code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            if (bitcnt_ == 0) {
                if (in_pos_ >= in_.size()) {
                    in_pos_ = save_pos;
                    bitbuf_ = save_buf;
                    bitcnt_ = save_cnt;
                    return Sym::kNeedInput;
                }
                bitbuf_ |= static_cast<std::uint64_t>(PullByte()) << bitcnt_;
                bitcnt_ += 8;
            }
            code |= static_cast<std::uint32_t>(bitbuf_ & 1);
            bitbuf_ >>= 1;
            --bitcnt_;
            const int count = h->counts[len];
            if (code < first + count) {
                *sym = h->symbols[index + (code - first)];
                return Sym::kOk;
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        in_pos_ = save_pos;
        bitbuf_ = save_buf;
        bitcnt_ = save_cnt;
        return Sym::kError;  // over-subscribed/incomplete code
    }

    static int BuildHuffman(Huffman* h, const std::uint8_t* lengths, int n) {
        std::memset(h->counts, 0, sizeof(h->counts));
        for (int i = 0; i < n; ++i) ++h->counts[lengths[i]];
        if (h->counts[0] == n) return 0;  // no symbols -> empty code
        h->counts[0] = 0;
        std::uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len)
            offs[len + 1] = offs[len] + h->counts[len];
        for (int i = 0; i < n; ++i)
            if (lengths[i] != 0) h->symbols[offs[lengths[i]]++] =
                static_cast<std::uint16_t>(i);
        return 0;
    }

    void BuildFixed() {
        std::uint8_t lit[288];
        for (int i = 0; i < 144; ++i) lit[i] = 8;
        for (int i = 144; i < 256; ++i) lit[i] = 9;
        for (int i = 256; i < 280; ++i) lit[i] = 7;
        for (int i = 280; i < 288; ++i) lit[i] = 8;
        BuildHuffman(&litcode_, lit, 288);
        std::uint8_t dst[30];
        for (int i = 0; i < 30; ++i) dst[i] = 5;
        BuildHuffman(&distcode_, dst, 30);
    }

    std::vector<std::uint8_t> in_;
    std::vector<std::uint8_t> out_;
    std::size_t in_pos_ = 0;
    std::size_t in_high_ = 0;  // furthest pull frontier (guest-visible consumed)
    std::uint64_t bitbuf_ = 0;
    int bitcnt_ = 0;

    State state_ = State::kZlibHeader;
    bool last_block_ = false;
    int stored_len_ = 0;
    int hlit_ = 0, hdist_ = 0, hclen_ = 0;
    int cl_index_ = 0;
    std::uint8_t cl_lengths_[19]{};
    int lens_index_ = 0;
    std::vector<std::uint8_t> lengths_;
    Huffman clcode_, litcode_, distcode_;
    int copy_len_ = 0, copy_dist_ = 0, copy_left_ = 0;
    int len_sym_ = 0;   // literal/length symbol pending its extra bits
    int dist_sym_ = 0;  // distance symbol pending its extra bits

    static constexpr std::uint8_t kClOrder[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    static constexpr std::uint16_t kLenBase[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static constexpr std::uint8_t kLenExtra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static constexpr std::uint16_t kDistBase[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
        12289, 16385, 24577};
    static constexpr std::uint8_t kDistExtra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
};

}  // namespace host_inflate
