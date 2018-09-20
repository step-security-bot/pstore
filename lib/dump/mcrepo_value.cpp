//*                                                   _             *
//*  _ __ ___   ___ _ __ ___ _ __   ___   __   ____ _| |_   _  ___  *
//* | '_ ` _ \ / __| '__/ _ \ '_ \ / _ \  \ \ / / _` | | | | |/ _ \ *
//* | | | | | | (__| | |  __/ |_) | (_) |  \ V / (_| | | |_| |  __/ *
//* |_| |_| |_|\___|_|  \___| .__/ \___/    \_/ \__,_|_|\__,_|\___| *
//*                         |_|                                     *
//===- lib/dump/mcrepo_value.cpp ------------------------------------------===//
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
#include "pstore/dump/mcrepo_value.hpp"

#include "pstore/config/config.hpp"
#include "pstore/dump/db_value.hpp"
#include "pstore/dump/mcdebugline_value.hpp"
#include "pstore/dump/mcdisassembler_value.hpp"
#include "pstore/dump/value.hpp"
#include "pstore/core/db_archive.hpp"
#include "pstore/core/hamt_map.hpp"
#include "pstore/core/hamt_set.hpp"

namespace pstore {
    namespace dump {

        value_ptr make_value (pstore::repo::section_kind t) {
            char const * name = "*unknown*";
#define X(k)                                                                                       \
    case pstore::repo::section_kind::k: name = #k; break;

            switch (t) {
                PSTORE_MCREPO_SECTION_KINDS
            case pstore::repo::section_kind::last: break;
            }
            return pstore::dump::make_value (name);
#undef X
        }

        value_ptr make_value (repo::internal_fixup const & ifx) {
            auto v = std::make_shared<object> (object::container{
                {"section", make_value (ifx.section)},
                {"type", make_value (static_cast<std::uint64_t> (ifx.type))},
                {"offset", make_value (ifx.offset)},
                {"addend", make_value (ifx.addend)},
            });
            v->compact ();
            return v;
        }

        value_ptr make_value (database const & db, repo::external_fixup const & xfx) {
            using serialize::read;
            using serialize::archive::make_reader;

            return make_value (object::container{
                {"name", make_value (indirect_string::read (db, xfx.name))},
                {"type", make_value (static_cast<std::uint64_t> (xfx.type))},
                {"offset", make_value (xfx.offset)},
                {"addend", make_value (xfx.addend)},
            });
        }

        value_ptr make_value (database const & db, repo::generic_section const & section,
                              repo::section_kind sk, bool hex_mode) {

            (void) sk;
            auto const & data = section.data ();
            value_ptr data_value;
#if PSTORE_IS_INSIDE_LLVM
            if (sk == repo::section_kind::text) {
                std::uint8_t const * const first = data.data ();
                std::uint8_t const * const last = first + data.size ();
                data_value = make_disassembled_value (first, last, hex_mode);
            }
#endif
            if (!data_value) {
                if (hex_mode) {
                    data_value = std::make_shared<binary16> (std::begin (data), std::end (data));
                } else {
                    data_value = std::make_shared<binary> (std::begin (data), std::end (data));
                }
            }
            return make_value (object::container{
                {"align", make_value (section.align ())},
                {"data", data_value},
                {"ifixups",
                 make_value (std::begin (section.ifixups ()), std::end (section.ifixups ()))},
                {"xfixups",
                 make_value (
                     std::begin (section.xfixups ()), std::end (section.xfixups ()),
                     [&db](repo::external_fixup const & xfx) { return make_value (db, xfx); })},
            });
        }

        value_ptr make_value (database const & db, repo::dependents const & dependents,
                              repo::section_kind sk, bool hex_mode) {
            (void) sk;
            (void) hex_mode;
            return make_value (std::begin (dependents), std::end (dependents),
                               [&db](typed_address<repo::ticket_member> const & member) {
                                   return make_value (db, *db.getro (member));
                               });
        }

