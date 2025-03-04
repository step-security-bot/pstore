//===- unittests/core/test_indirect_string.cpp ----------------------------===//
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

// 3rd party includes
#include <gtest/gtest.h>

// pstore includes
#include "pstore/core/hamt_set.hpp"
#include "pstore/core/index_types.hpp"
#include "pstore/serialize/types.hpp"

// Local includes
#include "check_for_error.hpp"
#include "empty_store.hpp"

using namespace std::string_view_literals;

namespace {

  class IndirectString : public testing::Test {
  public:
    IndirectString ()
            : db_{store_.file ()} {
      db_.set_vacuum_mode (pstore::database::vacuum_mode::disabled);
    }

  protected:
    in_memory_store store_;
    pstore::database db_;
  };

} // end anonymous namespace

TEST_F (IndirectString, InMemoryEquality) {
  auto const view = pstore::make_sstring_view ("body");
  pstore::indirect_string const x (db_, &view);
  pstore::indirect_string const y (db_, &view);

  EXPECT_EQ (x.length (), 4U);
  EXPECT_EQ (y.length (), 4U);

  pstore::shared_sstring_view owner;
  EXPECT_EQ (x.as_string_view (&owner), "body"sv);
  EXPECT_TRUE (x == y);
  EXPECT_FALSE (x != y);
}

TEST_F (IndirectString, StoreRefToHeapRoundTrip) {
  constexpr auto str = "string";
  auto const sstring = pstore::make_sstring_view (str);

  auto const pointer_addr = [this, &sstring] () -> pstore::address {
    // Create a transaction
    mock_mutex mutex;
    auto transaction = begin (db_, std::unique_lock<mock_mutex>{mutex});

    // Construct the indirect string and write it to the store.
    pstore::indirect_string indirect{db_, &sstring};
    pstore::address const indirect_addr =
      pstore::serialize::write (pstore::serialize::archive::make_writer (transaction), indirect);
    EXPECT_EQ (transaction.size (), sizeof (pstore::address));

    transaction.commit ();
    return indirect_addr;
  }();

  auto const ind2 = pstore::serialize::read<pstore::indirect_string> (
    pstore::serialize::archive::make_reader (db_, pointer_addr));

  EXPECT_EQ (ind2.length (), std::strlen (str));
  pstore::shared_sstring_view owner;
  EXPECT_EQ (ind2.as_string_view (&owner), pstore::make_sstring_view (str));
}

TEST_F (IndirectString, StoreRoundTrip) {
  constexpr auto str = "string";

  auto const pointer_addr = [this, str] () -> pstore::address {
    // Create a transaction.
    mock_mutex mutex;
    auto transaction = begin (db_, std::unique_lock<mock_mutex>{mutex});

    // Construct the string and the indirect string. Write the indirect pointer to the store.
    pstore::raw_sstring_view const sstring = pstore::make_sstring_view (str);
    pstore::indirect_string indirect{db_, &sstring};
    auto const indirect_addr =
      pstore::serialize::write (pstore::serialize::archive::make_writer (transaction), indirect);
    EXPECT_EQ (transaction.size (), sizeof (pstore::address));

    // Now the body of the string (and patch the pointer).
    pstore::indirect_string::write_body_and_patch_address (
      transaction, sstring, pstore::typed_address<pstore::address> (indirect_addr));

    // Commit the transaction.
    transaction.commit ();
    return indirect_addr;
  }();

  auto const ind2 = pstore::serialize::read<pstore::indirect_string> (
    pstore::serialize::archive::make_reader (db_, pointer_addr));

  pstore::shared_sstring_view owner;
  EXPECT_EQ (ind2.as_string_view (&owner), pstore::make_sstring_view (str));

  // check the 'get_sstring_view' helper function.
  EXPECT_EQ (
    get_sstring_view (db_, pstore::typed_address<pstore::indirect_string> (pointer_addr), &owner),
    pstore::make_sstring_view (str));

  EXPECT_EQ (get_sstring_view (db_, ind2.in_store_address (), &owner),
             pstore::make_sstring_view (str));
  EXPECT_EQ (get_sstring_view (db_, ind2.in_store_address (), std::strlen (str), &owner),
             pstore::make_sstring_view (str));
}

