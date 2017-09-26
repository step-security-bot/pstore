//*  _ _                                _ _       _                *
//* | | |_   ___ __ ___    _____      _(_) |_ ___| |__   ___  ___  *
//* | | \ \ / / '_ ` _ \  / __\ \ /\ / / | __/ __| '_ \ / _ \/ __| *
//* | | |\ V /| | | | | | \__ \\ V  V /| | || (__| | | |  __/\__ \ *
//* |_|_| \_/ |_| |_| |_| |___/ \_/\_/ |_|\__\___|_| |_|\___||___/ *
//*                                                                *
//===- tools/read/llvm_switches.cpp ---------------------------------------===//
// Copyright (c) 2017 by Sony Interactive Entertainment, Inc. 
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


#include "switches.hpp"

#if PSTORE_IS_INSIDE_LLVM

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"

#include "pstore_support/error.hpp"
#include "pstore_cmd_util/str_to_revision.h"
#include "pstore_support/utf.hpp"

namespace {

    using namespace llvm;

	// FIXME: Need to remove RevisionOpt since the same code is in pstore-dump!
    // based on DiagnosticInfo.cpp/PassRemarksOpt.
    struct RevisionOpt {
        unsigned r = pstore::head_revision;

        RevisionOpt & operator= (std::string const & Val) {
            if (!Val.empty ()) {
                auto rp = pstore::str_to_revision (Val);
                r = rp.first;
                if (!rp.second) {
                    report_fatal_error ("error: revision must be a revision number or 'HEAD'\n",
                                        false);
                }
            }
            return *this;
        }
    };

    cl::opt<RevisionOpt, false, cl::parser<std::string>>
        Revision ("revision", cl::desc ("The starting revision number (or 'HEAD')"));

    cl::opt<std::string> DbPath (cl::Positional,
                                 cl::desc ("Path of the pstore repository to be read."),
                                 cl::Required);
    cl::opt<std::string> Key (cl::Positional,
                              cl::desc ("Reads the value associated with the key in the index."),
                              cl::Required);
    cl::opt<bool>
        StringMode ("strings", cl::init (false),
                    cl::desc ("Reads from the 'strings' index rather than the 'names' index."));


} // end anonymous namespace


std::pair<switches, int> get_switches (int argc, char * argv[]) {
    llvm::cl::ParseCommandLineOptions (argc, argv, "pstore write utility\n");

    switches result;
    result.revision = static_cast<RevisionOpt> (Revision).r;
    result.db_path = pstore::utf::from_native_string (DbPath);
    result.key = pstore::utf::from_native_string (Key);
    result.string_mode = StringMode;

    return {result, EXIT_SUCCESS};
}
#endif // PSTORE_IS_INSIDE_LLVM
// eof: tools/read/llvm_switches.cpp
