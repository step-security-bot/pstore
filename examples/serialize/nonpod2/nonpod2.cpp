//===- examples/serialize/nonpod2/nonpod2.cpp -----------------------------===//
//*                                    _ ____   *
//*  _ __   ___  _ __  _ __   ___   __| |___ \  *
//* | '_ \ / _ \| '_ \| '_ \ / _ \ / _` | __) | *
//* | | | | (_) | | | | |_) | (_) | (_| |/ __/  *
//* |_| |_|\___/|_| |_| .__/ \___/ \__,_|_____| *
//*                   |_|                       *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include <iostream>

#include "pstore/serialize/archive.hpp"
#include "pstore/serialize/types.hpp"

namespace {

  class foo {
    friend struct pstore::serialize::serializer<foo>;

  public:
    constexpr explicit foo (int const a) noexcept
            : a_ (a) {}
    std::ostream & write (std::ostream & os) const { return os << "foo(" << a_ << ')'; }

  private:
    int a_;
  };

  std::ostream & operator<< (std::ostream & os, foo const & f) {
    return f.write (os);
  }

} // end anonymous namespace

namespace pstore::serialize {

  // A serializer for struct foo
  template <>
  struct serializer<foo> {
    using value_type = foo;

    // Writes an instance of foo to an archive. The data stream contains
    // a single int value.
    template <typename Archive>
    static auto write (Archive && archive, value_type const & value) ->
      typename std::decay_t<Archive>::result_type {
      return serialize::write (std::forward<Archive> (archive), value.a_);
    }

    // Reads an instance of foo from an archive.
    template <typename Archive>
    static void read (Archive && archive, value_type & sp) {
      new (&sp) foo (serialize::read<int> (std::forward<Archive> (archive)));
    }
  };

} // end namespace pstore::serialize

namespace {

  auto write_foo () {
    std::vector<std::uint8_t> bytes;
    pstore::serialize::archive::vector_writer writer (bytes);

    foo f (42);
    std::cout << "Writing: " << f << '\n';
    pstore::serialize::write (writer, f);

    std::cout << "Wrote these bytes: " << writer << '\n';
    return bytes;
  }

  void read_foo (std::vector<std::uint8_t> const & bytes) {
    auto reader = pstore::serialize::archive::make_reader (std::begin (bytes));
    foo f = pstore::serialize::read<foo> (reader);
    std::cout << "Read: " << f << '\n';
  }

} // end anonymous namespace

int main () {
  std::vector<std::uint8_t> bytes = write_foo ();
  read_foo (bytes);
}
