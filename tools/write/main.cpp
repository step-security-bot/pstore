//*                  _        *
//*  _ __ ___   __ _(_)_ __   *
//* | '_ ` _ \ / _` | | '_ \  *
//* | | | | | | (_| | | | | | *
//* |_| |_| |_|\__,_|_|_| |_| *
//*                           *
//===- tools/write/main.cpp -----------------------------------------------===//
// Copyright (c) 2017-2018 by Sony Interactive Entertainment, Inc.
// All rights reserved.
//
// Developed by:
//   Toolchain Team
//   SN Systems, Ltd.
//   www.snsystems.com
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal with the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// - Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimers.
//
// - Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimers in the
//   documentation and/or other materials provided with the distribution.
//
// - Neither the names of SN Systems Ltd., Sony Interactive Entertainment,
//   Inc. nor the names of its contributors may be used to endorse or
//   promote products derived from this Software without specific prior
//   written permission.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR
// ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE SOFTWARE.
//===----------------------------------------------------------------------===//

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>

#ifdef _WIN32
#include <tchar.h>
#else
#include <unistd.h>

// On Windows, the TCHAR type may be either char or whar_t depending on the selected
// Unicode mode. Everywhere else, I need to add this type for compatibility.
using TCHAR = char;
#endif

// pstore includes.
#include "pstore/db_archive.hpp"
#include "pstore/hamt_map.hpp"
#include "pstore/hamt_set.hpp"
#include "pstore/index_types.hpp"
#include "pstore/serialize/standard_types.hpp"
#include "pstore/sstring_view_archive.hpp"
#include "pstore/transaction.hpp"
#include "pstore_support/error.hpp"
#include "pstore_support/utf.hpp" // for UTF-8 to UTF-16 conversion on Windows.

// Local includes
#include "switches.hpp"

namespace {

#if defined(_WIN32) && defined(_UNICODE)
    auto & error_stream = std::wcerr;
#else
    auto & error_stream = std::cerr;
#endif

    bool add_file (pstore::transaction<pstore::transaction_lock> & transaction,
                   pstore::index::write_index & names, std::string const & key,
                   std::string const & path) {

        using namespace pstore::file;
        bool ok = true;
        pstore::file::file_handle file;
        file.open (path, pstore::file::file_handle::create_mode::open_existing,
                   pstore::file::file_handle::writable_mode::read_only);
        if (!file.is_open ()) {
            ok = false;
        } else {
            auto const size = file.size ();

            // Allocate space in the transaction for 'size' bytes.
            auto addr = pstore::address::null ();
            std::shared_ptr<std::uint8_t> ptr;
            std::tie (ptr, addr) = transaction.alloc_rw<std::uint8_t> (size);

            // Copy from the source file to the data store. The destination for the read_span() is
            // the memory that we just allocated in the data store.
            auto span = ::pstore::gsl::make_span (ptr.get (), static_cast<std::ptrdiff_t> (size));
            std::size_t bytes_read = file.read_span (span);

            auto const expected_size = span.size_bytes ();
            assert (expected_size >= 0);
            if (bytes_read !=
                static_cast<std::make_unsigned<decltype (expected_size)>::type> (expected_size)) {
                error_stream << NATIVE_TEXT ("Did not read the number of bytes requested");
                std::exit (EXIT_FAILURE);
            }

            // Add it to the names index.
            names.insert_or_assign (transaction, key, pstore::extent{addr, size});
        }

        return ok;
    }

    template <typename Transaction>
    auto append_string (Transaction & transaction, std::string const & v) -> pstore::extent {
        // Since the read utility prefers to get raw string value in the system tests, this function
        // is changed to store raw string into the store instead of using serialize write.

        auto const size = v.size ();
        using element_type = typename std::string::value_type;

        // Allocate space in the transaction for the value block
        auto addr = pstore::address::null ();
        std::shared_ptr<element_type> ptr;
        std::tie (ptr, addr) = transaction.template alloc_rw<element_type> (size);

        // Copy the string to the store.
        std::copy (std::begin (v), std::end (v), ptr.get ());

        return {addr, size};
    }
} // namespace


#if PSTORE_CPP_EXCEPTIONS
#define TRY try
#define CATCH(ex, code) catch (ex) code
#else
#define TRY
#define CATCH(ex, code)
#endif

#if defined(_WIN32) && !defined(PSTORE_IS_INSIDE_LLVM)
int _tmain (int argc, TCHAR * argv[]) {
#else
int main (int argc, char * argv[]) {
#endif
    int exit_code = EXIT_SUCCESS;

    using pstore::utf::to_native_string;

    TRY {
        switches opt;
        std::tie (opt, exit_code) = get_switches (argc, argv);
        if (exit_code != EXIT_SUCCESS) {
            return exit_code;
        }

        pstore::database database (opt.db_path, pstore::database::access_mode::writable);
        database.set_vacuum_mode (opt.vmode);

        {
            // Start a transaction...
            auto transaction = pstore::begin (database);

            // Read the write and name indexes.
            pstore::index::name_index * const name = pstore::index::get_name_index (database);
            pstore::index::write_index * const write = pstore::index::get_write_index (database);

            // Scan through the string value arguments from the command line. These are of
            // the form key,value where value is a string which is stored directly.
            for (std::pair<std::string, std::string> const & v : opt.add) {
                write->insert_or_assign (transaction, v.first,
                                         append_string (transaction, v.second));
            }

            // Now record the files requested on the command line.
            for (std::pair<std::string, std::string> const & v : opt.files) {
                if (!add_file (transaction, *write, v.first, v.second)) {
                    error_stream << to_native_string (v.second)
                                 << NATIVE_TEXT (": No such file or directory\n");
                    exit_code = EXIT_FAILURE;
                }
            }

            // Scan through the string arguments from the command line.
            for (std::string const & v : opt.strings) {
                name->insert (transaction, pstore::make_sstring_view (v.data (), v.length ()));
            }

            transaction.commit ();
        }

        database.close ();
    }
    CATCH (std::exception const & ex,
           {
               auto what = ex.what ();
               error_stream << NATIVE_TEXT ("An error occurred: ") << to_native_string (what)
                            << std::endl;
               exit_code = EXIT_FAILURE;
           })
    CATCH (...,
           {
               std::cerr << "An unknown error occurred." << std::endl;
               exit_code = EXIT_FAILURE;
           })

        return exit_code;
}
// eof: tools/write/main.cpp
