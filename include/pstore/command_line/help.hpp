//===- include/pstore/command_line/help.hpp ---------------*- mode: C++ -*-===//
//*  _          _        *
//* | |__   ___| |_ __   *
//* | '_ \ / _ \ | '_ \  *
//* | | | |  __/ | |_) | *
//* |_| |_|\___|_| .__/  *
//*              |_|     *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef PSTORE_COMMAND_LINE_HELP_HPP
#define PSTORE_COMMAND_LINE_HELP_HPP

#include <iomanip>
#include <map>
#include <set>
#include <tuple>

#include "pstore/adt/small_vector.hpp"
#include "pstore/command_line/option.hpp"
#include "pstore/command_line/stream_traits.hpp"
#include "pstore/command_line/word_wrapper.hpp"
#include "pstore/support/array_elements.hpp"
#include "pstore/support/unsigned_cast.hpp"

namespace pstore::command_line {

  namespace details {

    template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
    constexpr int int_cast (T value) noexcept {
      using common = std::common_type_t<T, unsigned>;
      return static_cast<int> (
        std::min (static_cast<common> (value),
                  static_cast<common> (unsigned_cast (std::numeric_limits<int>::max ()))));
    }

    /// The maximum allowed length of the name of an option in the help output.
    constexpr std::size_t overlong_opt_max = 20;

    /// This string is used as a prefix for all option names in the help output.
    constexpr char const prefix_indent[] = "  ";
    constexpr auto const prefix_indent_len = static_cast<int> (array_elements (prefix_indent) - 1U);

    struct less_name {
      bool operator() (gsl::not_null<option const *> x, gsl::not_null<option const *> y) const;
    };

    using options_set = std::set<gsl::not_null<option const *>, less_name>;
    using categories_collection = std::map<option_category const *, options_set>;
    using switch_strings =
      std::map<gsl::not_null<option const *>, small_vector<std::tuple<std::string, std::size_t>, 1>,
               less_name>;

    /// Returns an estimation of the terminal width. This can be used to determine the
    /// point at which output text should be word-wrapped.
    std::size_t get_max_width ();

    /// Returns true if the program has any non-positional arguments.
    ///
    /// \param self  Should be the help option so that it is excluded from the test.
    /// \param all  The collection of all switches.
    /// \return True if the program has any non-positional arguments, false otherwise.
    bool has_switches (option const * self, option::options_container const & all);

    /// Builds a container which maps from an option to one or more fully decorated
    /// strings. Each string has leading dashes and trailing meta-description added.
    /// Alias options are not found in the collection: instead their description is
    /// included in that of the original option. Multiple short descriptions are folded
    /// into a single string for presentation to the user.
    switch_strings get_switch_strings (options_set const & ops);

    /// Builds a container which maps from each option-category to the set of its member
    /// options (including their aliases).
    ///
    /// \param self  Should be the help option so that it is excluded from the test.
    /// \param all  The collection of all switches.
    /// \returns  A container which maps from each option-category to the set of its
    ///   member options (including their aliases).
    categories_collection build_categories (option const * self,
                                            option::options_container const & all);

    /// Scans the collection of option names and returns the longest that will be
    /// presented the user. The maximum return value is overlong_opt_max.
    ///
    /// \returns  The width to be allowed in the left column for option names.
    std::size_t widest_option (categories_collection const & categories);

  } // end namespace details

  template <typename OutputStream>
  class help final : public option {
  public:
    template <class... Mods>
    explicit help (std::string program_name, std::string program_overview, OutputStream & outs,
                   Mods const &... mods)
            : program_name_{std::move (program_name)}
            , overview_{std::move (program_overview)}
            , outs_{outs} {

      apply_to_option (*this, mods...);
    }

    help (help const &) = delete;
    help (help &&) = delete;
    ~help () override = default;

    help & operator= (help const &) = delete;
    help & operator= (help &&) = delete;

    bool takes_argument () const override { return false; }
    bool add_occurrence () override;
    parser_base * get_parser () override { return nullptr; }
    bool value (std::string const &) override { return false; }

    void show ();

  private:
    using ostream_traits = stream_trait<typename OutputStream::char_type>;

    /// Returns true if the program has any non-positional arguments.
    bool has_switches () const;
    /// Writes the program's usage string to the output stream given to the ctor.
    void usage () const;

    std::string const program_name_;
    std::string const overview_;
    OutputStream & outs_;
  };

  // add occurrence
  // ~~~~~~~~~~~~~~
  template <typename OutputStream>
  bool help<OutputStream>::add_occurrence () {
    this->show ();
    return false;
  }

  // has switches
  // ~~~~~~~~~~~~
  template <typename OutputStream>
  bool help<OutputStream>::has_switches () const {
    return details::has_switches (this, option::all ());
  }

  // usage
  // ~~~~~
  template <typename OutputStream>
  void help<OutputStream>::usage () const {
    outs_ << ostream_traits::out_text ("USAGE: ") << ostream_traits::out_string (program_name_);
    if (this->has_switches ()) {
      outs_ << ostream_traits::out_text (" [options]");
    }
    for (option const * const op : option::all ()) {
      if (op != this && op->is_positional ()) {
        outs_ << ostream_traits::out_text (" ") << ostream_traits::out_string (op->usage ());
      }
    }
    outs_ << '\n';
  }

  // show
  // ~~~~
  template <typename OutputStream>
  void help<OutputStream>::show () {
    static constexpr char const separator[] = " - ";
    static constexpr auto const separator_len = array_elements (separator) - 1U;

    std::size_t const max_width = details::get_max_width ();
    auto const new_line = ostream_traits::out_text ("\n");

    outs_ << ostream_traits::out_text ("OVERVIEW: ") << ostream_traits::out_string (overview_)
          << new_line;
    usage ();

    auto const categories = details::build_categories (this, option::all ());
    std::size_t const max_name_len = widest_option (categories);

    auto const indent = max_name_len + separator_len;
    auto const description_width =
      max_width - max_name_len - separator_len - details::prefix_indent_len;

    for (auto const & cat : categories) {
      outs_ << new_line
            << ostream_traits::out_string (cat.first == nullptr ? "OPTIONS" : cat.first->title ())
            << ostream_traits::out_text (":\n\n");

      for (auto const & sw : details::get_switch_strings (cat.second)) {
        option const * const op = sw.first;
        auto is_first = true;
        auto is_overlong = false;
        for (std::tuple<std::string, std::size_t> const & name : sw.second) {
          if (!is_first) {
            outs_ << new_line;
          }
          outs_ << details::prefix_indent << std::left
                << std::setw (details::int_cast (max_name_len))
                << ostream_traits::out_string (std::get<std::string> (name));

          is_first = false;
          PSTORE_ASSERT (pstore::utf::length (std::get<std::string> (name)) ==
                         std::get<std::size_t> (name));
          is_overlong = std::get<std::size_t> (name) > details::overlong_opt_max;
        }
        outs_ << separator;

        std::string const & description = op->description ();
        is_first = true;
        std::for_each (
          word_wrapper (description, description_width),
          word_wrapper::end (description, description_width), [&] (std::string const & str) {
            if (!is_first || is_overlong) {
              outs_ << new_line
                    << std::setw (details::int_cast (indent + details::prefix_indent_len)) << ' ';
            }
            outs_ << ostream_traits::out_string (str);
            is_first = false;
            is_overlong = false;
          });
        outs_ << new_line;
      }
    }
  }

} // namespace pstore::command_line

#endif // PSTORE_COMMAND_LINE_HELP_HPP
