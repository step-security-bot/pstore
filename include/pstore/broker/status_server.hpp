//*      _        _                                             *
//*  ___| |_ __ _| |_ _   _ ___   ___  ___ _ ____   _____ _ __  *
//* / __| __/ _` | __| | | / __| / __|/ _ \ '__\ \ / / _ \ '__| *
//* \__ \ || (_| | |_| |_| \__ \ \__ \  __/ |   \ V /  __/ |    *
//* |___/\__\__,_|\__|\__,_|___/ |___/\___|_|    \_/ \___|_|    *
//*                                                             *
//===- include/pstore/broker/status_server.hpp ----------------------------===//
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
#ifndef PSTORE_BROKER_STATUS_SERVER_HPP
#define PSTORE_BROKER_STATUS_SERVER_HPP

#include <condition_variable>
#include <memory>
#include <mutex>
#ifdef _WIN32
using in_port_t = unsigned short; // FIXME: multiple definitions of this type.
#else
#include <netinet/in.h>
#endif

#include "pstore/broker_intf/descriptor.hpp"
#include "pstore/support/maybe.hpp"

namespace pstore {
    namespace broker {

        class self_client_connection {
        public:
            /// Allowed state transitions are:
            /// - initilizing -> closed
            /// - initializing -> listening -> closed
            /// The first of these will happen if the initialization fails for some reason.
            enum class state { initializing, listening, closed };

            self_client_connection () = default;
            self_client_connection (self_client_connection const &) = delete;
            self_client_connection & operator= (self_client_connection const &) = delete;
            self_client_connection & operator= (self_client_connection &&) = delete;

            using get_port_result_type = std::pair<in_port_t, std::unique_lock<std::mutex>>;
            maybe<get_port_result_type> get_port () const;

            void listening (in_port_t port);
            void closed ();

        private:
            mutable std::mutex mut_;
            mutable std::condition_variable cv_;

            state state_ = state::initializing;
            maybe<in_port_t> port_;
        };

        void status_server (std::shared_ptr<self_client_connection> client_ptr);

    } // namespace broker
} // namespace pstore

#endif // PSTORE_BROKER_STATUS_SERVER_HPP