        value_ptr make_value (database const & db, repo::debug_line_section const & section,
                              repo::section_kind sk, bool hex_mode) {
            return make_value (object::container{
                {"header", make_value (section.header_extent ())},
                {"generic", make_value (db, section.generic (), sk, hex_mode)},
            });
        }


        value_ptr make_value (database const & db, repo::fragment const & fragment, bool hex_mode) {

#define X(k)                                                                                       \
    case repo::section_kind::k:                                                                    \
        contents = make_value (db, fragment.at<repo::section_kind::k> (), kind, hex_mode);         \
        break;

            array::container array;
            for (repo::section_kind const kind : fragment) {
                assert (fragment.has_section (kind));
                value_ptr contents;
                switch (kind) {
                    PSTORE_MCREPO_SECTION_KINDS
                case repo::section_kind::last: assert (false); break;
                }
                array.emplace_back (make_value (
                    object::container{{"type", make_value (kind)}, {"contents", contents}}));
            }
            return make_value (std::move (array));
#undef X
        }


        value_ptr make_fragments (database & db, bool hex_mode) {
            array::container result;
            if (std::shared_ptr<index::digest_index> const digests =
                    index::get_digest_index (db, false /* create */)) {

                array::container members;
                for (auto const & kvp : *digests) {
                    auto const fragment = repo::fragment::load (db, kvp.second);
                    result.emplace_back (make_value (
                        object::container{{"digest", make_value (kvp.first)},
                                          {"fragment", make_value (db, *fragment, hex_mode)}}));
                }
            }
            return make_value (result);
        }


        value_ptr make_value (repo::linkage_type t) {
#define X(a)                                                                                       \
    case (repo::linkage_type::a): Name = #a; break;

            char const * Name = "*unknown*";
            switch (t) { PSTORE_REPO_LINKAGE_TYPES }
            return make_value (Name);
#undef X
        }

        value_ptr make_value (database const & db, repo::ticket_member const & member) {
            using namespace serialize;
            using archive::make_reader;
            return make_value (object::container{
                {"digest", make_value (member.digest)},
                {"name", make_value (indirect_string::read (db, member.name))},
                {"linkage", make_value (member.linkage)},
            });
        }

        value_ptr make_value (database const & db, std::shared_ptr<repo::ticket const> ticket) {
            array::container members;
            members.reserve (ticket->size ());
            for (auto const & member : *ticket) {
                members.emplace_back (make_value (db, member));
            }

            using namespace serialize;
            using archive::make_reader;
            return make_value (object::container{
                {"members", make_value (members)},
                {"path", make_value (indirect_string::read (db, ticket->path ()))},
            });
        }

        value_ptr make_tickets (database & db) {
            array::container result;
            if (std::shared_ptr<index::ticket_index> const tickets =
                    index::get_ticket_index (db, false /* create */)) {

                for (auto const & kvp : *tickets) {
                    auto const ticket = repo::ticket::load (db, kvp.second);
                    result.emplace_back (make_value (object::container{
                        {"digest", make_value (kvp.first)}, {"ticket", make_value (db, ticket)}}));
                }
            }
            return make_value (result);
        }

        value_ptr make_value (database const & db,
                              pstore::index::debug_line_header_index::value_type const & value,
                              bool hex_mode) {
            auto const debug_line_header = db.getro (value.second);
            return make_value (object::container{
                {"digest", make_value (value.first)},
                {"debug_line_header",
                 make_debuglineheader_value (debug_line_header.get (),
                                             debug_line_header.get () + value.second.size,
                                             hex_mode)}});
        }

        value_ptr make_debug_line_headers (database & db, bool hex_mode) {
            array::container members;
            if (std::shared_ptr<index::debug_line_header_index> const headers =
                    index::get_debug_line_header_index (db, false /* create */)) {
                members.reserve (headers->size ());
                for (auto const & kvp : *headers) {
                    members.emplace_back (make_value (db, kvp, hex_mode));
                }
            }
            return make_value (members);
        }

    } // namespace dump
} // namespace pstore
