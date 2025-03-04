//===- lib/os/file_posix.cpp ----------------------------------------------===//
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
/// \file file_posix.cpp
/// \brief POSIX implementation of the cross-platform file APIs

#ifndef _WIN32

#  include "pstore/os/file.hpp"

// standard includes
#  include <array>
#  include <cerrno>
#  include <cstdio>
#  include <cstdlib>
#  include <cstring>
#  include <limits>
#  include <sstream>

// platform includes
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>

// pstore includes
#  include "pstore/adt/small_vector.hpp"
#  include "pstore/os/path.hpp"
#  include "pstore/support/gsl.hpp"
#  include "pstore/support/quoted.hpp"
#  include "pstore/support/unsigned_cast.hpp"

// includes which depend on values in config.hpp
#  ifdef PSTORE_HAVE_SYS_SYSCALL_H
#    include <sys/syscall.h> // For SYS_xxx definitions.
#  endif
#  ifdef PSTORE_HAVE_LINUX_FS_H
#    include <linux/fs.h> // For RENAME_NOREPLACE
#  endif

// Not all versions of linux/fs.h include the definition of RENAME_NOREPLACE.
#  if defined(PSTORE_HAVE_RENAMEAT2) || defined(PSTORE_HAVE_SYS_renameat2)
#    ifndef RENAME_NOREPLACE
#      define RENAME_NOREPLACE (1 << 0) // Don't overwrite target.
#    endif
#  endif

namespace {

  using uoff_type = std::make_unsigned<off_t>::type;
  constexpr auto uoff_max = pstore::unsigned_cast (std::numeric_limits<off_t>::max ());

  template <typename ErrorCode, typename MessageStr, typename PathStr>
  PSTORE_NO_RETURN void raise_file_error (ErrorCode err, MessageStr const message,
                                          PathStr const path) {
    std::ostringstream str;
    str << message << ' ' << pstore::quoted (path);
    pstore::raise_error_code (err, str.str ());
  }

  template <typename MessageStr, typename PathStr>
  PSTORE_NO_RETURN void raise_file_error (int const err, MessageStr const message,
                                          PathStr const path) {
    std::ostringstream str;
    str << message << ' ' << pstore::quoted (path);
    raise (pstore::errno_erc{err}, str.str ());
  }

  int rename_noreplace (pstore::gsl::czstring const old_path,
                        pstore::gsl::czstring const new_path) {
    auto const is_unavailable = [] (int const e) noexcept {
      return e == EINVAL || errno == ENOSYS || errno == ENOTTY;
    };

    auto ret_val = -1L;
    auto err = EINVAL;
    // Try the preferred function first which could come in any one of three forms:
    // - The macOS renamex_np() function.
    // - If available, the glibc wrapper around the Linux renameat2() function.
    // - A direct call to the Linux renameat2 syscall.
#  if defined(PSTORE_HAVE_RENAMEX_NP)
    ret_val = ::renamex_np (old_path, new_path, RENAME_EXCL);
    err = errno;
#  elif defined(PSTORE_HAVE_RENAMEAT2)
    ret_val = ::renameat2 (AT_FDCWD, old_path, AT_FDCWD, new_path, RENAME_NOREPLACE);
    err = errno;
#  elif defined(PSTORE_HAVE_SYS_renameat2)
    // Support for renameat2() was added in glibc 2.28. If we have an older version, we have
    // to invoke the syscall dfirectly.
    ret_val = ::syscall (SYS_renameat2, AT_FDCWD, old_path, AT_FDCWD, new_path, RENAME_NOREPLACE);
    err = errno;
#  else
    // A platform with no renameat2() equivalent that we know about.
    ret_val = -1;
    err = EINVAL;
#  endif
    if (ret_val >= 0) {
      return 0;
    }
    // Apple added renamex_np() for APFS; renameat2() was added in Linux 3.15, but some file
    // systems added support later. If our first try involved an unsupported API, we need to use
    // a fall-back implementation.
    if (!is_unavailable (err)) {
      return -err;
    }

    // Use linkat()/unlinkat() as our first fallback. This doesn't work on directories and on
    // some file systems that do not support hard links (such as FAT).
    if (::linkat (AT_FDCWD, old_path, AT_FDCWD, new_path, 0) >= 0) {
      if (::unlinkat (AT_FDCWD, old_path, 0) < 0) {
        err = errno; // Save errno before unlinkat() clobbers it.
        (void) ::unlinkat (AT_FDCWD, new_path, 0);
        return -err;
      }
      return 0;
    }
    err = errno;
    if (!is_unavailable (err) && err != EPERM) { // FAT returns EPERM on link()
      return -err;
    }

    // Neither renameat2() nor linkat()/unlinkat() worked. Fallback to the TOCTOU vulnerable
    // accessat(F_OK) check followed by classic, replacing renameat(), we have nothing better.
    if (::faccessat (AT_FDCWD, new_path, F_OK, AT_SYMLINK_NOFOLLOW) >= 0) {
      return -EEXIST;
    }
    if (errno != ENOENT) {
      return -errno;
    }
    if (::renameat (AT_FDCWD, old_path, AT_FDCWD, new_path) < 0) {
      return -errno;
    }
    return 0;
  }

} // end anonymous namespace


