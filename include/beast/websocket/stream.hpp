//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_STREAM_HPP
#define BEAST_WEBSOCKET_STREAM_HPP

#include <beast/config.hpp>
#include <beast/websocket/option.hpp>
#include <beast/websocket/detail/hybi13.hpp>
#include <beast/websocket/detail/stream_base.hpp>
#include <beast/http/message.hpp>
#include <beast/http/string_body.hpp>
#include <beast/core/async_result.hpp>
#include <beast/core/buffered_read_stream.hpp>
#include <beast/core/string_view.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <boost/asio.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace beast {
namespace websocket {

/// The type of object holding HTTP Upgrade requests
using request_type = http::request_header;

/// The type of object holding HTTP Upgrade responses
using response_type =
    //http::response_header;
    http::response<http::string_body, http::fields>;

/** Information about a WebSocket frame.

    This information is provided to callers during frame
    read operations.
*/
struct frame_info
{
    /// Indicates the type of message (binary or text).
    opcode op;

    /// `true` if this is the last frame in the current message.
    bool fin;
};

//--------------------------------------------------------------------

/** Provides message-oriented functionality using WebSocket.

    The @ref stream class template provides asynchronous and blocking
    message-oriented functionality necessary for clients and servers
    to utilize the WebSocket protocol.

    @par Thread Safety
    @e Distinct @e objects: Safe.@n
    @e Shared @e objects: Unsafe.
    
    For asynchronous operations, the application must ensure
    that they are are all performed within the same implicit
    or explicit strand.

    @par Example

    To use the @ref stream template with an `ip::tcp::socket`,
    you would write:

    @code
    websocket::stream<ip::tcp::socket> ws{io_service};
    @endcode
    Alternatively, you can write:
    @code
    ip::tcp::socket sock(io_service);
    websocket::stream<ip::tcp::socket&> ws(sock);
    @endcode

    @tparam NextLayer The type representing the next layer, to which
    data will be read and written during operations. For synchronous
    operations, the type must support the @b SyncStream concept.
    For asynchronous operations, the type must support the
    @b AsyncStream concept.

    @note A stream object must not be moved or destroyed while there
    are pending asynchronous operations associated with it.

    @par Concepts
        @b AsyncStream,
        @b DynamicBuffer,
        @b SyncStream
*/
template<class NextLayer>
class stream : public detail::stream_base
{
    friend class stream_test;

    buffered_read_stream<NextLayer, multi_buffer> stream_;

public:
    /// The type of the next layer.
    using next_layer_type =
        typename std::remove_reference<NextLayer>::type;

    /// The type of the lowest layer.
    using lowest_layer_type =
    #if BEAST_DOXYGEN
        implementation_defined;
    #else
        typename beast::detail::get_lowest_layer<
            next_layer_type>::type;
    #endif

    /** Move-construct a stream.

        If @c NextLayer is move constructible, this function
        will move-construct a new stream from the existing stream.

        @note The behavior of move assignment on or from streams
        with active or pending operations is undefined.
    */
    stream(stream&&) = default;

    /** Move assignment.

        If `NextLayer` is move constructible, this function
        will move-construct a new stream from the existing stream.

        @note The behavior of move assignment on or from streams
        with active or pending operations is undefined.
    */
    stream& operator=(stream&&) = default;

    /** Construct a WebSocket stream.

        This constructor creates a websocket stream and initializes
        the next layer object.

        @throws Any exceptions thrown by the NextLayer constructor.

        @param args The arguments to be passed to initialize the
        next layer object. The arguments are forwarded to the next
        layer's constructor.
    */
    template<class... Args>
    explicit
    stream(Args&&... args);

    /** Destructor.

        @note A stream object must not be destroyed while there
        are pending asynchronous operations associated with it.
    */
    ~stream() = default;

    /** Set options on the stream.

        The application must ensure that calls to set options
        are performed within the same implicit or explicit strand.

        @param args One or more stream options to set.
    */
#if BEAST_DOXYGEN
    template<class... Args>
    void
    set_option(Args&&... args)
#else
    template<class A1, class A2, class... An>
    void
    set_option(A1&& a1, A2&& a2, An&&... an)
#endif
    {
        set_option(std::forward<A1>(a1));
        set_option(std::forward<A2>(a2),
            std::forward<An>(an)...);
    }

    /// Set the automatic fragment size option
    void
    set_option(auto_fragment const& o)
    {
        wr_autofrag_ = o.value;
    }

    /// Set the outgoing message type
    void
    set_option(message_type const& o)
    {
        wr_opcode_ = o.value;
    }

    /// Set the permessage-deflate extension options
    void
    set_option(permessage_deflate const& o);

    /// Get the permessage-deflate extension options
    void
    get_option(permessage_deflate& o)
    {
        o = pmd_opts_;
    }

    /// Set the ping callback
    void
    set_option(ping_callback o)
    {
        ping_cb_ = std::move(o.value);
    }

    /// Set the read buffer size
    void
    set_option(read_buffer_size const& o)
    {
        rd_buf_size_ = o.value;
        // VFALCO What was the thinking here?
        //stream_.capacity(o.value);
    }

    /// Set the maximum incoming message size allowed
    void
    set_option(read_message_max const& o)
    {
        rd_msg_max_ = o.value;
    }

    /// Set the size of the write buffer
    void
    set_option(write_buffer_size const& o)
    {
        wr_buf_size_ = o.value;
    }

    /** Get the io_service associated with the stream.

        This function may be used to obtain the io_service object
        that the stream uses to dispatch handlers for asynchronous
        operations.

        @return A reference to the io_service object that the stream
        will use to dispatch handlers. Ownership is not transferred
        to the caller.
    */
    boost::asio::io_service&
    get_io_service()
    {
        return stream_.get_io_service();
    }

    /** Get a reference to the next layer.

        This function returns a reference to the next layer
        in a stack of stream layers.

        @return A reference to the next layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    next_layer_type&
    next_layer()
    {
        return stream_.next_layer();
    }

    /** Get a reference to the next layer.

        This function returns a reference to the next layer in a
        stack of stream layers.

        @return A reference to the next layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    next_layer_type const&
    next_layer() const
    {
        return stream_.next_layer();
    }

    /** Get a reference to the lowest layer.

        This function returns a reference to the lowest layer
        in a stack of stream layers.

        @return A reference to the lowest layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    lowest_layer_type&
    lowest_layer()
    {
        return stream_.lowest_layer();
    }

    /** Get a reference to the lowest layer.

        This function returns a reference to the lowest layer
        in a stack of stream layers.

        @return A reference to the lowest layer in the stack of
        stream layers. Ownership is not transferred to the caller.
    */
    lowest_layer_type const&
    lowest_layer() const
    {
        return stream_.lowest_layer();
    }

