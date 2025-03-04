//===- include/pstore/core/storage.hpp --------------------*- mode: C++ -*-===//
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

#ifndef PSTORE_CORE_STORAGE_HPP
#define PSTORE_CORE_STORAGE_HPP

#include "pstore/core/address.hpp"
#include "pstore/core/region.hpp"
#include "pstore/support/aligned.hpp"
#include "pstore/support/assert.hpp"

namespace pstore {

  /// An entry in the segment address table.
  struct sat_entry {
    /// A pointer to the data belonging to the segment represented by this
    /// entry in the segment address table. The pointer will always lie
    /// within the memory-mapped region given by 'region'.
    std::shared_ptr<void> value;

    /// The memory-mapped region to which the 'value' pointer belongs.
    region::memory_mapper_ptr region;

#ifndef NDEBUG
    bool is_valid () const noexcept {
      if (value == nullptr && region == nullptr) {
        return true;
      }

      auto * const ptr = static_cast<std::uint8_t const *> (value.get ());
      auto * const region_base = static_cast<std::uint8_t const *> (region->data ().get ());
      auto * const region_end = region_base + region->size ();
      return ptr >= region_base && ptr + address::segment_size <= region_end;
    }
#endif
  };

  /// \brief The number of entries in the \ref segment_address_table.
  static auto constexpr sat_elements = address::max_segment + 1U;
  using segment_address_table = std::array<sat_entry, sat_elements>;
  using sat_iterator = segment_address_table::iterator;
  using file_ptr = std::shared_ptr<file::file_base>;

  class storage {
  public:
    static auto constexpr full_region_size = UINT64_C (1) << 32U; // 4 Gigabytes
    static auto constexpr min_region_size = UINT64_C (1) << 22U;  // 4 Megabytes
    // Check that full_region_size is a multiple of min_region_size
    PSTORE_STATIC_ASSERT (full_region_size % min_region_size == 0);

    using region_container = std::vector<region::memory_mapper_ptr>;

    template <typename File>
    storage (std::shared_ptr<File> const & file,
             std::unique_ptr<system_page_size_interface> && page_size,
             std::unique_ptr<region::factory> && region_factory)
            : file_{std::static_pointer_cast<file::file_base> (file)}
            , page_size_{std::move (page_size)}
            , region_factory_{std::move (region_factory)}
            , regions_{region_factory_->init ()} {}

    template <typename File>
    explicit storage (std::shared_ptr<File> const & file)
            : file_{std::static_pointer_cast<file::file_base> (file)}
            , region_factory_{region::get_factory (
                std::static_pointer_cast<file::file_handle> (file), full_region_size,
                min_region_size)}
            , regions_{region_factory_->init ()} {}

    file::file_base * file () noexcept { return file_.get (); }
    file::file_base const * file () const noexcept { return file_.get (); }

    /// Changes the number of physical allocated bytes so that they can accommodate the new
    /// logical size requirement.
    ///
    /// \param old_logical_size  The existing logical size of the allocated storage in bytes.
    /// \param new_logical_size  The requested logical size of the allocated storage in bytes.
    void map_bytes (std::uint64_t old_logical_size, std::uint64_t new_logical_size);

    /// Adjust the size of the underlying file to match that of the allocated regions.
    void truncate_to_physical_size ();

    /// Called to add newly created memory-mapped regions to the segment address table. This
    /// happens when the file is initially opened, and when it is grown by calling allocate().
    void update_master_pointers (std::size_t old_length);

    struct copy_from_store_traits {
      using in_store_pointer = std::uint8_t const *;
      using temp_pointer = std::uint8_t *;
    };
    struct copy_to_store_traits {
      using in_store_pointer = std::uint8_t *;
      using temp_pointer = std::uint8_t const *;
    };

    /// This function performs the bulk of the work of creating a "shadow" block when a request
    /// spans more than one memory-mapped region (or when PSTORE_ALWAYS_SPANNING is enabled). It
    /// breaks the data into a series of copies (each read or writing as much data as possible)
    /// and calls the provided "copier" function to perform the actual copy. This same function
    /// is used to copy data from the store into a newly allocated block, and to copy from a
    /// contiguous block back to the store.
    ///
    /// The template traits argument controls the direction of copy: either
    /// copy_from_store_traits or copy_to_store_traits may be used.
    ///
    /// \param addr    The store address of the data to be copied.
    /// \param size    The number of bytes to be copied.
    /// \param p       A pointer to a block of memory of at least 'size' bytes.
    /// \param copier  A function that will perform the actual copy. The expected type of the
    ///                function's arguments are controlled by the 'traits' template argument.
    ///                The function will be passed 3 arguments:
    ///                - Traits::in_store_pointer: An address lying within the data store.
    ///                - Traits::temp_pointer: A pointer within the memory block given by
    ///                [p,p+size).
    ///                - std::uint64_t: The number of bytes to be copied.
    template <typename Traits, typename Function>
    void copy (address addr, std::size_t size, typename Traits::temp_pointer p,
               Function copier) const;

