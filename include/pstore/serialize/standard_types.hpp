//===- include/pstore/serialize/standard_types.hpp --------*- mode: C++ -*-===//
//*      _                  _               _   _                          *
//*  ___| |_ __ _ _ __   __| | __ _ _ __ __| | | |_ _   _ _ __   ___  ___  *
//* / __| __/ _` | '_ \ / _` |/ _` | '__/ _` | | __| | | | '_ \ / _ \/ __| *
//* \__ \ || (_| | | | | (_| | (_| | | | (_| | | |_| |_| | |_) |  __/\__ \ *
//* |___/\__\__,_|_| |_|\__,_|\__,_|_|  \__,_|  \__|\__, | .__/ \___||___/ *
//*                                                 |___/|_|               *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file standard_types.hpp
/// \brief Provides serialization capabilities for common types.

#ifndef PSTORE_SERIALIZE_STANDARD_TYPES_HPP
#define PSTORE_SERIALIZE_STANDARD_TYPES_HPP

#include <atomic>
#include <map>
#include <set>

#include "pstore/serialize/types.hpp"
#include "pstore/support/varint.hpp"

///@{
/// \name Reading and writing standard types
/// A collection of convenience methods which each know how to serialize the types defined by the
/// standard library (string, vector, set, etc.)
namespace pstore::serialize {

  struct string_helper {
    /// \brief Writes an instance of a string type (e.g. `sstring_view`) to an archive.
    ///
    /// Writes a variable length value followed by a sequence of characters. The
    /// length uses the format defined by varint::encode() but we ensure that at
    /// least two bytes are produced. This means that the read() member can rely
    /// on being able to read two bytes and reduce the number of pstore accesses
    /// to two for strings < (2^14 - 1) characters (and three for strings longer
    /// than that.
    ///
    /// \param archive  The Archiver to which the value 'str' should be written.
    /// \param str      The string whose content is to be written to the archive.
    /// \returns The value returned by writing the first byte of the string length.
    ///   By convention, this is the "address" of the string data (although the precise
    ///   meaning is determined by the archive type).
    template <typename Archive, typename StringType>
    static auto write (Archive && archive, StringType const & str) -> archive_result_type<Archive> {
      auto const length = str.length ();

      // Encode the string length as a variable-length integer.
      std::array<std::uint8_t, varint::max_output_length> encoded_length;
      auto first = std::begin (encoded_length);
      auto last = varint::encode (length, first);
      auto length_bytes = std::distance (first, last);
      PSTORE_ASSERT (length_bytes > 0 &&
                     static_cast<std::size_t> (length_bytes) <= encoded_length.size ());
      if (length_bytes == 1) {
        *(last++) = 0;
      }
      // Emit the string length.
      auto const resl = serialize::write (
        archive, gsl::make_span (std::addressof (*first), std::addressof (*last)));

      // Emit the string body.
      serialize::write (std::forward<Archive> (archive), gsl::make_span (str));
      return resl;
    }

    template <typename Archive>
    static std::size_t read_length (Archive && archive) {
      std::array<std::uint8_t, varint::max_output_length> encoded_length{{0}};
      // First read the two initial bytes. These contain the variable length value
      // but might not be enough for the entire value.
      static_assert (varint::max_output_length >= 2, "maximum encoded varint length must be >= 2");
      serialize::read_uninit (archive, gsl::make_span (encoded_length.data (), 2));

      auto const varint_length = varint::decode_size (std::begin (encoded_length));
      PSTORE_ASSERT (varint_length > 0);
      // Was that initial read of 2 bytes enough? If not get the rest of the
      // length value.
      if (varint_length > 2) {
        PSTORE_ASSERT (varint_length <= encoded_length.size ());
        serialize::read_uninit (archive,
                                gsl::make_span (encoded_length.data () + 2, varint_length - 2));
      }

      return varint::decode (encoded_length.data (), varint_length);
    }
  };

  /// \brief A serializer for std::string.
  template <>
  struct serializer<std::string> {
    using value_type = std::string;

    /// \brief Writes an instance of `std::string` to an archive.
    template <typename Archive>
    static auto write (Archive && archive, value_type const & str) -> archive_result_type<Archive> {
      return string_helper::write (std::forward<Archive> (archive), str);
    }

    /// \brief Reads an instance of `std::string` from an archiver.
    ///
    /// \param archive  The Archiver from which a string will be read.
    /// \param str      A reference to uninitialized memory that is suitable for a new
    /// string instance.
    template <typename Archive>
    static void read (Archive && archive, value_type & str) {
      // Read the body of the string.
      new (&str) value_type;

      // Deleter will ensure that the string is destroyed on exit if an exception is
      // raised here.
      auto dtor = [] (value_type * const p) {
        using string = std::string;
        p->~string ();
      };
      std::unique_ptr<value_type, decltype (dtor)> deleter (&str, dtor);

      auto const length = string_helper::read_length (archive);
      str.resize (length);

#ifdef PSTORE_HAVE_NON_CONST_STD_STRING_DATA
      char * const data = str.data ();
#else
      // TODO: this is technically undefined behaviour. Remove once we've got access to
      // the C++17 library on our platforms.
      auto * const data = const_cast<char *> (str.data ());
#endif
      // Now read the body of the string.
      serialize::read_uninit (archive, gsl::make_span (data, static_cast<std::ptrdiff_t> (length)));

      // Release ownership from the deleter so that the initialized object is returned to
      // the caller.
      deleter.release ();
    }
  };