    /** Returns the close reason received from the peer.

        This is only valid after a read completes with error::closed.
    */
    close_reason const&
    reason() const
    {
        return cr_;
    }

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The call blocks
        until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @throws system_error Thrown on failure.
    */
    void
    accept();

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The call blocks
        until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

        @throws system_error Thrown on failure.
    */
    template<class ResponseDecorator>
    void
    accept_ex(ResponseDecorator const& decorator);

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The call blocks
        until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param ec Set to indicate what error occurred, if any.
    */
    void
    accept(error_code& ec);

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The call blocks
        until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

        @param ec Set to indicate what error occurred, if any.
    */
    template<class ResponseDecorator>
    void
    accept_ex(ResponseDecorator const& decorator,
        error_code& ec);

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The call blocks
        until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param buffers Caller provided data that has already been
        received on the stream. The implementation will copy the
        caller provided data before the function returns.

        @throws system_error Thrown on failure.
    */
    template<class ConstBufferSequence>
    void
    accept(ConstBufferSequence const& buffers);

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The call blocks
        until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param buffers Caller provided data that has already been
        received on the stream. The implementation will copy the
        caller provided data before the function returns.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

        @throws system_error Thrown on failure.
    */
    template<class ConstBufferSequence,
        class ResponseDecorator>
    void
    accept_ex(ConstBufferSequence const& buffers,
        ResponseDecorator const& decorator);

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The call blocks
        until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param buffers Caller provided data that has already been
        received on the stream. The implementation will copy the
        caller provided data before the function returns.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class ConstBufferSequence>
    void
    accept(ConstBufferSequence const& buffers, error_code& ec);

    /** Read and respond to a WebSocket HTTP Upgrade request.

        This function is used to synchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The call blocks
        until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param buffers Caller provided data that has already been
        received on the stream. The implementation will copy the
        caller provided data before the function returns.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

        @param ec Set to indicate what error occurred, if any.
    */
    template<class ConstBufferSequence,
        class ResponseDecorator>
    void
    accept_ex(ConstBufferSequence const& buffers,
        ResponseDecorator const& decorator,
            error_code& ec);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade.
        The call blocks until one of the following conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not
        access this object from other threads.

        @throws system_error Thrown on failure.
    */
    template<class Fields>
    void
    accept(http::header<true, Fields> const& req);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade.
        The call blocks until one of the following conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not
        access this object from other threads.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

        @throws system_error Thrown on failure.
    */
    template<class Fields, class ResponseDecorator>
    void
    accept_ex(http::header<true, Fields> const& req,
        ResponseDecorator const& decorator);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade.
        The call blocks until one of the following conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not
        access this object from other threads.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class Fields>
    void
    accept(http::header<true, Fields> const& req,
        error_code& ec);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade.
        The call blocks until one of the following conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not
        access this object from other threads.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

        @param ec Set to indicate what error occurred, if any.
    */
    template<class Fields, class ResponseDecorator>
    void
    accept_ex(http::header<true, Fields> const& req,
        ResponseDecorator const& decorator,
            error_code& ec);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade.
        The call blocks until one of the following conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not
        access this object from other threads.

        @param buffers Caller provided data that has already been
        received on the stream. This must not include the octets
        corresponding to the HTTP Upgrade request. The implementation
        will copy the caller provided data before the function returns.

        @throws system_error Thrown on failure.
    */
    template<class Fields, class ConstBufferSequence>
    void
    accept(http::header<true, Fields> const& req,
        ConstBufferSequence const& buffers);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade.
        The call blocks until one of the following conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not
        access this object from other threads.

        @param buffers Caller provided data that has already been
        received on the stream. This must not include the octets
        corresponding to the HTTP Upgrade request. The implementation
        will copy the caller provided data before the function returns.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

        @throws system_error Thrown on failure.
    */
    template<class Fields, class ConstBufferSequence,
        class ResponseDecorator>
    void
    accept_ex(http::header<true, Fields> const& req,
        ConstBufferSequence const& buffers,
            ResponseDecorator const& decorator);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade.
        The call blocks until one of the following conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not
        access this object from other threads.

        @param buffers Caller provided data that has already been
        received on the stream. This must not include the octets
        corresponding to the HTTP Upgrade request. The implementation
        will copy the caller provided data before the function returns.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class Fields, class ConstBufferSequence>
    void
    accept(http::header<true, Fields> const& req,
        ConstBufferSequence const& buffers, error_code& ec);

    /** Respond to a WebSocket HTTP Upgrade request

        This function is used to synchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade.
        The call blocks until one of the following conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to
        the next layer's `read_some` and `write_some` functions.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When this call returns, the stream is then ready to send and
        receive WebSocket protocol frames and messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not
        access this object from other threads.

        @param buffers Caller provided data that has already been
        received on the stream. This must not include the octets
        corresponding to the HTTP Upgrade request. The implementation
        will copy the caller provided data before the function returns.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

        @param ec Set to indicate what error occurred, if any.
    */
    template<class Fields, class ConstBufferSequence,
        class ResponseDecorator>
    void
    accept_ex(http::header<true, Fields> const& req,
        ConstBufferSequence const& buffers,
            ResponseDecorator const& decorator,
                error_code& ec);

