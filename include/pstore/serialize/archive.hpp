//===- include/pstore/serialize/archive.hpp ---------------*- mode: C++ -*-===//
//*                 _     _            *
//*   __ _ _ __ ___| |__ (_)_   _____  *
//*  / _` | '__/ __| '_ \| \ \ / / _ \ *
//* | (_| | | | (__| | | | |\ V /  __/ *
//*  \__,_|_|  \___|_| |_|_| \_/ \___| *
//*                                    *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file serialize/archive.hpp
/// \brief The basic archive reader and writer types.


/// \page archives Serialization Archives
/// There are two types of archiver: readers and writers.
///
/// \section archive-writers Archive Writers
/// \code
/// class archive_writer : public writer_base <archive_writer> {
///     friend class writer_base <archive_writer>;
/// private:
///     // Returns the number of bytes that have been written to this archive.
///     virtual std::size_t size_impl () const;
///
///     // An optional method which flushes any remaining output.
///     virtual void flush_impl ();
///
///     // Wrties an instance of standard-layout type T to the archive.
///     template <typename T>
///         void put_impl (T const & );
/// };
/// \endcode
///
/// See the zlib_archive.cpp example.
///
/// \section archive-readers Archive Readers
/// archive-readers:
/// \code
/// struct range_reader {
///     // Reads and returns a single instance of a standard-layout type T from the archive and
///     returns the value
///     // extracted.
///     template <typename T>
///         T get ();
/// };
/// \endcode


/// \example ostream_writer.cpp
/// \brief A simple archive-writer which writes data to a std::ostream stream.
///
/// It will produce the following output:
///
///     3
///     73
///     127
///     179
///
/// The first of these values is produced by the std::set<> serializer: it is the number of members
/// in the set. The following numbers are the contents of the container in normal iteration order.
///
/// \see istream_reader.cpp

/// \example istream_reader.cpp
/// \brief A simple archive-reader which reads data from a std::istream stream.
///
/// It will produce the following output:
///
///     73
///     127
///     179
///
/// \see ostream_writer.cpp

/// \example zlib_archive.cpp
/// \brief An example of writing a non-trivial archive-writer.
/// This example uses ZLib to compress any data that is written to it, recording the result in a
/// byte vector (std::vector<std::uint8_t>).
///
/// It will produce output which looks like:
///
///     Started with these 197 bytes:
///     53 70 61 63 65 20 69 73 20 62 69 67 2e 20 52 65 61 6c 6c 79 20 62 69 67 2e 20 59 6f 75 20 6a
///     75 73 74 20 77 6f 6e 27 74 20 62 65 6c 69 65 76 65 20 68 6f 77 20 76 61 73 74 6c 79 2c 20 68
///     75 67 65 6c 79 2c 20 6d 69 6e 64 2d 62 6f 67 67 6c 69 6e 67 6c 79 20 62 69 67 20 69 74 20 69
///     73 2e 20 49 20 6d 65 61 6e 2c 20 79 6f 75 20 6d 61 79 20 74 68 69 6e 6b 20 69 74 27 73 20 61
///     20 6c 6f 6e 67 20 77 61 79 20 64 6f 77 6e 20 74 68 65 20 72 6f 61 64 20 74 6f 20 74 68 65 20
///     63 68 65 6d 69 73 74 2c 20 62 75 74 20 74 68 61 74 27 73 20 6a 75 73 74 20 70 65 61 6e 75 74
///     73 20 74 6f 20 73 70 61 63 65 2e
///
///     Wrote these 147 bytes:
///     78 da 25 8e b1 12 c3 20 0c 43 fb 29 da b2 a4 f9 8f ae ed d4 11 12 1f b8 05 9c 2b 26 1c 7f 5f
///     48 26 eb 24 eb 9d 5e bb 59 09 9c 61 d9 2d 78 92 09 a1 5d fa 2d 05 9f 92 15 55 d2 a4 b0 14 98
///     0e 82 97 8a c3 64 0d 6d 86 2f 8e c6 8d 9c b6 bb 15 e7 02 27 77 f5 c1 da a9 0b 1e 88 64 d2 8c
///     d6 69 d1 34 a8 e7 f4 ed e1 94 61 10 24 39 d4 ee 6e 52 53 8f 08 3f 31 1b 54 4e bd 7a 8a 9c 75
///     86 2d da 0d 33 3a e7 a0 bd 13 8b e6 f1 97 c7 fc e5 f6 07 e6 0e 45 39

