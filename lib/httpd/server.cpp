//*                                *
//*  ___  ___ _ ____   _____ _ __  *
//* / __|/ _ \ '__\ \ / / _ \ '__| *
//* \__ \  __/ |   \ V /  __/ |    *
//* |___/\___|_|    \_/ \___|_|    *
//*                                *
//===- lib/httpd/server.cpp -----------------------------------------------===//
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
#include "pstore/httpd/server.hpp"

// Standard library includes
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

// OS-specific includes
#ifdef _WIN32

#    include <io.h>
#    include <winsock2.h>
#    include <ws2tcpip.h>

#else

#    include <netdb.h>
#    include <sys/socket.h>
#    include <sys/types.h>

#endif

// Local includes
#include "pstore/broker_intf/descriptor.hpp"
#include "pstore/httpd/buffered_reader.hpp"
#include "pstore/httpd/headers.hpp"
#include "pstore/httpd/net_txrx.hpp"
#include "pstore/httpd/query_to_kvp.hpp"
#include "pstore/httpd/quit.hpp"
#include "pstore/httpd/request.hpp"
#include "pstore/httpd/send.hpp"
#include "pstore/httpd/serve_dynamic_content.hpp"
#include "pstore/httpd/serve_static_content.hpp"
#include "pstore/httpd/ws_server.hpp"
#include "pstore/httpd/wskey.hpp"
#include "pstore/support/logging.hpp"

namespace {

    using socket_descriptor = pstore::broker::socket_descriptor;

    // get_last_error
    // ~~~~~~~~~~~~~~
    inline std::error_code get_last_error () noexcept {
#ifdef _WIN32
        return make_error_code (pstore::win32_erc{static_cast<DWORD> (WSAGetLastError ())});
#else
        return make_error_code (std::errc (errno));
#endif // !_WIN32
    }


    template <typename Sender, typename IO>
    pstore::error_or<IO> cerror (Sender sender, IO io, char const * cause, unsigned error_no,
                                 char const * shortmsg, char const * longmsg) {
        static constexpr auto crlf = pstore::httpd::crlf;
        std::ostringstream os;
        os << "HTTP/1.1 " << error_no << ' ' << shortmsg << crlf << "Content-type: text/html"
           << crlf << crlf;

        os << "<!DOCTYPE html>\n"
              "<html lang=\"en\">"
              "<head>\n"
              "<meta charset=\"utf-8\">\n"
              "<title>pstore-httpd Error</title>\n"
              "</head>\n"
              "<body>\n"
              "<h1>pstore-httpd Web Server Error</h1>\n"
              "<p>"
           << error_no << ": " << shortmsg
           << "</p>"
              "<p>"
           << longmsg << ": " << cause
           << "</p>\n"
              "<hr>\n"
              "<em>The pstore-httpd Web server</em>\n"
              "</body>\n"
              "</html>\n";
        return pstore::httpd::send (sender, io, os);
    }

    // initialize_socket
    // ~~~~~~~~~~~~~~~~~
    pstore::error_or<socket_descriptor> initialize_socket (in_port_t port_number) {
        using eo = pstore::error_or<socket_descriptor>;

        socket_descriptor fd{::socket (AF_INET, SOCK_STREAM, 0)};
        if (!fd.valid ()) {
            return eo{get_last_error ()};
        }

        int const optval = 1;
        if (::setsockopt (fd.get (), SOL_SOCKET, SO_REUSEADDR,
                          reinterpret_cast<char const *> (&optval), sizeof (optval))) {
            return eo{get_last_error ()};
        }

        sockaddr_in server_addr{}; // server's addr.
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = htonl (INADDR_ANY); // NOLINT
        server_addr.sin_port = htons (port_number);       // NOLINT
        if (::bind (fd.get (), reinterpret_cast<sockaddr *> (&server_addr), sizeof (server_addr)) <
            0) {
            return eo{get_last_error ()};
        }

        // Get ready to accept connection requests.
        if (::listen (fd.get (), 5) < 0) { // allow 5 requests to queue up.
            return eo{get_last_error ()};
        }

        return eo{std::move (fd)};
    }