    /// \brief Returns true if the given address range "spans" more than one region.
    ///
    /// \note The PSTORE_ALWAYS_SPANNING configure-time setting can cause this function to
    /// always return true.
    ///
    /// \note We expect \p addr and \p size to have been checked for sanity by the caller.
    ///
    /// \param addr The start of the address range to be considered.
    /// \param size The size of the address range to be considered.
    /// \returns true if the given address range "spans" more than one region.
    bool request_spans_regions (address const & addr, std::size_t size) const noexcept;

    /// Marks the address range [first, last) as read-only.
    void protect (address first, address last);

    ///@{
    /// Returns the base address of a segment given its index.
    /// \param segment The segment number whose base address it to be returned. The segment
    ///                number must lie within the memory mapped regions.
    std::shared_ptr<void const> segment_base (address::segment_type const segment) const noexcept {
      return segment_base_impl (*this, segment);
    }
    std::shared_ptr<void> const & segment_base (address::segment_type const segment) noexcept {
      return segment_base_impl (*this, segment);
    }
    ///@}

    /// \name Converting a store address to a shared pointer.
    ///@{
    std::shared_ptr<void const> address_to_pointer (address const addr) const noexcept {
      return address_to_pointer_impl (*this, addr);
    }
    std::shared_ptr<void> address_to_pointer (address const addr) noexcept {
      return address_to_pointer_impl (*this, addr);
    }

    template <typename T>
    std::shared_ptr<T const> address_to_pointer (typed_address<T> const addr) const noexcept {
      return std::static_pointer_cast<T const> (address_to_pointer (addr.to_address ()));
    }
    template <typename T>
    std::shared_ptr<T> address_to_pointer (typed_address<T> const addr) noexcept {
      return std::static_pointer_cast<T> (address_to_pointer (addr.to_address ()));
    }
    ///@}

    /// \name Converting a store address to a raw pointers.
    ///@{
    std::uint8_t * address_to_raw_pointer (address const addr) noexcept {
      return address_to_raw_pointer_impl (*this, addr);
    }
    std::uint8_t const * address_to_raw_pointer (address const addr) const noexcept {
      return address_to_raw_pointer_impl (*this, addr);
    }
    template <typename T>
    T * address_to_raw_pointer (typed_address<T> const addr) noexcept {
      return address_to_raw_pointer (addr.to_address ());
    }
    template <typename T>
    T const * address_to_raw_pointer (typed_address<T> const addr) const noexcept {
      return address_to_raw_pointer (addr.to_address ());
    }
    ///@}

    // For unit testing only.
    region_container const & regions () const { return regions_; }

  private:
    void shrink (std::uint64_t new_size);

    static sat_iterator
    slice_region_into_segments (std::shared_ptr<memory_mapper_base> const & region,
                                sat_iterator segment_it, sat_iterator segment_end);

    template <typename Storage,
              typename ResultType = typename inherit_const<Storage, std::shared_ptr<void> const &,
                                                           std::shared_ptr<void const>>::type>
    static auto segment_base_impl (Storage & storage, address::segment_type const segment) noexcept
      -> ResultType;

    /// Converts a store address to the corresponding shared pointer.
    ///
    /// \tparam Storage  The type of the storage object. Should be the const or non-const
    ///   storage class.
    /// \tparam ResultType  The type of the result as derived from the const-ness
    ///   of Storage.
    ///
    /// \param storage  The instance of the storage class.
    /// \param addr  The store address.
    /// \returns  The pointer which corresponds to \p addr.
    template <typename Storage,
              typename ResultType = typename inherit_const<Storage, std::shared_ptr<void>,
                                                           std::shared_ptr<void const>>::type>
    static ResultType address_to_pointer_impl (Storage & storage, address const addr) noexcept;

    /// Converts a store address to the corresponding raw pointer.
    ///
    /// \tparam Storage  The type of the storage object. Should be the const or non-const
    ///   storage class.
    /// \tparam ResultType  The type of the result as derived from the const-ness
    ///   of Storage.
    ///
    /// \param storage  The instance of the storage class.
    /// \param addr  The store address.
    /// \returns  The raw pointer which corresponds to \p addr.
    template <typename Storage, typename ResultType = typename inherit_const<
                                  Storage, std::uint8_t *, std::uint8_t const *>::type>
    static ResultType address_to_raw_pointer_impl (Storage & storage, address addr) noexcept;


    /// The Segment Address Table: an array of pointers to the base-address of each segment's
    /// memory-mapped storage and their corresponding region object.
    std::unique_ptr<segment_address_table> sat_ = std::make_unique<segment_address_table> ();