namespace {

  // Construct the string and the indirect string. Write the indirect pointer to the store.
  /// \returns A pair of the indirect-object address and the string-body address.
  std::pair<pstore::address, pstore::address>
  write_indirected_string (pstore::transaction_base & transaction, pstore::gsl::czstring str) {
    using namespace pstore;

    raw_sstring_view const sstring = make_sstring_view (str);

    address const indirect_addr = serialize::write (serialize::archive::make_writer (transaction),
                                                    indirect_string{transaction.db (), &sstring});

    address const body_addr = indirect_string::write_body_and_patch_address (
      transaction, sstring, typed_address<pstore::address>{indirect_addr});

    return {indirect_addr, body_addr};
  }

} // end anonymous namespace

TEST_F (IndirectString, BadDatabaseAddress) {
  using namespace pstore;

  mock_mutex mutex;
  auto transaction = begin (db_, std::unique_lock<mock_mutex>{mutex});

  address indirect_addr;
  address body_addr;
  std::tie (indirect_addr, body_addr) = write_indirected_string (transaction, "string");

  // Write a bogus string-body pointer over the indirect object.
  *db_.getrw (typed_address<address> (indirect_addr)) = address{0x01};

  check_for_error (
    [this, indirect_addr] () {
      shared_sstring_view owner;
      get_sstring_view (db_, typed_address<indirect_string>{indirect_addr}, &owner);
    },
    error_code::bad_address);

  transaction.commit ();
}


namespace {

  class IndirectStringAdder : public testing::Test {
  public:
    IndirectStringAdder ()
            : db_{store_.file ()} {
      db_.set_vacuum_mode (pstore::database::vacuum_mode::disabled);
    }

  protected:
    in_memory_store store_;
    pstore::database db_;
  };

} // end anonymous namespace

TEST_F (IndirectStringAdder, NothingAdded) {
  mock_mutex mutex;
  auto transaction = begin (db_, std::unique_lock<mock_mutex>{mutex});
  auto const name_index = pstore::index::get_index<pstore::trailer::indices::name> (db_);

  pstore::indirect_string_adder adder;
  adder.flush (transaction);
  EXPECT_EQ (transaction.size (), 0U);
  transaction.commit ();
}

TEST_F (IndirectStringAdder, NewString) {
  constexpr auto str = "string";
  {
    mock_mutex mutex;
    auto transaction = begin (db_, std::unique_lock<mock_mutex>{mutex});
    {
      auto const name_index = pstore::index::get_index<pstore::trailer::indices::name> (db_);

      // Use the string adder to insert a string into the index and flush it to the store.
      pstore::indirect_string_adder adder;
      auto const sstring1 = pstore::make_sstring_view (str);
      auto const sstring2 = pstore::make_sstring_view (str);
      {
        std::pair<pstore::index::name_index::iterator, bool> res1 =
          adder.add (transaction, name_index, &sstring1);
        pstore::shared_sstring_view res1_owner;
        EXPECT_EQ (res1.first->as_string_view (&res1_owner), sstring1);
        EXPECT_TRUE (res1.second);
      }
      {
        // adding the same string again should result in nothing being written.
        std::pair<pstore::index::name_index::iterator, bool> const res2 =
          adder.add (transaction, name_index, &sstring2);

        pstore::shared_sstring_view res2_owner;
        EXPECT_EQ (res2.first->as_string_view (&res2_owner), sstring1);
        EXPECT_FALSE (res2.second);
      }
      EXPECT_EQ (transaction.size (), sizeof (pstore::address));
      adder.flush (transaction);
    }
    transaction.commit ();
  }
  {
    auto const name_index = pstore::index::get_index<pstore::trailer::indices::name> (db_);
    auto const sstring = pstore::make_sstring_view (str);
    auto const pos = name_index->find (db_, pstore::indirect_string{db_, &sstring});
    ASSERT_NE (pos, name_index->cend (db_));

    pstore::shared_sstring_view owner;
    EXPECT_EQ (pos->as_string_view (&owner), pstore::make_sstring_view (str));
  }
}
