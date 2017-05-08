//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_WRITE_HPP
#define BEAST_HTTP_WRITE_HPP

#include <beast/config.hpp>
#include <beast/core/consuming_buffers.hpp>
#include <beast/core/multi_buffer.hpp>
#include <beast/http/chunk_encode.hpp>
#include <beast/http/message.hpp>
#include <beast/core/error.hpp>
#include <beast/core/async_result.hpp>
#include <boost/variant.hpp>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

/** A chunk decorator which does nothing.

    When selected as a chunk decorator, objects of this type
    affect the output of messages specifying chunked
    transfer encodings as follows:

    @li chunk headers will have empty chunk extensions, and

    @li final chunks will have an empty set of trailers.

    @see @ref write_stream
*/
struct null_chunk_decorator
{
};

/** Provides stream-oriented HTTP message serialization functionality.

    Objects of this type may be used to perform synchronous or
    asynchronous serialization of an HTTP message on a stream.
    Unlike functions such as @ref write or @ref async_write,
    the stream operations provided here guarantee that bounded
    work will be performed. This is accomplished by making one
    or more calls to the underlying stream's `write_some` or
    `async_write_some` member functions. In order to fully
    serialize the message, multiple calls are required.

    The ability to incrementally serialize a message, peforming
    bounded work at each iteration is useful in many scenarios,
    such as:

    @li Setting consistent, per-call timeouts

    @li Efficiently relaying body content from another stream

    @li Performing application-layer flow control

    To use this class, construct an instance with the message
    to be sent. To make it easier to declare the type, the
    helper function @ref make_write_stream is provided:

    The implementation will automatically perform chunk encoding
    if the contents of the message indicate that chunk encoding
    is required. If the semantics of the message indicate that
    the connection should be closed after the message is sent,
    the error delivered from stream operations will be
    `boost::asio::error::eof`.

    @code
    template<class Stream>
    void send(Stream& stream, request<string_body> const& msg)
    {
        write_stream<true, string_body, fields> w{msg};
        do
        {
            w.write_some();
        }
        while(! w.is_done());
    }
    @endcode

    @see @ref make_write_stream
*/
template<
    bool isRequest, class Body, class Fields,
    class ChunkDecorator = null_chunk_decorator,
    class Allocator = std::allocator<char>
>
class write_stream
{
    template<class Stream, class Handler>
    class async_op;

    enum
    {
        do_init                =  0,
        do_header_only         =  2,
        do_header              =  4,
        do_body_read           =  6,
        do_body                =  8,
        
        do_chunked_init        = 10,
        do_chunked_header_only = 12,
        do_chunked_header      = 14,
        do_chunked_body_read   = 16,
        do_chunked_body        = 18,
        do_chunked_final       = 20,

        do_complete            = 22
    };

    using buffer_type =
        basic_multi_buffer<Allocator>;

    using is_deferred =
        typename Body::writer::is_deferred;

    using cb0_t = consuming_buffers<buffers_view<
        typename buffer_type::const_buffers_type,
        typename Body::writer::const_buffers_type>>;

    using cb1_t = consuming_buffers<
        typename Body::writer::const_buffers_type>;

    using ch0_t = consuming_buffers<buffers_view<
        typename buffer_type::const_buffers_type,
        detail::chunk_header,
        typename Body::writer::const_buffers_type,
        boost::asio::const_buffers_1>>;
    
    using ch1_t = consuming_buffers<buffers_view<
        detail::chunk_header,
        typename Body::writer::const_buffers_type,
        boost::asio::const_buffers_1>>;

    message<isRequest, Body, Fields> const& m_;
    typename Body::writer w_;
    buffer_type b_;
    bool chunked_;
    bool close_;
    int s_;
    boost::variant<boost::blank,
        cb0_t, cb1_t, ch0_t, ch1_t> cb_;
    bool more_;

public:
    /** Constructor

        @param msg The message to serialize. The message object
        must remain valid for the lifetime of the write stream.

        @param alloc An optional allocator to use.
    */
    explicit
    write_stream(message<isRequest, Body, Fields> const& msg,
        Allocator const& alloc = Allocator{});

    /** Return `true` if serialization is complete

        The operation is complete when all octets corresponding
        to the serialized representation of the message have been
        successfully delivered to the stream.
    */
    bool
    is_done() const
    {
        return s_ == do_complete;
    }

    /** Write some serialized message data to the stream.

        This function is used to write serialized message data to the
        stream. The function call will block until one of the following
        conditions is true:
        
        @li One or more bytes have been transferred.

        @li An error occurs on the stream.

        In order to completely serialize a message, this function
        should be called until @ref is_done returns `true`. If the
        semantics of the message indicate that the connection should
        be closed after the message is sent, the error delivered from
        this call will be `boost::asio::error::eof`.
    
        @param stream The stream to write to. This type must
        satisfy the requirements of @b SyncWriteStream.

        @throws system_error Thrown on failure.
    */
    template<class SyncWriteStream>
    void
    write_some(SyncWriteStream& stream);

