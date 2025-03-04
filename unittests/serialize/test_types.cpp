//===- unittests/serialize/test_types.cpp ---------------------------------===//
//*  _                          *
//* | |_ _   _ _ __   ___  ___  *
//* | __| | | | '_ \ / _ \/ __| *
//* | |_| |_| | |_) |  __/\__ \ *
//*  \__|\__, | .__/ \___||___/ *
//*      |___/|_|               *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "pstore/serialize/types.hpp"

// Standard library includes
#include <new>
#include <type_traits>
#include <vector>

// 3rd party includes
#include <gmock/gmock.h>

// pstore includes
#include "pstore/serialize/archive.hpp"

namespace {

  class NonIntrusiveSerializer : public ::testing::Test {
  public:
    struct non_standard_layout_type {
      explicit non_standard_layout_type (int a_) noexcept
              : a (a_) {}
      non_standard_layout_type (non_standard_layout_type const &) noexcept = default;
      virtual ~non_standard_layout_type () noexcept = default;

      bool operator== (non_standard_layout_type const & rhs) const noexcept { return a == rhs.a; }

      int a;
    };

    static_assert (!std::is_standard_layout<non_standard_layout_type>::value,
                   "expected non_standard_layout_type to be, well..., non-standard-layout");
  };

} // end anonymous namespace

namespace pstore::serialize {

  // A non-intrusive serializer for non_standard_layout_type
  template <>
  struct serializer<NonIntrusiveSerializer::non_standard_layout_type> {
    using value_type = NonIntrusiveSerializer::non_standard_layout_type;

    template <typename Archive>
    static auto write (Archive && archive, value_type const & p) -> archive_result_type<Archive> {
      return serialize::write (std::forward<Archive> (archive), p.a);
    }

    template <typename Archive>
    static void read (Archive && archive, value_type & out) {
      new (&out) value_type (serialize::read<int> (std::forward<Archive> (archive)));
    }
  };

} // end namespace pstore::serialize

TEST_F (NonIntrusiveSerializer, WriteAndRead) {
  non_standard_layout_type const expected{42};

  std::vector<std::uint8_t> bytes;
  pstore::serialize::archive::vector_writer writer (bytes);
  pstore::serialize::write (writer, expected);

  auto reader = pstore::serialize::archive::make_reader (std::begin (writer));
  auto actual = pstore::serialize::read<non_standard_layout_type> (reader);
  EXPECT_EQ (expected, actual);
  EXPECT_EQ (std::end (writer), reader.iterator ());
}

namespace {

  class SerializeSpanFallback : public ::testing::Test {
  public:
    struct simple_struct {
      int a;
    };

  protected:
    // This mock class is used to monitor the serializer's interaction with of
    // serializer<simple_struct>.
    class mock_fallback {
    public:
      using result_type = pstore::serialize::archive::void_type;
      MOCK_METHOD1 (write, result_type (simple_struct const &));
      MOCK_METHOD1 (read, void (simple_struct &));
    };
  };

} // end anonymous namespace

namespace pstore::serialize {

  template <>
  struct serializer<SerializeSpanFallback::simple_struct> {
    using value_type = SerializeSpanFallback::simple_struct;

    template <typename Archive>
    static auto write (Archive && archive, value_type const & p) -> archive_result_type<Archive> {
      return archive.write (p);
    }

    template <typename Archive>
    static void read (Archive && archive, value_type & out) {
      archive.read (out);
    }
  };

} // end namespace pstore::serialize

TEST_F (SerializeSpanFallback, Write) {
  using ::testing::_;
  using ::testing::Return;

  mock_fallback arch;
  std::array<simple_struct, 2> my;
  auto const ret = pstore::serialize::archive::void_type{};
  EXPECT_CALL (arch, write (_)).Times (2).WillRepeatedly (Return (ret));
  EXPECT_CALL (arch, read (_)).Times (0);
  pstore::serialize::write (arch, pstore::gsl::span<simple_struct>{my});
}
TEST_F (SerializeSpanFallback, Read) {
  using ::testing::_;

  mock_fallback arch;
  std::array<simple_struct, 2> arr;
  EXPECT_CALL (arch, write (_)).Times (0);
  EXPECT_CALL (arch, read (_)).Times (2);
  pstore::serialize::read (arch, pstore::gsl::span<simple_struct>{arr});
}

namespace {

  class SerializeSpan : public ::testing::Test {
  public:
    struct simple_struct {
      int a;
    };

  protected:
    // This mock class is used to monitor the serializer's interaction with of
    // serializer<simple_struct>.
    class mock_span_archive {
    public:
      using result_type = pstore::serialize::archive::void_type;
      MOCK_METHOD1 (writen, result_type (pstore::gsl::span<simple_struct>));
      MOCK_METHOD1 (readn, void (pstore::gsl::span<simple_struct>));
    };
  };

} // end anonymous namespace

