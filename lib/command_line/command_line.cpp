//===- lib/command_line/command_line.cpp ----------------------------------===//
//*                                                _   _ _             *
//*   ___ ___  _ __ ___  _ __ ___   __ _ _ __   __| | | (_)_ __   ___  *
//*  / __/ _ \| '_ ` _ \| '_ ` _ \ / _` | '_ \ / _` | | | | '_ \ / _ \ *
//* | (_| (_) | | | | | | | | | | | (_| | | | | (_| | | | | | | |  __/ *
//*  \___\___/|_| |_| |_|_| |_| |_|\__,_|_| |_|\__,_| |_|_|_| |_|\___| *
//*                                                                    *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "pstore/command_line/command_line.hpp"

#include "pstore/command_line/string_distance.hpp"

namespace pstore::command_line::details {

  // lookup nearest option
  // ~~~~~~~~~~~~~~~~~~~~~
  std::optional<option *> lookup_nearest_option (std::string const & arg,
                                                 option::options_container const & all_options) {
    std::optional<option *> best_option;
    if (arg.empty ()) {
      return best_option;
    }
    // Find the closest match.
    auto best_distance = std::numeric_limits<std::string::size_type>::max ();
    for (auto const & opt : all_options) {
      auto const distance = string_distance (opt->name (), arg, best_distance);
      if (distance < best_distance) {
        best_option = opt;
        best_distance = distance;
      }
    }
    return best_option;
  }

  // starts with
  // ~~~~~~~~~~~
  bool starts_with (std::string const & s, gsl::czstring prefix) {
    auto const end = std::end (s);
    for (auto it = std::begin (s); *prefix != '\0' && it != end; ++it, ++prefix) {
      if (*it != *prefix) {
        return false;
      }
    }
    return *prefix == '\0';
  }

  // find handler
  // ~~~~~~~~~~~~
  std::optional<option *> find_handler (std::string const & name) {
    auto const & all_options = option::all ();
    auto const end = std::end (all_options);
    auto const it =
      std::find_if (std::begin (all_options), end,
                    [&name] (option const * const opt) { return opt->name () == name; });
    return it != end ? std::optional<option *> (*it) : std::optional<option *> ();
  }

  // argument is positional
  // ~~~~~~~~~~~~~~~~~~~~~~
  bool argument_is_positional (std::string const & arg_name) {
    return arg_name.empty () || arg_name.front () != '-';
  }

  // handler takes argument
  // ~~~~~~~~~~~~~~~~~~~~~~
  bool handler_takes_argument (std::optional<option *> handler) {
    return handler && (*handler)->takes_argument ();
  }

  // handler set value
  // ~~~~~~~~~~~~~~~~~
  bool handler_set_value (std::optional<option *> handler, std::string const & value) {
    PSTORE_ASSERT (handler_takes_argument (handler));
    if (!(*handler)->add_occurrence ()) {
      return false;
    }
    return (*handler)->value (value);
  }

  // get option and value
  // ~~~~~~~~~~~~~~~~~~~~
  std::tuple<std::string, std::optional<std::string>> get_option_and_value (std::string arg) {
    static constexpr char double_dash[] = "--";
    static constexpr auto double_dash_len = std::string::size_type{2};

    std::optional<std::string> value;
    if (starts_with (arg, double_dash)) {
      std::size_t const equal_pos = arg.find ('=', double_dash_len);
      if (equal_pos == std::string::npos) {
        arg.erase (0U, double_dash_len);
      } else {
        value = arg.substr (equal_pos + 1, std::string::npos);
        PSTORE_ASSERT (equal_pos >= double_dash_len);
        arg = arg.substr (double_dash_len, equal_pos - double_dash_len);
      }
    } else {
      PSTORE_ASSERT (starts_with (arg, "-"));
      arg.erase (0U, 1U);
    }
    return std::make_tuple (arg, value);
  }

} // end namespace pstore::command_line::details
