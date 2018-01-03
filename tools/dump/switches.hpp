//*               _ _       _                *
//*  _____      _(_) |_ ___| |__   ___  ___  *
//* / __\ \ /\ / / | __/ __| '_ \ / _ \/ __| *
//* \__ \\ V  V /| | || (__| | | |  __/\__ \ *
//* |___/ \_/\_/ |_|\__\___|_| |_|\___||___/ *
//*                                          *
//===- tools/dump/switches.hpp --------------------------------------------===//
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
#ifndef PSTORE_DUMP_SWITCHES_HPP
#define PSTORE_DUMP_SWITCHES_HPP 1

#include <list>
#include <string>
#include <utility>

#ifdef _WIN32
#include <tchar.h>
#define NATIVE_TEXT(str) _TEXT (str)
#else
typedef char TCHAR;
#define NATIVE_TEXT(str) str
#endif

#include "pstore/database.hpp"
#include "dump_config.hpp"

struct switches {
    bool show_all = false;
    bool show_contents = false;
    std::list<std::string> fragments;
    bool show_all_fragments = false;
    bool show_tickets = false;
    bool show_header = false;
    bool show_indices = false;
    bool show_log = false;
    bool show_shared = false;

    unsigned revision = pstore::head_revision;

    bool hex = false;
    bool expanded_addresses = false;
    bool no_times = false;

    std::list<std::string> paths;
};

#if defined(_WIN32) && !defined(PSTORE_IS_INSIDE_LLVM)
std::pair<switches, int> get_switches (int argc, TCHAR * argv[]);
#else
std::pair<switches, int> get_switches (int argc, char * argv[]);
#endif

#endif // PSTORE_DUMP_SWITCHES_HPP
// eof: tools/dump/switches.hpp
