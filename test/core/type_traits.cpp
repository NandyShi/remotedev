//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/type_traits.hpp>

#include <boost/asio/ip/tcp.hpp>

namespace beast {

namespace detail {

namespace {

//
// is_invocable
//

struct is_invocable_udt1
{
    void operator()(int) const;
};

struct is_invocable_udt2
{
    int operator()(int) const;
};

struct is_invocable_udt3
{
    int operator()(int);
};

#ifndef __INTELLISENSE__
// VFALCO Fails to compile with Intellisense
static_assert(is_invocable<
    is_invocable_udt1, void(int)>::value, "");

static_assert(! is_invocable<
    is_invocable_udt1, void(void)>::value, "");

static_assert(is_invocable<
    is_invocable_udt2, int(int)>::value, "");

static_assert(! is_invocable<
    is_invocable_udt2, int(void)>::value, "");

static_assert(! is_invocable<
    is_invocable_udt2, void(void)>::value, "");

static_assert(is_invocable<
    is_invocable_udt3, int(int)>::value, "");

static_assert(! is_invocable<
    is_invocable_udt3 const, int(int)>::value, "");
#endif

//
// get_lowest_layer
//

struct F1 {};
struct F2 {};

template<class F>
struct F3
{
    using next_layer_type =
        typename std::remove_reference<F>::type;

    using lowest_layer_type = typename
        get_lowest_layer<next_layer_type>::type;
};

template<class F>
struct F4
{
    using next_layer_type =
        typename std::remove_reference<F>::type;

    using lowest_layer_type = typename
        get_lowest_layer<next_layer_type>::type;
};

static_assert(! has_lowest_layer<F1>::value, "");
static_assert(! has_lowest_layer<F2>::value, "");
static_assert(has_lowest_layer<F3<F1>>::value, "");
static_assert(has_lowest_layer<F4<F3<F2>>>::value, "");

static_assert(std::is_same<
    get_lowest_layer<F1>::type, F1>::value, "");

static_assert(std::is_same<
    get_lowest_layer<F2>::type, F2>::value, "");

static_assert(std::is_same<
    get_lowest_layer<F3<F1>>::type, F1>::value, "");

static_assert(std::is_same<
    get_lowest_layer<F3<F2>>::type, F2>::value, "");

static_assert(std::is_same<
    get_lowest_layer<F4<F1>>::type, F1>::value, "");

static_assert(std::is_same<
    get_lowest_layer<F4<F2>>::type, F2>::value, "");

static_assert(std::is_same<
    get_lowest_layer<F4<F3<F1>>>::type, F1>::value, "");

static_assert(std::is_same<
    get_lowest_layer<F4<F3<F2>>>::type, F2>::value, "");

} // (anonymous)

} // detail

//
// buffer concepts
//

namespace {

struct T {};

static_assert(is_const_buffer_sequence<detail::ConstBufferSequence>::value, "");
static_assert(! is_const_buffer_sequence<T>::value, "");

static_assert(is_mutable_buffer_sequence<detail::MutableBufferSequence>::value, "");
static_assert(! is_mutable_buffer_sequence<T>::value, "");

} // (anonymous)

//
// handler concepts
//

namespace {

struct H
{
    void operator()(int);
};

} // anonymous

static_assert(is_completion_handler<H, void(int)>::value, "");
static_assert(! is_completion_handler<H, void(void)>::value, "");

//
// stream concepts
//

//namespace {

using stream_type = boost::asio::ip::tcp::socket;

static_assert(has_get_io_service<stream_type>::value, "");
static_assert(is_async_read_stream<stream_type>::value, "");
static_assert(is_async_write_stream<stream_type>::value, "");
static_assert(is_async_stream<stream_type>::value, "");
static_assert(is_sync_read_stream<stream_type>::value, "");
static_assert(is_sync_write_stream<stream_type>::value, "");
static_assert(is_sync_stream<stream_type>::value, "");

static_assert(! has_get_io_service<int>::value, "");
static_assert(! is_async_read_stream<int>::value, "");
static_assert(! is_async_write_stream<int>::value, "");
static_assert(! is_sync_read_stream<int>::value, "");
static_assert(! is_sync_write_stream<int>::value, "");

//} // (anonymous)

} // beast
