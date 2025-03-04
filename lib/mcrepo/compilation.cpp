//===- lib/mcrepo/compilation.cpp -----------------------------------------===//
//*                            _ _       _   _              *
//*   ___ ___  _ __ ___  _ __ (_) | __ _| |_(_) ___  _ __   *
//*  / __/ _ \| '_ ` _ \| '_ \| | |/ _` | __| |/ _ \| '_ \  *
//* | (_| (_) | | | | | | |_) | | | (_| | |_| | (_) | | | | *
//*  \___\___/|_| |_| |_| .__/|_|_|\__,_|\__|_|\___/|_| |_| *
//*                     |_|                                 *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file compilation.cpp
/// \brief Definitions for the compilation data structure.

#include "pstore/mcrepo/compilation.hpp"

#include "pstore/mcrepo/repo_error.hpp"
#include "pstore/support/round2.hpp"

using namespace pstore::repo;

std::ostream & pstore::repo::operator<< (std::ostream & os, linkage const l) {
  char const * str = "unknown";
  switch (l) {
#define X(a)                                                                                       \
  case linkage::a: str = #a; break;
    PSTORE_REPO_LINKAGES
#undef X
  }
  return os << str;
}

namespace {

  /// Checks that a bitfield<> instance BitField has the correct number of bits to store the
  /// values of the enumeration type Enum.
  ///
  /// \tparam Enum  An enumeration type.
  /// \tparam Bitfield  A bitfield<> instance.
  /// \param init  The member values of the enumeration.
  template <typename Enum, typename Bitfield>
  constexpr void assert_enum_field_width (std::initializer_list<Enum> init) noexcept {
    (void) init;
    PSTORE_ASSERT (pstore::round_to_power_of_2 (
                     static_cast<typename std::underlying_type<Enum>::type> (std::max (init)) +
                     1U) == Bitfield::max () + 1U);
  }

} // end anonymous namespace


//*     _      __ _      _ _   _           *
//*  __| |___ / _(_)_ _ (_) |_(_)___ _ _   *
//* / _` / -_)  _| | ' \| |  _| / _ \ ' \  *
//* \__,_\___|_| |_|_||_|_|\__|_\___/_||_| *
//*                                        *
// ctor
// ~~~~
definition::definition (pstore::index::digest const d, pstore::extent<fragment> const x,
                        pstore::typed_address<pstore::indirect_string> const n,
                        enum linkage const l, enum visibility const v) noexcept
        : digest{d}
        , fext{x}
        , name{n} {

  PSTORE_STATIC_ASSERT (std::is_standard_layout<definition>::value);
  PSTORE_STATIC_ASSERT (alignof (definition) == 16);
  PSTORE_STATIC_ASSERT (sizeof (definition) == 48);
  PSTORE_STATIC_ASSERT (offsetof (definition, digest) == 0);
  PSTORE_STATIC_ASSERT (offsetof (definition, fext) == 16);
  PSTORE_STATIC_ASSERT (offsetof (definition, name) == 32);
  PSTORE_STATIC_ASSERT (offsetof (definition, bf) == 40);
  PSTORE_STATIC_ASSERT (offsetof (definition, padding1) == 41);
  PSTORE_STATIC_ASSERT (offsetof (definition, padding2) == 42);
  PSTORE_STATIC_ASSERT (offsetof (definition, padding3) == 44);

#define X(a) repo::linkage::a,
  assert_enum_field_width<enum linkage, decltype (linkage_)> ({PSTORE_REPO_LINKAGES});
#undef X
#define X(a) repo::visibility::a,
  assert_enum_field_width<enum visibility, decltype (visibility_)> ({PSTORE_REPO_VISIBILITIES});
#undef X
  linkage_ = static_cast<std::underlying_type<enum linkage>::type> (l);
  visibility_ = static_cast<std::underlying_type<enum visibility>::type> (v);
}


//*                    _ _      _   _           *
//*  __ ___ _ __  _ __(_) |__ _| |_(_)___ _ _   *
//* / _/ _ \ '  \| '_ \ | / _` |  _| / _ \ ' \  *
//* \__\___/_|_|_| .__/_|_\__,_|\__|_\___/_||_| *
//*              |_|                            *
// operator new
// ~~~~~~~~~~~~
void * compilation::operator new (std::size_t const s, nmembers const size) {
  (void) s;
  std::size_t const actual_bytes = compilation::size_bytes (size.n);
  PSTORE_ASSERT (actual_bytes >= s);
  return ::operator new (actual_bytes);
}

void * compilation::operator new (std::size_t const s, void * const ptr) {
  return ::operator new (s, ptr);
}

// operator delete
// ~~~~~~~~~~~~~~~
void compilation::operator delete (void * const p, nmembers const /*size*/) {
  ::operator delete (p);
}

void compilation::operator delete (void * const /*p*/, void * const /*ptr*/) {}

void compilation::operator delete (void * const p) {
  ::operator delete (p);
}

// load
// ~~~~
auto compilation::load (pstore::database const & db, pstore::extent<compilation> const & location)
  -> std::shared_ptr<compilation const> {
  std::shared_ptr<compilation const> t = db.getro (location);
#if PSTORE_SIGNATURE_CHECKS_ENABLED
  if (t->signature_ != compilation_signature_) {
    raise_error_code (make_error_code (error_code::bad_compilation_record));
  }
#endif
  if (t->size_bytes () != location.size) {
    raise_error_code (make_error_code (error_code::bad_compilation_record));
  }
  return t;
}