    pstore::error_or<std::string> get_client_name (sockaddr_in const & client_addr) {
        std::array<char, 64> host_name{{'\0'}};
        constexpr std::size_t size = host_name.size ();
#ifdef _WIN32
        using size_type = DWORD;
#else
        using size_type = std::size_t;
#endif //!_WIN32
        int const gni_err = ::getnameinfo (
            reinterpret_cast<sockaddr const *> (&client_addr), sizeof (client_addr),
            host_name.data (), static_cast<size_type> (size), nullptr, socklen_t{0}, 0 /*flags*/);
        if (gni_err != 0) {
            return pstore::error_or<std::string>{get_last_error ()};
        }
        host_name.back () = '\0'; // guarantee nul termination.
        return pstore::error_or<std::string>{std::string{host_name.data ()}};
    }


    // Here we bridge from the std::error_code world to HTTP status codes.
    void report_error (std::error_code error, pstore::httpd::request_info const & request,
                       socket_descriptor & socket) {
        auto report = [&error, &request, &socket](unsigned code, char const * message) {
            cerror (pstore::httpd::net::network_sender, std::ref (socket), request.uri ().c_str (),
                    code, message, error.message ().c_str ());
        };

        log (pstore::logging::priority::error, "Error:", error.message ());
        if (error == pstore::httpd::error_code::bad_request) {
            report (400, "Bad request");
        } else if (error == pstore::romfs::error_code::enoent ||
                   error == pstore::romfs::error_code::enotdir) {
            report (404, "Not found");
        } else {
            report (501, "Server internal error");
        }
    }

    template <typename Reader, typename IO>
    pstore::error_or<std::unique_ptr<std::thread>>
    upgrade_to_ws (Reader & reader, IO io, pstore::httpd::header_info const & header_contents) {
        using return_type = pstore::error_or<std::unique_ptr<std::thread>>;
        using pstore::logging::priority;
        assert (header_contents.connection_upgrade && header_contents.upgrade_to_websocket);

        log (priority::info, "WebSocket upgrade requested");

        // Validate the request headers
        if (!header_contents.websocket_key || !header_contents.websocket_version) {
            log (priority::error, "Missing WebSockets upgrade key or version header.");
            return return_type{pstore::httpd::error_code::bad_request};
        }

        if (*header_contents.websocket_version != 13) {
            // send back a "Sec-WebSocket-Version: 13" header along with a 400 error.
        }

        // Send back the server handshake response.
        auto const accept_ws_connection = [&header_contents, &io]() {
            log (priority::info, "Accepting WebSockets upgrade");

            static constexpr auto crlf = pstore::httpd::crlf;
            std::ostringstream os;
            os << "HTTP/1.1 101 Switching Protocols" << crlf << "Upgrade: websocket" << crlf
               << "Connection: upgrade" << crlf << "Sec-WebSocket-Accept: "
               << pstore::httpd::source_key (*header_contents.websocket_key) << crlf << crlf;
            // Here I assume that the send() IO param is the same as the Reader's IO parameter.
            return pstore::httpd::send (pstore::httpd::net::network_sender, std::ref (io), os);
        };

        auto server_loop_thread = [](Reader reader2, socket_descriptor io2) {
            PSTORE_TRY {
                constexpr auto ident = "websocket";
                pstore::threads::set_name (ident);
                pstore::logging::create_log_stream (ident);

                log (priority::info, "Started WebSockets session");

                assert (io2.valid ());
                ws_server_loop (reader2, pstore::httpd::net::network_sender, std::ref (io2));

                log (priority::info, "Ended WebSockets session");
            }
            PSTORE_CATCH (std::exception const & ex,
                          { log (priority::error, "Error: ", ex.what ()); })
            PSTORE_CATCH (..., { log (priority::error, "Unknown exception"); })
        };

        // Spawn a thread to manage this WebSockets session.
        auto const create_ws_server = [&reader, server_loop_thread](socket_descriptor & s) {
            assert (s.valid ());
            return return_type{
                pstore::in_place,
                new std::thread (server_loop_thread, std::move (reader), std::move (s))};
        };

        assert (io.get ().valid ());
        return accept_ws_connection () >>= create_ws_server;
    }

} // end anonymous namespace

