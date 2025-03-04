//===- include/pstore/core/hamt_set.hpp -------------------*- mode: C++ -*-===//
//*  _                     _              _    *
//* | |__   __ _ _ __ ___ | |_   ___  ___| |_  *
//* | '_ \ / _` | '_ ` _ \| __| / __|/ _ \ __| *
//* | | | | (_| | | | | | | |_  \__ \  __/ |_  *
//* |_| |_|\__,_|_| |_| |_|\__| |___/\___|\__| *
//*                                            *
//===----------------------------------------------------------------------===//
//
// Part of the pstore project, under the Apache License v2.0 with LLVM Exceptions.
// See https://github.com/paulhuggett/pstore/blob/master/LICENSE.txt for license
// information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file hamt_set.hpp

#ifndef PSTORE_CORE_HAMT_SET_HPP
#define PSTORE_CORE_HAMT_SET_HPP

// pstore includes
#include "pstore/core/hamt_map.hpp"

namespace pstore {
  namespace index {

    namespace details {
      class empty_class {};
    } // namespace details

    template <typename KeyType, typename Hash, typename KeyEqual>
    class hamt_set final : public index_base {
      using index_pointer = pstore::index::details::index_pointer;

      template <typename MapIterator>
      class set_iterator {
        static_assert (std::is_same_v<typename MapIterator::value_type::first_type,
                                      typename std::add_const_t<KeyType>>,
                       "hamt_set key type does not match the iterator key type");

      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = typename std::add_const_t<KeyType>;
        using difference_type = std::ptrdiff_t;
        using pointer = value_type *;
        using reference = value_type &;

        explicit set_iterator (MapIterator const & it)
                : it_ (it) {}

        bool operator== (set_iterator const & other) const { return it_ == other.it_; }
        bool operator!= (set_iterator const & other) const { return it_ != other.it_; }

        /// Dereference operator
        /// @return the value of the element to which this set_iterator is currently
        /// pointing
        reference operator* () const { return it_->first; }

        pointer operator->() const { return &it_->first; }

        /// Prefix increment
        set_iterator & operator++ () {
          ++it_;
          return *this;
        }
        /// Postfix increment operator (e.g., it++)
        set_iterator operator++ (int) {
          auto const old (*this);
          it_++;
          return old;
        }

        address get_address () const { return it_.get_address (); }

      private:
        MapIterator it_;
      };

    public:
      // types
      using key_type = KeyType;
      using value_type = key_type;
      using key_equal = KeyEqual;
      using hasher = Hash;
      using reference = value_type &;
      using const_reference = value_type const &;

      using const_iterator = set_iterator<
        typename hamt_map<value_type, details::empty_class, hasher, key_equal>::const_iterator>;
      using iterator = const_iterator;

      explicit hamt_set (database const & db,
                         typed_address<header_block> ip = typed_address<header_block>::null (),
                         hasher const & hash = hasher ())
              : map_ (db, ip, hash) {}

      /// \name Iterators
      ///@{

      range<database, hamt_set, iterator> make_range (database & db) { return {db, *this}; }
      range<database const, hamt_set const, const_iterator> make_range (database const & db) const {
        return {db, *this};
      }

      iterator begin (database & db) { return iterator{map_.begin (db)}; }
      const_iterator begin (database const & db) const { return const_iterator{map_.cbegin (db)}; }
      const_iterator cbegin (database const & db) const { return const_iterator{map_.cbegin (db)}; }

      iterator end (database & db) { return iterator{map_.end (db)}; }
      const_iterator end (database const & db) const { return const_iterator{map_.cend (db)}; }
      const_iterator cend (database const & db) const { return const_iterator{map_.cend (db)}; }
      ///@}

      /// \name Capacity
      ///@{

      /// \brief Checks whether the container is empty.
      bool empty () const { return map_.empty (); }

      /// \brief Returns the number of elements in the container.
      std::size_t size () const { return map_.size (); }
      ///@}

      /// \brief Inserts an element into the container, if the container doesn't already
      /// contain an element with an equivalent key.
      ///
      /// \tparam OtherKeyType  A type whose serialized representation is compatible with
      /// KeyType.
      /// \param transaction  The transaction into which the new value element will be
      /// inserted.
      /// \param key  Element value to insert.
      /// \returns A pair consisting of an iterator to the inserted element (or to the element
      /// that prevented the insertion) and a bool value set to true if the insertion took
      /// place.
      template <typename OtherKeyType, typename = typename std::enable_if_t<
                                         serialize::is_compatible_v<KeyType, OtherKeyType>>>
      std::pair<iterator, bool> insert (transaction_base & transaction, OtherKeyType const & key) {
        auto it = map_.insert (transaction, std::make_pair (key, details::empty_class ()));
        return {iterator{it.first}, it.second};
      }

      /// \brief Find the element with a specific key.
      /// Finds an element with key equivalent to \p key.
      ///
      /// \tparam OtherKeyType  A type whose serialized representation is compatible with
      /// KeyType.
      /// \param db A database to which the index belongs.
      /// \param key  The key value of the element to search for.
      /// \return Iterator an an element with key equivalent to \p key. If no such element
      /// is found, the past-the-end iterator is returned.
      template <typename OtherKeyType, typename = typename std::enable_if_t<
                                         serialize::is_compatible_v<KeyType, OtherKeyType>>>
      const_iterator find (database const & db, OtherKeyType const & key) const {
        return const_iterator{map_.find (db, key)};
      }

      /// Flush any modified index nodes to the store.
      ///
      /// \param transaction  The transaction to which the set will be written.
      /// \param generation The generation number to which the set will be written.
      /// \returns The address of the index root node.
      typed_address<header_block> flush (transaction_base & transaction, unsigned generation) {
        return map_.flush (transaction, generation);
      }

      /// \name Accessors
      /// Provide access to index internals.
      ///@{

      /// Read a leaf node from a store.
      value_type load_leaf (database const & db, address const addr) const {
        return map_.load_leaf (db, addr).first;
      }

      index_pointer root () const { return map_.root (); }
      ///@}

    private:
      hamt_map<value_type, details::empty_class, hasher, key_equal> map_;
    };

  } // namespace index

  namespace serialize {

    template <>
    struct serializer<index::details::empty_class> {
      using value_type = index::details::empty_class;

      template <typename Archive>
      static auto write (Archive && archive, value_type const &) -> archive_result_type<Archive> {
        // Tell the archiver to write an array of 0 elements. This should write
        // nothing at all but yield the location at which it would have gone (with the
        // correct type).
        auto const dummy = std::uint8_t{0};
        return serialize::write (std::forward<Archive> (archive), gsl::make_span (&dummy, &dummy));
      }

      template <typename Archive>
      static void read (Archive &&, value_type & value) {
        new (&value) value_type;
      }
    };

  } // namespace serialize

} // namespace pstore
#endif // PSTORE_CORE_HAMT_SET_HPP
