//===- lib/core/indirect_string.cpp ---------------------------------------===//
//*  _           _ _               _         _        _              *
//* (_)_ __   __| (_)_ __ ___  ___| |_   ___| |_ _ __(_)_ __   __ _  *
//* | | '_ \ / _` | | '__/ _ \/ __| __| / __| __| '__| | '_ \ / _` | *
//* | | | | | (_| | | | |  __/ (__| |_  \__ \ |_| |  | | | | | (_| | *
//* |_|_| |_|\__,_|_|_|  \___|\___|\__| |___/\__|_|  |_|_| |_|\__, | *
//*                                                           |___/  *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "pstore/core/indirect_string.hpp"

namespace pstore {

  //*  _         _ _            _        _       _            *
  //* (_)_ _  __| (_)_ _ ___ __| |_   __| |_ _ _(_)_ _  __ _  *
  //* | | ' \/ _` | | '_/ -_) _|  _| (_-<  _| '_| | ' \/ _` | *
  //* |_|_||_\__,_|_|_| \___\__|\__| /__/\__|_| |_|_||_\__, | *
  //*                                                  |___/  *

  // as string view
  // ~~~~~~~~~~~~~~
  raw_sstring_view
  indirect_string::as_string_view (gsl::not_null<shared_sstring_view *> const owner) const {
    if (is_pointer_) {
      return *str_;
    }
    if (address_ & in_heap_mask) {
      return *reinterpret_cast<sstring_view<char const *> const *> (address_ & ~in_heap_mask);
    }
    return get_sstring_view (db_, address{address_}, owner);
  }

  // length
  // ~~~~~~
  std::size_t indirect_string::length () const {
    if (is_pointer_) {
      return str_->length ();
    }
    if (address_ & in_heap_mask) {
      return reinterpret_cast<sstring_view<char const *> const *> (address_ & ~in_heap_mask)
        ->length ();
    }
    return serialize::string_helper::read_length (
      serialize::archive::make_reader (db_, address{address_}));
  }

  // operator<
  // ~~~~~~~~~
  bool indirect_string::operator<(indirect_string const & rhs) const {
    PSTORE_ASSERT (&db_ == &rhs.db_);
    shared_sstring_view lhs_owner;
    shared_sstring_view rhs_owner;
    return this->as_string_view (&lhs_owner) < rhs.as_string_view (&rhs_owner);
  }

  // operator==
  // ~~~~~~~~~~
  bool indirect_string::operator== (indirect_string const & rhs) const {
    PSTORE_ASSERT (&db_ == &rhs.db_);
    if (!is_pointer_ && !rhs.is_pointer_) {
      // We define that all strings in the database are unique. That means
      // that if both this and the rhs string are in the store then we can
      // simply compare their addresses.
      auto const equal = address_ == rhs.address_;
      PSTORE_ASSERT (this->equal_contents (rhs) == equal);
      return equal;
    }
    if (is_pointer_ && rhs.is_pointer_ && str_ == rhs.str_) {
      // Note that we can't immediately return false if str_ != rhs.str_
      // because two strings with different address may still have identical
      // contents.
      PSTORE_ASSERT (this->equal_contents (rhs));
      return true;
    }
    return equal_contents (rhs);
  }

  // equal contents
  // ~~~~~~~~~~~~~~
  bool indirect_string::equal_contents (indirect_string const & rhs) const {
    shared_sstring_view lhs_owner;
    shared_sstring_view rhs_owner;
    return this->as_string_view (&lhs_owner) == rhs.as_string_view (&rhs_owner);
  }

  // write string and patch address
  // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  address
  indirect_string::write_body_and_patch_address (transaction_base & transaction,
                                                 raw_sstring_view const & str,
                                                 typed_address<address> const address_to_patch) {
    PSTORE_ASSERT (address_to_patch != typed_address<address>::null ());

    // Make sure the alignment of the string is 2 to ensure that the LSB is clear.
    constexpr auto aligned_to = std::size_t{1U << in_heap_mask};
    transaction.allocate (0, aligned_to);

    // Write the string body.
    auto const body_address = serialize::write (serialize::archive::make_writer (transaction), str);

    // Modify the in-store address field so that it points to the string body.
    auto const addr = transaction.getrw (address_to_patch);
    *addr = body_address;
    return body_address;
  }


  //*  _         _ _            _        _       _                     _    _          *
  //* (_)_ _  __| (_)_ _ ___ __| |_   __| |_ _ _(_)_ _  __ _   __ _ __| |__| |___ _ _  *
  //* | | ' \/ _` | | '_/ -_) _|  _| (_-<  _| '_| | ' \/ _` | / _` / _` / _` / -_) '_| *
  //* |_|_||_\__,_|_|_| \___\__|\__| /__/\__|_| |_|_||_\__, | \__,_\__,_\__,_\___|_|   *
  //*                                                  |___/                           *
  // ctor
  // ~~~~
  indirect_string_adder::indirect_string_adder (std::size_t const expected_size) {
    views_.reserve (expected_size);
  }

  // read
  // ~~~~
  indirect_string indirect_string::read (database const & db,
                                         typed_address<indirect_string> const addr) {
    return serialize::read<indirect_string> (
      serialize::archive::make_reader (db, addr.to_address ()));
  }

  // flush
  // ~~~~~
  void indirect_string_adder::flush (transaction_base & transaction) {
    for (auto const & v : views_) {
      PSTORE_ASSERT (v.second != typed_address<address>::null ());
      indirect_string::write_body_and_patch_address (transaction,
                                                     *std::get<0> (v), // string body
                                                     std::get<1> (v)   // address to patch
      );
    }
    views_.clear ();
  }

  //*  _        _                  __              _   _           *
  //* | |_  ___| |_ __  ___ _ _   / _|_  _ _ _  __| |_(_)___ _ _   *
  //* | ' \/ -_) | '_ \/ -_) '_| |  _| || | ' \/ _|  _| / _ \ ' \  *
  //* |_||_\___|_| .__/\___|_|   |_|  \_,_|_||_\__|\__|_\___/_||_| *
  //*            |_|                                               *
  // get sstring view
  // ~~~~~~~~~~~~~~~~
  raw_sstring_view get_sstring_view (database const & db, typed_address<indirect_string> const addr,
                                     gsl::not_null<shared_sstring_view *> const owner) {
    return indirect_string::read (db, addr).as_db_string_view (owner);
  }

  raw_sstring_view get_sstring_view (database const & db, address const addr,
                                     gsl::not_null<shared_sstring_view *> const owner) {
    *owner = serialize::read<shared_sstring_view> (serialize::archive::make_reader (db, addr));
    return {owner->data (), owner->length ()};
  }

  raw_sstring_view get_sstring_view (database const & db, address const addr,
                                     std::size_t const length,
                                     gsl::not_null<shared_sstring_view *> const owner) {
    *owner = shared_sstring_view{
      db.getro (typed_address<char>::make (addr + std::max (varint::encoded_size (length), 2U)),
                length),
      length};
    return {owner->data (), length};
  }

  // get unique sstring view
  // ~~~~~~~~~~~~~~~~~~~~~~~
  raw_sstring_view
  get_unique_sstring_view (database const & db, address const addr, std::size_t const length,
                           gsl::not_null<unique_pointer_sstring_view *> const owner) {
    *owner = unique_pointer_sstring_view{
      db.getrou (typed_address<char>::make (addr + std::max (varint::encoded_size (length), 2U)),
                 length),
      length};
    return {owner->data (), length};
  }

} // end namespace pstore