#ifndef PSTORE_SERIALIZE_ARCHIVE_HPP
#define PSTORE_SERIALIZE_ARCHIVE_HPP

#include <cstring>

#include "pstore/serialize/common.hpp"
#include "pstore/support/error.hpp"

namespace pstore::serialize::archive {

  template <typename T>
  auto unsigned_cast (T const & t) -> std::make_unsigned_t<T> {
    using unsigned_type = typename std::make_unsigned<T>::type;
    PSTORE_ASSERT (t >= 0);
    return static_cast<unsigned_type> (t);
  }

  /// The archiver put() and [optional] putn() methods can optionally return a
  /// value to the caller. By convention, the return value represents the location
  /// to which the value(s) were written. However, for some archivers this doesn't
  /// make sense. For example, an archiver which writes to stdout can't really say
  /// anything useful. In these cases, we use 'void_type' to be a standin for the
  /// void type.
  struct void_type {};


  // *****************************
  // *   w r i t e r   b a s e   *
  // *****************************
  /// \brief The base class for archive-writer objects.

  template <typename WriterPolicy>
  class writer_base {
  public:
    using policy_type = WriterPolicy;
    using result_type = typename policy_type::result_type;

    writer_base (writer_base const &) = delete;
    writer_base (writer_base &&) noexcept = default;

    virtual ~writer_base () noexcept {
      no_ex_escape ([this] () { this->flush (); });
    }

    writer_base & operator= (writer_base &&) noexcept = default;
    writer_base & operator= (writer_base const &) = delete;

    ///@{
    /// \brief Writes one or more instances of a standard-layout type Ty to the output.
    /// Must not be used once the stream has been flushed.

    /// \param t  The value to be written to the output.
    template <typename Ty>
    auto put (Ty const & t) -> result_type {
      static_assert (std::is_standard_layout<Ty>::value,
                     "writer_base can only write standard-layout types!");
      PSTORE_ASSERT (!flushed_);
      result_type r = policy_.put (t);
      bytes_consumed_ += sizeof (t);
      return r;
    }

    /// \brief Writes a span of values to the output.
    ///
    /// This will call either the put() or putn() method on the archive type depending
    /// on whether the latter is available.
    ///
    /// \param sp  The span of values to be written.
    /// \returns The value returned by the archive putn() function.
    template <typename Span>
    auto putn (Span sp) -> result_type {
      using element_type = typename Span::element_type;
      static_assert (std::is_standard_layout<element_type>::value,
                     "writer_base can only write standard-layout types!");
      PSTORE_ASSERT (!flushed_);
      auto r = putn_helper::template putn<Span> (policy_, sp);
      bytes_consumed_ += unsigned_cast (sp.size_bytes ());
      return r;
    }
    ///@}

    /// \brief Flushes the stream to the output.
    void flush () {
      if (!flushed_) {
        policy_.flush ();
        flushed_ = true;
      }
    }

    /// \brief Returns the number of bytes that have been written via this archive.
    std::size_t bytes_consumed () const { return bytes_consumed_; }

    /// \brief Returns the number of bytes that the policy object wrote to its final
    /// destination.
    std::size_t bytes_produced () const {
      return bytes_produced_helper (*this).get (policy_, nullptr);
    }

    ///@{
    /// Returns the writer_base output policy object.
    WriterPolicy & writer_policy () noexcept { return policy_; }
    WriterPolicy const & writer_policy () const noexcept { return policy_; }
    ///@}

  protected:
    explicit writer_base (WriterPolicy policy = WriterPolicy ())
            : policy_{std::move (policy)} {}

  private:
    /// A wrapper class which is used to call either policy.bytes_produced() if it is
    /// supported by the policy object and writer_base.bytes_consumed() if it is not.
    class bytes_produced_helper {
    public:
      explicit bytes_produced_helper (writer_base const & writer)
              : writer_{writer} {}
      // (This is a varargs function so that it is considered last in template
      // resolution.)
      template <typename Policy>
      std::size_t get (Policy const & /*policy*/, ...) { // NOLINT(cert-dcl50-cpp)
        return writer_.bytes_consumed ();
      }
      template <typename Policy>
      std::size_t get (Policy const & policy, decltype (&Policy::bytes_produced)) {
        return policy.bytes_produced ();
      }

