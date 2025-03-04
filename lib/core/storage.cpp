//===- lib/core/storage.cpp -----------------------------------------------===//
//*      _                              *
//*  ___| |_ ___  _ __ __ _  __ _  ___  *
//* / __| __/ _ \| '__/ _` |/ _` |/ _ \ *
//* \__ \ || (_) | | | (_| | (_| |  __/ *
//* |___/\__\___/|_|  \__,_|\__, |\___| *
//*                         |___/       *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file storage.cpp

#include "pstore/core/storage.hpp"
#include "pstore/core/file_header.hpp"

namespace {

  ///@{
  /// Rounds the value 'v' down the next lowest multiple of 'boundary'.
  /// The result is equivalent to a function such as:
  ///     round_down x b = x - x `mod` b

  /// \param x  The value to be rounded.
  /// \param b  The boundary to which 'x' is to be rounded. It must be a power of 2.
  /// \return  The value x2 such as x2 <= x and x2 `mod` b == 0
  constexpr std::uint64_t round_down (std::uint64_t const x, std::uint64_t const b) noexcept {
    PSTORE_ASSERT (pstore::is_power_of_two (b));
    std::uint64_t const r = x & ~(b - 1U);
    PSTORE_ASSERT (r <= x && r % b == 0);
    return r;
  }

  /// \param x  The value to be rounded.
  /// \param b  The boundary to which 'x' is to be rounded. It must be a power of 2.
  /// \return  The value x2 such as x2 <= x and x2 `mod` b == 0
  constexpr pstore::address round_down (pstore::address const x, std::uint64_t const b) noexcept {
    return pstore::address{round_down (x.absolute (), b)};
  }
  ///@}

} // end anonymous namespace

namespace pstore {

  // truncate to physical size
  // ~~~~~~~~~~~~~~~~~~~~~~~~~
  void storage::truncate_to_physical_size () {
    file_->truncate (regions_.empty () ? 0 : regions_.back ()->end ());
  }

  // map bytes
  // ~~~~~~~~~
  void storage::map_bytes (std::uint64_t const old_logical_size,
                           std::uint64_t const new_logical_size) {
    // Get the file offset of the end of the last memory mapped region.
    std::uint64_t const old_physical_size =
      regions_.empty () ? std::uint64_t{0} : regions_.back ()->end ();
    if (new_logical_size > old_physical_size) {
      // if growing the storage
      auto const old_num_regions = regions_.size ();
      // Allocate new memory region(s) to accommodate the additional bytes requested.
      region_factory_->add (&regions_, old_physical_size, new_logical_size);
      this->update_master_pointers (old_num_regions);
      return;
    }
    if (new_logical_size < old_logical_size) {
      // if shrinking the storage
      this->shrink (new_logical_size);
    }
  }

  // shrink
  // ~~~~~~
  void storage::shrink (std::uint64_t const new_size) {
    // We now look backwards through the regions, discarding segments and regions introduced
    // by this transaction
    while (!regions_.empty ()) {
      region::memory_mapper_ptr const & region = regions_.back ();
      PSTORE_ASSERT (region->offset () % address::segment_size == 0 &&
                     region->size () % address::segment_size == 0);
      if (region->offset () < new_size) {
        // Guarantee that segments beyond the mapped memory blocks are null.
        PSTORE_ASSERT (std::all_of (
          std::begin (*sat_) + region->end () / address::segment_size, std::end (*sat_),
          [] (sat_entry const & s) { return s.value == nullptr && s.region == nullptr; }));
        return;
      }

      // Remove the entries in the segment address table.
      auto const sp = gsl::make_span (*sat_).subspan (region->offset () / address::segment_size,
                                                      region->size () / address::segment_size);
      std::for_each (std::begin (sp), std::end (sp), [&region] (sat_entry & segment) {
        PSTORE_ASSERT (segment.region == region);
        segment.region = nullptr;
        segment.value = nullptr;
      });
      // Check that we caught every reference to this region.
      PSTORE_ASSERT (
        std::all_of (std::begin (*sat_), std::end (*sat_),
                     [&region] (sat_entry const & segment) { return segment.region != region; }));

      // Remove the region itself.
      regions_.pop_back ();
    }

    // There are no mapped memory blocks. The segment address table should be all null.
    PSTORE_ASSERT (std::all_of (std::begin (*sat_), std::end (*sat_), [] (sat_entry const & s) {
      return s.value == nullptr && s.region == nullptr;
    }));
  }

