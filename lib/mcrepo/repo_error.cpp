//*                                                   *
//*  _ __ ___ _ __   ___     ___ _ __ _ __ ___  _ __  *
//* | '__/ _ \ '_ \ / _ \   / _ \ '__| '__/ _ \| '__| *
//* | | |  __/ |_) | (_) | |  __/ |  | | | (_) | |    *
//* |_|  \___| .__/ \___/   \___|_|  |_|  \___/|_|    *
//*          |_|                                      *
//===- lib/mcrepo/repo_error.cpp ------------------------------------------===//
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
#include "pstore/mcrepo/repo_error.hpp"

namespace pstore {
    namespace repo {

        char const * error_category::name () const noexcept { return "pstore_mcrepo category"; }

        std::string error_category::message (int error) const {
            switch (static_cast<error_code> (error)) {
            case error_code::bad_fragment_record: return "bad fragment record";
            case error_code::bad_fragment_type: return "bad fragment type";
            case error_code::bad_compilation_record: return "bad compilation record";
            }
            return "unknown error";
        }

    } // namespace repo
} // namespace pstore

namespace {

    std::error_category const & get_error_category () {
        static pstore::repo::error_category const cat;
        return cat;
    }

} // namespace

namespace std {

    std::error_code make_error_code (pstore::repo::error_code e) {
        static_assert (std::is_same<std::underlying_type<decltype (e)>::type, int>::value,
                       "base type of error_code must be int to permit safe static cast");
        return {static_cast<int> (e), get_error_category ()};
    }

} // namespace std