    /** Start reading and responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The function call
        always returns immediately. The asynchronous operation will
        continue until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to
        the next layer's `async_read_some` and `async_write_some`
        functions, and is known as a <em>composed operation</em>. The
        program must ensure that the stream performs no other
        asynchronous operations until this operation completes.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When the completion handler is invoked, the stream is then
        ready to send and receive WebSocket protocol frames and
        messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure, and
        the completion handler will be invoked with a suitable error
        code set.

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
    template<class AcceptHandler>
    async_return_type<
        AcceptHandler, void(error_code)>
    async_accept(AcceptHandler&& handler);

    /** Start reading and responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The function call
        always returns immediately. The asynchronous operation will
        continue until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to
        the next layer's `async_read_some` and `async_write_some`
        functions, and is known as a <em>composed operation</em>. The
        program must ensure that the stream performs no other
        asynchronous operations until this operation completes.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When the completion handler is invoked, the stream is then
        ready to send and receive WebSocket protocol frames and
        messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure, and
        the completion handler will be invoked with a suitable error
        code set.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

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
    template<class ResponseDecorator, class AcceptHandler>
    async_return_type<
        AcceptHandler, void(error_code)>
    async_accept_ex(ResponseDecorator const& decorator,
        AcceptHandler&& handler);

    /** Start reading and responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The function call
        always returns immediately. The asynchronous operation will
        continue until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to
        the next layer's `async_read_some` and `async_write_some`
        functions, and is known as a <em>composed operation</em>. The
        program must ensure that the stream performs no other
        asynchronous operations until this operation completes.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When the completion handler is invoked, the stream is then
        ready to send and receive WebSocket protocol frames and
        messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure, and
        the completion handler will be invoked with a suitable error
        code set.

        @param buffers Caller provided data that has already been
        received on the stream. This may be used for implementations
        allowing multiple protocols on the same stream. The
        buffered data will first be applied to the handshake, and
        then to received WebSocket frames. The implementation will
        copy the caller provided data before the function returns.

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
    template<class ConstBufferSequence, class AcceptHandler>
    async_return_type<
        AcceptHandler, void(error_code)>
    async_accept(ConstBufferSequence const& buffers,
        AcceptHandler&& handler);

    /** Start reading and responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously read an HTTP WebSocket
        Upgrade request and send the HTTP response. The function call
        always returns immediately. The asynchronous operation will
        continue until one of the following conditions is true:

        @li The HTTP request finishes receiving, and the HTTP response
        finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to
        the next layer's `async_read_some` and `async_write_some`
        functions, and is known as a <em>composed operation</em>. The
        program must ensure that the stream performs no other
        asynchronous operations until this operation completes.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When the completion handler is invoked, the stream is then
        ready to send and receive WebSocket protocol frames and
        messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure, and
        the completion handler will be invoked with a suitable error
        code set.

        @param buffers Caller provided data that has already been
        received on the stream. This may be used for implementations
        allowing multiple protocols on the same stream. The
        buffered data will first be applied to the handshake, and
        then to received WebSocket frames. The implementation will
        copy the caller provided data before the function returns.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

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
    template<class ConstBufferSequence,
        class ResponseDecorator, class AcceptHandler>
    async_return_type<
        AcceptHandler, void(error_code)>
    async_accept_ex(ConstBufferSequence const& buffers,
        ResponseDecorator const& decorator,
            AcceptHandler&& handler);

    /** Start responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade
        request. The function call always returns immediately. The
        asynchronous operation will continue until one of the following
        conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to
        the next layer's `async_write_some` functions, and is known as
        a <em>composed operation</em>. The program must ensure that the
        stream performs no other operations until this operation
        completes.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When the completion handler is invoked, the stream is then
        ready to send and receive WebSocket protocol frames and
        messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure, and
        the completion handler will be invoked with a suitable error
        code set.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not access
        this object from other threads.

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
    template<class Fields, class AcceptHandler>
    async_return_type<
        AcceptHandler, void(error_code)>
    async_accept(http::header<true, Fields> const& req,
        AcceptHandler&& handler);

    /** Start responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade
        request. The function call always returns immediately. The
        asynchronous operation will continue until one of the following
        conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to
        the next layer's `async_write_some` functions, and is known as
        a <em>composed operation</em>. The program must ensure that the
        stream performs no other operations until this operation
        completes.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When the completion handler is invoked, the stream is then
        ready to send and receive WebSocket protocol frames and
        messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure, and
        the completion handler will be invoked with a suitable error
        code set.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not access
        this object from other threads.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

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
    template<class Fields,
        class ResponseDecorator, class AcceptHandler>
    async_return_type<
        AcceptHandler, void(error_code)>
    async_accept_ex(http::header<true, Fields> const& req,
        ResponseDecorator const& decorator,
            AcceptHandler&& handler);

    /** Start responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade
        request. The function call always returns immediately. The
        asynchronous operation will continue until one of the following
        conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to
        the next layer's `async_write_some` functions, and is known as
        a <em>composed operation</em>. The program must ensure that the
        stream performs no other operations until this operation
        completes.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When the completion handler is invoked, the stream is then
        ready to send and receive WebSocket protocol frames and
        messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure, and
        the completion handler will be invoked with a suitable error
        code set.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not access
        this object from other threads.

        @param buffers Caller provided data that has already been
        received on the stream. This may be used for implementations
        allowing multiple protocols on the same stream. The
        buffered data will first be applied to the handshake, and
        then to received WebSocket frames. The implementation will
        copy the caller provided data before the function returns.

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
    template<class Fields,
        class ConstBufferSequence, class AcceptHandler>
    async_return_type<
        AcceptHandler, void(error_code)>
    async_accept(http::header<true, Fields> const& req,
        ConstBufferSequence const& buffers,
            AcceptHandler&& handler);

    /** Start responding to a WebSocket HTTP Upgrade request.

        This function is used to asynchronously send the HTTP response
        to an HTTP request possibly containing a WebSocket Upgrade
        request. The function call always returns immediately. The
        asynchronous operation will continue until one of the following
        conditions is true:

        @li The HTTP response finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to
        the next layer's `async_write_some` functions, and is known as
        a <em>composed operation</em>. The program must ensure that the
        stream performs no other operations until this operation
        completes.

        If the stream receives a valid HTTP WebSocket Upgrade request,
        an HTTP response is sent back indicating a successful upgrade.
        When the completion handler is invoked, the stream is then
        ready to send and receive WebSocket protocol frames and
        messages.

        If the HTTP Upgrade request is invalid or cannot be satisfied,
        an HTTP response is sent indicating the reason and status code
        (typically 400, "Bad Request"). This counts as a failure, and
        the completion handler will be invoked with a suitable error
        code set.

        @param req An object containing the HTTP Upgrade request.
        Ownership is not transferred, the implementation will not access
        this object from other threads.

        @param buffers Caller provided data that has already been
        received on the stream. This may be used for implementations
        allowing multiple protocols on the same stream. The
        buffered data will first be applied to the handshake, and
        then to received WebSocket frames. The implementation will
        copy the caller provided data before the function returns.

        @param decorator A function object which will be called to modify
        the HTTP response object delivered by the implementation. This
        could be used to set the Server field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            response_type& res
        ); @endcode

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
    template<class Fields, class ConstBufferSequence,
        class ResponseDecorator, class AcceptHandler>
    async_return_type<
        AcceptHandler, void(error_code)>
    async_accept_ex(http::header<true, Fields> const& req,
        ConstBufferSequence const& buffers,
            ResponseDecorator const& decorator,
                AcceptHandler&& handler);

    /** Send an HTTP WebSocket Upgrade request and receive the response.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream

        This function is implemented in terms of one or more calls to the
        next layer's `read_some` and `write_some` functions.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param host The name of the remote host,
        required by the HTTP protocol.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol.

        @throws system_error Thrown on failure.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws{io_service};
        ...
        try
        {
            ws.handshake("localhost", "/");
        }
        catch(...)
        {
            // An error occurred.
        }
        @endcode
    */
    void
    handshake(string_view const& host,
        string_view const& target);

