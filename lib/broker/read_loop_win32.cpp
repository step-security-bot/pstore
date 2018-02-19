//*                     _   _                    *
//*  _ __ ___  __ _  __| | | | ___   ___  _ __   *
//* | '__/ _ \/ _` |/ _` | | |/ _ \ / _ \| '_ \  *
//* | | |  __/ (_| | (_| | | | (_) | (_) | |_) | *
//* |_|  \___|\__,_|\__,_| |_|\___/ \___/| .__/  *
//*                                      |_|     *
//===- lib/broker/read_loop_win32.cpp -------------------------------------===//
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
/// \file read_loop_win32.cpp
/// \brief The read loop thread entry point for Windows.

#include "broker/read_loop.hpp"

#ifdef _WIN32

// Standard includes
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <utility>

// Platform includes
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// pstore includes
#include "pstore_broker_intf/fifo_path.hpp"
#include "pstore_broker_intf/message_type.hpp"
#include "pstore_broker_intf/unique_handle.hpp"
#include "pstore_support/gsl.hpp"
#include "pstore_support/logging.hpp"
#include "pstore_support/utf.hpp"

// Local includes
#include "broker/command.hpp"
#include "broker/globals.hpp"
#include "broker/intrusive_list.hpp"
#include "broker/message_pool.hpp"
#include "broker/quit.hpp"
#include "broker/recorder.hpp"

namespace {
    using namespace pstore::broker;
    using namespace pstore::gsl;
    using namespace pstore::logging;

    //*                  _          *
    //*  _ _ ___ __ _ __| |___ _ _  *
    //* | '_/ -_) _` / _` / -_) '_| *
    //* |_| \___\__,_\__,_\___|_|   *
    //*                             *
    class reader {
    public:
        reader () = default;
        reader (unique_handle && ph, not_null<command_processor *> cp, recorder * record_file);

        ~reader () noexcept;

        reader * initiate_read ();
        void cancel ();

        list_member<reader> & get_list_member () { return listm_; }

    private:
        /// Must be the first object in the structure. The address of this member is passed to the
        /// read_completed() function when the OS notifies us of the completion of a read.
        OVERLAPPED overlap_ = {0};

        list_member<reader> listm_;

        unique_handle pipe_handle_;
        message_ptr request_;

        command_processor * command_processor_ = nullptr;
        recorder * record_file_ = nullptr;

        /// Is a read using this buffer in progress? Used as a debugging check to ensure that
        /// the object is not "active" when it is being destroyed.
        bool is_in_flight_ = false;

        bool read ();
        static VOID WINAPI read_completed (DWORD errcode, DWORD bytes_read, LPOVERLAPPED overlap);

        void completed ();
        void completed_with_error ();

        /// Called when the series of reads for a connection has been completed. This function
        /// removes the reader from the list of in-flight reads and deletes the associated object.
        ///
        /// \param r  The reader instance to be removed and deleted.
        /// \returns Always nullptr
        static reader * done (reader * r) noexcept;
    };


    // (ctor)
    // ~~~~~~
    reader::reader (unique_handle && ph, not_null<command_processor *> cp, recorder * record_file)
            : pipe_handle_{std::move (ph)}
            , command_processor_{cp}
            , record_file_{record_file} {
        using T = std::remove_pointer<decltype (this)>::type;
        static_assert (offsetof (T, overlap_) == 0,
                       "OVERLAPPED must be the first member of the request structure");
        assert (pipe_handle_.valid ());
    }

    // (dtor)
    // ~~~~~~
    reader::~reader () noexcept {
        assert (!is_in_flight_);
        // Close this pipe instance.
        if (pipe_handle_.valid ()) {
            ::DisconnectNamedPipe (pipe_handle_.get ());
        }
    }

    // done
    // ~~~~
    reader * reader::done (reader * r) noexcept {
        assert (!r->is_in_flight_);
        intrusive_list<reader>::erase (r);
        delete r;
        return nullptr;
    }