    private:
      writer_base const & writer_;
    };

    struct putn_helper {
    public:
      using result_type = typename policy_type::result_type;

      template <typename Span>
      static auto putn (WriterPolicy & policy, Span span) -> result_type {
        return invoke (policy, span, nullptr);
      }

    private:
      // This overload is always in the set of overloads but a function with
      // ellipsis parameter has the lowest ranking for overload resolution.
      template <typename P, typename Span>
      static auto invoke (P & policy, Span span, ...) // NOLINT(cert-dcl50-cpp)
        -> result_type {
        sticky_assign<result_type> r;
        for (auto & v : span) {
          r = policy.put (v);
        }
        return r.get ();
      }

      // This overload is called if P has a putn<>() method. SFINAE means that we fall
      // back to the ellipsis overload if it does not.
      template <typename P, typename Span>
      static auto invoke (P & policy, Span span, decltype (&P::template putn<Span>))
        -> result_type {
        return policy.putn (span);
      }
    };

    WriterPolicy policy_;
    std::size_t bytes_consumed_ = 0U;
    bool flushed_{false}; ///< Has the stream been flushed?
  };


  namespace details {
    class vector_writer_policy {
    public:
      using result_type = std::size_t;
      using container = std::vector<std::uint8_t>;
      using const_iterator = container::const_iterator;

      explicit vector_writer_policy (container & bytes) noexcept
              : bytes_ (bytes) {}

      template <typename Ty>
      auto put (Ty const & t) -> result_type {
        auto const old_size = bytes_.size ();
        auto const * const first = reinterpret_cast<std::uint8_t const *> (&t);
        std::copy (first, first + sizeof (Ty), std::back_inserter (bytes_));
        return old_size;
      }

      template <typename SpanType>
      auto putn (SpanType sp) -> result_type {
        auto const old_size = bytes_.size ();
        auto const * const first = reinterpret_cast<std::uint8_t const *> (sp.data ());
        std::copy (first, first + sp.size_bytes (), std::back_inserter (bytes_));
        return old_size;
      }

      /// Returns the size of the byte vector managed by the object.
      std::size_t size () const noexcept { return bytes_.size (); }

      void flush () noexcept {}

      /// Returns a const_iterator for the beginning of the byte vector managed by the
      /// object.
      container::const_iterator begin () const { return std::begin (bytes_); }
      /// Returns a const_iterator for the end of the byte vector managed by the
      /// object.
      container::const_iterator end () const { return std::end (bytes_); }

    private:
      container & bytes_; ///< The container into which written data is accumulated.
    };
  }                       // namespace details

  // *******************************
  // *  v e c t o r   w r i t e r  *
  // *******************************

  /// \brief An archive-writer which writes data to a std::vector of std::uint8_t bytes.
  /// Owns a vector of bytes to which data is appended when the put<>() method is called.

  class vector_writer final : public writer_base<details::vector_writer_policy> {
  public:
    explicit vector_writer (std::vector<std::uint8_t> & container)
            : writer_base<details::vector_writer_policy> (
                details::vector_writer_policy{container}) {}
    vector_writer (vector_writer const &) = delete;
    vector_writer (vector_writer &&) = delete;

    ~vector_writer () noexcept override;

    vector_writer & operator= (vector_writer const &) = delete;
    vector_writer & operator= (vector_writer &&) = delete;

    using container = policy_type::container;
    using const_iterator = policy_type::const_iterator;

    /// Returns a const_iterator for the beginning of the byte vector managed by the
    /// object.
    const_iterator begin () const { return writer_policy ().begin (); }
    /// Returns a const_iterator for the end of the byte vector managed by the object.
    const_iterator end () const { return writer_policy ().end (); }
  };



  /// Writes the contents of a vector_writer object to an ostream as a stream of
  /// space-separated two-digit hexadecimal values.
  ///
  /// \param os  The output stream to which output will be written.
  /// \param writer  The vector_writer whose contents are to be dumped to the output
  ///                stream.
  /// \result  The value passed as the 'os' parameter.
  std::ostream & operator<< (std::ostream & os, vector_writer const & writer);


