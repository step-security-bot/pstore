//===- tools/genromfs/copy.cpp --------------------------------------------===//
//*                         *
//*   ___ ___  _ __  _   _  *
//*  / __/ _ \| '_ \| | | | *
//* | (_| (_) | |_) | |_| | *
//*  \___\___/| .__/ \__, | *
//*           |_|    |___/  *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "copy.hpp"

// Standard Library includes
#include <array>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>

// pstore includes
#include "pstore/support/array_elements.hpp"
#include "pstore/support/error.hpp"
#include "pstore/support/portab.hpp"
#include "pstore/support/quoted.hpp"
#include "pstore/support/utf.hpp"

// Local includes
#include "indent.hpp"
#include "vars.hpp"

namespace {


  FILE * file_open (std::string const & filename) {
#ifdef _WIN32
    return _wfopen (pstore::utf::win32::to16 (filename).c_str (), L"r");
#else
    return std::fopen (filename.c_str (), "r");
#endif
  }

  void file_close (FILE * f) {
    if (f != nullptr) {
      std::fclose (f);
    }
  }

  PSTORE_NO_RETURN void open_failed (int erc, std::string const & path) {
    std::stringstream str;
    str << "fopen " << pstore::quoted (path);
    raise (pstore::errno_erc{erc}, str.str ());
  }

  class snprintf_failed_error : public std::runtime_error {
  public:
    snprintf_failed_error ()
            : std::runtime_error ("snprintf failed") {}
  };

  class read_failed_error : public std::runtime_error {
  public:
    explicit read_failed_error (std::string const & message)
            : std::runtime_error (message) {}
  };

  PSTORE_NO_RETURN void read_failed (std::string const & path) {
    std::stringstream str;
    str << "read of file " << pstore::quoted (path) << " failed";
    pstore::raise_exception (read_failed_error{str.str ()});
  }

} // end anonymous namespace

void copy (std::string const & path, unsigned file_no) {
  static constexpr auto indent_size = pstore::array_elements (indent) - 1U;
  static constexpr auto crindent_size = pstore::array_elements (crindent) - 1U;
  static constexpr auto line_width = std::size_t{80} - indent_size;
  static constexpr auto separator_size = std::size_t{1};  // empty or comma
  static constexpr auto byte_value_size = std::size_t{3}; // base10: 0-255.

  std::ostream & os = std::cout;

  auto getcr = [] (std::size_t width) {
    return width >= line_width ? std::make_pair (std::size_t{0}, crindent)
                               : std::make_pair (width, "");
  };

  constexpr auto buffer_size = std::size_t{1024};
  std::uint8_t buffer[buffer_size] = {0};
  std::unique_ptr<FILE, decltype (&file_close)> file (file_open (path), &file_close);
  if (!file) {
    open_failed (errno, path);
  }
  os << "std::uint8_t const " << file_var (file_no) << "[] = {\n" << indent;
  std::size_t width = indent_size;
  char const * separator = "";
  auto num_read = std::size_t{0};
  do {
    num_read = std::fread (&buffer[0], sizeof (buffer[0]), buffer_size, file.get ());
    num_read = std::min (buffer_size, num_read);
    if (std::ferror (file.get ())) {
      read_failed (path);
    }
    for (auto n = std::size_t{0}; n < num_read; ++n) {
      char const * cr;
      std::tie (width, cr) = getcr (width);

      PSTORE_ASSERT (std::strlen (separator) <= separator_size);
      std::array<char, separator_size + crindent_size + byte_value_size + 1> vbuf{{0}};
      int written = std::snprintf (vbuf.data (), vbuf.size (), "%s%s%u", separator, cr,
                                   static_cast<unsigned> (buffer[n]));
      if (written < 0) {
        // Is there anything more sensible we can do?
        pstore::raise_exception (snprintf_failed_error ());
      }
      PSTORE_ASSERT (vbuf[vbuf.size () - 1U] == '\0' && "vbuf was not big enough");
      PSTORE_ASSERT (static_cast<unsigned> (written) <= vbuf.size ());
      // Absolute guarantee of nul termination.
      vbuf[vbuf.size () - 1U] = '\0';
      os << vbuf.data ();
      width += static_cast<std::make_unsigned<decltype (written)>::type> (written);
      separator = ",";
    }
  } while (num_read >= buffer_size);
  os << "\n};\n";
}