    // initiate_read
    // ~~~~~~~~~~~~~
    reader * reader::initiate_read () {
        // Start an asynchronous read. This will either start to read data if it's available
        // or signal (by returning nullptr) that we've read all of the data already. In the latter
        // case we tear down this reader instance by calling done().
        return this->read () ? this : done (this);
    }

    // cancel
    // ~~~~~~
    void reader::cancel () { ::CancelIoEx (pipe_handle_.get (), &overlap_); }

    // read
    // ~~~~
    /// \return True if the pipe read does not return an error. If false is returned, the client has
    /// gone away and this pipe instance should be closed.
    bool reader::read () {
        assert (!is_in_flight_);

        // Pull a buffer from pool to use for storing the data read from the pipe connection.
        assert (request_.get () == nullptr);
        request_ = pool.get_from_pool ();

        // Start the read and call read_completed() when it finishes.
        assert (sizeof (*request_) <= std::numeric_limits<DWORD>::max ());
        is_in_flight_ = ::ReadFileEx (pipe_handle_.get (), request_.get (),
                                      static_cast<DWORD> (sizeof (*request_)), &overlap_,
                                      &read_completed) != FALSE;
        return is_in_flight_;
    }

    // read_completed
    // ~~~~~~~~~~~~~~
    /// An I/O completion routine that's called after a read request completes.
    /// If we've received a complete message, then it is queued for processing. We then try to read
    /// more from the client.
    ///
    /// \param errcode  The I/O completion status: one of the system error codes.
    /// \param bytes_read  This number of bytes transferred.
    /// \param overlap  A pointer to the OVERLAPPED structure specified by the asynchronous I/O
    /// function.
    VOID WINAPI reader::read_completed (DWORD errcode, DWORD bytes_read, LPOVERLAPPED overlap) {
        auto * r = reinterpret_cast<reader *> (overlap);
        assert (r != nullptr);

        try {
            r->is_in_flight_ = false;

            if (errcode == ERROR_SUCCESS && bytes_read != 0) {
                if (bytes_read != message_size) {
                    log (priority::error, "Partial message received. Length ", bytes_read);
                    r->completed_with_error ();
                } else {
                    // The read operation has finished successfully, so process the request.
                    r->completed ();
                }
            } else {
                log (priority::error, "error received ", errcode);
                r->completed_with_error ();
            }

            // Try reading some more from this pipe client.
            r = r->initiate_read ();
        } catch (std::exception const & ex) {
            log (priority::error, "error: ", ex.what ());
            // This object should now kill itself?
        } catch (...) {
            log (priority::error, "unknown error");
            // This object should now kill itself?
        }
    }

    // completed
    // ~~~~~~~~~
    void reader::completed () {
        assert (command_processor_ != nullptr);
        command_processor_->push_command (std::move (request_), record_file_);
    }

    void reader::completed_with_error () { request_.reset (); }


    //*                           _    *
    //*  _ _ ___ __ _ _  _ ___ __| |_  *
    //* | '_/ -_) _` | || / -_|_-<  _| *
    //* |_| \___\__, |\_,_\___/__/\__| *
    //*            |_|                 *
    /// Manages the process of asynchronously reading from the named pipe.
    class request {
    public:
        explicit request (not_null<command_processor *> cp, recorder * record_file);

        void attach_pipe (unique_handle && p);
        ~request ();

        void cancel ();

    private:
        intrusive_list<reader> list_;

        not_null<command_processor *> command_processor_;
        recorder * const record_file_;

        /// A class used to insert a reader instance into the list of extant reads and
        /// move it from that list unless ownership has been released (by a call to the
        /// release() method).
        class raii_insert {
        public:
            raii_insert (intrusive_list<reader> & list, reader * r) noexcept;
            ~raii_insert () noexcept;
            reader * release () noexcept;

        private:
            reader * r_;
        };
    };

    // (ctor)
    // ~~~~~~
    request::request (not_null<command_processor *> cp, recorder * record_file)
            : command_processor_{cp}
            , record_file_{record_file} {}

