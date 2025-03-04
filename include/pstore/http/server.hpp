//===- include/pstore/http/server.hpp ---------------------*- mode: C++ -*-===//
//*                                *
//*  ___  ___ _ ____   _____ _ __  *
//* / __|/ _ \ '__\ \ / / _ \ '__| *
//* \__ \  __/ |   \ V /  __/ |    *
//* |___/\___|_|    \_/ \___|_|    *
//*                                *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file server.hpp
/// \brief Top-level entry point for the HTTP server.

#ifndef PSTORE_HTTP_SERVER_HPP
#define PSTORE_HTTP_SERVER_HPP

#include "pstore/http/ws_server.hpp"
#include "pstore/romfs/romfs.hpp"

namespace pstore {
  namespace http {

    class server_status;

    int server (romfs::romfs & file_system, gsl::not_null<server_status *> status,
                channel_container const & channels,
                std::function<void (in_port_t)> notify_listening);

    void quit (in_port_t port_number);

  } // end namespace http
} // end namespace pstore

#endif // PSTORE_HTTP_SERVER_HPP