    /** Write some serialized message data to the stream.

        This function is used to write serialized message data to the
        stream. The function call will block until one of the following
        conditions is true:
        
        @li One or more bytes have been transferred.

        @li An error occurs on the stream.

        In order to completely serialize a message, this function
        should be called until @ref is_done returns `true`. If the
        semantics of the message indicate that the connection should
        be closed after the message is sent, the error delivered from
        this call will be `boost::asio::error::eof`.

        @param stream The stream to write to. This type must
        satisfy the requirements of @b SyncWriteStream.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class SyncWriteStream>
    void
    write_some(SyncWriteStream& stream, error_code &ec);

    /** Start an asynchronous write of some serialized message data.

        This function is used to asynchronously write serialized
        message data to the stream. The function call always returns
        immediately. The asynchronous operation will continue until
        one of the following conditions is true:

        @li One or more bytes have been transferred.

        @li An error occurs on the stream.

        In order to completely serialize a message, this function
        should be called until @ref is_done returns `true`. If the
        semantics of the message indicate that the connection should
        be closed after the message is sent, the error delivered from
        this call will be `boost::asio::error::eof`.

        @param stream The stream to write to. This type must
        satisfy the requirements of @b SyncWriteStream.

        @param handler The handler to be called when the request
        completes. Copies will be made of the handler as required. The
        equivalent function signature of the handler must be:
        @code void handler(
            error_code const& ec    // Result of operation
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class AsyncWriteStream, class WriteHandler>
    async_return_type<WriteHandler, void(error_code)>
    async_write_some(AsyncWriteStream& stream,
        WriteHandler&& handler);
};

/** Return a stateful object to serialize an HTTP message.

    This convenience function makes it easier to declare
    the variable for a given message.
*/
template<bool isRequest, class Body, class Fields>
inline
write_stream<isRequest, Body, Fields>
make_write_stream(message<isRequest, Body, Fields> const& m)
{
    return write_stream<isRequest, Body, Fields>{m};
}

//------------------------------------------------------------------------------

/** Write a HTTP/1 header to a stream.

    This function is used to synchronously write a header to
    a stream. The call will block until one of the following
    conditions is true:

    @li The entire header is written.

    @li An error occurs.

    This operation is implemented in terms of one or more calls
    to the stream's `write_some` function.

    Regardless of the semantic meaning of the header (for example,
    specifying "Content-Length: 0" and "Connection: close"),
    this function will not return `boost::asio::error::eof`.

    @param stream The stream to which the data is to be written.
    The type must support the @b SyncWriteStream concept.

    @param msg The header to write.

    @throws system_error Thrown on failure.
*/
template<class SyncWriteStream,
    bool isRequest, class Fields>
void
write(SyncWriteStream& stream,
    header<isRequest, Fields> const& msg);

/** Write a HTTP/1 header to a stream.

    This function is used to synchronously write a header to
    a stream. The call will block until one of the following
    conditions is true:

    @li The entire header is written.

    @li An error occurs.

    This operation is implemented in terms of one or more calls
    to the stream's `write_some` function.

    Regardless of the semantic meaning of the header (for example,
    specifying "Content-Length: 0" and "Connection: close"),
    this function will not return `boost::asio::error::eof`.

    @param stream The stream to which the data is to be written.
    The type must support the @b SyncWriteStream concept.

    @param msg The header to write.

    @param ec Set to the error, if any occurred.
*/
template<class SyncWriteStream,
    bool isRequest, class Fields>
void
write(SyncWriteStream& stream,
    header<isRequest, Fields> const& msg,
        error_code& ec);

/** Write a HTTP/1 header asynchronously to a stream.

    This function is used to asynchronously write a header to
    a stream. The function call always returns immediately. The
    asynchronous operation will continue until one of the following
    conditions is true:

    @li The entire header is written.

    @li An error occurs.

    This operation is implemented in terms of one or more calls to
    the stream's `async_write_some` functions, and is known as a
    <em>composed operation</em>. The program must ensure that the
    stream performs no other write operations until this operation
    completes.

    Regardless of the semantic meaning of the header (for example,
    specifying "Content-Length: 0" and "Connection: close"),
    this function will not return `boost::asio::error::eof`.

    @param stream The stream to which the data is to be written.
    The type must support the @b AsyncWriteStream concept.

    @param msg The header to write. The object must remain valid
    at least until the completion handler is called; ownership is
    not transferred.

    @param handler The handler to be called when the operation
    completes. Copies will be made of the handler as required.
    The equivalent function signature of the handler must be:
    @code void handler(
        error_code const& error // result of operation
    ); @endcode
    Regardless of whether the asynchronous operation completes
    immediately or not, the handler will not be invoked from within
    this function. Invocation of the handler will be performed in a
    manner equivalent to using `boost::asio::io_service::post`.
*/
template<class AsyncWriteStream,
    bool isRequest, class Fields,
        class WriteHandler>
#if BEAST_DOXYGEN
    void_or_deduced
#else
async_return_type<
    WriteHandler, void(error_code)>
#endif
async_write(AsyncWriteStream& stream,
    header<isRequest, Fields> const& msg,
        WriteHandler&& handler);

//------------------------------------------------------------------------------

/** Write a HTTP/1 message to a stream.

    This function is used to write a message to a stream. The call
    will block until one of the following conditions is true:

    @li The entire message is written.

    @li An error occurs.

    This operation is implemented in terms of one or more calls
    to the stream's `write_some` function.

    The implementation will automatically perform chunk encoding if
    the contents of the message indicate that chunk encoding is required.
    If the semantics of the message indicate that the connection should
    be closed after the message is sent, the error thrown from this
    function will be `boost::asio::error::eof`.

    @param stream The stream to which the data is to be written.
    The type must support the @b SyncWriteStream concept.

    @param msg The message to write.

    @throws system_error Thrown on failure.
*/
template<class SyncWriteStream,
    bool isRequest, class Body, class Fields>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg);

