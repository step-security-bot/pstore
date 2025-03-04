//===- lib/mcrepo/repo_error.cpp ------------------------------------------===//
//*                                                   *
//*  _ __ ___ _ __   ___     ___ _ __ _ __ ___  _ __  *
//* | '__/ _ \ '_ \ / _ \   / _ \ '__| '__/ _ \| '__| *
//* | | |  __/ |_) | (_) | |  __/ |  | | | (_) | |    *
//* |_|  \___| .__/ \___/   \___|_|  |_|  \___/|_|    *
//*          |_|                                      *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "pstore/mcrepo/repo_error.hpp"

namespace pstore {
  namespace repo {

    char const * error_category::name () const noexcept {
      return "pstore_mcrepo category";
    }

    std::string error_category::message (int const error) const {
      auto * result = "unknown error";
      switch (static_cast<error_code> (error)) {
      case error_code::bad_fragment_record: result = "bad fragment record"; break;
      case error_code::bad_fragment_type: result = "bad fragment type"; break;
      case error_code::bad_compilation_record: result = "bad compilation record"; break;
      case error_code::too_many_members_in_compilation:
        result = "too many members in a compilation";
        break;
      case error_code::bss_section_too_large: result = "bss section too large"; break;
      }
      return result;
    }

    std::error_code make_error_code (pstore::repo::error_code const e) {
      static_assert (std::is_same_v<std::underlying_type<decltype (e)>::type, int>,
                     "base type of error_code must be int to permit safe static cast");
      static pstore::repo::error_category const cat;
      return {static_cast<int> (e), cat};
    }

  } // namespace repo
} // namespace pstore
