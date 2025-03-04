//===- unittests/adt/test_small_vector.cpp --------------------------------===//
//*                      _ _                  _              *
//*  ___ _ __ ___   __ _| | | __   _____  ___| |_ ___  _ __  *
//* / __| '_ ` _ \ / _` | | | \ \ / / _ \/ __| __/ _ \| '__| *
//* \__ \ | | | | | (_| | | |  \ V /  __/ (__| || (_) | |    *
//* |___/_| |_| |_|\__,_|_|_|   \_/ \___|\___|\__\___/|_|    *
//*                                                          *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "pstore/adt/small_vector.hpp"

#include <numeric>

#include <gmock/gmock.h>

TEST (SmallVector, DefaultCtor) {
  pstore::small_vector<int, 8> b;
  EXPECT_EQ (0U, b.size ()) << "expected the initial size to be number number of stack elements";
  EXPECT_EQ (8U, b.capacity ());
  EXPECT_TRUE (b.empty ());
}

TEST (SmallVector, ExplicitCtorLessThanStackBuffer) {
  pstore::small_vector<int, 8> const b (std::size_t{5});
  EXPECT_EQ (5U, b.size ());
  EXPECT_EQ (8U, b.capacity ());
  EXPECT_EQ (5U * sizeof (int), b.size_bytes ());
}

TEST (SmallVector, ExplicitCtor0) {
  pstore::small_vector<int, 8> const b (std::size_t{0});
  EXPECT_EQ (0U, b.size ());
  EXPECT_EQ (8U, b.capacity ());
  EXPECT_EQ (0U * sizeof (int), b.size_bytes ());
  EXPECT_TRUE (b.empty ());
}

TEST (SmallVector, ExplicitCtorGreaterThanStackBuffer) {
  pstore::small_vector<int, 8> const b (std::size_t{10});
  EXPECT_EQ (10U, b.size ());
  EXPECT_EQ (10U, b.capacity ());
  EXPECT_EQ (10 * sizeof (int), b.size_bytes ());
}

TEST (SmallVector, CtorInitializerList) {
  pstore::small_vector<int, 8> const b{1, 2, 3};
  EXPECT_EQ (3U, b.size ());
  EXPECT_EQ (8U, b.capacity ());
  EXPECT_THAT (b, ::testing::ElementsAre (1, 2, 3));
}

TEST (SmallVector, CtorInitializerList2) {
  pstore::small_vector<int, 2> b{1, 2, 3, 4};
  EXPECT_THAT (b, ::testing::ElementsAre (1, 2, 3, 4));
}

TEST (SmallVector, CtorCopy) {
  pstore::small_vector<int, 3> const b{3, 5};
  pstore::small_vector<int, 3> c = b;
  EXPECT_EQ (2U, c.size ());
  EXPECT_THAT (c, ::testing::ElementsAre (3, 5));
}

TEST (SmallVector, CtorCopy2) {
  pstore::small_vector<int, 3> const b{3, 5, 7, 11, 13};
  pstore::small_vector<int, 3> c = b;
  EXPECT_EQ (5U, c.size ());
  EXPECT_THAT (c, ::testing::ElementsAre (3, 5, 7, 11, 13));
}

TEST (SmallVector, MoveCtor) {
  pstore::small_vector<int, 4> a (std::size_t{4});
  std::iota (a.begin (), a.end (), 0); // fill with increasing values
  pstore::small_vector<int, 4> const b (std::move (a));

  EXPECT_THAT (b, ::testing::ElementsAre (0, 1, 2, 3));
}

TEST (SmallVector, AssignInitializerList) {
  pstore::small_vector<int, 3> b{1, 2, 3};
  b.assign ({4, 5, 6, 7});
  EXPECT_THAT (b, ::testing::ElementsAre (4, 5, 6, 7));
}

TEST (SmallVector, AssignCopy) {
  pstore::small_vector<int, 3> const b{5, 7};
  pstore::small_vector<int, 3> c;
  c = b;
  EXPECT_THAT (c, ::testing::ElementsAre (5, 7));
}

