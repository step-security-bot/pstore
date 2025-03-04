//===- lib/http/error.cpp -------------------------------------------------===//
//*                            *
//*   ___ _ __ _ __ ___  _ __  *
//*  / _ \ '__| '__/ _ \| '__| *
//* |  __/ |  | | | (_) | |    *
//*  \___|_|  |_|  \___/|_|    *
//*                            *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file error.cpp
/// \brief Implementation of the HTTP error category.

#include "pstore/http/error.hpp"

namespace pstore {
  namespace http {

    // ******************
    // * error category *
    // ******************
    // name
    // ~~~~
    gsl::czstring error_category::name () const noexcept {
      return "pstore httpd category";
    }

    // message
    // ~~~~~~~
    std::string error_category::message (int const error) const {
      switch (static_cast<error_code> (error)) {
      case error_code::bad_request: return "Bad request";
      case error_code::bad_websocket_version: return "Bad WebSocket version requested";
      case error_code::not_implemented: return "Not implemented";
      case error_code::string_too_long: return "String too long";
      case error_code::refill_out_of_range: return "Refill result out of range";
      }
      return "unknown pstore::category error";
    }

    // **********************
    // * get_error_category *
    // **********************
    std::error_category const & get_error_category () noexcept {
      static error_category const cat;
      return cat;
    }

  } // end namespace http
} // end namespace pstore