  // update master pointers
  // ~~~~~~~~~~~~~~~~~~~~~~
  void storage::update_master_pointers (std::size_t const old_length) {

    std::uint64_t last_sat_entry = 0;
    if (old_length > 0) {
      PSTORE_ASSERT (old_length < regions_.size ());
      region::memory_mapper_ptr const & region = regions_[old_length - 1];
      last_sat_entry = (region->offset () + region->size ()) / address::segment_size;
      PSTORE_ASSERT (sat_->at (last_sat_entry - 1).value != nullptr);
    }

    auto segment_it = std::begin (*sat_);
    auto const segment_end = std::end (*sat_);
    using segment_difference_type = std::iterator_traits<decltype (segment_it)>::difference_type;

    PSTORE_ASSERT (last_sat_entry <=
                   static_cast<std::make_unsigned<segment_difference_type>::type> (
                     std::numeric_limits<segment_difference_type>::max ()));
    std::advance (segment_it, static_cast<segment_difference_type> (last_sat_entry));

    auto region_it = std::begin (regions_);
    auto const region_end = std::end (regions_);

    using region_difference_type = std::iterator_traits<decltype (region_it)>::difference_type;
    PSTORE_ASSERT (old_length <= static_cast<std::make_unsigned<region_difference_type>::type> (
                                   std::numeric_limits<region_difference_type>::max ()));
    std::advance (region_it, static_cast<region_difference_type> (old_length));

    for (; region_it != region_end; ++region_it) {
      segment_it = storage::slice_region_into_segments (*region_it, segment_it, segment_end);
    }

    // Guarantee that segments beyond the mapped memory blocks are null.
    PSTORE_ASSERT (std::all_of (segment_it, segment_end, [] (sat_entry const & s) {
      return s.value == nullptr && s.region == nullptr;
    }));
  }

  // slice region into segments
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~
  auto storage::slice_region_into_segments (std::shared_ptr<memory_mapper_base> const & region,
                                            sat_iterator segment_it, sat_iterator const segment_end)
    -> sat_iterator {

    (void) segment_end; // silence unused argument warning in release build.
    std::shared_ptr<void> const data = region->data ();

    auto ptr = std::static_pointer_cast<std::uint8_t> (data).get ();
    auto const end = ptr + region->size ();
    for (; ptr < end; ptr += address::segment_size) {
      PSTORE_ASSERT (segment_it != segment_end);
      sat_entry & segment = *segment_it;
      PSTORE_ASSERT (segment.value == nullptr && segment.region == nullptr);

      // The segment's memory (at 'ptr') is managed by the 'data' shared_ptr.
      segment.value = std::shared_ptr<void> (data, ptr);
      segment.region = region;

      ++segment_it;
    }
    return segment_it;
  }

  // protect
  // ~~~~~~~
  void storage::protect (address first, address last) {
    std::uint64_t const page_size = memory_mapper::page_size (*page_size_);
    PSTORE_ASSERT (page_size > 0 && is_power_of_two (page_size));

    first = std::max (round_down (first, page_size),
                      address{round_down (leader_size + page_size - 1U, page_size)});
    last = round_down (last, page_size);

    auto const end = regions_.rend ();
    for (auto region_it = regions_.rbegin (); region_it != end; ++region_it) {
      std::shared_ptr<memory_mapper_base> & region = *region_it;

      PSTORE_ASSERT (region->offset () % page_size == 0);
      std::uint64_t const first_offset = std::max (region->offset (), first.absolute ());
      std::uint64_t const last_offset =
        std::min (region->offset () + region->size (), last.absolute ());

      if (region->offset () + region->size () < first_offset) {
        break;
      }

      if (first_offset >= first.absolute () && last_offset > first_offset) {
        auto const pfirst =
          this->address_to_pointer (typed_address<std::uint8_t>::make (first_offset));

        PSTORE_ASSERT (pfirst >= region->data ());
        PSTORE_ASSERT (last_offset - region->offset () <= region->size ());

        region->read_only (pfirst.get (), last_offset - first_offset);
      }
    }
  }

} // end namespace pstore
