//===- lib/command_line/parser.cpp ----------------------------------------===//
//*                                  *
//*  _ __   __ _ _ __ ___  ___ _ __  *
//* | '_ \ / _` | '__/ __|/ _ \ '__| *
//* | |_) | (_| | |  \__ \  __/ |    *
//* | .__/ \__,_|_|  |___/\___|_|    *
//* |_|                              *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "pstore/command_line/parser.hpp"

namespace pstore::command_line {

  //*                               _                   *
  //*  _ __  __ _ _ _ ___ ___ _ _  | |__  __ _ ___ ___  *
  //* | '_ \/ _` | '_(_-</ -_) '_| | '_ \/ _` (_-</ -_) *
  //* | .__/\__,_|_| /__/\___|_|   |_.__/\__,_/__/\___| *
  //* |_|                                               *
  parser_base::~parser_base () noexcept = default;

  void parser_base::add_literal_option (std::string const & name, int const value,
                                        std::string const & description) {
    literals_.emplace_back (literal{name, value, description});
  }


  //*                                  _       _            *
  //*  _ __  __ _ _ _ ___ ___ _ _   __| |_ _ _(_)_ _  __ _  *
  //* | '_ \/ _` | '_(_-</ -_) '_| (_-<  _| '_| | ' \/ _` | *
  //* | .__/\__,_|_| /__/\___|_|   /__/\__|_| |_|_||_\__, | *
  //* |_|                                            |___/  *
  parser<std::string>::~parser () noexcept = default;

  std::optional<std::string> parser<std::string>::operator() (std::string const & v) const {
    // If this one of the literal strings?
    auto const begin = this->begin ();
    auto const end = this->end ();
    if (std::distance (begin, end) != 0 &&
        std::find_if (begin, end, [&v] (literal const & lit) { return v == lit.name; }) == end) {
      return {};
    }

    return {v};
  }

} // end namespace pstore::command_line