TEST (SmallVector, SizeAfterResizeLarger) {
  pstore::small_vector<int, 4> b (std::size_t{4});
  std::size_t const size{10};
  b.resize (size);
  EXPECT_EQ (size, b.size ());
  EXPECT_GE (size, b.capacity ()) << "expected capacity to be at least " << size
                                  << " (the container size)";
}

TEST (SmallVector, ContentsAfterResizeLarger) {
  constexpr auto orig_size = std::size_t{8};
  constexpr auto new_size = std::size_t{10};

  pstore::small_vector<int, orig_size> b (orig_size);
  std::iota (std::begin (b), std::end (b), 37);
  b.resize (new_size);
  ASSERT_EQ (b.size (), new_size);

  std::vector<int> actual;
  std::copy_n (std::begin (b), orig_size, std::back_inserter (actual));
  EXPECT_THAT (actual, ::testing::ElementsAre (37, 38, 39, 40, 41, 42, 43, 44));
}

TEST (SmallVector, SizeAfterResizeSmaller) {
  pstore::small_vector<int, 8> b (std::size_t{8});
  b.resize (5);
  EXPECT_EQ (5U, b.size ());
  EXPECT_EQ (8U, b.capacity ());
  EXPECT_FALSE (b.empty ());
}

TEST (SmallVector, SizeAfterResize0) {
  pstore::small_vector<int, 8> b (std::size_t{8});
  b.resize (0);
  EXPECT_EQ (0U, b.size ());
  EXPECT_EQ (8U, b.capacity ());
  EXPECT_TRUE (b.empty ());
}

TEST (SmallVector, DataAndConstDataMatch) {
  pstore::small_vector<int, 8> b (std::size_t{8});
  auto const * const bconst = &b;
  EXPECT_EQ (bconst->data (), b.data ());
}

TEST (SmallVector, IteratorNonConst) {
  pstore::small_vector<int, 4> buffer (std::size_t{4});

  // I populate the buffer manually here to ensure coverage of basic iterator
  // operations, but use std::iota() elsewhere to keep the tests simple.
  int value = 42;
  for (decltype (buffer)::iterator it = buffer.begin (), end = buffer.end (); it != end; ++it) {
    *it = value++;
  }

  {
    // Manually copy the contents of the buffer to a new vector.
    std::vector<int> actual;
    for (decltype (buffer)::iterator it = buffer.begin (), end = buffer.end (); it != end; ++it) {

      actual.push_back (*it);
    }
    EXPECT_THAT (actual, ::testing::ElementsAre (42, 43, 44, 45));
  }
}

TEST (SmallVector, IteratorConstFromNonConstContainer) {
  pstore::small_vector<int, 4> buffer (std::size_t{4});
  std::iota (buffer.begin (), buffer.end (), 42);

  {
    // Manually copy the contents of the buffer to a new vector but use a
    /// const iterator to do it this time.
    std::vector<int> actual;
    for (decltype (buffer)::const_iterator it = buffer.cbegin (), end = buffer.cend (); it != end;
         ++it) {

      actual.push_back (*it);
    }
    EXPECT_THAT (actual, ::testing::ElementsAre (42, 43, 44, 45));
  }
}

TEST (SmallVector, IteratorConstIteratorFromConstContainer) {
  pstore::small_vector<int, 4> buffer (std::size_t{4});
  std::iota (buffer.begin (), buffer.end (), 42);

  auto const & cbuffer = buffer;
  std::vector<int> const actual (cbuffer.begin (), cbuffer.end ());
  EXPECT_THAT (actual, ::testing::ElementsAre (42, 43, 44, 45));
}

TEST (SmallVector, IteratorNonConstReverse) {
  pstore::small_vector<int, 4> buffer (std::size_t{4});
  std::iota (buffer.begin (), buffer.end (), 42);

  {
    std::vector<int> const actual (buffer.rbegin (), buffer.rend ());
    EXPECT_THAT (actual, ::testing::ElementsAre (45, 44, 43, 42));
  }
  {
    std::vector<int> const actual (buffer.rcbegin (), buffer.rcend ());
    EXPECT_THAT (actual, ::testing::ElementsAre (45, 44, 43, 42));
  }
}