    /// The file used to hold the data.
    file_ptr file_;
    std::unique_ptr<system_page_size_interface> page_size_ = std::make_unique<system_page_size> ();
    std::unique_ptr<region::factory> region_factory_;
    region_container regions_;
  };

  // segment base
  // ~~~~~~~~~~~~
  template <typename Storage, typename ResultType>
  inline auto storage::segment_base_impl (Storage & storage,
                                          address::segment_type const segment) noexcept
    -> ResultType {
    PSTORE_ASSERT (segment < storage.sat_->size ());
    sat_entry & e = (*storage.sat_)[segment];
    PSTORE_ASSERT (e.is_valid ());
    return e.value;
  }

  // address to pointer
  // ~~~~~~~~~~~~~~~~~~
  template <typename Storage, typename ResultType>
  inline auto storage::address_to_pointer_impl (Storage & storage, address const addr) noexcept
    -> ResultType {
    auto segment_base = storage.segment_base (addr.segment ());
    using uint8_type = typename inherit_const<Storage, std::uint8_t>::type;
    return {segment_base,
            std::static_pointer_cast<uint8_type> (segment_base).get () + addr.offset ()};
  }

  template <typename Storage, typename ResultType>
  inline ResultType storage::address_to_raw_pointer_impl (Storage & storage,
                                                          address const addr) noexcept {

    address::segment_type const segment = addr.segment ();
    PSTORE_ASSERT (segment < storage.sat_->size ());
    sat_entry & e = (*storage.sat_)[segment];
    PSTORE_ASSERT (e.is_valid ());
    return static_cast<ResultType> (static_cast<std::uint8_t *> (e.value.get ()) + addr.offset ());
  }


  // request spans regions
  // ~~~~~~~~~~~~~~~~~~~~~
  inline bool storage::request_spans_regions (address const & addr,
                                              std::size_t const size) const noexcept {
    (void) addr;
    if (size == 0) {
      return false;
    }
#ifdef PSTORE_ALWAYS_SPANNING
    return true;
#else
    return (*sat_)[addr.segment ()].region != (*sat_)[(addr + size - 1U).segment ()].region;
#endif // PSTORE_ALWAYS_SPANNING
  }

  // copy
  // ~~~~
  template <typename Traits, typename Function>
  void storage::copy (address const addr, std::size_t size, typename Traits::temp_pointer p,
                      Function copier) const {

    PSTORE_STATIC_ASSERT (std::numeric_limits<std::size_t>::max () <=
                          std::numeric_limits<std::uint64_t>::max ());
    address::segment_type segment = addr.segment ();
    PSTORE_STATIC_ASSERT (std::numeric_limits<decltype (segment)>::max () <= sat_elements);
    sat_entry const & segment_pointer = (*sat_)[segment];
    PSTORE_ASSERT (segment_pointer.value != nullptr && segment_pointer.region != nullptr &&
                   segment_pointer.is_valid ());

    auto in_store_ptr =
      static_cast<typename Traits::in_store_pointer> (segment_pointer.value.get ()) +
      addr.offset ();
    auto region_base =
      static_cast<typename Traits::in_store_pointer> (segment_pointer.region->data ().get ());
    PSTORE_ASSERT (in_store_ptr >= region_base &&
                   in_store_ptr <= region_base + segment_pointer.region->size ());

    std::uint64_t copy_size =
      segment_pointer.region->size () - static_cast<std::uintptr_t> (in_store_ptr - region_base);
    static_assert (std::numeric_limits<std::size_t>::max () <=
                     std::numeric_limits<std::uint64_t>::max (),
                   "size_t must not be larger than uint64!");
    copy_size = std::min (copy_size, std::uint64_t{size});

    // An initial copy for the tail of the first of the regions covered by the addr..addr+size
    // range.
    copier (in_store_ptr, p, copy_size);

    // Now copy the subsequent region(s).
    for (p += copy_size, size -= copy_size; size > 0; p += copy_size, size -= copy_size) {

      // We copied all of the necessary data to the previous region. Now move to the next
      // region and do the same.
      std::uint64_t const inc = (copy_size + address::segment_size - 1) / address::segment_size;
      PSTORE_ASSERT (inc < std::numeric_limits<address::segment_type>::max ());
      PSTORE_ASSERT (segment + inc < sat_elements);
      segment += static_cast<address::segment_type> (inc);

      region::memory_mapper_ptr const & region = (*sat_)[segment].region;
      PSTORE_ASSERT (region != nullptr);

      copy_size = std::min (static_cast<std::uint64_t> (size), region->size ());
      in_store_ptr = static_cast<typename Traits::in_store_pointer> (region->data ().get ());
      copier (in_store_ptr, p, copy_size);
    }
  }

} // namespace pstore

#endif // PSTORE_CORE_STORAGE_HPP
