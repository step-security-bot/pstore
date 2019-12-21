//*              *
//*   __ _  ___  *
//*  / _` |/ __| *
//* | (_| | (__  *
//*  \__, |\___| *
//*  |___/       *
//===- unittests/broker/test_gc.cpp ---------------------------------------===//
// Copyright (c) 2017-2019 by Sony Interactive Entertainment, Inc.
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
#include "pstore/broker/gc.hpp"

#include <cstring>
#include <initializer_list>
#include <thread>
#include <vector>

#include <gmock/gmock.h>

#include "pstore/support/error.hpp"

using testing::_;
using testing::ElementsAre;
using testing::Eq;
using testing::Expectation;
using testing::Return;
using testing::StrEq;

namespace {

    using czstring = pstore::gsl::czstring;
    using process_identifier = pstore::broker::process_identifier;

    class test_watch_thead : public pstore::broker::gc_watch_thread {
    public:
        test_watch_thead () = default;
        // No copying or assignment.
        test_watch_thead (test_watch_thead const &) = delete;
        test_watch_thead (test_watch_thead &&) = delete;
        test_watch_thead & operator= (test_watch_thead const &) = delete;
        test_watch_thead & operator= (test_watch_thead &&) = delete;

        MOCK_METHOD1 (spawn, process_identifier (std::initializer_list<czstring>));
        MOCK_METHOD1 (kill, void (process_identifier const &));
    };

    class Gc : public ::testing::Test {
    protected:
        static std::string const vacuum_exe;

        using spawn_params = std::tuple<std::string, process_identifier>;

        /// Creates a platform-specific fake process identifier.
        /// \param index  A value used to make unique process IDs.
        static process_identifier make_process_id (int index = 0);

        /// Creates expectations of calls on the test_watch_thread mock that a GC process for the
        /// file at \p path will be spawned and later killed.
        ///
        /// \param gc  A GC watch-thread instance.
        /// \param path The (fake) path of the process executable being created.
        /// \param pid  The pid used to identify the (fake) process.
        static void expect_call (test_watch_thead & gc, std::string const & path,
                                 process_identifier pid);
        /// Creates expectations of calls on the test_watch_thread mock that a GC process for the
        /// file at \p path will be spawned and later killed.
        ///
        /// \param gc  A GC watch-thread instance.
        /// \param params A tuple containing both the path of the file to be garbage-collected and
        /// the fake process ID.
        static void expect_call (test_watch_thead & gc, spawn_params const & params);

        /// Creates a series of \p num expectations that multiple GC requests will be performed.
        /// Each will spawn a GC process for it to be later killed when the gc-watcher thread exits.
        ///
        /// \param gc  A GC watch-thread instance.
        /// \param num The number of processes that will be created.
        static auto expect_spawn_calls (test_watch_thead & gc, unsigned num)
            -> std::vector<spawn_params>;

        static spawn_params call_params (int count);
    };

    std::string const Gc::vacuum_exe = test_watch_thead::vacuumd_path ();

    // make_process_id
    // ~~~~~~~~~~~~~~~
    process_identifier Gc::make_process_id (int index) {
        int const id = 7919 + index; // No significance to this number: it's just the 1000th prime
#ifdef _WIN32
        HANDLE event = ::CreateEventW (nullptr, // lpEventAttributes,
                                       false,   // bManualReset,
                                       false,   // bInitialState,
                                       nullptr  // lpName
        );
        if (event == nullptr) {
            raise (pstore::win32_erc{::GetLastError ()}, "CreateEventW");
        }
        return std::make_shared<pstore::broker::win32::process_pair> (event, id);
#else
        return id;
#endif // _WIN32
    }

    // call_params
    // ~~~~~~~~~~~
    auto Gc::call_params (int count) -> spawn_params {
        return spawn_params{"path" + std::to_string (count), make_process_id (count)};
    }

    // expect_call
    // ~~~~~~~~~~~
    void Gc::expect_call (test_watch_thead & gc, std::string const & path, process_identifier pid) {
        Expectation const exp =
            EXPECT_CALL (gc,
                         spawn (ElementsAre (StrEq (vacuum_exe.c_str ()), StrEq (path), nullptr)))
                .WillOnce (Return (pid));
        EXPECT_CALL (gc, kill (Eq (pid))).Times (1).After (exp);
    }

    void Gc::expect_call (test_watch_thead & gc, spawn_params const & params) {
        expect_call (gc, std::get<std::string> (params), std::get<process_identifier> (params));
    }

    // expect_spawn_calls
    // ~~~~~~~~~~~~~~~~~~
    auto Gc::expect_spawn_calls (test_watch_thead & gc, unsigned num) -> std::vector<spawn_params> {
        std::vector<spawn_params> calls;
        calls.reserve (num);

        assert (num <= static_cast<unsigned> (std::numeric_limits<int>::max ()));
        for (auto count = 0; count < static_cast<int> (num); ++count) {
            calls.push_back (call_params (static_cast<int> (count)));
            expect_call (gc, calls.back ());
        }

        return calls;
    }

} // end anonymous namespace

TEST_F (Gc, Nothing) {
    test_watch_thead gc;
    EXPECT_CALL (gc, spawn (_)).Times (0);
    EXPECT_CALL (gc, kill (_)).Times (0);

    std::thread thread{[&gc] () { gc.watcher (); }};
    gc.stop ();
    thread.join ();
}

TEST_F (Gc, SpawnOne) {
    constexpr auto path = "db-path";

    test_watch_thead gc;
    expect_call (gc, path, make_process_id ());

    std::thread thread{[&gc] () { gc.watcher (); }};
    // Initiate garbage collection of the pstore file at path.
    gc.start_vacuum (path);
    // Our simulation never indicates that the GC process has exited. Therefore a second GC
    // request should be ignored.
    gc.start_vacuum (path);

    gc.stop ();
    thread.join ();
}

TEST_F (Gc, SpawnTwo) {
    auto call0 = call_params (0);
    auto call1 = call_params (1);

    test_watch_thead gc;
    expect_call (gc, call0);
    expect_call (gc, call1);

    std::thread thread{[&gc] () { gc.watcher (); }};

    gc.start_vacuum (std::get<std::string> (call0));
    gc.start_vacuum (std::get<std::string> (call1));
    gc.start_vacuum (std::get<std::string> (call0));
    gc.start_vacuum (std::get<std::string> (call1));

    gc.stop ();
    thread.join ();
}

TEST_F (Gc, SpawnMax) {
    test_watch_thead gc;

    auto const sp = expect_spawn_calls (gc, pstore::broker::gc_watch_thread::max_gc_processes);
    std::thread thread{[&gc] () { gc.watcher (); }};
    for (auto const & c : sp) {
        gc.start_vacuum (std::get<std::string> (c));
    }
    gc.stop ();
    thread.join ();
}

TEST_F (Gc, SpawnMaxPlus1) {
    test_watch_thead gc;

    auto const sp = expect_spawn_calls (gc, pstore::broker::gc_watch_thread::max_gc_processes);
    std::thread thread{[&gc] () { gc.watcher (); }};
    for (auto const & c : sp) {
        gc.start_vacuum (std::get<std::string> (c));
    }
    gc.start_vacuum ("one-extra-call");
    gc.stop ();
    thread.join ();
}