namespace pstore::serialize {

  // Note that read() and write() are not implemented ensuring that if the
  // code wants to call it, then we'll get a compilation failure.
  template <>
  struct serializer<SerializeSpan::simple_struct> {
    using value_type = SerializeSpan::simple_struct;

    template <typename Archive, typename SpanType>
    static auto writen (Archive && mock, SpanType sp) -> archive_result_type<Archive> {
      return mock.writen (sp);
    }
    template <typename Archive, typename SpanType>
    static void readn (Archive && mock, SpanType sp) {
      mock.readn (sp);
    }
  };

} // namespace pstore::serialize

TEST_F (SerializeSpan, Write) {
  using ::testing::_;
  using ::testing::Return;

  SerializeSpan::mock_span_archive arch;

  auto const ret = pstore::serialize::archive::void_type{};
  EXPECT_CALL (arch, writen (_)).Times (1).WillOnce (Return (ret));
  EXPECT_CALL (arch, readn (_)).Times (0);

  std::array<simple_struct, 2> my;
  ::pstore::gsl::span<simple_struct> span{my};
  pstore::serialize::write (arch, span);
}
TEST_F (SerializeSpan, Read) {
  using ::testing::_;

  SerializeSpan::mock_span_archive arch;
  EXPECT_CALL (arch, writen (_)).Times (0);
  EXPECT_CALL (arch, readn (_)).Times (1);

  std::array<simple_struct, 2> my;
  pstore::serialize::read (arch, ::pstore::gsl::span<simple_struct>{my});
}


namespace {

  class ArchiveSpanFallback : public ::testing::Test {
  protected:
    // This mock class is used to monitor the serializer's use of
    // the archive type if the span methods are not implemented.
    class mock_fallback_policy {
    public:
      using result_type = pstore::serialize::archive::void_type;

      mock_fallback_policy () noexcept {}
      mock_fallback_policy (mock_fallback_policy const &) noexcept {}
      mock_fallback_policy (mock_fallback_policy &&) noexcept {}
      mock_fallback_policy & operator= (mock_fallback_policy const &) noexcept { return *this; }
      // mock_fallback_policy & operator= (mock_fallback_policy && ) noexcept { return *this;
      // }
      MOCK_METHOD1 (put, result_type (int const &));
      void flush () {}
    };

    class archive_type : public pstore::serialize::archive::writer_base<mock_fallback_policy> {
    public:
      MOCK_METHOD1 (get, void (int &));
    };
  };

} // end anonymous namespace

TEST_F (ArchiveSpanFallback, Write) {
  using ::testing::_;
  using ::testing::Return;

  archive_type archive;
  auto const ret = pstore::serialize::archive::void_type{};
  EXPECT_CALL (archive.writer_policy (), put (_)).Times (3).WillRepeatedly (Return (ret));
  EXPECT_CALL (archive, get (_)).Times (0);

  std::array<int, 3> arr;
  pstore::serialize::write (archive, ::pstore::gsl::span<int> (arr));
}
TEST_F (ArchiveSpanFallback, Read) {
  using ::testing::_;
  using ::testing::ContainerEq;
  using ::testing::Sequence;
  using ::testing::SetArgReferee;

  Sequence seq;

  archive_type archive;
  EXPECT_CALL (archive.writer_policy (), put (_)).Times (0);
  EXPECT_CALL (archive, get (_)).InSequence (seq).WillOnce (SetArgReferee<0> (13));
  EXPECT_CALL (archive, get (_)).InSequence (seq).WillOnce (SetArgReferee<0> (17));
  EXPECT_CALL (archive, get (_)).InSequence (seq).WillOnce (SetArgReferee<0> (19));

  std::array<int, 3> arr{{0, 0, 0}};
  pstore::serialize::read (archive, ::pstore::gsl::span<int>{arr});

  EXPECT_THAT (arr, ContainerEq (std::array<int, 3>{{13, 17, 19}}));
}


namespace {

  class ArchiveSpan : public ::testing::Test {
  protected:
    // This mock class is used to monitor the serializer's use of
    // the archive type if the span methods are not implemented.
    class mock_span_archive {
    public:
      using result_type = pstore::serialize::archive::void_type;

      MOCK_METHOD1 (put, result_type (int const &));
      MOCK_METHOD1 (get, void (int &));

      // The span methods. We can't mock the template function directly so calls
      // are forwarded to the mock (get/putn_mock) from the real thing (getn/putn).
      MOCK_METHOD1 (putn_mock, result_type (::pstore::gsl::span<int>));
      MOCK_METHOD1 (getn_mock, void (::pstore::gsl::span<int>));