    /** Send an HTTP WebSocket Upgrade request and receive the response.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream

        This function is implemented in terms of one or more calls to the
        next layer's `read_some` and `write_some` functions.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param res The HTTP Upgrade response returned by the remote
        endpoint.

        @param host The name of the remote host,
        required by the HTTP protocol.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol.

        @throws system_error Thrown on failure.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws{io_service};
        ...
        try
        {
            response_type res;
            ws.handshake(res, "localhost", "/");
        }
        catch(...)
        {
            // An error occurred.
        }
        @endcode
    */
    void
    handshake(response_type& res,
        string_view const& host,
            string_view const& target);

    /** Send an HTTP WebSocket Upgrade request and receive the response.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream

        This function is implemented in terms of one or more calls to the
        next layer's `read_some` and `write_some` functions.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param host The name of the remote host,
        required by the HTTP protocol.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol.

        @param decorator A function object which will be called to modify
        the HTTP request object generated by the implementation. This
        could be used to set the User-Agent field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            request_type& req
        ); @endcode

        @throws system_error Thrown on failure.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws{io_service};
        ...
        try
        {
            ws.handshake("localhost", "/",
                [](request_type& req)
                {
                    req.fields.insert("User-Agent", "Beast");
                });
        }
        catch(...)
        {
            // An error occurred.
        }
        @endcode
    */
    template<class RequestDecorator>
    void
    handshake_ex(string_view const& host,
        string_view const& target,
            RequestDecorator const& decorator);

    /** Send an HTTP WebSocket Upgrade request and receive the response.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream

        This function is implemented in terms of one or more calls to the
        next layer's `read_some` and `write_some` functions.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param res The HTTP Upgrade response returned by the remote
        endpoint.

        @param host The name of the remote host,
        required by the HTTP protocol.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol.

        @param decorator A function object which will be called to modify
        the HTTP request object generated by the implementation. This
        could be used to set the User-Agent field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            request_type& req
        ); @endcode

        @throws system_error Thrown on failure.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws{io_service};
        ...
        try
        {
            response_type res;
            ws.handshake(res, "localhost", "/",
                [](request_type& req)
                {
                    req.fields.insert("User-Agent", "Beast");
                });
        }
        catch(...)
        {
            // An error occurred.
        }
        @endcode
    */
    template<class RequestDecorator>
    void
    handshake_ex(response_type& res,
        string_view const& host,
            string_view const& target,
                RequestDecorator const& decorator);

    /** Send an HTTP WebSocket Upgrade request and receive the response.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream

        This function is implemented in terms of one or more calls to the
        next layer's `read_some` and `write_some` functions.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param host The name of the remote host,
        required by the HTTP protocol.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol.

        @param ec Set to indicate what error occurred, if any.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws{io_service};
        ...
        error_code ec;
        ws.handshake(host, target, ec);
        if(ec)
        {
            // An error occurred.
        }
        @endcode
    */
    void
    handshake(string_view const& host,
        string_view const& target, error_code& ec);

    /** Send an HTTP WebSocket Upgrade request and receive the response.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream

        This function is implemented in terms of one or more calls to the
        next layer's `read_some` and `write_some` functions.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param host The name of the remote host,
        required by the HTTP protocol.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol.

        @param ec Set to indicate what error occurred, if any.

        @param res The HTTP Upgrade response returned by the remote
        endpoint. If `ec is set, the return value is undefined.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws{io_service};
        ...
        error_code ec;
        response_type res;
        ws.handshake(res, host, target, ec);
        if(ec)
        {
            // An error occurred.
        }
        @endcode
    */
    void
    handshake(response_type& res,
        string_view const& host,
            string_view const& target,
                error_code& ec);

    /** Send an HTTP WebSocket Upgrade request and receive the response.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream

        This function is implemented in terms of one or more calls to the
        next layer's `read_some` and `write_some` functions.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param host The name of the remote host,
        required by the HTTP protocol.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol.

        @param decorator A function object which will be called to modify
        the HTTP request object generated by the implementation. This
        could be used to set the User-Agent field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            request_type& req
        ); @endcode

        @param ec Set to indicate what error occurred, if any.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws{io_service};
        ...
        error_code ec;
        ws.handshake("localhost", "/",
            [](request_type& req)
            {
                req.fields.insert("User-Agent", "Beast");
            },
            ec);
        if(ec)
        {
            // An error occurred.
        }
        @endcode
    */
    template<class RequestDecorator>
    void
    handshake_ex(string_view const& host,
        string_view const& target,
            RequestDecorator const& decorator,
                error_code& ec);

    /** Send an HTTP WebSocket Upgrade request and receive the response.

        This function is used to synchronously send the WebSocket
        upgrade HTTP request. The call blocks until one of the
        following conditions is true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream

        This function is implemented in terms of one or more calls to the
        next layer's `read_some` and `write_some` functions.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param res The HTTP Upgrade response returned by the remote
        endpoint.

        @param host The name of the remote host,
        required by the HTTP protocol.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol.

        @param decorator A function object which will be called to modify
        the HTTP request object generated by the implementation. This
        could be used to set the User-Agent field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            request_type& req
        ); @endcode

        @param ec Set to indicate what error occurred, if any.

        @par Example
        @code
        websocket::stream<ip::tcp::socket> ws{io_service};
        ...
        error_code ec;
        response_type res;
        ws.handshake(res, "localhost", "/",
            [](request_type& req)
            {
                req.fields.insert("User-Agent", "Beast");
            },
            ec);
        if(ec)
        {
            // An error occurred.
        }
        @endcode
    */
    template<class RequestDecorator>
    void
    handshake_ex(response_type& res,
        string_view const& host,
            string_view const& target,
                RequestDecorator const& decorator,
                    error_code& ec);

