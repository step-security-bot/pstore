//*      _          _                         _     _              *
//*  ___| |_ _ __  | |_ ___    _ __ _____   _(_)___(_) ___  _ __   *
//* / __| __| '__| | __/ _ \  | '__/ _ \ \ / / / __| |/ _ \| '_ \  *
//* \__ \ |_| |    | || (_) | | | |  __/\ V /| \__ \ | (_) | | | | *
//* |___/\__|_|     \__\___/  |_|  \___| \_/ |_|___/_|\___/|_| |_| *
//*                                                                *
//===- unittests/pstore_cmd_util/test_str_to_revision.cpp -----------------===//
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

#include "pstore_cmd_util/str_to_revision.h"

#include <utility>
#include "gmock/gmock.h"
#include "pstore/head_revision.hpp"

TEST (StrToRevision, SingleCharacterNumber) {
    EXPECT_THAT (pstore::str_to_revision ("1"), ::testing::Pair (1U, true));
}

TEST (StrToRevision, MultiCharacterNumber) {
    EXPECT_THAT (pstore::str_to_revision ("200000"), ::testing::Pair (200000U, true));
}

TEST (StrToRevision, NumberLeadingWS) {
    EXPECT_THAT (pstore::str_to_revision ("    200000"), ::testing::Pair (200000U, true));
}

TEST (StrToRevision, NumberTrailingWS) {
    EXPECT_THAT (pstore::str_to_revision ("12345   "), ::testing::Pair (12345U, true));
}

TEST (StrToRevision, Empty) {
    EXPECT_THAT (pstore::str_to_revision (""), ::testing::Pair (0U, false));
}

TEST (StrToRevision, JustWhitespace) {
    EXPECT_THAT (pstore::str_to_revision ("  \t"), ::testing::Pair (0U, false));
}

TEST (StrToRevision, Zero) {
    EXPECT_THAT (pstore::str_to_revision ("0"), ::testing::Pair (0U, true));
}

TEST (StrToRevision, HeadLowerCase) {
    EXPECT_THAT (pstore::str_to_revision ("head"),
                 ::testing::Pair (pstore::head_revision, true));
}

TEST (StrToRevision, HeadMixedCase) {
    EXPECT_THAT (pstore::str_to_revision ("HeAd"),
                 ::testing::Pair (pstore::head_revision, true));
}

TEST (StrToRevision, HeadLeadingWhitespace) {
    EXPECT_THAT (pstore::str_to_revision ("  HEAD"),
                 ::testing::Pair (pstore::head_revision, true));
}

TEST (StrToRevision, HeadTraingWhitespace) {
    EXPECT_THAT (pstore::str_to_revision ("HEAD  "),
                 ::testing::Pair (pstore::head_revision, true));
}

TEST (StrToRevision, BadString) {
    EXPECT_THAT (pstore::str_to_revision ("bad"), ::testing::Pair (0U, false));
}

TEST (StrToRevision, NumberFollowedByString) {
    EXPECT_THAT (pstore::str_to_revision ("123Bad"), ::testing::Pair (0U, false));
}

TEST (StrToRevision, PositiveOverflow) {
    std::ostringstream str;
    str << std::numeric_limits<unsigned>::max () + 1ULL;
    EXPECT_THAT (pstore::str_to_revision (str.str ()), ::testing::Pair (0, false));
}

TEST (StrToRevision, Negative) {
    EXPECT_THAT (pstore::str_to_revision ("-2"), ::testing::Pair (0U, false));
}

TEST (StrToRevision, Hex) {
    EXPECT_THAT (pstore::str_to_revision ("0x23"), ::testing::Pair (0U, false));
}

// eof: unittests/pstore_cmd_util/test_str_to_revision.cpp
