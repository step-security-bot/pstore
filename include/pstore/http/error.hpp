//===- include/pstore/http/error.hpp ----------------------*- mode: C++ -*-===//
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
#ifndef PSTORE_HTTP_ERROR_HPP
#define PSTORE_HTTP_ERROR_HPP

#include <string>
#include <system_error>

#ifdef _WIN32
#  include <winsock2.h>
#endif // _WIN32

#include "pstore/support/error.hpp"

namespace pstore {
  namespace http {

    // **************
    // * error code *
    // **************
    enum class error_code : int {
      bad_request = 1,
      bad_websocket_version,
      not_implemented,
      string_too_long,
      refill_out_of_range,
    };

    // ******************
    // * error category *
    // ******************
    class error_category final : public std::error_category {
    public:
      char const * name () const noexcept override;
      std::string message (int error) const override;
    };

    std::error_category const & get_error_category () noexcept;

    inline std::error_code make_error_code (error_code const e) {
      static_assert (
        std::is_same_v<std::underlying_type_t<decltype (e)>, int>,
        "base type of pstore::httpd::error_code must be int to permit safe static cast");
      return {static_cast<int> (e), get_error_category ()};
    }


    // get last error
    // ~~~~~~~~~~~~~~
    inline std::error_code get_last_error () noexcept {
#ifdef _WIN32
      return make_error_code (win32_erc{static_cast<DWORD> (WSAGetLastError ())});
#else
      return make_error_code (errno_erc{errno});
#endif // !_WIN32
    }

  } // end namespace http
} // end namespace pstore


namespace std {

  template <>
  struct is_error_code_enum<pstore::http::error_code> : std::true_type {};

} // end namespace std


#endif // PSTORE_HTTP_ERROR_HPP