    /** Start an asynchronous operation to send an upgrade request and receive the response.

        This function is used to asynchronously send the HTTP WebSocket
        upgrade request and receive the HTTP WebSocket Upgrade response.
        This function call always returns immediately. The asynchronous
        operation will continue until one of the following conditions is
        true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_read_some` and `async_write_some` functions, and
        is known as a <em>composed operation</em>. The program must ensure
        that the stream performs no other operations until this operation
        completes.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param host The name of the remote host, required by
        the HTTP protocol. Copies may be made as needed.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol. Copies of this parameter may
        be made as needed.

        @param handler The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& ec    // Result of operation
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class HandshakeHandler>
    async_return_type<
        HandshakeHandler, void(error_code)>
    async_handshake(string_view const& host,
        string_view const& target,
            HandshakeHandler&& handler);

    /** Start an asynchronous operation to send an upgrade request and receive the response.

        This function is used to asynchronously send the HTTP WebSocket
        upgrade request and receive the HTTP WebSocket Upgrade response.
        This function call always returns immediately. The asynchronous
        operation will continue until one of the following conditions is
        true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_read_some` and `async_write_some` functions, and
        is known as a <em>composed operation</em>. The program must ensure
        that the stream performs no other operations until this operation
        completes.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param res The HTTP Upgrade response returned by the remote
        endpoint. The caller must ensure this object is valid for at
        least until the completion handler is invoked.

        @param host The name of the remote host, required by
        the HTTP protocol. Copies may be made as needed.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol. Copies of this parameter may
        be made as needed.

        @param handler The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& ec     // Result of operation
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class HandshakeHandler>
    async_return_type<
        HandshakeHandler, void(error_code)>
    async_handshake(response_type& res,
        string_view const& host,
            string_view const& target,
                HandshakeHandler&& handler);

    /** Start an asynchronous operation to send an upgrade request and receive the response.

        This function is used to asynchronously send the HTTP WebSocket
        upgrade request and receive the HTTP WebSocket Upgrade response.
        This function call always returns immediately. The asynchronous
        operation will continue until one of the following conditions is
        true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_read_some` and `async_write_some` functions, and
        is known as a <em>composed operation</em>. The program must ensure
        that the stream performs no other operations until this operation
        completes.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param host The name of the remote host, required by
        the HTTP protocol. Copies may be made as needed.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol. Copies of this parameter may
        be made as needed.

        @param decorator A function object which will be called to modify
        the HTTP request object generated by the implementation. This
        could be used to set the User-Agent field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            request_type& req
        ); @endcode

        @param handler The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& ec     // Result of operation
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class RequestDecorator, class HandshakeHandler>
    async_return_type<
        HandshakeHandler, void(error_code)>
    async_handshake_ex(string_view const& host,
        string_view const& target,
            RequestDecorator const& decorator,
                HandshakeHandler&& handler);

    /** Start an asynchronous operation to send an upgrade request and receive the response.

        This function is used to asynchronously send the HTTP WebSocket
        upgrade request and receive the HTTP WebSocket Upgrade response.
        This function call always returns immediately. The asynchronous
        operation will continue until one of the following conditions is
        true:

        @li A HTTP request finishes sending and an HTTP response finishes
        receiving.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_read_some` and `async_write_some` functions, and
        is known as a <em>composed operation</em>. The program must ensure
        that the stream performs no other operations until this operation
        completes.

        The operation is successful if the received HTTP response indicates
        a successful HTTP Upgrade (represented by a Status-Code of 101,
        "switching protocols").

        @param res The HTTP Upgrade response returned by the remote
        endpoint. The caller must ensure this object is valid for at
        least until the completion handler is invoked.

        @param host The name of the remote host, required by
        the HTTP protocol. Copies may be made as needed.

        @param target The Request Target, which may not be empty,
        required by the HTTP protocol. Copies of this parameter may
        be made as needed.

        @param decorator A function object which will be called to modify
        the HTTP request object generated by the implementation. This
        could be used to set the User-Agent field, subprotocols, or other
        application or HTTP specific fields. The object will be called
        with this equivalent signature:
        @code void decorator(
            request_type& req
        ); @endcode

        @param handler The handler to be called when the request completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& ec     // Result of operation
        ); @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class RequestDecorator, class HandshakeHandler>
    async_return_type<
        HandshakeHandler, void(error_code)>
    async_handshake_ex(response_type& res,
        string_view const& host,
            string_view const& target,
                RequestDecorator const& decorator,
                    HandshakeHandler&& handler);

    /** Send a WebSocket close frame.

        This function is used to synchronously send a close frame on
        the stream. The call blocks until one of the following is true:

        @li The close frame finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls
        to the next layer's `write_some` functions.

        If the close reason specifies a close code other than
        @ref beast::websocket::close_code::none, the close frame is
        sent with the close code and optional reason string. Otherwise,
        the close frame is sent with no payload.

        Callers should not attempt to write WebSocket data after
        initiating the close. Instead, callers should continue
        reading until an error occurs. A read returning @ref error::closed
        indicates a successful connection closure.

        @param cr The reason for the close.

        @throws system_error Thrown on failure.
    */
    void
    close(close_reason const& cr);

    /** Send a WebSocket close frame.

        This function is used to synchronously send a close frame on
        the stream. The call blocks until one of the following is true:

        @li The close frame finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls
        to the next layer's `write_some` functions.

        If the close reason specifies a close code other than
        @ref beast::websocket::close_code::none, the close frame is
        sent with the close code and optional reason string. Otherwise,
        the close frame is sent with no payload.

        Callers should not attempt to write WebSocket data after
        initiating the close. Instead, callers should continue
        reading until an error occurs. A read returning @ref error::closed
        indicates a successful connection closure.

        @param cr The reason for the close.

        @param ec Set to indicate what error occurred, if any.
    */
    void
    close(close_reason const& cr, error_code& ec);

    /** Start an asynchronous operation to send a WebSocket close frame.

        This function is used to asynchronously send a close frame on
        the stream. This function call always returns immediately. The
        asynchronous operation will continue until one of the following
        conditions is true:

        @li The close frame finishes sending.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_write_some` functions, and is known as a
        <em>composed operation</em>. The program must ensure that the
        stream performs no other write operations (such as @ref async_ping,
        @ref stream::async_write, @ref stream::async_write_frame, or
        @ref stream::async_close) until this operation completes.

        If the close reason specifies a close code other than
        @ref beast::websocket::close_code::none, the close frame is
        sent with the close code and optional reason string. Otherwise,
        the close frame is sent with no payload.

        Callers should not attempt to write WebSocket data after
        initiating the close. Instead, callers should continue
        reading until an error occurs. A read returning @ref error::closed
        indicates a successful connection closure.

        @param cr The reason for the close.

        @param handler The handler to be called when the close operation
        completes. Copies will be made of the handler as required. The
        function signature of the handler must be:
        @code
        void handler(
            error_code const& ec     // Result of operation
        );
        @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class CloseHandler>
    async_return_type<
        CloseHandler, void(error_code)>
    async_close(close_reason const& cr, CloseHandler&& handler);

    /** Send a WebSocket ping frame.

        This function is used to synchronously send a ping frame on
        the stream. The call blocks until one of the following is true:

        @li The ping frame finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to the
        next layer's `write_some` functions.

        @param payload The payload of the ping message, which may be empty.

        @throws system_error Thrown on failure.
    */
    void
    ping(ping_data const& payload);

    /** Send a WebSocket ping frame.

        This function is used to synchronously send a ping frame on
        the stream. The call blocks until one of the following is true:

        @li The ping frame finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to the
        next layer's `write_some` functions.

        @param payload The payload of the ping message, which may be empty.

        @param ec Set to indicate what error occurred, if any.
    */
    void
    ping(ping_data const& payload, error_code& ec);