    // (dtor)
    // ~~~~~~
    request::~request () {}

    // attach_pipe
    // ~~~~~~~~~~~
    /// Associates the given pipe handle with this request object and starts a read operation.
    void request::attach_pipe (pstore::broker::unique_handle && pipe) {
        auto r = std::make_unique<reader> (std::move (pipe), command_processor_, record_file_);

        // Insert this new object into the list of active reads. We now have two pointers to it:
        // the unique_ptr and the one in the list.
        raii_insert inserter (list_, r.get ());

        r->initiate_read ();

        inserter.release ();
        r.release ();
    }

    // cancel
    // ~~~~~~
    void request::cancel () {
        list_.check ();
        for (reader & r : list_) {
            r.cancel ();
        }
    }

    request::raii_insert::raii_insert (intrusive_list<reader> & list, reader * r) noexcept
            : r_{r} {
        list.insert_before (r, list.tail ());
    }

    request::raii_insert::~raii_insert () noexcept {
        if (r_) {
            intrusive_list<reader>::erase (r_);
        }
    }

    reader * request::raii_insert::release () noexcept {
        auto result = r_;
        r_ = nullptr;
        return result;
    }

} // namespace

namespace {

    // connect_to_new_client
    // ~~~~~~~~~~~~~~~~~~~~~
    /// Initiates the connection between a named pipe and a client.
    bool connect_to_new_client (HANDLE pipe, OVERLAPPED & overlapped) {
        // Start an overlapped connection for this pipe instance.
        auto cnp_res = ::ConnectNamedPipe (pipe, &overlapped);

        // From MSDN: "In nonblocking mode, ConnectNamedPipe() returns a non-zero value the first
        // time it is called for a pipe instance that is disconnected from a previous client. This
        // indicates that the pipe is now available to be connected to a new client process. In all
        // other situations when the pipe handle is in nonblocking mode, ConnectNamedPipe() returns
        // zero."

        auto const errcode = ::GetLastError ();
        if (cnp_res) {
            raise (::pstore::win32_erc (errcode), "ConnectNamedPipe");
        }

        bool pending_io = false;
        switch (errcode) {
        case ERROR_IO_PENDING:
            pending_io = true; // The overlapped connection in progress.
            break;
        case ERROR_NO_DATA:
        case ERROR_PIPE_CONNECTED:
            // The client is already connected, so signal an event.
            if (!::SetEvent (overlapped.hEvent)) {
                raise (::pstore::win32_erc (::GetLastError ()), "SetEvent");
            }
            break;
        default:
            // An error occurred during the connect operation.
            raise (::pstore::win32_erc (errcode), "ConnectNamedPipe");
        }
        return pending_io;
    }


    // create_and_connect_instance
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~
    /// Creates a pipe instance and connects to the client.
    /// \param pipe_name  The name of the pipe.
    /// \param overlap  A OVERLAPPED instance which is used to track the state of the asynchronour
    /// pipe creation.
    /// \return  A pair containing both the pipe handle and a boolean which will be true if the
    /// connect operation is pending, and false if the connection has been completed.
    std::pair<pstore::broker::unique_handle, bool>
    create_and_connect_instance (std::wstring const & pipe_name, OVERLAPPED & overlap) {
        // The default time-out value, in milliseconds,
        static constexpr auto default_pipe_timeout =
            DWORD{5 * 1000}; // TODO: make this user-configurable.

        pstore::broker::unique_handle pipe =
            ::CreateNamedPipeW (pipe_name.c_str (),
                                PIPE_ACCESS_INBOUND |             // read/write access
                                    FILE_FLAG_OVERLAPPED,         // overlapped mode
                                PIPE_TYPE_BYTE |                  // message-type pipe
                                    PIPE_READMODE_BYTE |          // message-read mode
                                    PIPE_WAIT,                    // blocking mode
                                PIPE_UNLIMITED_INSTANCES,         // unlimited instances
                                0,                                // output buffer size
                                pstore::broker::message_size * 4, // input buffer size
                                default_pipe_timeout,             // client time-out
                                nullptr);                         // default security attributes
        if (pipe.get () == INVALID_HANDLE_VALUE) {
            raise (::pstore::win32_erc (::GetLastError ()), "CreateNamedPipeW");
        }

        auto const pending_io = connect_to_new_client (pipe.get (), overlap);
        return std::make_pair (std::move (pipe), pending_io);
    }


