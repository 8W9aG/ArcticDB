/* Copyright 2023 Man Group Operations Limited
 *
 * Use of this software is governed by the Business Source License 1.1 included in the file licenses/BSL.txt.
 *
 * As of the Change Date specified in that file, in accordance with the Business Source License, use of this software will be governed by the Apache License, version 2.0.
 */

#pragma once

#include <arcticdb/entity/key.hpp>
#include <arcticdb/entity/types.hpp>
#include <arcticdb/entity/index_range.hpp>
#include <arcticdb/util/string_utils.hpp>
#include <variant>
#include <fmt/format.h>
#include <string_view>

namespace arcticdb::entity {

class AtomKeyImpl {
  public:

    template<class IndexValueType>
    AtomKeyImpl(
        StreamId id,
        VersionId version_id,
        timestamp creation_ts,
        ContentHash content_hash,
        IndexValueType start_index,
        IndexValueType end_index,
        KeyType key_type)
        :
        id_(std::move(id)),
        version_id_(version_id),
        creation_ts_(creation_ts),
        content_hash_(content_hash),
        key_type_(key_type),
        index_start_(std::move(start_index)),
        index_end_(std::move(end_index)),
        str_() { }

    AtomKeyImpl() = default;
    AtomKeyImpl(const AtomKeyImpl &other) = default;
    AtomKeyImpl &operator=(const AtomKeyImpl &other) = default;
    AtomKeyImpl(AtomKeyImpl &&other) = default;
    AtomKeyImpl &operator=(AtomKeyImpl &&other) = default;

    const auto& id() const { return id_; }
    const auto& version_id() const { return version_id_; }
    const auto& gen_id() const { return version_id_; }
    const auto& creation_ts() const { return creation_ts_; }
    TimestampRange time_range() const { return {start_time(), end_time()}; }
    timestamp start_time() const { if (std::holds_alternative<timestamp>(index_start_)) return std::get<timestamp>(index_start_); else return 0LL; }
    timestamp end_time() const { if (std::holds_alternative<timestamp>(index_end_)) return std::get<timestamp>(index_end_); else return 0LL; }
    const auto& content_hash() const { return content_hash_; }
    const auto& type() const { return key_type_; }
    auto& type() { return key_type_; }
    const IndexValue &start_index() const { return index_start_; }
    const IndexValue &end_index() const { return index_end_; }
    IndexRange index_range() const { IndexRange ir = {index_start_, index_end_}; ir.end_closed_ = false; return ir;}

    auto change_type(KeyType new_type) {
        key_type_ = new_type;
        reset_cached();
    }

    /**
     * Useful for caching/replacing the ID with an existing shared instance.
     * @param id Will be moved.
     * @return The old id moved out.
     */
    StreamId change_id(StreamId id) {
        auto out = std::move(id_);
        id_ = std::move(id);
        reset_cached();
        return out;
    }

    friend bool operator==(const AtomKeyImpl &l, const AtomKeyImpl &r) {
        return l.version_id() == r.version_id()
            && l.creation_ts() == r.creation_ts()
            && l.content_hash() == r.content_hash()
            && l.start_index() == r.start_index()
            && l.end_index() == r.end_index()
            && l.type() == r.type()
            && l.id() == r.id();
    }

    friend bool operator!=(const AtomKeyImpl &l, const AtomKeyImpl &r) {
        return !(l == r);
    }

    friend bool operator<(const AtomKeyImpl &l, const AtomKeyImpl &r) {
        auto lt = std::tie(l.id_, l.version_id_, l.index_start_, l.index_end_, l.creation_ts_);
        auto rt = std::tie(r.id_, r.version_id_, r.index_start_, r.index_end_, r.creation_ts_);
        return lt < rt;
    }

    friend bool operator>(const AtomKeyImpl &l, const AtomKeyImpl &r) {
        return !(l < r) && (l != r);
    }

    size_t get_cached_hash() const {
        if (!hash_) {
            // arcticdb::commutative_hash_combine needs extra template specialisations for our variant types, folly's
            // built-in variant forwards to std::hash which should be good enough for these simple types
            hash_ = folly::hash::hash_combine(id_, version_id_, creation_ts_, content_hash_, key_type_, index_start_,
                    index_end_);
        }
        return *hash_;
    }

