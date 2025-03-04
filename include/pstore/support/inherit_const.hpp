//===- include/pstore/support/inherit_const.hpp -----------*- mode: C++ -*-===//
//*  _       _               _ _                         _    *
//* (_)_ __ | |__   ___ _ __(_) |_    ___ ___  _ __  ___| |_  *
//* | | '_ \| '_ \ / _ \ '__| | __|  / __/ _ \| '_ \/ __| __| *
//* | | | | | | | |  __/ |  | | |_  | (_| (_) | | | \__ \ |_  *
//* |_|_| |_|_| |_|\___|_|  |_|\__|  \___\___/|_| |_|___/\__| *
//*                                                           *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file inherit_const.hpp
/// \brief  A utility template intended to simplify the writing of the const- and non-const variants
/// of member functions.
///
/// It's not uncommon for a class to supply const- and non-const variants of the same member
/// function where the constness of the return type changes according to the constness of the
/// function itself. For example, std::vector<> has:
///
///     reference operator[] (size_type pos);
///     const_reference operator[] (size_type pos) const;
///
/// Often the implementation of these two functions is identical: the constness is the only thing
/// changing. This encourages us to want to share the implementation because duplicating code is
/// almost always to be avoided. Writers typically adopt one of three solutions:
///
/// 1. Just duplicate the code and move on. For trivial one liners this is a perfectly good
/// solution. For anything more complex however, there's a risk that future changes will modify the
/// implementations and that they'll begin to diverge with time, which can result in new bugs being
/// introduced.
///
///     reference operator[] (size_type pos) {
///        return base_ + pos;
///     }
///     const_reference operator[] (size_type pos) const {
///        return base_ + pos;
///     }
///
/// 2. Write one of the functions in terms of the other and use const_cast<> to silence the compiler
/// errors. An attractive options since duplication is avoided completely. However, using
/// const_cast<> is potentially dangerous because by using it the code is really lying to the
/// compiler: the underlying data may be truly read-only even if the types no longer are. This can
/// be a new source of bugs.
///
///     reference operator[] (size_type pos) {
///        auto const * cthis = this;
///        return const_cast<reference> cthis->operator[] (pos);
///     }
///     const_reference operator[] (size_type pos) const {
///        return base_ + pos;
///     }
///
/// 3. Use macros to past the repeated implementation into the code. Happily, for most writers of
/// C++, this isn't really considered a viable option.
///
///     #define OPERATOR_INDEX(p)  (base_ + (p))
///     reference operator[] (size_type pos) {
///        return OPERATOR_INDEX(pos);
///     }
///     const_reference operator[] (size_type pos) const {
///        return OPERATOR_INDEX(pos);
///     }
///     #undef OPERATOR_INDEX
///
/// The utility here is intended to resolve some of these concerns. For true one-liner member
/// functions it is very likely overkill, but can be useful for implementations that are just
/// fractionally more complex. In the example below I've added an assertion to the shared
/// implementation.
///
///     class vector {
///     public:
///         ...
///         reference operator[] (size_type pos) {
///             return index_impl (*this, pos);
///         }
///         const_reference operator[] (size_type pos) const {
///             return index_impl (*this, pos);
///         }
///         ...
///     private:
///         template <typename Vector, typename ResultType = typename inherit_const<Vector,
///             reference>::type>
///         static ResultType index_impl (Vector & v, size_type pos) {
///             PSTORE_ASSERT (pos < v.size ());
///             return v.base_ + pos;
///         }
///     };

#ifndef PSTORE_SUPPORT_INHERIT_CONST_HPP
#define PSTORE_SUPPORT_INHERIT_CONST_HPP

#include <type_traits>

namespace pstore {

  /// Provides a member typedef inherit_const::type, which is defined as \p R const if
  /// \p T is a const type and \p R if \p T is non-const.
  ///
  /// \tparam T  A type whose constness will determine the constness of
  ///   inherit_const::type.
  /// \tparam R  The result type.
  /// \tparam RC  The const-result type.
  template <typename T, typename R, typename RC = R const>
  struct inherit_const {
    /// If \p T is const, \p R const otherwise \p R.
    using type = typename std::conditional_t<std::is_const_v<std::remove_reference_t<T>>, RC, R>;
  };

  template <typename T, typename R, typename RC = R const>
  using inherit_const_t = typename inherit_const<T, R, RC>::type;

  static_assert (std::is_same_v<inherit_const_t<int, bool>, bool>, "int -> bool");
  static_assert (std::is_same_v<inherit_const<int const, bool>::type, bool const>,
                 "int const -> bool const");
  static_assert (std::is_same_v<inherit_const_t<int &, bool>, bool>, "int& -> bool");
  static_assert (std::is_same_v<inherit_const<int const &, bool>::type, bool const>,
                 "int const & -> bool const");
  static_assert (std::is_same_v<inherit_const_t<int &&, bool>, bool>, "int && -> bool");
  static_assert (std::is_same_v<inherit_const_t<int const &&, bool>, bool const>,
                 "int const && -> bool const");

} // namespace pstore

#endif // PSTORE_SUPPORT_INHERIT_CONST_HPP