namespace pstore::file {

  //*   __ _ _       _                 _ _      *
  //*  / _(_) |___  | |_  __ _ _ _  __| | |___  *
  //* |  _| | / -_) | ' \/ _` | ' \/ _` | / -_) *
  //* |_| |_|_\___| |_||_\__,_|_||_\__,_|_\___| *
  //*                                           *
  // open
  // ~~~~
  void file_handle::open (create_mode const create, writable_mode const writable,
                          present_mode const present) {
    this->close ();

    is_writable_ = writable == writable_mode::read_write;

    int oflag = is_writable_ ? O_RDWR : O_RDONLY;
    switch (create) {
    // Creates a new file, only if it does not already exist
    case create_mode::create_new:
      // NOLINTNEXTLINE(hicpp-signed-bitwise)
      oflag |= O_CREAT | O_EXCL;
      break;
    // Opens a file only if it already exists
    case create_mode::open_existing: break;
    // Opens an existing file if present, and creates a new file otherwise.
    case create_mode::open_always:
      // NOLINTNEXTLINE(hicpp-signed-bitwise)
      oflag |= O_CREAT;
      break;
    }

    // user, group, and others have read permission.
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    mode_t pmode = S_IRUSR | S_IRGRP | S_IROTH;
    if (is_writable_) {
      // user, group, and others have read and write permission.
      // NOLINTNEXTLINE(hicpp-signed-bitwise)
      pmode |= S_IWUSR | S_IWGRP | S_IWOTH;
    }

    file_ = ::open (path_.c_str (), oflag, pmode); // NOLINT
    if (file_ == -1) {
      int const err = errno;
      if (present == present_mode::allow_not_found && err == ENOENT) {
        file_ = -1;
      } else {
        raise_file_error (err, "Unable to open", path_);
      }
    }
  }

  void file_handle::open (unique const, std::string const & directory) {
    this->close ();

    std::string const path = path::join (directory, "pst-XXXXXX");

    // mkstemp() modifies its input parameter so that on return it contains
    // the actual name of the temporary file that was created.
    small_vector<char> buffer (path.length () + 1);
    auto out = std::copy (std::begin (path), std::end (path), std::begin (buffer));
    *out = '\0';

    file_ = ::mkstemp (buffer.data ());
    int const err = errno;
    path_ = buffer.data ();
    is_writable_ = true;
    if (file_ == -1) {
      raise_file_error (err, "Unable to create unique file in directory", directory);
    }
  }

  void file_handle::open (temporary const, std::string const & directory) {
    this->open (unique{}, directory);
    if (::unlink (this->path ().c_str ()) == -1) {
      int const err = errno;
      raise_file_error (err, "Unable to create temporary file in directory", directory);
    }
  }

  // close
  // ~~~~~
  void file_handle::close () {
    is_writable_ = false;
    auto file_or_error = file_handle::close_noex (file_);
    if (!file_or_error) {
      raise_file_error (file_or_error.get_error (), "Unable to close", path_);
    }
    file_ = *file_or_error;
  }

  // close noex
  // ~~~~~~~~~~
  auto file_handle::close_noex (oshandle const file) -> error_or<oshandle> {
    if (file != invalid_oshandle && ::close (file) == -1) {
      return error_or<oshandle>{make_error_code (pstore::errno_erc (errno))};
    }
    return error_or<oshandle>{std::in_place, invalid_oshandle};
  }

