//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_STATIC_BUFFER_IPP
#define BEAST_IMPL_STATIC_BUFFER_IPP

#include <beast/core/detail/type_traits.hpp>
#include <boost/asio/buffer.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>

namespace beast {

class static_buffer::const_buffers_type
{
    std::size_t n_;
    std::uint8_t const* p_;

public:
    using value_type = boost::asio::const_buffer;

    class const_iterator;

    const_buffers_type() = delete;
    const_buffers_type(
        const_buffers_type const&) = default;
    const_buffers_type& operator=(
        const_buffers_type const&) = default;

    const_iterator
    begin() const;

    const_iterator
    end() const;

private:
    friend class static_buffer;

    const_buffers_type(
            std::uint8_t const* p, std::size_t n)
        : n_(n)
        , p_(p)
    {
    }
};

class static_buffer::const_buffers_type::const_iterator
{
    std::size_t n_ = 0;
    std::uint8_t const* p_ = nullptr;

public:
    using value_type = boost::asio::const_buffer;
    using pointer = value_type const*;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::bidirectional_iterator_tag;

    const_iterator() = default;
    const_iterator(const_iterator&& other) = default;
    const_iterator(const_iterator const& other) = default;
    const_iterator& operator=(const_iterator&& other) = default;
    const_iterator& operator=(const_iterator const& other) = default;

    bool
    operator==(const_iterator const& other) const
    {
        return p_ == other.p_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return value_type{p_, n_};
    }

    pointer
    operator->() const = delete;

    const_iterator&
    operator++()
    {
        p_ += n_;
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

    const_iterator&
    operator--()
    {
        p_ -= n_;
        return *this;
    }

    const_iterator
    operator--(int)
    {
        auto temp = *this;
        --(*this);
        return temp;
    }

private:
    friend class const_buffers_type;

    const_iterator(
            std::uint8_t const* p, std::size_t n)
        : n_(n)
        , p_(p)
    {
    }
};

inline
auto
static_buffer::const_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{p_, n_};
}

inline
auto
static_buffer::const_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{p_ + n_, n_};
}

//------------------------------------------------------------------------------

class static_buffer::mutable_buffers_type
{
    std::size_t n_;
    std::uint8_t* p_;

public:
    using value_type = boost::asio::mutable_buffer;

    class const_iterator;

    mutable_buffers_type() = delete;
    mutable_buffers_type(
        mutable_buffers_type const&) = default;
    mutable_buffers_type& operator=(
        mutable_buffers_type const&) = default;

    const_iterator
    begin() const;

    const_iterator
    end() const;

private:
    friend class static_buffer;

    mutable_buffers_type(
            std::uint8_t* p, std::size_t n)
        : n_(n)
        , p_(p)
    {
    }
};

class static_buffer::mutable_buffers_type::const_iterator
{
    std::size_t n_ = 0;
    std::uint8_t* p_ = nullptr;

public:
    using value_type = boost::asio::mutable_buffer;
    using pointer = value_type const*;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::bidirectional_iterator_tag;

    const_iterator() = default;
    const_iterator(const_iterator&& other) = default;
    const_iterator(const_iterator const& other) = default;
    const_iterator& operator=(const_iterator&& other) = default;
    const_iterator& operator=(const_iterator const& other) = default;

    bool
    operator==(const_iterator const& other) const
    {
        return p_ == other.p_;
    }

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const
    {
        return value_type{p_, n_};
    }

    pointer
    operator->() const = delete;

    const_iterator&
    operator++()
    {
        p_ += n_;
        return *this;
    }

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

    const_iterator&
    operator--()
    {
        p_ -= n_;
        return *this;
    }

    const_iterator
    operator--(int)
    {
        auto temp = *this;
        --(*this);
        return temp;
    }

private:
    friend class mutable_buffers_type;

    const_iterator(std::uint8_t* p, std::size_t n)
        : n_(n)
        , p_(p)
    {
    }
};

inline
auto
static_buffer::mutable_buffers_type::begin() const ->
    const_iterator
{
    return const_iterator{p_, n_};
}

inline
auto
static_buffer::mutable_buffers_type::end() const ->
    const_iterator
{
    return const_iterator{p_ + n_, n_};
}

//------------------------------------------------------------------------------

inline
auto
static_buffer::data() const ->
    const_buffers_type
{
    return const_buffers_type{in_,
        static_cast<std::size_t>(out_ - in_)};
}

inline
auto
static_buffer::prepare(std::size_t n) ->
    mutable_buffers_type
{
    if(n > static_cast<std::size_t>(end_ - out_))
        throw detail::make_exception<std::length_error>(
            "static_buffer overflow", __FILE__, __LINE__);
    last_ = out_ + n;
    return mutable_buffers_type{out_, n};
}

} // beast

#endif