    /** Start an asynchronous operation to send a WebSocket ping frame.

        This function is used to asynchronously send a ping frame to
        the stream. The function call always returns immediately. The
        asynchronous operation will continue until one of the following
        is true:

        @li The entire ping frame is sent.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_write_some` functions, and is known as a
        <em>composed operation</em>. The program must ensure that the
        stream performs no other writes until this operation completes.

        If a close frame is sent or received before the ping frame is
        sent, the completion handler will be called with the error
        set to `boost::asio::error::operation_aborted`.

        @param payload The payload of the ping message, which may be empty.

        @param handler The handler to be called when the read operation
        completes. Copies will be made of the handler as required. The
        function signature of the handler must be:
        @code
        void handler(
            error_code const& ec     // Result of operation
        );
        @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class WriteHandler>
    async_return_type<
        WriteHandler, void(error_code)>
    async_ping(ping_data const& payload, WriteHandler&& handler);

    /** Send a WebSocket pong frame.

        This function is used to synchronously send a pong frame on
        the stream. The call blocks until one of the following is true:

        @li The pong frame finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to the
        next layer's `write_some` functions.

        The WebSocket protocol allows pong frames to be sent from either
        end at any time. It is not necessary to first receive a ping in
        order to send a pong. The remote peer may use the receipt of a
        pong frame as an indication that the connection is not dead.

        @param payload The payload of the pong message, which may be empty.

        @throws system_error Thrown on failure.
    */
    void
    pong(ping_data const& payload);

    /** Send a WebSocket pong frame.

        This function is used to synchronously send a pong frame on
        the stream. The call blocks until one of the following is true:

        @li The pong frame finishes sending.

        @li An error occurs on the stream.

        This function is implemented in terms of one or more calls to the
        next layer's `write_some` functions.

        The WebSocket protocol allows pong frames to be sent from either
        end at any time. It is not necessary to first receive a ping in
        order to send a pong. The remote peer may use the receipt of a
        pong frame as an indication that the connection is not dead.

        @param payload The payload of the pong message, which may be empty.

        @param ec Set to indicate what error occurred, if any.
    */
    void
    pong(ping_data const& payload, error_code& ec);

    /** Start an asynchronous operation to send a WebSocket pong frame.

        This function is used to asynchronously send a pong frame to
        the stream. The function call always returns immediately. The
        asynchronous operation will continue until one of the following
        is true:

        @li The entire pong frame is sent.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_write_some` functions, and is known as a
        <em>composed operation</em>. The program must ensure that the
        stream performs no other writes until this operation completes.

        The WebSocket protocol allows pong frames to be sent from either
        end at any time. It is not necessary to first receive a ping in
        order to send a pong. The remote peer may use the receipt of a
        pong frame as an indication that the connection is not dead.

        If a close frame is sent or received before the pong frame is
        sent, the completion handler will be called with the error
        set to `boost::asio::error::operation_aborted`.

        @param payload The payload of the pong message, which may be empty.

        @param handler The handler to be called when the read operation
        completes. Copies will be made of the handler as required. The
        function signature of the handler must be:
        @code
        void handler(
            error_code const& ec     // Result of operation
        );
        @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class WriteHandler>
    async_return_type<
        WriteHandler, void(error_code)>
    async_pong(ping_data const& payload, WriteHandler&& handler);

    /** Read a message from the stream.

        This function is used to synchronously read a message from
        the stream. The call blocks until one of the following is true:

        @li A complete message is received.

        @li An error occurs on the stream.

        This call is implemented in terms of one or more calls to the
        stream's `read_some` and `write_some` operations.

        Upon a success, op is set to either binary or text depending on
        the message type, and the input area of the stream buffer will
        hold all the message payload bytes (which may be zero in length).

        During reads, the implementation handles control frames as
        follows:

        @li A pong frame is sent when a ping frame is received.

        @li The @ref ping_callback is invoked when a ping frame
            or pong frame is received.

        @li The WebSocket close procedure is started if a close frame
            is received. In this case, the operation will eventually
            complete with the error set to @ref error::closed.

        @param op A value to receive the message type.
        This object must remain valid until the handler is called.

        @param buffer A dynamic buffer to hold the message data after
        any masking or decompression has been applied.

        @throws system_error Thrown on failure.
    */
    template<class DynamicBuffer>
    void
    read(opcode& op, DynamicBuffer& buffer);

    /** Read a message from the stream.

        This function is used to synchronously read a message from
        the stream. The call blocks until one of the following is true:

        @li A complete message is received.

        @li An error occurs on the stream.

        This call is implemented in terms of one or more calls to the
        stream's `read_some` and `write_some` operations.

        Upon a success, op is set to either binary or text depending on
        the message type, and the input area of the stream buffer will
        hold all the message payload bytes (which may be zero in length).

        During reads, the implementation handles control frames as
        follows:

        @li The @ref ping_callback is invoked when a ping frame
            or pong frame is received.

        @li A pong frame is sent when a ping frame is received.

        @li The WebSocket close procedure is started if a close frame
            is received. In this case, the operation will eventually
            complete with the error set to @ref error::closed.

        @param op A value to receive the message type.
        This object must remain valid until the handler is called.

        @param buffer A dynamic buffer to hold the message data after
        any masking or decompression has been applied.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class DynamicBuffer>
    void
    read(opcode& op, DynamicBuffer& buffer, error_code& ec);

    /** Start an asynchronous operation to read a message from the stream.

        This function is used to asynchronously read a message from
        the stream. The function call always returns immediately. The
        asynchronous operation will continue until one of the following
        is true:

        @li A complete message is received.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_read_some` and `async_write_some` functions,
        and is known as a <em>composed operation</em>. The program must
        ensure that the stream performs no other reads until this operation
        completes.

        Upon a success, op is set to either binary or text depending on
        the message type, and the input area of the stream buffer will
        hold all the message payload bytes (which may be zero in length).

        During reads, the implementation handles control frames as
        follows:

        @li The @ref ping_callback is invoked when a ping frame
            or pong frame is received.

        @li A pong frame is sent when a ping frame is received.

        @li The WebSocket close procedure is started if a close frame
            is received. In this case, the operation will eventually
            complete with the error set to @ref error::closed.

        Because of the need to handle control frames, read operations
        can cause writes to take place. These writes are managed
        transparently; callers can still have one active asynchronous
        read and asynchronous write operation pending simultaneously
        (a user initiated call to @ref async_close counts as a write).

        @param op A value to receive the message type.
        This object must remain valid until the handler is called.

        @param buffer A dynamic buffer to hold the message data after
        any masking or decompression has been applied. This object must
        remain valid until the handler is called.

        @param handler The handler to be called when the read operation
        completes. Copies will be made of the handler as required. The
        function signature of the handler must be:
        @code
        void handler(
            error_code const& ec     // Result of operation
        );
        @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class DynamicBuffer, class ReadHandler>
    async_return_type<
        ReadHandler, void(error_code)>
    async_read(opcode& op, DynamicBuffer& buffer, ReadHandler&& handler);

    /** Read a message frame from the stream.

        This function is used to synchronously read a single message
        frame from the stream. The call blocks until one of the following
        is true:

        @li A complete frame is received.

        @li An error occurs on the stream.

        This call is implemented in terms of one or more calls to the
        stream's `read_some` and `write_some` operations.

        Upon success, `fi` is filled out to reflect the message payload
        contents. `op` is set to binary or text, and the `fin` flag
        indicates if all the message data has been read in. To read the
        entire message, callers should keep calling @ref read_frame
        until `fi.fin == true`. A message with no payload will have
        `fi.fin == true`, and zero bytes placed into the stream buffer.

        During reads, the implementation handles control frames as
        follows:

        @li The @ref ping_callback is invoked when a ping frame
            or pong frame is received.

        @li A pong frame is sent when a ping frame is received.

        @li The WebSocket close procedure is started if a close frame
            is received. In this case, the operation will eventually
            complete with the error set to @ref error::closed.

        @param fi An object to store metadata about the message.

        @param buffer A dynamic buffer to hold the message data after
        any masking or decompression has been applied.

        @throws system_error Thrown on failure.
    */
    template<class DynamicBuffer>
    void
    read_frame(frame_info& fi, DynamicBuffer& buffer);