  // seek
  // ~~~~
  void file_handle::seek (std::uint64_t position) {
    this->ensure_open ();

    using common_type = std::common_type<uoff_type, std::uint64_t>::type;
    static constexpr auto max = static_cast<common_type> (std::numeric_limits<off_t>::max ());

    int mode = SEEK_SET;
    do {
      auto const offset = std::min (static_cast<common_type> (position), max);

      if (::lseek (file_, static_cast<off_t> (offset), mode) == off_t{-1}) {
        int const err = errno;
        raise_file_error (err, "lseek/SEEK_SET failed", this->path ());
      }

      position -= offset;
      mode = SEEK_CUR;
    } while (position > 0);
  }

  // tell
  // ~~~~
  std::uint64_t file_handle::tell () {
    this->ensure_open ();

    off_t const r = ::lseek (file_, off_t{0}, SEEK_CUR);
    if (r == off_t{-1}) {
      int const err = errno;
      raise_file_error (err, "lseek/SEEK_CUR failed", this->path ());
    }
    PSTORE_ASSERT (r >= 0);
    return static_cast<std::uint64_t> (r);
  }

  // read buffer
  // ~~~~~~~~~~~
  std::size_t file_handle::read_buffer (gsl::not_null<void *> const buffer,
                                        std::size_t const nbytes) {
    if (nbytes > unsigned_cast (std::numeric_limits<ssize_t>::max ())) {
      raise (std::errc::invalid_argument, "read_buffer");
    }
    this->ensure_open ();

    ssize_t const r = ::read (file_, buffer.get (), nbytes);
    if (r < 0) {
      int const err = (r == -1) ? errno : EINVAL;
      raise_file_error (err, "read failed", this->path ());
    }
    return static_cast<std::size_t> (r);
  }

  // write buffer
  // ~~~~~~~~~~~~
  void file_handle::write_buffer (gsl::not_null<void const *> const buffer,
                                  std::size_t const nbytes) {
    this->ensure_open ();

    if (::write (file_, buffer.get (), nbytes) == -1) {
      int const err = errno;
      raise_file_error (err, "write failed", this->path ());
    }

    // If the write call succeeded, then the file must have been writable!
    PSTORE_ASSERT (is_writable_);
  }

  // size
  // ~~~~
  std::uint64_t file_handle::size () {
    this->ensure_open ();
    struct stat buf {};
    if (::fstat (file_, &buf) == -1) {
      int const err = errno;
      raise_file_error (err, "fstat failed", this->path ());
    }

    static_assert (std::numeric_limits<std::uint64_t>::max () >=
                     unsigned_cast (std::numeric_limits<decltype (buf.st_size)>::max ()),
                   "stat.st_size is too large for uint64_t");
    PSTORE_ASSERT (buf.st_size >= 0);
    return static_cast<std::uint64_t> (buf.st_size);
  }

  // truncate
  // ~~~~~~~~
  void file_handle::truncate (std::uint64_t const size) {
    this->ensure_open ();
    if (size > uoff_max) {
      raise (std::errc::invalid_argument, "truncate");
    }
    if (::ftruncate (file_, static_cast<off_t> (size)) == -1) {
      int const err = errno;
      raise_file_error (err, "ftruncate failed", this->path ());
    }
  }

  // rename
  // ~~~~~~
  bool file_handle::rename (std::string const & new_name) {
    bool result = true;
    auto const & path = this->path ();
    int err = rename_noreplace (path.c_str (), new_name.c_str ());
    if (err >= 0) {
      path_ = new_name;
    } else {
      err = -err;
      if (err == EEXIST) {
        result = false;
      } else {
        std::ostringstream message;
        message << "Unable to rename " << pstore::quoted (path);
        raise (errno_erc{err}, message.str ());
      }
    }
    return result;
  }

  // lock reg
  // ~~~~~~~~
  /// A helper function for the lock() and unlock() methods. It is a simple wrapper for the
  /// fcntl() system call which fills in all of the fields of the flock struct as necessary.
  //[static]
  int file_handle::lock_reg (int const fd, int const cmd, short const type, off_t const offset,
                             short const whence, off_t const len) {
    struct flock lock {};
    lock.l_type = type;            // type of lock: F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_whence = whence;        // how to interpret l_start (SEEK_SET/SEEK_CUR/SEEK_END)
    lock.l_start = offset;         // starting offset for lock
    lock.l_len = len;              // number of bytes to lock
    lock.l_pid = 0;                // PID of blocking process (set by F_GETLK and F_OFD_GETLK)
    return fcntl (fd, cmd, &lock); // NOLINT
  }

