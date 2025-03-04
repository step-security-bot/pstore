//===- include/pstore/support/varint.hpp ------------------*- mode: C++ -*-===//
//*                  _       _    *
//* __   ____ _ _ __(_)_ __ | |_  *
//* \ \ / / _` | '__| | '_ \| __| *
//*  \ V / (_| | |  | | | | | |_  *
//*   \_/ \__,_|_|  |_|_| |_|\__| *
//*                               *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file varint.hpp
///
/// \brief Implements a prefix-style variable-length integer.
///
/// This code implements a variation on the UTF-8/LEB128 style variable-length integer in which the
/// first bit of each byte indicates whether further bytes are to follow. The motivation for this
/// difference is that we'd like to minimize the number of calls to database::get() and its variant
/// so its useful to know immediately how many bytes make up the value rather than having to
/// discover this as we go. Otherwise the concepts are the same.
///
/// In the code here, the low bits of the first byte of the number denote the length of the
/// encoding. The number of bytes can be found simply by call ctz(x | 0x100)+1 where ctz() is a
/// function which returns the number of trailing zeros starting at the LSB) and x is the value of
/// the first byte. See decode_size() for the real implementation.
///
/// Example:
/// The number 1 is encoded in a single byte.
///
///         +---------------------------------+
/// bit     | 7   6   5   4   3   2   1    0  |
///         +---------------------------+-----+
/// meaning |           value           | (*) |
///         +---------------------------+-----+
/// value   | 0 | 0 | 0 | 0 | 0 | 0 | 1 |  1  |
///         +---------------------------+-----+
/// (*) "1 byte varint value"
///
/// The number 2^8 is encoded in two bytes as shown below:
///
///                      byte 0                            byte 1
///         +-----------------------+-------+ +-------------------------------+
/// bit     | 7   6   5   4   3   2   1   0 | | 7   6   5   4   3   2   1   0 |
///         +-----------------------+-------+ +-------------------------------+
/// meaning |         value         |   2   | |             value             |
///         |       bits 0-5        | bytes | |           bits 6-13           |
///         +-----------------------+-------+ +--------------------------------
/// value   | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 |
///         +-----------------------+-------+ +-------------------------------+

#ifndef PSTORE_SUPPORT_VARINT_HPP
#define PSTORE_SUPPORT_VARINT_HPP

#include "pstore/support/bit_count.hpp"

namespace pstore {
  namespace varint {

    /// The maximum number of bytes that encode() will produce.
    constexpr std::size_t const max_output_length = 9;

    constexpr unsigned encoded_size (std::uint64_t const x) noexcept {
      // Each additional byte that we emit steals one bit from the first byte. We therefore
      // manage 7 bits per byte.
      constexpr auto nine_byte_threshold = (UINT64_C (1) << (7U * 8U)) - 1U;
      if (x > nine_byte_threshold) {
        return 9;
      }

      // The input to clz() is ORed with 1 to guarantee we don't pass 0 (which will
      // require 1 byte to store anyway).
      unsigned const bits = 64U - bit_count::clz (x | 1U);
      return (bits - 1U) / 7U + 1U;
    }

    template <typename OutputIterator>
    OutputIterator encode (std::uint64_t x, OutputIterator out) {
      unsigned const bits = 64U - bit_count::clz (x | 1U);
      unsigned bytes;
      if (bits > 56U) {
        bytes = 8U;
        *(out++) = 0U;
      } else {
        bytes = (bits - 1U) / 7U + 1U;
        // Encode the number of bytes in the low bits of the value itself.
        x = (2U * x + 1U) << (bytes - 1U);
      }
      PSTORE_ASSERT (bytes < 9);
      // clang-format off
      switch (bytes) {
      // NOLINTNEXTLINE(bugprone-branch-clone)
      case 8: *(out++) = x & 0xFFU; x >>= 8U; PSTORE_FALLTHROUGH;
      case 7: *(out++) = x & 0xFFU; x >>= 8U; PSTORE_FALLTHROUGH;
      case 6: *(out++) = x & 0xFFU; x >>= 8U; PSTORE_FALLTHROUGH;
      case 5: *(out++) = x & 0xFFU; x >>= 8U; PSTORE_FALLTHROUGH;
      case 4: *(out++) = x & 0xFFU; x >>= 8U; PSTORE_FALLTHROUGH;
      case 3: *(out++) = x & 0xFFU; x >>= 8U; PSTORE_FALLTHROUGH;
      case 2: *(out++) = x & 0xFFU; x >>= 8U; PSTORE_FALLTHROUGH;
      default:
          *(out++) = x & 0xFFU;
      }
      // clang-format on
      return out;
    }


    template <typename InputIterator>
    inline unsigned decode_size (InputIterator in) {
      // (Note that ORing with 0x100 guarantees that bit 8 is set. This in turn ensures that
      // we won't ever pass 0 to clz() which would result in undefined behavior.
      return bit_count::ctz (*in | 0x100U) + 1U;
    }

    namespace details {

      template <typename InputIterator>
      std::uint64_t decode9 (InputIterator in) {
        ++in; // skip the length byte
        auto result = std::uint64_t{0};
        for (auto shift = 0U; shift < 64U; shift += 8U) {
          result |= std::uint64_t{*(in++)} << shift;
        }
        return result;
      }

    } // end namespace details

    template <typename InputIterator>
    std::uint64_t decode (InputIterator in, unsigned size) {
      PSTORE_ASSERT (size > 0 && size == decode_size (in));
      if (size == 9) {
        return details::decode9 (in);
      }

      auto result = std::uint64_t{0};
      auto shift = 0U;
      // clang-format off
      switch (size) {
      // NOLINTNEXTLINE(bugprone-branch-clone)
      case 8: result |= std::uint64_t{*(in++)} << shift; shift += 8; PSTORE_FALLTHROUGH;
      case 7: result |= std::uint64_t{*(in++)} << shift; shift += 8; PSTORE_FALLTHROUGH;
      case 6: result |= std::uint64_t{*(in++)} << shift; shift += 8; PSTORE_FALLTHROUGH;
      case 5: result |= std::uint64_t{*(in++)} << shift; shift += 8; PSTORE_FALLTHROUGH;
      case 4: result |= std::uint64_t{*(in++)} << shift; shift += 8; PSTORE_FALLTHROUGH;
      case 3: result |= std::uint64_t{*(in++)} << shift; shift += 8; PSTORE_FALLTHROUGH;
      case 2: result |= std::uint64_t{*(in++)} << shift; shift += 8; PSTORE_FALLTHROUGH;
      default:
          result |= std::uint64_t{*(in++)} << shift;
      }
      // clang-format on
      return result >> size; // throw away the unwanted size bytes frm the first byte.
    }

    template <typename InputIterator>
    std::uint64_t decode (InputIterator in) {
      return decode (in, decode_size (in));
    }

  } // end namespace varint
} // end namespace pstore

#endif // PSTORE_SUPPORT_VARINT_HPP
