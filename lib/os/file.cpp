//===- lib/os/file.cpp ----------------------------------------------------===//
//*   __ _ _       *
//*  / _(_) | ___  *
//* | |_| | |/ _ \ *
//* |  _| | |  __/ *
//* |_| |_|_|\___| *
//*                *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file file.cpp
/// \brief Definitions of the cross platform file management functions and classes.
#include "pstore/os/file.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "pstore/config/config.hpp"
#include "pstore/support/portab.hpp"

namespace pstore {
  namespace file {
    //*                 _                                              *
    //*   ___ _   _ ___| |_ ___ _ __ ___     ___ _ __ _ __ ___  _ __   *
    //*  / __| | | / __| __/ _ \ '_ ` _ \   / _ \ '__| '__/ _ \| '__|  *
    //*  \__ \ |_| \__ \ ||  __/ | | | | | |  __/ |  | | | (_) | |     *
    //*  |___/\__, |___/\__\___|_| |_| |_|  \___|_|  |_|  \___/|_|     *
    //*       |___/                                                    *
    // (ctor)
    // ~~~~~~
    system_error::system_error (std::error_code const code, std::string const & user_message,
                                std::string path)
            : std::system_error (code, message (user_message, path))
            , path_ (std::move (path)) {}

    system_error::system_error (std::error_code const code, gsl::czstring const user_message,
                                std::string path)
            : std::system_error (code,
                                 message (user_message != nullptr ? user_message : "File", path))
            , path_ (std::move (path)) {}

    // dtor
    // ~~~~
    system_error::~system_error () noexcept = default;

    // message
    // ~~~~~~~
    template <typename MessageStringType>
    std::string system_error::message (MessageStringType const & user_message,
                                       std::string const & path) {
      std::ostringstream stream;
      stream << user_message;
      if (path.length () > 0) {
        stream << " \"" << path << '\"';
      }
      return stream.str ();
    }

    //*                                _            _      *
    //*   _ __ __ _ _ __   __ _  ___  | | ___   ___| | __  *
    //*  | '__/ _` | '_ \ / _` |/ _ \ | |/ _ \ / __| |/ /  *
    //*  | | | (_| | | | | (_| |  __/ | | (_) | (__|   <   *
    //*  |_|  \__,_|_| |_|\__, |\___| |_|\___/ \___|_|\_\  *
    //*                   |___/                            *
    // (ctor)
    // ~~~~~~
    range_lock::range_lock (file_base * const file, std::uint64_t const offset,
                            std::size_t const size, file_base::lock_kind const kind) noexcept
            : file_{file}
            , offset_{offset}
            , size_{size}
            , kind_{kind} {}

    range_lock::range_lock (range_lock && other) noexcept
            : file_{other.file_}
            , offset_ (other.offset_)
            , size_ (other.size_)
            , kind_ (other.kind_)
            , locked_ (other.locked_) {

      other.file_ = nullptr;
      other.locked_ = false;
    }

    // (dtor)
    // ~~~~~~
    range_lock::~range_lock () noexcept {
      no_ex_escape ([this] () { this->unlock (); });
    }

    // operator=
    // ~~~~~~~~~
    range_lock & range_lock::operator= (range_lock && other) noexcept {
      if (&other != this) {
        no_ex_escape ([this] () { this->unlock (); });
        file_ = other.file_;
        offset_ = other.offset_;
        size_ = other.size_;
        kind_ = other.kind_;
        locked_ = other.locked_;

        other.file_ = nullptr;
        other.locked_ = false;
      }
      return *this;
    }

    // lock
    // ~~~~
    bool range_lock::lock () {
      return this->lock_impl (file_base::blocking_mode::blocking);
    }

    // try lock
    // ~~~~~~~~
    bool range_lock::try_lock () {
      return this->lock_impl (file_base::blocking_mode::non_blocking);
    }

    // lock impl
    // ~~~~~~~~~
    bool range_lock::lock_impl (file_base::blocking_mode const mode) {
      if (locked_) {
        return false;
      }
      if (file_ != nullptr) {
        locked_ = file_->lock (offset_, size_, kind_, mode);
      }
      return locked_;
    }

    // unlock
    // ~~~~~~
    void range_lock::unlock () {
      if (locked_ && file_ != nullptr) {
        file_->unlock (offset_, size_);
      }
      locked_ = false;
    }


    //*   __ _ _       _                   *
    //*  / _(_) |___  | |__  __ _ ___ ___  *
    //* |  _| | / -_) | '_ \/ _` (_-</ -_) *
    //* |_| |_|_\___| |_.__/\__,_/__/\___| *
    //*                                    *
    file_base::~file_base () noexcept = default;


    //*  _                                      *
    //* (_)_ _    _ __  ___ _ __  ___ _ _ _  _  *
    //* | | ' \  | '  \/ -_) '  \/ _ \ '_| || | *
    //* |_|_||_| |_|_|_\___|_|_|_\___/_|  \_, | *
    //*                                   |__/  *
    // path
    // ~~~~
    std::string in_memory::path () const {
      // In-memory files obviously don't have a path, so it's hard to know quite what to
      // return!
      return ":in-memory:";
    }

    // check writable
    // ~~~~~~~~~~~~~~
    void in_memory::check_writable () const {
      if (!writable_) {
        raise (std::errc::permission_denied);
      }
    }