TEST (SmallVector, IteratorConstReverse) {
  // Wrap the buffer construction code in a lambda to hide the non-const
  // small_vector instance.
  auto const & cbuffer = [] () {
    pstore::small_vector<int, 4> buffer (std::size_t{4});
    std::iota (std::begin (buffer), std::end (buffer), 42); // fill with increasing values
    return buffer;
  }();

  std::vector<int> actual (cbuffer.rbegin (), cbuffer.rend ());
  EXPECT_THAT (actual, ::testing::ElementsAre (45, 44, 43, 42));
}

TEST (SmallVector, ElementAccess) {
  pstore::small_vector<int, 4> buffer (std::size_t{4});
  int count = 42;
  for (std::size_t index = 0, end = buffer.size (); index != end; ++index) {
    buffer[index] = count++;
  }

  std::array<int, 4> const expected{{42, 43, 44, 45}};
  EXPECT_TRUE (std::equal (std::begin (buffer), std::end (buffer), std::begin (expected)));
}

TEST (SmallVector, MoveSmall) {
  pstore::small_vector<int, 4> a (std::size_t{3});
  pstore::small_vector<int, 4> b (std::size_t{4});
  std::fill (std::begin (a), std::end (a), 0);
  std::fill (std::begin (b), std::end (b), 73);

  a = std::move (b);
  EXPECT_THAT (a, ::testing::ElementsAre (73, 73, 73, 73));
}

TEST (SmallVector, MoveLarge) {
  // The two containers start out with different sizes; one uses the small
  // buffer, the other, large.
  pstore::small_vector<int, 3> a (std::size_t{0});
  pstore::small_vector<int, 3> b (std::size_t{4});
  std::fill (std::begin (a), std::end (a), 0);
  std::fill (std::begin (b), std::end (b), 73);
  a = std::move (b);

  EXPECT_THAT (a, ::testing::ElementsAre (73, 73, 73, 73));
}

TEST (SmallVector, Clear) {
  // The two containers start out with different sizes; one uses the small
  // buffer, the other, large.
  pstore::small_vector<int> a (std::size_t{4});
  EXPECT_EQ (4U, a.size ());
  a.clear ();
  EXPECT_EQ (0U, a.size ());
}

TEST (SmallVector, PushBack) {
  using ::testing::ElementsAre;
  pstore::small_vector<int, 2> a;
  a.push_back (1);
  EXPECT_THAT (a, ElementsAre (1));
  a.push_back (2);
  EXPECT_THAT (a, ElementsAre (1, 2));
  a.push_back (3);
  EXPECT_THAT (a, ElementsAre (1, 2, 3));
  a.push_back (4);
  EXPECT_THAT (a, ElementsAre (1, 2, 3, 4));
}

TEST (SmallVector, EmplaceBack) {
  using ::testing::ElementsAre;
  pstore::small_vector<int, 2> a;
  a.emplace_back (1);
  EXPECT_THAT (a, ElementsAre (1));
  a.emplace_back (2);
  EXPECT_THAT (a, ElementsAre (1, 2));
  a.emplace_back (3);
  EXPECT_THAT (a, ElementsAre (1, 2, 3));
  a.emplace_back (4);
  EXPECT_THAT (a, ElementsAre (1, 2, 3, 4));
}

TEST (SmallVector, Back) {
  pstore::small_vector<int, 1> a;
  a.push_back (1);
  EXPECT_EQ (a.back (), 1);
  a.push_back (2);
  EXPECT_EQ (a.back (), 2);
}

TEST (SmallVector, AppendIteratorRange) {
  pstore::small_vector<int, 4> a (std::size_t{4});
  std::iota (std::begin (a), std::end (a), 0);

  std::array<int, 4> extra;
  std::iota (std::begin (extra), std::end (extra), 100);

  a.append (std::begin (extra), std::end (extra));

  EXPECT_THAT (a, ::testing::ElementsAre (0, 1, 2, 3, 100, 101, 102, 103));
}