/** Write a HTTP/1 message on a stream.

    This function is used to write a message to a stream. The call
    will block until one of the following conditions is true:

    @li The entire message is written.

    @li An error occurs.

    This operation is implemented in terms of one or more calls
    to the stream's `write_some` function.

    The implementation will automatically perform chunk encoding if
    the contents of the message indicate that chunk encoding is required.
    If the semantics of the message indicate that the connection should
    be closed after the message is sent, the error returned from this
    function will be `boost::asio::error::eof`.

    @param stream The stream to which the data is to be written.
    The type must support the @b SyncWriteStream concept.

    @param msg The message to write.

    @param ec Set to the error, if any occurred.
*/
template<class SyncWriteStream,
    bool isRequest, class Body, class Fields>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg,
        error_code& ec);

/** Write a HTTP/1 message asynchronously to a stream.

    This function is used to asynchronously write a message to
    a stream. The function call always returns immediately. The
    asynchronous operation will continue until one of the following
    conditions is true:

    @li The entire message is written.

    @li An error occurs.

    This operation is implemented in terms of one or more calls to
    the stream's `async_write_some` functions, and is known as a
    <em>composed operation</em>. The program must ensure that the
    stream performs no other write operations until this operation
    completes.

    The implementation will automatically perform chunk encoding if
    the contents of the message indicate that chunk encoding is required.
    If the semantics of the message indicate that the connection should
    be closed after the message is sent, the operation will complete with
    the error set to `boost::asio::error::eof`.

    @param stream The stream to which the data is to be written.
    The type must support the @b AsyncWriteStream concept.

    @param msg The message to write. The object must remain valid
    at least until the completion handler is called; ownership is
    not transferred.

    @param handler The handler to be called when the operation
    completes. Copies will be made of the handler as required.
    The equivalent function signature of the handler must be:
    @code void handler(
        error_code const& error // result of operation
    ); @endcode
    Regardless of whether the asynchronous operation completes
    immediately or not, the handler will not be invoked from within
    this function. Invocation of the handler will be performed in a
    manner equivalent to using `boost::asio::io_service::post`.
*/
template<class AsyncWriteStream,
    bool isRequest, class Body, class Fields,
        class WriteHandler>
async_return_type<
    WriteHandler, void(error_code)>
async_write(AsyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg,
        WriteHandler&& handler);

//------------------------------------------------------------------------------

/** Serialize a HTTP/1 header to a `std::ostream`.

    The function converts the header to its HTTP/1 serialized
    representation and stores the result in the output stream.

    @param os The output stream to write to.

    @param msg The message fields to write.
*/
template<bool isRequest, class Fields>
std::ostream&
operator<<(std::ostream& os,
    header<isRequest, Fields> const& msg);

/** Serialize a HTTP/1 message to a `std::ostream`.

    The function converts the message to its HTTP/1 serialized
    representation and stores the result in the output stream.

    The implementation will automatically perform chunk encoding if
    the contents of the message indicate that chunk encoding is required.

    @param os The output stream to write to.

    @param msg The message to write.
*/
template<bool isRequest, class Body, class Fields>
std::ostream&
operator<<(std::ostream& os,
    message<isRequest, Body, Fields> const& msg);

} // http
} // beast

#include <beast/http/impl/write.ipp>

#endif