    /** Read a message frame from the stream.

        This function is used to synchronously read a single message
        frame from the stream. The call blocks until one of the following
        is true:

        @li A complete frame is received.

        @li An error occurs on the stream.

        This call is implemented in terms of one or more calls to the
        stream's `read_some` and `write_some` operations.

        Upon success, `fi` is filled out to reflect the message payload
        contents. `op` is set to binary or text, and the `fin` flag
        indicates if all the message data has been read in. To read the
        entire message, callers should keep calling @ref read_frame
        until `fi.fin == true`. A message with no payload will have
        `fi.fin == true`, and zero bytes placed into the stream buffer.

        During reads, the implementation handles control frames as
        follows:

        @li The @ref ping_callback is invoked when a ping frame
            or pong frame is received.

        @li A pong frame is sent when a ping frame is received.

        @li The WebSocket close procedure is started if a close frame
            is received. In this case, the operation will eventually
            complete with the error set to @ref error::closed.

        @param fi An object to store metadata about the message.

        @param buffer A dynamic buffer to hold the message data after
        any masking or decompression has been applied.

        @param ec Set to indicate what error occurred, if any.
    */
    template<class DynamicBuffer>
    void
    read_frame(frame_info& fi, DynamicBuffer& buffer, error_code& ec);

    /** Start an asynchronous operation to read a message frame from the stream.

        This function is used to asynchronously read a single message
        frame from the websocket. The function call always returns
        immediately. The asynchronous operation will continue until
        one of the following conditions is true:

        @li A complete frame is received.

        @li An error occurs on the stream.

        This operation is implemented in terms of one or more calls to the
        next layer's `async_read_some` and `async_write_some` functions,
        and is known as a <em>composed operation</em>. The program must
        ensure that the stream performs no other reads until this operation
        completes.

        Upon a successful completion, `fi` is filled out to reflect the
        message payload contents. `op` is set to binary or text, and the
        `fin` flag indicates if all the message data has been read in.
        To read the entire message, callers should keep calling
        @ref read_frame until `fi.fin == true`. A message with no payload
        will have `fi.fin == true`, and zero bytes placed into the stream
        buffer.

        During reads, the implementation handles control frames as
        follows:

        @li The @ref ping_callback is invoked when a ping frame
            or pong frame is received.

        @li A pong frame is sent when a ping frame is received.

        @li The WebSocket close procedure is started if a close frame
            is received. In this case, the operation will eventually
            complete with the error set to @ref error::closed.

        Because of the need to handle control frames, read operations
        can cause writes to take place. These writes are managed
        transparently; callers can still have one active asynchronous
        read and asynchronous write operation pending simultaneously
        (a user initiated call to @ref async_close counts as a write).

        @param fi An object to store metadata about the message.
        This object must remain valid until the handler is called.

        @param buffer A dynamic buffer to hold the message data after
        any masking or decompression has been applied. This object must
        remain valid until the handler is called.

        @param handler The handler to be called when the read operation
        completes. Copies will be made of the handler as required. The
        function signature of the handler must be:
        @code
        void handler(
            error_code const& ec     // Result of operation
        );
        @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using boost::asio::io_service::post().
    */
    template<class DynamicBuffer, class ReadHandler>
    async_return_type<
        ReadHandler, void(error_code)>
    async_read_frame(frame_info& fi,
        DynamicBuffer& buffer, ReadHandler&& handler);

    /** Write a message to the stream.

        This function is used to synchronously write a message to
        the stream. The call blocks until one of the following conditions
        is met:

        @li The entire message is sent.

        @li An error occurs.

        This operation is implemented in terms of one or more calls to the
        next layer's `write_some` function.

        The current setting of the @ref message_type option controls
        whether the message opcode is set to text or binary. If the
        @ref auto_fragment option is set, the message will be split
        into one or more frames as necessary. The actual payload contents
        sent may be transformed as per the WebSocket protocol settings.

        @param buffers The buffers containing the entire message
        payload. The implementation will make copies of this object
        as needed, but ownership of the underlying memory is not
        transferred. The caller is responsible for ensuring that
        the memory locations pointed to by buffers remains valid
        until the completion handler is called.

        @throws system_error Thrown on failure.

        @note This function always sends an entire message. To
        send a message in fragments, use @ref write_frame.
    */
    template<class ConstBufferSequence>
    void
    write(ConstBufferSequence const& buffers);

    /** Write a message to the stream.

        This function is used to synchronously write a message to
        the stream. The call blocks until one of the following conditions
        is met:

        @li The entire message is sent.

        @li An error occurs.

        This operation is implemented in terms of one or more calls to the
        next layer's `write_some` function.

        The current setting of the @ref message_type option controls
        whether the message opcode is set to text or binary. If the
        @ref auto_fragment option is set, the message will be split
        into one or more frames as necessary. The actual payload contents
        sent may be transformed as per the WebSocket protocol settings.

        @param buffers The buffers containing the entire message
        payload. The implementation will make copies of this object
        as needed, but ownership of the underlying memory is not
        transferred. The caller is responsible for ensuring that
        the memory locations pointed to by buffers remains valid
        until the completion handler is called.

        @param ec Set to indicate what error occurred, if any.

        @throws system_error Thrown on failure.

        @note This function always sends an entire message. To
        send a message in fragments, use @ref write_frame.
    */
    template<class ConstBufferSequence>
    void
    write(ConstBufferSequence const& buffers, error_code& ec);