    void set_string() const {
        str_ = fmt::format("{}", *this);
    }

    std::string_view view() const { if(str_.empty()) set_string(); return {str_}; }

private:
    StreamId id_;
    VersionId version_id_ = 0;
    timestamp creation_ts_ = 0;
    ContentHash content_hash_ = 0;
    KeyType key_type_ = KeyType::UNDEFINED;
    IndexValue index_start_;
    IndexValue index_end_;
    mutable std::string str_; //TODO internalized string
    mutable std::optional<size_t> hash_;

    void reset_cached() {
        str_.clear();
        hash_.reset();
    }
};

/**
 * Builder introduced since I feel having a ctor for the key with 4 fields with the same type next
 * to each other is going to result in inverted fields making it difficult at call site
 * to see what's happening.
 * It might be avoided in perf critical situations.
 * @tparam StringViewable
 */
class AtomKeyBuilder {
  public:
    auto &version_id(VersionId v) {
        version_id_ = v;
        return *this;
    }

    auto &gen_id(VersionId v) {
        util::check_arg(version_id_ == 0, "Should not set both version_id and version id on a key");
        version_id_ = v;
        return *this;
    }

    auto &creation_ts(timestamp v) {
        creation_ts_ = v;
        return *this;
    }

    auto &string_index(const std::string &s) {
        index_start_ = s;
        return *this;
    }
    auto &start_index(const IndexValue &iv) {
        index_start_ = iv;
        return *this;
    }

    auto &end_index(const IndexValue &iv) {
        index_end_ = iv;
        return *this;
    }

    auto &content_hash(ContentHash v) {
        content_hash_ = v;
        return *this;
    }

    template<KeyType KT>
    AtomKeyImpl build(StreamId id) {
        return {
            std::move(id), version_id_, creation_ts_, content_hash_, index_start_, index_end_, KT
        };
    }

    AtomKeyImpl build(StreamId id, KeyType key_type) {
        return {
            std::move(id), version_id_, creation_ts_, content_hash_, index_start_, index_end_, key_type
        };
    }

  private:
    VersionId version_id_ = 0;
    arcticdb::entity::timestamp creation_ts_ = 0;
    ContentHash content_hash_ = 0;
    IndexValue index_start_;
    IndexValue index_end_;
};

using AtomKey = AtomKeyImpl;

// Aliases to aid implicit documentation of functions. To be made fully type-safe later:
/**
 * AtomKey that matches the is_index_key_type() check (i.e. TABLE_INDEX and equivalent).
 */
using IndexTypeKey = AtomKey;

inline auto atom_key_builder() {
    return AtomKeyBuilder{};
}

inline AtomKey null_key() {
    return atom_key_builder().build("", KeyType::UNDEFINED);
}

} // namespace arcticdb::entity


// The formatting below deals with the display of keys in logs etc., i.e. in a human-readable
// format. Transformation of keys for persistence is handled elsewhere.
namespace fmt {

using namespace arcticdb::entity;

template<class FormatTag>
struct formatter<FormattableRef < AtomKey, FormatTag>> {
template<typename ParseContext>
constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

template<typename FormatContext>
auto format(const FormattableRef <arcticdb::entity::AtomKey, FormatTag> &f, FormatContext &ctx) const {
    const auto &key = f.ref;
    return format_to(ctx.out(), FMT_STRING(FormatTag::format),
                    key.type(), key.id(), key.version_id(),
                     key.content_hash(), key.creation_ts(), tokenized_index(key.start_index()), tokenized_index(key.end_index()));
}

};

template<>
struct formatter<AtomKey> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const arcticdb::entity::AtomKey &key, FormatContext &ctx) const {
        formatter<FormattableRef < arcticdb::entity::AtomKey, DefaultAtomKeyFormat>>
        f;
        return f.format(formattable(key), ctx);
    }
};
}

namespace std {
template<>
struct hash<arcticdb::entity::AtomKeyImpl> {
    inline arcticdb::HashedValue operator()(const arcticdb::entity::AtomKeyImpl &k) const noexcept {
        return k.get_cached_hash();
    }
};
}