  // *********************************
  // *   b u f f e r   w r i t e r   *
  // *********************************

  namespace details {
    class buffer_writer_policy {
    public:
      using result_type = void *;

      buffer_writer_policy (void * const first, void * const last) noexcept
              : begin_ (static_cast<std::uint8_t *> (first))
#ifndef NDEBUG
              , end_ (static_cast<std::uint8_t *> (last))
#endif
              , it_ (static_cast<std::uint8_t *> (first)) {

        (void) last;
        PSTORE_ASSERT (end_ >= it_);
      }

      /// Writes an object to the output buffer.
      /// \param v The value to be written to the output container.
      template <typename Ty>
      auto put (Ty const & v) -> result_type {
        auto const size = sizeof (v);
        PSTORE_ASSERT (it_ + size <= end_);
        auto const result = it_;
        std::memcpy (it_, &v, size);
        it_ += size;
        PSTORE_ASSERT (it_ <= end_);
        return result;
      }

      /// Returns the number of bytes written to the buffer.
      std::size_t size () const noexcept {
        PSTORE_ASSERT (it_ >= begin_);
        static_assert (sizeof (std::size_t) >= sizeof (std::ptrdiff_t),
                       "sizeof size_t should be at least sizeof ptrdiff_t");
        return static_cast<std::size_t> (it_ - begin_);
      }

      void flush () noexcept {}

      using const_iterator = std::uint8_t const *;

      /// Returns a const_iterator for the beginning of the byte range.
      const_iterator begin () const noexcept { return begin_; }
      /// Returns a const_iterator for the end of byte range written to the buffer.
      const_iterator end () const noexcept { return it_; }

    private:
      /// The start of the input buffer.
      std::uint8_t * begin_;
#ifndef NDEBUG
      /// The end of the input buffer range.
      std::uint8_t * end_;
#endif
      /// Initially equal to begin_, but incremented as data is written to the
      /// archive. Always <= end_;
      std::uint8_t * it_;
    };
  } // namespace details

  class buffer_writer final : public writer_base<details::buffer_writer_policy> {
  public:
    /// \brief Constructs the writer using the range [first, last).
    /// \param first The start address of the buffer to which the buffer_writer will
    ///              write data.
    /// \param last  The end of the range of address to which the buffer_writer will
    ///              write data.
    buffer_writer (void * const first, void * const last)
            : writer_base<policy_type> (policy_type{first, last}) {}
    buffer_writer (buffer_writer const &) = delete;
    buffer_writer (buffer_writer &&) = delete;

    /// \brief Constructs the writer starting at the address given by 'first' and with a
    ///        number of bytes 'size'.
    /// \param first  The start address of the buffer to which buffer_writer will write
    ///               data.
    /// \param size   The size, in bytes, of the buffer pointed to by 'first'.
    buffer_writer (void * const first, std::size_t const size)
            : buffer_writer (static_cast<std::uint8_t *> (first),
                             static_cast<std::uint8_t *> (first) + size) {}

    /// \brief Constructs a buffer_writer from a pointer to allocated uninitialized
    /// storage.
    template <typename T>
    explicit buffer_writer (T * t)
            : buffer_writer (t, sizeof (T)) {}

    ~buffer_writer () noexcept override;

    buffer_writer & operator= (buffer_writer const &) = delete;
    buffer_writer & operator= (buffer_writer &&) = delete;

    using const_iterator = policy_type::const_iterator;

    /// Returns a const_iterator for the beginning of the byte vector managed by the
    /// object.
    const_iterator begin () const { return writer_policy ().begin (); }
    /// Returns a const_iterator for the end of the byte vector managed by the object.
    const_iterator end () const { return writer_policy ().end (); }
  };


  /// Writes the contents of a buffer_writer object to an ostream as a stream of
  /// space-separated two-digit hexadecimal values.
  ///
  /// \param os  The output stream to which output will be written.
  /// \param writer  The buffer_writer whose contents are to be dumped to the output
  ///                stream.
  /// \result  The value passed as the 'os' parameter.
  std::ostream & operator<< (std::ostream & os, buffer_writer const & writer);


  // ***************
  // *   n u l l   *
  // ***************

  namespace details {