    /** Start an asynchronous operation to write a message to the stream.

        This function is used to asynchronously write a message to
        the stream. The function call always returns immediately.
        The asynchronous operation will continue until one of the
        following conditions is true:

        @li The entire message is sent.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the next layer's `async_write_some` functions, and is known
        as a <em>composed operation</em>. The program must ensure that
        the stream performs no other write operations (such as
        stream::async_write, stream::async_write_frame, or
        stream::async_close).

        The current setting of the @ref message_type option controls
        whether the message opcode is set to text or binary. If the
        @ref auto_fragment option is set, the message will be split
        into one or more frames as necessary. The actual payload contents
        sent may be transformed as per the WebSocket protocol settings.

        @param buffers The buffers containing the entire message
        payload. The implementation will make copies of this object
        as needed, but ownership of the underlying memory is not
        transferred. The caller is responsible for ensuring that
        the memory locations pointed to by buffers remains valid
        until the completion handler is called.

        @param handler The handler to be called when the write operation
        completes. Copies will be made of the handler as required. The
        function signature of the handler must be:
        @code
        void handler(
            error_code const& ec     // Result of operation
        );
        @endcode
        Regardless of whether the asynchronous operation completes
        immediately or not, the handler will not be invoked from within
        this function. Invocation of the handler will be performed in a
        manner equivalent to using `boost::asio::io_service::post`.
    */
    template<class ConstBufferSequence, class WriteHandler>
    async_return_type<
        WriteHandler, void(error_code)>
    async_write(ConstBufferSequence const& buffers,
        WriteHandler&& handler);

    /** Write partial message data on the stream.

        This function is used to write some or all of a message's
        payload to the stream. The call will block until one of the
        following conditions is true:

        @li A frame is sent.

        @li Message data is transferred to the write buffer.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the stream's `write_some` function.

        If this is the beginning of a new message, the message opcode
        will be set to text or binary as per the current setting of
        the @ref message_type option. The actual payload sent
        may be transformed as per the WebSocket protocol settings.

        @param fin `true` if this is the last frame in the message.

        @param buffers The input buffer sequence holding the data to write.

        @return The number of bytes consumed in the input buffers.

        @throws system_error Thrown on failure.
    */
    template<class ConstBufferSequence>
    void
    write_frame(bool fin, ConstBufferSequence const& buffers);

    /** Write partial message data on the stream.

        This function is used to write some or all of a message's
        payload to the stream. The call will block until one of the
        following conditions is true:

        @li A frame is sent.

        @li Message data is transferred to the write buffer.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the stream's `write_some` function.

        If this is the beginning of a new message, the message opcode
        will be set to text or binary as per the current setting of
        the @ref message_type option. The actual payload sent
        may be transformed as per the WebSocket protocol settings.

        @param fin `true` if this is the last frame in the message.

        @param buffers The input buffer sequence holding the data to write.

        @param ec Set to indicate what error occurred, if any.

        @return The number of bytes consumed in the input buffers.
    */
    template<class ConstBufferSequence>
    void
    write_frame(bool fin,
        ConstBufferSequence const& buffers, error_code& ec);

    /** Start an asynchronous operation to send a message frame on the stream.

        This function is used to asynchronously write a message frame
        on the stream. This function call always returns immediately.
        The asynchronous operation will continue until one of the following
        conditions is true:

        @li The entire frame is sent.

        @li An error occurs.

        This operation is implemented in terms of one or more calls
        to the next layer's `async_write_some` functions, and is known
        as a <em>composed operation</em>. The actual payload sent
        may be transformed as per the WebSocket protocol settings. The
        program must ensure that the stream performs no other write
        operations (such as stream::async_write, stream::async_write_frame,
        or stream::async_close).

        If this is the beginning of a new message, the message opcode
        will be set to text or binary as per the current setting of
        the @ref message_type option. The actual payload sent
        may be transformed as per the WebSocket protocol settings.

        @param fin A bool indicating whether or not the frame is the
        last frame in the corresponding WebSockets message.

        @param buffers A object meeting the requirements of
        ConstBufferSequence which holds the payload data before any
        masking or compression. Although the buffers object may be copied
        as necessary, ownership of the underlying buffers is retained by
        the caller, which must guarantee that they remain valid until
        the handler is called.

        @param handler The handler to be called when the write completes.
        Copies will be made of the handler as required. The equivalent
        function signature of the handler must be:
        @code void handler(
            error_code const& ec    // Result of operation
        ); @endcode
    */
    template<class ConstBufferSequence, class WriteHandler>
    async_return_type<
        WriteHandler, void(error_code)>
    async_write_frame(bool fin,
        ConstBufferSequence const& buffers, WriteHandler&& handler);

private:
    template<class Decorator, class Handler> class accept_op;
    template<class Handler> class close_op;
    template<class Handler> class handshake_op;
    template<class Handler> class ping_op;
    template<class Handler> class response_op;
    template<class Buffers, class Handler> class write_op;
    template<class Buffers, class Handler> class write_frame_op;
    template<class DynamicBuffer, class Handler> class read_op;
    template<class DynamicBuffer, class Handler> class read_frame_op;

    static
    void
    default_decorate_req(request_type& res)
    {
    }

    static
    void
    default_decorate_res(response_type& res)
    {
    }

    void
    reset();

    template<class Decorator>
    void
    do_accept(Decorator const& decorator,
        error_code& ec);

    template<class Fields, class Decorator>
    void
    do_accept(http::header<true, Fields> const& req,
        Decorator const& decorator, error_code& ec);

    template<class RequestDecorator>
    void
    do_handshake(response_type* res_p,
        string_view const& host,
            string_view const& target,
                RequestDecorator const& decorator,
                    error_code& ec);

    template<class Decorator>
    request_type
    build_request(detail::sec_ws_key_type& key,
        string_view const& host,
            string_view const& target,
                Decorator const& decorator);

    template<class Decorator>
    response_type
    build_response(request_type const& req,
        Decorator const& decorator);

    void
    do_response(http::response_header const& resp,
        detail::sec_ws_key_type const& key, error_code& ec);
};

} // websocket
} // beast

#include <beast/websocket/impl/accept.ipp>
#include <beast/websocket/impl/close.ipp>
#include <beast/websocket/impl/handshake.ipp>
#include <beast/websocket/impl/ping.ipp>
#include <beast/websocket/impl/read.ipp>
#include <beast/websocket/impl/stream.ipp>
#include <beast/websocket/impl/write.ipp>

#endif