    // read buffer
    // ~~~~~~~~~~~
    std::size_t in_memory::read_buffer (gsl::not_null<void *> const ptr, std::size_t nbytes) {
      // The second half of this check is to catch integer overflows.
      if (pos_ + nbytes > eof_ || pos_ + nbytes < pos_) {
        nbytes = eof_ - pos_;
      }

      using element_type = decltype (buffer_)::element_type;
      using index_type = gsl::span<decltype (buffer_)::element_type>::index_type;

#ifndef NDEBUG
      {
        constexpr auto max = std::numeric_limits<index_type>::max ();
        PSTORE_ASSERT (length_ <= max && pos_ <= max && nbytes <= max);
      }
#endif
      auto const length = static_cast<index_type> (length_);
      auto const pos = static_cast<index_type> (pos_);
      auto span =
        gsl::make_span (buffer_.get (), length).subspan (pos, static_cast<index_type> (nbytes));
      std::copy (std::begin (span), std::end (span), static_cast<element_type *> (ptr.get ()));

      PSTORE_ASSERT (pos_ + nbytes >= pos_);
      pos_ += nbytes;
      return nbytes;
    }

    // write buffer
    // ~~~~~~~~~~~~
    void in_memory::write_buffer (gsl::not_null<void const *> const ptr, std::size_t const nbytes) {
      this->check_writable ();
      PSTORE_ASSERT (length_ > pos_);
      if (nbytes > length_ - pos_) {
        raise (std::errc::invalid_argument);
      }

      using element_type = decltype (buffer_)::element_type;
      using index_type = gsl::span<element_type>::index_type;
#ifndef NDEBUG
      {
        // TODO: if nbytes > index_type max if would be much better to split the
        // write into two pieces rather than just fail...
        constexpr auto max = std::numeric_limits<index_type>::max ();
        PSTORE_ASSERT (length_ <= max && pos_ <= max && nbytes <= max);
      }
#endif
      auto dest_span =
        gsl::make_span (buffer_.get (), static_cast<index_type> (length_))
          .subspan (static_cast<index_type> (pos_), static_cast<index_type> (nbytes));
      auto src_span = gsl::make_span (static_cast<element_type const *> (ptr.get ()),
                                      static_cast<index_type> (nbytes));

      static_assert (std::is_same_v<decltype (src_span)::index_type, index_type>,
                     "expected index_type of src_span and dest_span to be the same");

      std::copy (std::begin (src_span), std::end (src_span), std::begin (dest_span));
      PSTORE_ASSERT (pos_ + nbytes >= pos_);
      pos_ += nbytes;
      if (pos_ > eof_) {
        eof_ = pos_;
      }
    }

    // seek
    // ~~~~
    void in_memory::seek (std::uint64_t const position) {
      if (position > eof_) {
        raise (std::errc::invalid_argument);
      }
      pos_ = position;
    }

    // truncate
    // ~~~~~~~~
    void in_memory::truncate (std::uint64_t const size) {
      PSTORE_ASSERT (eof_ <= length_);
      PSTORE_ASSERT (pos_ <= eof_);
      this->check_writable ();

      if (size > length_) {
        raise (std::errc::invalid_argument);
      }
      if (size > eof_) {
        // Fill from the current end of file to the end of the newly available region.
        std::memset (buffer_.get () + eof_, 0, size - eof_);
      }
      eof_ = size;
      // Clamp pos inside the new file extent
      pos_ = std::min (pos_, eof_);
    }

    // latest time
    // ~~~~~~~~~~~
    std::time_t in_memory::latest_time () const {
      return 0;
    }


    //*    __ _ _        _                     _ _        *
    //*   / _(_) | ___  | |__   __ _ _ __   __| | | ___   *
    //*  | |_| | |/ _ \ | '_ \ / _` | '_ \ / _` | |/ _ \  *
    //*  |  _| | |  __/ | | | | (_| | | | | (_| | |  __/  *
    //*  |_| |_|_|\___| |_| |_|\__,_|_| |_|\__,_|_|\___|  *
    //*                                                   *
    // (rvalue constructor)
    // ~~~~~~~~~~~~~~~~~~~~
    file_handle::file_handle (file_handle && other) noexcept
            : path_{std::move (other.path_)}
            , file_{other.file_}
            , is_writable_{other.is_writable_} {

      other.file_ = invalid_oshandle;
      other.is_writable_ = false;
    }

    // ~file_handle
    // ~~~~~~~~~~~~
    file_handle::~file_handle () noexcept {
      is_writable_ = false;
      file_handle::close_noex (file_);
      file_ = invalid_oshandle;
    }

    // operator= (rvalue assignment)
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    file_handle & file_handle::operator= (file_handle && rhs) noexcept {
      no_ex_escape ([this] () { this->close (); });

      path_ = std::move (rhs.path_);
      file_ = rhs.file_;
      is_writable_ = rhs.is_writable_;

      rhs.file_ = invalid_oshandle;
      rhs.is_writable_ = false;
      return *this;
    }

    std::ostream & operator<< (std::ostream & os, file_handle const & fh) {
      return os << R"({ file:")" << fh.path () << R"(" })";
    }

    //*       _      _      _              _                      *
    //*    __| | ___| | ___| |_ ___ _ __  | |__   __ _ ___  ___   *
    //*   / _` |/ _ \ |/ _ \ __/ _ \ '__| | '_ \ / _` / __|/ _ \  *
    //*  | (_| |  __/ |  __/ ||  __/ |    | |_) | (_| \__ \  __/  *
    //*   \__,_|\___|_|\___|\__\___|_|    |_.__/ \__,_|___/\___|  *
    //*                                                           *
    deleter_base::deleter_base (std::string path, unlink_proc unlinker)
            : path_{std::move (path)}
            , unlinker_{std::move (unlinker)} {}

    deleter_base::~deleter_base () noexcept {
      no_ex_escape ([this] () { this->unlink (); });
    }

    void deleter_base::unlink () {
      if (!released_) {
        unlinker_ (path_);
        released_ = true;
      }
    }
  } // end of namespace file
} // end of namespace pstore