    // create_event
    // ~~~~~~~~~~~~
    /// Creates a manual-reset event which is initially signaled.
    pstore::broker::unique_handle create_event () {
        if (HANDLE h = ::CreateEvent (nullptr, true, true, nullptr)) {
            return {h};
        } else {
            raise (::pstore::win32_erc (::GetLastError ()), "CreateEvent");
        }
    }

} // namespace


namespace pstore {
    namespace broker {

        // read_loop
        // ~~~~~~~~~
        void read_loop (fifo_path & path, std::shared_ptr<recorder> & record_file,
                        std::shared_ptr<command_processor> & cp) {
            try {
                pstore::logging::log (pstore::logging::priority::notice, "listening to named pipe ",
                                      pstore::logging::quoted (path.get ().c_str ()));
                auto const pipe_name = pstore::utf::win32::to16 (path.get ());

                // Create one event object for the connect operation.
                unique_handle connect_event = create_event ();

                OVERLAPPED connect{0};
                connect.hEvent = connect_event.get ();

                // Create a pipe instance and and wait for a the client to connect.
                bool pending_io;
                unique_handle pipe;
                std::tie (pipe, pending_io) = create_and_connect_instance (pipe_name, connect);

                request req (cp.get (), record_file.get ());

                while (!done) {
                    constexpr DWORD timeout_ms = 60 * 1000; // 60 seconds // TODO: shared with POSIX
                    // Wait for a client to connect, or for a read or write operation to be
                    // completed,
                    // which causes a completion routine to be queued for execution.
                    auto const cause = ::WaitForSingleObjectEx (connect_event.get (), timeout_ms,
                                                                true /*alertable wait?*/);
                    switch (cause) {
                    case WAIT_OBJECT_0:
                        // A connect operation has been completed. If an operation is pending, get
                        // the
                        // result of the connect operation.
                        if (pending_io) {
                            auto bytes_transferred = DWORD{0};
                            if (!::GetOverlappedResult (pipe.get (), &connect, &bytes_transferred,
                                                        false /*do not wait*/)) {
                                raise (::pstore::win32_erc (::GetLastError ()), "ConnectNamedPipe");
                            }
                        }

                        // Start the read operation for this client.
                        req.attach_pipe (std::move (pipe));

                        // Create new pipe instance for the next client.
                        std::tie (pipe, pending_io) =
                            create_and_connect_instance (pipe_name, connect);
                        break;

                    case WAIT_IO_COMPLETION:
                        // The wait was satisfied by a completed read operation.
                        break;
                    case WAIT_TIMEOUT:
                        pstore::logging::log (pstore::logging::priority::notice, "wait timeout");
                        break;
                    default:
                        raise (::pstore::win32_erc (::GetLastError ()), "WaitForSingleObjectEx");
                    }
                }

                // Try to cancel any reads that are still in-flight.
                req.cancel ();
            } catch (std::exception const & ex) {
                pstore::logging::log (pstore::logging::priority::error, "error: ", ex.what ());
                exit_code = EXIT_FAILURE;
                notify_quit_thread ();
            } catch (...) {
                pstore::logging::log (pstore::logging::priority::error, "unknown error");
                exit_code = EXIT_FAILURE;
                notify_quit_thread ();
            }
            pstore::logging::log (pstore::logging::priority::notice, "exiting read loop");
        }

    } // namespace broker
} // namespace pstore

#endif // _WIN32
// eof: lib/broker/read_loop_win32.cpp