  // lock
  // ~~~~
  bool file_handle::lock (std::uint64_t const offset, std::size_t const size, lock_kind const kind,
                          blocking_mode const block) {
    if (offset > uoff_max || size > uoff_max) {
      raise (std::errc::invalid_argument, "lock");
    }
    this->ensure_open ();

    int cmd = 0;
    switch (block) {
    case blocking_mode::non_blocking: cmd = F_SETLK; break;
    case blocking_mode::blocking: cmd = F_SETLKW; break;
    }

    short type = 0;
    switch (kind) {
    case lock_kind::shared_read: type = F_RDLCK; break;
    case lock_kind::exclusive_write: type = F_WRLCK; break;
    }

    bool got_lock = true;
    if (file_handle::lock_reg (file_,
                               cmd, // set a file lock (maybe a blocking one),
                               type, static_cast<off_t> (offset), SEEK_SET,
                               static_cast<off_t> (size)) != 0) {
      int const err = errno;
      if (block == blocking_mode::non_blocking && (err == EACCES || err == EAGAIN)) {
        // The cmd argument is F_SETLK; the type of lock (l_type) is a shared (F_RDLCK)
        // or exclusive (F_WRLCK) lock and the segment of a file to be locked is already
        // exclusive-locked by another process, or the type is an exclusive lock and
        // some portion of the segment of a file to be locked is already shared-locked
        // or exclusive-locked by another process
        got_lock = false;
      } else {
        raise_file_error (err, "fcntl/lock failed", this->path ());
      }
    }
    return got_lock;
  }

  // unlock
  // ~~~~~~
  void file_handle::unlock (std::uint64_t const offset, std::size_t const size) {
    if (offset > uoff_max || size > uoff_max) {
      raise (std::errc::invalid_argument, "lock");
    }
    this->ensure_open ();
    if (file_handle::lock_reg (file_, F_SETLK,
                               F_UNLCK, // release an existing lock
                               static_cast<off_t> (offset), SEEK_SET,
                               static_cast<off_t> (size)) != 0) {
      int const err = errno;
      raise_file_error (err, "fcntl/unlock failed", this->path ());
    }
  }

  // latest time
  // ~~~~~~~~~~~
  std::time_t file_handle::latest_time () const {
    struct stat buf {};
    if (::stat (path_.c_str (), &buf) != 0) {
      int const err = errno;
      raise_file_error (err, "stat failed", path_);
    }
/// The time members of struct stat might be called st_Xtimespec (of type struct timespec) or
/// st_Xtime (and be of type time_t). This macro is defined in the former situation.
#  ifdef PSTORE_STAT_TIMESPEC
    auto compare = [] (struct timespec const & lhs, struct timespec const & rhs) {
      return std::make_pair (lhs.tv_sec, lhs.tv_nsec) < std::make_pair (rhs.tv_sec, rhs.tv_nsec);
    };
    auto const t = std::max ({buf.st_atimespec, buf.st_mtimespec, buf.st_ctimespec}, compare);
    constexpr auto nano = 1000000000;
    return t.tv_sec + (t.tv_nsec + (nano / 2)) / nano;
#  else
    return std::max ({buf.st_atime, buf.st_mtime, buf.st_ctime});
#  endif
  }

  // get temporary directory
  // ~~~~~~~~~~~~~~~~~~~~~~~
  // [static]
  std::string file_handle::get_temporary_directory () {
    // Following boost filesystem, we check some select environment variables
    // for user temporary directories before resorting to /tmp.
    static constexpr std::array<gsl::czstring, 4> env_var_names{{
      "TMPDIR",
      "TMP",
      "TEMP",
      "TEMPDIR",
    }};
    for (gsl::czstring const name : env_var_names) {
      if (gsl::czstring const val = std::getenv (name)) {
        return val;
      }
    }
    return "/tmp";
  }


  namespace posix {

    // An out-of-line virtual destructor to avoid a vtable in every file that includes
    // deleter.
    deleter::~deleter () noexcept = default;

    void deleter::platform_unlink (std::string const & path) {
      pstore::file::unlink (path, true);
    }

  } // namespace posix


  bool exists (std::string const & path) {
    return ::access (path.c_str (), F_OK) != -1;
  }

  void unlink (std::string const & path, bool const allow_noent) {
    if (::unlink (path.c_str ()) == -1) {
      int const err = errno;
      if (!allow_noent || err != ENOENT) {
        raise_file_error (err, "unlink failed", path);
      }
    }
  }

} // namespace pstore::file
#endif //_WIN32