      template <typename SpanType>
      result_type putn (SpanType span) {
        this->putn_mock (span);
        return {};
      }

      template <typename SpanType>
      void getn (SpanType span) {
        this->getn_mock (span);
      }
    };
  };

} // end anonymous namespace

TEST_F (ArchiveSpan, WriteSpan) {
  using ::testing::_;
  using ::testing::Return;

  mock_span_archive archive;
  auto const ret = pstore::serialize::archive::void_type{};
  EXPECT_CALL (archive, put (_)).Times (0);
  EXPECT_CALL (archive, putn_mock (_)).Times (1).WillOnce (Return (ret));
  EXPECT_CALL (archive, get (_)).Times (0);
  EXPECT_CALL (archive, getn_mock (_)).Times (0);

  std::array<int, 3> arr{{0}};
  pstore::serialize::write (archive, ::pstore::gsl::span<int> (arr));
}

// Writes a span containing a single element. This should be optimized to a write of the element
// and bypass the span-handling code.
TEST_F (ArchiveSpan, WriteSingleElementSpan) {
  using ::testing::_;
  using ::testing::Return;

  mock_span_archive archive;
  auto const ret = pstore::serialize::archive::void_type{};
  EXPECT_CALL (archive, put (_)).Times (1).WillOnce (Return (ret));
  EXPECT_CALL (archive, putn_mock (_)).Times (0);

  int a;
  pstore::serialize::write (archive, ::pstore::gsl::span<int, 1> (&a, 1));
}
TEST_F (ArchiveSpan, ReadSpan) {
  using ::testing::_;
  using ::testing::ContainerEq;
  using ::testing::Invoke;

  std::array<int, 3> const expected{{13, 17, 19}};

  mock_span_archive archive;
  EXPECT_CALL (archive, put (_)).Times (0);
  EXPECT_CALL (archive, putn_mock (_)).Times (0);
  EXPECT_CALL (archive, get (_)).Times (0);
  EXPECT_CALL (archive, getn_mock (_)).WillOnce (Invoke ([expected] (::pstore::gsl::span<int> sp) {
    std::copy (std::begin (expected), std::end (expected), std::begin (sp));
  }));

  std::array<int, 3> arr;
  pstore::serialize::read (archive, pstore::gsl::make_span (arr));
  EXPECT_THAT (arr, ContainerEq (expected));
}
TEST_F (ArchiveSpan, ReadSingleElementSpan) {
  using ::testing::_;
  using ::testing::SetArgReferee;

  mock_span_archive archive;
  EXPECT_CALL (archive, get (_)).WillOnce (SetArgReferee<0> (23));
  EXPECT_CALL (archive, getn_mock (_)).Times (0);

  std::array<int, 1> a;
  pstore::serialize::read (archive, ::pstore::gsl::make_span (a));
  EXPECT_EQ (a[0], 23);
}


#ifndef NDEBUG
namespace {

  class Flood : public ::testing::Test {
  protected:
    std::array<std::uint8_t, 5> buffer_{{0}};
    static constexpr std::array<std::uint8_t, 4> const expected_{
      {std::uint8_t{0xDE}, std::uint8_t{0xAD}, std::uint8_t{0xBE}, std::uint8_t{0xEF}}};
    static constexpr std::uint8_t zero_ = 0;
  };

} // end anonymous namespace

TEST_F (Flood, One) {
  pstore::serialize::flood (pstore::gsl::make_span (buffer_.data (), 1U));
  EXPECT_THAT (buffer_, ::testing::ElementsAre (expected_[0], zero_, zero_, zero_, zero_));
}
TEST_F (Flood, Two) {
  pstore::serialize::flood (pstore::gsl::make_span (buffer_.data (), 2U));
  EXPECT_THAT (buffer_, ::testing::ElementsAre (expected_[0], expected_[1], zero_, zero_, zero_));
}
TEST_F (Flood, Four) {
  pstore::serialize::flood (pstore::gsl::make_span (buffer_.data (), 4U));
  EXPECT_THAT (buffer_, ::testing::ElementsAre (expected_[0], expected_[1], expected_[2],
                                                expected_[3], zero_));
}
TEST_F (Flood, Five) {
  // Use the array version of make_span so that flood<> should be instantiated with a different
  // extent value.
  pstore::serialize::flood (pstore::gsl::make_span (buffer_));
  EXPECT_THAT (buffer_, ::testing::ElementsAre (expected_[0], expected_[1], expected_[2],
                                                expected_[3], expected_[0]));
}
#endif // NDEBUG