namespace pstore {
    namespace httpd {

        int server (in_port_t port_number, romfs::romfs & file_system) {
            log (logging::priority::info, "initializing");
            pstore::error_or<socket_descriptor> eparentfd = initialize_socket (port_number);
            if (!eparentfd) {
                log (logging::priority::error, "opening socket", eparentfd.get_error ().message ());
                return 0;
            }
            socket_descriptor const & parentfd = eparentfd.get ();

            // main loop: wait for a connection request, parse HTTP,
            // serve requested content, close connection.
            sockaddr_in client_addr{}; // client address.
            auto clientlen =
                static_cast<socklen_t> (sizeof (client_addr)); // byte size of client's address
            log (logging::priority::info, "starting server-loop");

            std::vector<std::unique_ptr<std::thread>> websockets_workers;

            server_state state{};
            while (!state.done) {
                // Wait for a connection request.
                socket_descriptor childfd{
                    ::accept (parentfd.get (), reinterpret_cast<struct sockaddr *> (&client_addr),
                              &clientlen)};
                if (!childfd.valid ()) {
                    log (logging::priority::error, "accept", get_last_error ().message ());
                    continue;
                }

                // Determine who sent the message.
                pstore::error_or<std::string> ename = get_client_name (client_addr);
                if (!ename) {
                    log (logging::priority::error, "getnameinfo", ename.get_error ().message ());
                    continue;
                }
                log (logging::priority::info, "Connection from ", ename.get ());

                // Get the HTTP request line.
                auto reader = make_buffered_reader<socket_descriptor &> (net::refiller);

                pstore::error_or<std::pair<socket_descriptor &, request_info>> eri =
                    read_request (reader, childfd);
                if (!eri) {
                    log (logging::priority::error, "reading HTTP request",
                         eri.get_error ().message ());
                    continue;
                }

                request_info const & request = std::get<1> (eri.get ());
                log (logging::priority::info, "Request: ",
                     request.method () + ' ' + request.version () + ' ' + request.uri ());

                // We only currently support the GET method.
                if (request.method () != "GET") {
                    cerror (pstore::httpd::net::network_sender, std::ref (childfd),
                            request.method ().c_str (), 501, "Not Implemented",
                            "httpd does not implement this method");
                    continue;
                }

                // Respond appropriately based on the request and headers.
                auto const serve_reply =
                    [&](socket_descriptor & io2, header_info const & header_contents) {
                        if (header_contents.connection_upgrade &&
                            header_contents.upgrade_to_websocket) {
                            return upgrade_to_ws (reader, std::ref (childfd), header_contents) >>=
                                   [&](std::unique_ptr<std::thread> & worker) {
                                       websockets_workers.emplace_back (std::move (worker));
                                       return error_or<server_state> (state);
                                   };
                        }

                        if (!details::starts_with (request.uri (), dynamic_path)) {
                            return serve_static_content (pstore::httpd::net::network_sender,
                                                         std::ref (io2), request.uri (),
                                                         file_system) >>=
                                   [&state](socket_descriptor &) {
                                       return error_or<server_state> (state);
                                   };
                        }

                        return serve_dynamic_content (pstore::httpd::net::network_sender,
                                                      std::ref (io2), request.uri (), state) >>=
                               [&state](std::pair<socket_descriptor &, server_state> const & p) {
                                   return error_or<server_state> (std::get<1> (p));
                               };
                    };

                // Scan the HTTP headers.
                error_or<server_state> const eo = read_headers (
                    reader, childfd,
                    [](header_info io, std::string const & key, std::string const & value) {
                        return io.handler (key, value);
                    },
                    header_info ()) >>= serve_reply;

                if (eo) {
                    state = *eo;
                } else {
                    // Report the error to the user as an HTTP error.
                    report_error (eo.get_error (), request, childfd);
                }
            }

            for (std::unique_ptr<std::thread> const & worker : websockets_workers) {
                worker->join ();
            }

            return 0;
        }

    } // end namespace httpd
} // end namespace pstore