    class null_policy {
    public:
      using result_type = void_type;
      template <typename Ty>
      auto put (Ty const &) -> result_type {
        return {};
      }
      template <typename SpanType>
      auto putn (SpanType) -> result_type {
        return {};
      }

      void flush () noexcept {}
    };

  } // end namespace details

  /// \brief An archive-writer which simply discards any data that it writes.
  /// write.
  class null final : public writer_base<details::null_policy> {
  public:
    null () = default;
    null (null const &) = delete;
    null (null &&) = delete;

    ~null () noexcept override;

    null & operator= (null const &) = delete;
    null & operator= (null &&) = delete;
  };


  // *******************************
  // *   r a n g e   r e a d e r   *
  // *******************************

  /// \brief An archive-reader which consumes data from an iterator.
  template <typename InputIterator>
  class range_reader {
    static_assert (sizeof (typename std::iterator_traits<InputIterator>::value_type) == 1,
                   "archive_reader reads from a byte-wide sequence");

  public:
    /// Constructs the writer using an input iterator.
    explicit range_reader (InputIterator first)
            : first_ (first) {}

    InputIterator iterator () { return first_; }

    /// Reads a single instance of a standard-layout type Ty from the input iterator and
    /// returns the value extracted.
    template <typename Ty>
    void get (Ty & v) {
      static_assert (std::is_standard_layout<Ty>::value,
                     "range_reader can only read standard-layout types");
      auto ptr = reinterpret_cast<std::uint8_t *> (&v);
      auto const * const last = ptr + sizeof (Ty);
      while (ptr != last) {
        *(ptr++) = *(first_++);
      }
    }

    template <typename SpanType>
    void getn (SpanType span) {
      using element_type = typename SpanType::element_type;
      static_assert (std::is_standard_layout<element_type>::value,
                     "range_reader can only read standard-layout types");
      auto out = reinterpret_cast<std::uint8_t *> (span.data ());
      auto const * const last = out + span.size_bytes ();
      while (out != last) {
        *(out++) = *(first_++);
      }
    }

  private:
    InputIterator first_; ///< The iterator from which data is read.
  };


  /// Constructs an archive-reader which will read from an iterator.
  ///
  /// \param first  The iterator from which bytes will be read.
  /// \result Returns an instance of range_reader which will consume bytes from the
  /// iterator.
  template <typename InputIterator>
  range_reader<InputIterator> make_reader (InputIterator first) {
    return range_reader<InputIterator>{first};
  }


  // *********************************
  // *   b u f f e r   r e a d e r   *
  // *********************************

  /// \brief An archive-reader which consumes data from a supplied pointer range.
  class buffer_reader {
  public:
    /// Constructs the writer using a pair of pointer to define the range [first, last).
    constexpr buffer_reader (void const * const first, void const * const last) noexcept
            : first_ (static_cast<std::uint8_t const *> (first))
            , last_ (static_cast<std::uint8_t const *> (last)) {}

    /// Constructs the writer using a pointer and size to define the range [first,
    /// first+size).
    constexpr buffer_reader (void const * const first, std::size_t const size) noexcept
            : first_ (static_cast<std::uint8_t const *> (first))
            , last_ (static_cast<std::uint8_t const *> (first) + size) {}

    /// Constructs the writer using a pointer and size to define the range [first,
    /// first+size).
    template <typename SpanType>
    explicit buffer_reader (SpanType const span) noexcept
            : first_ (reinterpret_cast<std::uint8_t const *> (span.data ()))
            , last_ (first_ + span.size_bytes ()) {}

    /// Reads a single instance of a standard-layout type T from the input iterator and
    /// returns the value extracted.
    template <typename T>
    T get () {
      std::remove_const_t<T> result;
      static_assert (std::is_standard_layout<T>::value,
                     "buffer_reader(T&) can only read standard-layout types");
      if (first_ + sizeof (T) > last_) {
        raise (std::errc::no_buffer_space, "Attempted to read past the end of a buffer.");
      }
      std::memcpy (&result, first_, sizeof (T));
      first_ += sizeof (T);
      return result;
    }

  private:
    std::uint8_t const * first_; ///< The start of the range from which data is read.
    std::uint8_t const * last_;  ///< The end of the range from which data is read.
  };

} // end namespace pstore::serialize::archive

#endif // PSTORE_SERIALIZE_ARCHIVE_HPP