  /// \brief A serializer for std::string const. It delegates both read and write operations
  ///        to the std::string serializer.
  template <>
  struct serializer<std::string const> {
    using value_type = std::string;
    template <typename Archive>
    static auto write (Archive && archive, value_type const & str) -> archive_result_type<Archive> {
      return serializer::write (std::forward<Archive> (archive), str);
    }
    template <typename Archive>
    static void read (Archive && archive, value_type & str) {
      serialize::read_uninit (std::forward<Archive> (archive), str);
    }
  };

  /// \brief A helper class which can be used to emit containers which have a size() method
  /// and
  ///        which support the requirements for range-based 'for'.
  template <typename Container>
  struct container_archive_helper {

    /// \brief Writes the contents of a container to an archive.
    ///
    /// Writes an initial std::size_t value with the number of elements in the container
    /// followed by an array of of those elements, in the order returned by iteration.
    ///
    /// \param archive The archive to which the container is to be serialized.
    /// \param ty The container whose contents are to be written.

    template <typename Archive>
    static auto write (Archive && archive, Container const & ty) -> archive_result_type<Archive> {
      // TODO: size_t is not a fixed-size type. Prefer uintXX_t.
      auto result = serialize::write (archive, std::size_t{ty.size ()});
      for (typename Container::value_type const & m : ty) {
        serialize::write (archive, m);
      }
      return result;
    }

    using insert_callback = std::function<void (typename Container::value_type const &)>;

    /// \brief Reads the contents of a container from an archive.
    ///
    /// Reads a std::size_t value -- the number of following elements -- and an array of
    /// Container::vaue_type elements. For each element, the inserter function is invoked:
    /// it is passed the container and value to be inserted. Its job is simply to insert
    /// the value into the given container.
    ///
    /// \param archive The archive from which the container will be read.
    /// \param inserter A function which is responsible for inserting each of the
    ///                 Container::value_type elements from the archive into the container.
    template <typename Archive>
    static void read (Archive && archive, insert_callback inserter) {
      // TODO: size_t is not a fixed-size type. Prefer uintXX_t.
      auto const num_members = serialize::read<std::size_t> (archive);
      auto num_read = std::size_t{0};
      for (; num_read < num_members; ++num_read) {
        inserter (serialize::read<typename Container::value_type> (archive));
      }
    }
  };


  /// \brief A serializer for std::atomic<T>
  template <typename T>
  struct serializer<std::atomic<T>> {
    using value_type = std::atomic<T>;

    /// \brief Writes an instance of `std::atomic<>` to an archive.
    ///
    /// The data stream format follows that of the underlying type.
    ///
    /// \param archive  The archive to which the atomic will be written.
    /// \param value    The `std::atomic<>` instance that is to be serialized.
    template <typename Archive>
    static auto write (Archive && archive, value_type const & value)
      -> archive_result_type<Archive> {
      return serialize::write (std::forward<Archive> (archive), value.load ());
    }

    /// \brief Reads an instance of `std::atomic<>` from an archive.
    ///
    /// \param archive  The archiver from which the value will be read.
    /// \param value  The de-serialized std::atomic value.
    template <typename Archive>
    static void read (Archive && archive, value_type & value) {
      serialize::read_uninit<T> (std::forward<Archive> (archive), value);
    }
  };


  /// \brief A serializer for std::pair<T,U>
  template <typename T, typename U>
  struct serializer<std::pair<T, U>> {
    using value_type = std::pair<T, U>;

    /// \brief Writes an instance of `std::pair<>` to an archive.
    ///
    /// The data stream format consists of the two pair elements, first
    /// and second, read and written in that order.
    ///
    /// \param archive  The archive to which the pair will be written.
    /// \param value    The `std::pair<>` instance that is to be serialized.
    template <typename Archive>
    static auto write (Archive && archive, value_type const & value)
      -> archive_result_type<Archive> {

      auto const result = serialize::write (archive, value.first);
      serialize::write (std::forward<Archive> (archive), value.second);
      return result;
    }

    /// \brief Reads an instance of `std::pair<>` from an archive.
    ///
    /// \param archive  The archiver from which the value will be read.
    /// \param value  A reference to uninitialized memory into which the de-serialized pair
    /// will be read.
    template <typename Archive>
    static void read (Archive && archive, value_type & value) {
      serialize::read_uninit (archive, value.first);
      serialize::read_uninit (std::forward<Archive> (archive), value.second);
    }
  };

} // end namespace pstore::serialize
///@}
#endif // PSTORE_SERIALIZE_STANDARD_TYPES_HPP