TEST (SmallVector, CapacityReserve) {
  pstore::small_vector<int, 4> a;
  EXPECT_EQ (a.capacity (), 4U);
  a.reserve (1U);
  EXPECT_EQ (a.capacity (), 4U);
  a.reserve (10U);
  EXPECT_EQ (a.capacity (), 10U);
  a.reserve (1U);
  EXPECT_EQ (a.capacity (), 10U);
}

TEST (SmallVector, PopBack) {
  pstore::small_vector<int, 2> a{1, 2};
  a.pop_back ();
  EXPECT_THAT (a, testing::ElementsAre (1));
  a.pop_back ();
  EXPECT_TRUE (a.empty ());

  pstore::small_vector<int, 2> b{1, 2, 3};
  b.pop_back ();
  EXPECT_THAT (b, testing::ElementsAre (1, 2));
  b.pop_back ();
  EXPECT_THAT (b, testing::ElementsAre (1));
  b.pop_back ();
  EXPECT_TRUE (b.empty ());
}

template <typename TypeParam>
class SmallVectorErase : public testing::Test {
};

using Sizes = testing::Types<std::integral_constant<std::size_t, 2>,
                             std::integral_constant<std::size_t, 3>,
                             std::integral_constant<std::size_t, 4>>;
TYPED_TEST_SUITE (SmallVectorErase, Sizes, );


// NOLINTNEXTLINE
TYPED_TEST (SmallVectorErase, SinglePos) {
  pstore::small_vector<int, TypeParam::value> v{1, 2, 3};
  auto const l1 = v.erase (v.cbegin ());
  EXPECT_EQ (l1, v.begin ());
  EXPECT_THAT (v, testing::ElementsAre (2, 3));
  auto const l2 = v.erase (v.cbegin ());
  EXPECT_EQ (l2, v.begin ());
  EXPECT_THAT (v, testing::ElementsAre (3));
  auto const l3 = v.erase (v.cbegin ());
  EXPECT_EQ (l3, v.begin ());
  EXPECT_TRUE (v.empty ());
}
// NOLINTNEXTLINE
TYPED_TEST (SmallVectorErase, SingleSecondElement) {
  pstore::small_vector<int, TypeParam::value> v{1, 2, 3};
  auto const last = v.erase (v.begin () + 1);
  EXPECT_EQ (last, v.begin () + 1);
  EXPECT_THAT (v, testing::ElementsAre (1, 3));
}
// NOLINTNEXTLINE
TYPED_TEST (SmallVectorErase, SingleFinalElement) {
  pstore::small_vector<int, TypeParam::value> v{1, 2, 3};
  auto const last = v.erase (v.begin () + 2);
  EXPECT_EQ (last, v.begin () + 2);
  EXPECT_THAT (v, testing::ElementsAre (1, 2));
}
// NOLINTNEXTLINE
TYPED_TEST (SmallVectorErase, RangeAll) {
  pstore::small_vector<int, TypeParam::value> a{1, 2, 3};
  auto const last = a.erase (a.begin (), a.end ());
  EXPECT_EQ (last, a.end ());
  EXPECT_TRUE (a.empty ());
}
// NOLINTNEXTLINE
TYPED_TEST(SmallVectorErase, RangeFirstTwo) {
  pstore::small_vector<int, TypeParam::value> b{1, 2, 3};
  auto const first = b.begin ();
  auto const last = b.erase (first, first + 2);
  EXPECT_EQ (last, first);
  EXPECT_THAT (b, testing::ElementsAre (3));
}
// NOLINTNEXTLINE
TYPED_TEST(SmallVectorErase, RangeFirstOnly) {
  pstore::small_vector<int, TypeParam::value> b{1, 2, 3};
  auto const first = b.begin ();
  auto const last = b.erase (first, first + 1);
  EXPECT_EQ (last, first);
  EXPECT_THAT (b, testing::ElementsAre (2, 3));
}
// NOLINTNEXTLINE
TYPED_TEST(SmallVectorErase, RangeSecondToEnd) {
  pstore::small_vector<int, TypeParam::value> b{1, 2, 3};
  auto const first = b.begin () + 1;
  auto const last = b.erase (first, b.end ());
  EXPECT_EQ (last, first);
  EXPECT_THAT (b, testing::ElementsAre (1));
}
