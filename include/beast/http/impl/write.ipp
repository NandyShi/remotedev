//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_WRITE_IPP
#define BEAST_HTTP_IMPL_WRITE_IPP

#include <beast/http/concepts.hpp>
#include <beast/http/chunk_encode.hpp>
#include <beast/http/error.hpp>
#include <beast/core/buffer_cat.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/ostream.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/multi_buffer.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/sync_ostream.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/asio/write.hpp>
#include <boost/throw_exception.hpp>
#include <condition_variable>
#include <mutex>
#include <ostream>
#include <sstream>
#include <type_traits>

namespace beast {
namespace http {

namespace detail {

template<class Fields>
void
write_start_line(std::ostream& os,
    header<true, Fields> const& msg)
{
    BOOST_ASSERT(msg.version == 10 || msg.version == 11);
    os << msg.method() << " " << msg.target();
    switch(msg.version)
    {
    case 10: os << " HTTP/1.0\r\n"; break;
    case 11: os << " HTTP/1.1\r\n"; break;
    }
}

template<class Fields>
void
write_start_line(std::ostream& os,
    header<false, Fields> const& msg)
{
    BOOST_ASSERT(msg.version == 10 || msg.version == 11);
    switch(msg.version)
    {
    case 10: os << "HTTP/1.0 "; break;
    case 11: os << "HTTP/1.1 "; break;
    }
    os << msg.status << " " << msg.reason() << "\r\n";
}

template<class FieldSequence>
void
write_fields(std::ostream& os,
    FieldSequence const& fields)
{
    //static_assert(is_FieldSequence<FieldSequence>::value,
    //    "FieldSequence requirements not met");
    for(auto const& field : fields)
    {
        auto const name = field.name();
        BOOST_ASSERT(! name.empty());
        if(name[0] == ':')
            continue;
        os << field.name() << ": " << field.value() << "\r\n";
    }
}

} // detail

//------------------------------------------------------------------------------

template<bool isRequest, class Body, class Fields,
    class ChunkDecorator, class Allocator>
template<class Stream, class Handler>
class write_stream<isRequest, Body, Fields,
    ChunkDecorator, Allocator>::async_op
{
    write_stream<isRequest, Body, Fields,
        ChunkDecorator, Allocator>& w_;
    Stream& s_;
    Handler h_;
    bool cont_;

public:
    async_op(async_op&&) = default;
    async_op(async_op const&) = default;

    async_op(Handler&& h, Stream& s,
        write_stream<isRequest, Body, Fields,
            ChunkDecorator, Allocator>& w)
        : w_(w)
        , s_(s)
        , h_(std::move(h))
    {
        using boost::asio::asio_handler_is_continuation;
        cont_ = asio_handler_is_continuation(
            std::addressof(h_));
    }

    async_op(Handler const& h, Stream& s,
        write_stream<isRequest, Body, Fields,
            ChunkDecorator, Allocator>& w)
        : w_(w)
        , s_(s)
        , h_(h)
    {
        using boost::asio::asio_handler_is_continuation;
        cont_ = asio_handler_is_continuation(
            std::addressof(h_));
    }

    void
    operator()(error_code ec, std::size_t
        bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, async_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->h_));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, async_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->h_));
    }

    friend
    bool asio_handler_is_continuation(async_op* op)
    {
        return op->cont_;
    }

    template<class Function>
    friend
    void asio_handler_invoke(
        Function&& f, async_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->h_));
    }
};

template<bool isRequest, class Body, class Fields,
    class ChunkDecorator, class Allocator>
template<class Stream, class Handler>
void
write_stream<isRequest, Body, Fields,
    ChunkDecorator, Allocator>::
async_op<Stream, Handler>::
operator()(error_code ec,
    std::size_t bytes_transferred, bool again)
{
    cont_ = again || cont_;
    using boost::asio::buffer_size;
    if(ec)
        goto upcall;
    switch(w_.s_)
    {
    case do_init:
    {
        w_.w_.init(ec);
        if(ec)
            return s_.get_io_service().post(
                bind_handler(std::move(*this), ec, 0));
        if(is_deferred::value)
            goto go_header_only;
        auto result = w_.w_.read(ec);
        if(ec)
            return s_.get_io_service().post(
                bind_handler(std::move(*this), ec, 0));
        if(! result)
            goto go_header_only;
        w_.more_ = result->second;
        w_.cb_ = cb0_t{
            boost::in_place_init,
            w_.b_.data(),
            result->first};
        // [[fallthrough]]
    }

    case do_header:
        w_.s_ = do_header + 1;
        return s_.async_write_some(
            boost::get<cb0_t>(w_.cb_),
                std::move(*this));

    case do_header + 1:
        boost::get<cb0_t>(w_.cb_).consume(
            bytes_transferred);
        if(buffer_size(
            boost::get<cb0_t>(w_.cb_)) > 0)
        {
            w_.s_ = do_header;
            break;
        }
        w_.cb_ = boost::blank{};
        w_.b_.consume(w_.b_.size()); // VFALCO delete b_?
        if(w_.more_)
            goto go_body_read;
        goto go_complete;

         go_header_only:
    case do_header_only:
        w_.s_ = do_header_only + 1;
        return s_.async_write_some(
            w_.b_.data(), std::move(*this));

    case do_header_only + 1:
        w_.b_.consume(bytes_transferred);
        if(buffer_size(w_.b_.data()) > 0)
        {
            w_.s_ = do_header_only;
            break;
        }
        // VFALCO delete b_?
        if(! is_deferred::value)
            goto go_complete;
        // [[fallthrough]]

go_body_read:
         w_.s_ = do_body_read;
    case do_body_read:
    {
        auto result = w_.w_.read(ec);
        if(ec)
            goto upcall;
        if(! result)
            goto go_complete;
        w_.more_ = result->second;
        w_.cb_ = cb1_t{result->first};
        // [[fallthrough]]
    }

    case do_body:
        w_.s_ = do_body + 1;
        return s_.async_write_some(
            boost::get<cb1_t>(w_.cb_),
                std::move(*this));

    case do_body + 1:
        boost::get<cb1_t>(w_.cb_).consume(
            bytes_transferred);
        if(buffer_size(
                boost::get<cb1_t>(w_.cb_)) > 0)
        {
            w_.s_ = do_body;
            break;
        }
        w_.cb_ = boost::blank{};
        if(! w_.more_)
            goto go_complete;
        w_.s_ = do_body_read;
        break;

    //----------------------------------------------------------------------

    case do_chunked_init:
    {
        w_.w_.init(ec);
        if(ec)
            return s_.get_io_service().post(
                bind_handler(std::move(*this), ec, 0));
        if(is_deferred::value)
            goto go_chunked_header_only;
        auto result = w_.w_.read(ec);
        if(ec)
            return s_.get_io_service().post(
                bind_handler(std::move(*this), ec, 0));
        if(! result)
            goto go_chunked_header_only;
        w_.more_ = result->second;
        if(w_.more_)
            w_.cb_ = ch0_t{
                boost::in_place_init,
                w_.b_.data(),
                detail::chunk_header{
                    buffer_size(result->first)},
                result->first,
                detail::chunk_crlf()};
        else
            w_.cb_ = ch0_t{
                boost::in_place_init,
                w_.b_.data(),
                detail::chunk_header{
                    buffer_size(result->first)},
                result->first,
                detail::chunk_crlf_final()};
        // [[fallthrough]]
    }

    case do_chunked_header:
        w_.s_ = do_chunked_header + 1;
        return s_.async_write_some(
            boost::get<ch0_t>(w_.cb_),
                std::move(*this));

    case do_chunked_header + 1:
        boost::get<ch0_t>(w_.cb_).consume(
            bytes_transferred);
        if(buffer_size(
            boost::get<ch0_t>(w_.cb_)) > 0)
        {
            w_.s_ = do_chunked_header;
            break;
        }
        w_.cb_ = boost::blank{};
        w_.b_.consume(w_.b_.size()); // VFALCO delete b_?
        if(w_.more_)
            goto go_chunked_body_read;
        goto go_complete;

         go_chunked_header_only:
    case do_chunked_header_only:
        w_.s_ = do_chunked_header_only + 1;
        return s_.async_write_some(
            w_.b_.data(), std::move(*this));

    case do_chunked_header_only + 1:
        w_.b_.consume(bytes_transferred);
        if(buffer_size(w_.b_.data()) > 0)
        {
            w_.s_ = do_chunked_header_only;
            break;
        }
        // VFALCO delete b_?
        if(is_deferred::value)
            w_.s_ = do_chunked_body_read;
        else
            w_.s_ = do_chunked_final;
        break;

go_chunked_body_read:
        w_.s_ = do_chunked_body_read;
    case do_chunked_body_read:
    {
        auto result = w_.w_.read(ec);
        if(ec)
            goto upcall;
        if(! result)
            goto go_chunked_final;
        w_.more_ = result->second;
        if(w_.more_)
            w_.cb_ = ch1_t{
                boost::in_place_init,
                detail::chunk_header{
                    buffer_size(result->first)},
                result->first,
                detail::chunk_crlf()};
        else
            w_.cb_ = ch1_t{
                boost::in_place_init,
                detail::chunk_header{
                    buffer_size(result->first)},
                result->first,
                detail::chunk_crlf_final()};
        // [[fallthrough]]
    }

    case do_chunked_body:
        w_.s_ = do_chunked_body + 1;
        return s_.async_write_some(
            boost::get<ch1_t>(w_.cb_),
                std::move(*this));

    case do_chunked_body + 1:
        boost::get<ch1_t>(w_.cb_).consume(
            bytes_transferred);
        if(buffer_size(
            boost::get<ch1_t>(w_.cb_)) > 0)
        {
            w_.s_ = do_chunked_body;
            break;
        }
        w_.cb_ = boost::blank{};
        if(! w_.more_)
            goto go_complete;
        w_.s_ = do_chunked_body_read;
        break;

         go_chunked_final:
    case do_chunked_final:
        w_.s_ = do_chunked_final + 1;
        return boost::asio::async_write(s_,
            detail::chunk_final(), std::move(*this));

    case do_chunked_final + 1:
        goto go_complete;

    default:
        BOOST_ASSERT(false);

         go_complete:
    case do_complete:
        w_.s_ = do_complete;
        if(w_.close_)
            // VFALCO TODO Decide on an error code
            ec = boost::asio::error::eof;
        break;
    }

upcall:
    h_(ec);
}

//------------------------------------------------------------------------------

template<bool isRequest, class Body, class Fields,
    class ChunkDecorator, class Allocator>
write_stream<isRequest, Body, Fields,
    ChunkDecorator, Allocator>::
write_stream(message<isRequest, Body, Fields> const& m,
        Allocator const& alloc)
    : m_(m)
    , w_(m)
    , b_(1024, alloc)
    , chunked_(token_list{
        m.fields["Transfer-Encoding"]}.exists("chunked"))
    , close_(token_list{
        m.fields["Connection"]}.exists("close") ||
            (m.version < 11 && ! m.fields.exists(
                "Content-Length")))
    , s_(chunked_ ? do_chunked_init : do_init)
{
    auto os = ostream(b_);
    detail::write_start_line(os, m_);
    detail::write_fields(os, m_.fields);
    os << "\r\n";
}

template<bool isRequest, class Body, class Fields,
    class ChunkDecorator, class Allocator>
template<class SyncWriteStream>
void
write_stream<isRequest, Body, Fields,
    ChunkDecorator, Allocator>::
write_some(SyncWriteStream& stream)
{
    static_assert(
        is_sync_write_stream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body::writer requirements not met");
#if 0
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
#endif
    error_code ec;
    write_some(stream, ec);
    if(ec)
        throw system_error{ec};
}

template<bool isRequest, class Body, class Fields,
    class ChunkDecorator, class Allocator>
template<class SyncWriteStream>
void
write_stream<isRequest, Body, Fields,
    ChunkDecorator, Allocator>::
write_some(SyncWriteStream& stream, error_code &ec)
{
    static_assert(
        is_sync_write_stream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body::writer requirements not met");
#if 0
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
#endif

    using boost::asio::buffer_size;
    switch(s_)
    {
    case do_init:
    {
        w_.init(ec);
        if(ec)
            return;
        if(is_deferred::value)
            goto go_header_only;
        auto result = w_.read(ec);
        if(ec)
            return;
        if(! result)
            goto go_header_only;
        more_ = result->second;
        cb_ = cb0_t{
            boost::in_place_init,
            b_.data(),
            result->first};
        s_ = do_header;
        // [[fallthrough]]
    }

    case do_header:
    {
        auto bytes_transferred =
            stream.write_some(
                boost::get<cb0_t>(cb_), ec);
        if(ec)
            return;
        boost::get<cb0_t>(cb_).consume(
            bytes_transferred);
        if(buffer_size(boost::get<cb0_t>(cb_)) > 0)
            break;
        cb_ = boost::blank{};
        b_.consume(b_.size()); // VFALCO delete b_?
        if(! more_)
            goto go_complete;
        goto go_body_read;
    }

         go_header_only:
    s_ = do_header_only;
    case do_header_only:
    {
        auto bytes_transferred =
            stream.write_some(b_.data(), ec);
        if(ec)
            return;
        b_.consume(bytes_transferred);
        if(buffer_size(b_.data()) > 0)
            break;
        // VFALCO delete b_?
        if(! is_deferred::value)
            goto go_complete;
        // [[fallthrough]]
    }

         go_body_read:
    s_ = do_body_read;
    case do_body_read:
    {
        auto result = w_.read(ec);
        if(ec)
            return;
        if(! result)
            goto go_complete;
        more_ = result->second;
        cb_ = cb1_t{result->first};
        s_ = do_body;
        // [[fallthrough]]
    }

    case do_body:
    {
        auto bytes_transferred =
            stream.write_some(
                boost::get<cb1_t>(cb_), ec);
        if(ec)
            return;
        boost::get<cb1_t>(cb_).consume(
            bytes_transferred);
        if(buffer_size(boost::get<cb1_t>(cb_)) > 0)
            break;
        cb_ = boost::blank{};
        if(! more_)
            goto go_complete;
        s_ = do_body_read;
        break;
    }

    //----------------------------------------------------------------------

    case do_chunked_init:
    {
        w_.init(ec);
        if(ec)
            return;
        if(is_deferred::value)
            goto go_chunked_header_only;
        auto result = w_.read(ec);
        if(ec)
            return;
        if(! result)
            goto go_chunked_header_only;
        more_ = result->second;
        if(more_)
            cb_ = ch0_t{
                boost::in_place_init,
                b_.data(),
                detail::chunk_header{
                    buffer_size(result->first)},
                result->first,
                detail::chunk_crlf()};
        else
            cb_ = ch0_t{
                boost::in_place_init,
                b_.data(),
                detail::chunk_header{
                    buffer_size(result->first)},
                result->first,
                detail::chunk_crlf_final()};
        s_ = do_chunked_header;
        // [[fallthrough]]
    }

    case do_chunked_header:
    {
        auto bytes_transferred =
            stream.write_some(
                boost::get<ch0_t>(cb_), ec);
        if(ec)
            return;
        boost::get<ch0_t>(cb_).consume(
            bytes_transferred);
        if(buffer_size(boost::get<ch0_t>(cb_)) > 0)
            break;
        cb_ = boost::blank{};
        b_.consume(b_.size()); // VFALCO delete b_?
        if(! more_)
            goto go_complete;
        goto go_chunked_body_read;
    }

         go_chunked_header_only:
    s_ = do_chunked_header_only;
    case do_chunked_header_only:
    {
        auto bytes_transferred =
            stream.write_some(b_.data(), ec);
        if(ec)
            return;
        b_.consume(bytes_transferred);
        if(buffer_size(b_.data()) > 0)
            break;
        // VFALCO delete b_?
        if(is_deferred::value)
            s_ = do_chunked_body_read;
        else
            s_ = do_chunked_final;
        break;
    }

         go_chunked_body_read:
    s_ = do_chunked_body_read;
    case do_chunked_body_read:
    {
        auto result = w_.read(ec);
        if(ec)
            return;
        if(! result)
            goto go_chunked_final;
        more_ = result->second;
        if(more_)
            cb_ = ch1_t{
                boost::in_place_init,
                detail::chunk_header{
                    buffer_size(result->first)},
                result->first,
                detail::chunk_crlf()};
        else
            cb_ = ch1_t{
                boost::in_place_init,
                detail::chunk_header{
                    buffer_size(result->first)},
                result->first,
                detail::chunk_crlf_final()};
        s_ = do_chunked_body;
        // [[fallthrough]]
    }

    case do_chunked_body:
    {
        auto bytes_transferred =
            stream.write_some(
                boost::get<ch1_t>(cb_), ec);
        if(ec)
            return;
        boost::get<ch1_t>(cb_).consume(
            bytes_transferred);
        if(buffer_size(boost::get<ch1_t>(cb_)) > 0)
            break;
        cb_ = boost::blank{};
        if(! more_)
            goto go_complete;
        s_ = do_chunked_body_read;
        break;
    }

         go_chunked_final:
    case do_chunked_final:
        boost::asio::write(stream,
            detail::chunk_final(), ec);
        if(ec)
            return;
        goto go_complete;

    default:
    case do_complete:
        BOOST_ASSERT(false);

    //----------------------------------------------------------------------

    go_complete:
        s_ = do_complete;
        if(close_)
        {
            // VFALCO TODO Decide on an error code
            ec = boost::asio::error::eof;
            return;
        }
        break;
    }
}

template<bool isRequest, class Body, class Fields,
    class ChunkDecorator, class Allocator>
template<class AsyncWriteStream, class WriteHandler>
async_return_type<WriteHandler, void(error_code)>
write_stream<isRequest, Body, Fields,
    ChunkDecorator, Allocator>::
async_write_some(AsyncWriteStream& stream,
    WriteHandler&& handler)
{
    static_assert(is_async_write_stream<AsyncWriteStream>::value,
        "AsyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body::writer requirements not met");
#if 0
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
#endif
    async_completion<WriteHandler,
        void(error_code)> init{handler};
    async_op<AsyncWriteStream, handler_type<
        WriteHandler, void(error_code)>>{
            init.completion_handler, stream, *this}(
                error_code{}, 0, false);

    return init.result.get();
}

//------------------------------------------------------------------------------

namespace detail {

template<class Stream, class Handler>
class write_streambuf_op
{
    struct data
    {
        bool cont;
        Stream& s;
        multi_buffer b;
        int state = 0;

        data(Handler& handler, Stream& s_,
                multi_buffer&& sb_)
            : s(s_)
            , b(std::move(sb_))
        {
            using boost::asio::asio_handler_is_continuation;
            cont = asio_handler_is_continuation(std::addressof(handler));
        }
    };

    handler_ptr<data, Handler> d_;

public:
    write_streambuf_op(write_streambuf_op&&) = default;
    write_streambuf_op(write_streambuf_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_streambuf_op(DeducedHandler&& h, Stream& s,
            Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            s, std::forward<Args>(args)...)
    {
        (*this)(error_code{}, 0, false);
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_streambuf_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_streambuf_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(write_streambuf_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, write_streambuf_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class Stream, class Handler>
void
write_streambuf_op<Stream, Handler>::
operator()(error_code ec, std::size_t, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        {
            d.state = 99;
            boost::asio::async_write(d.s,
                d.b.data(), std::move(*this));
            return;
        }
        }
    }
    d_.invoke(ec);
}

} // detail

template<class SyncWriteStream,
    bool isRequest, class Fields>
void
write(SyncWriteStream& stream,
    header<isRequest, Fields> const& msg)
{
    static_assert(is_sync_write_stream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    error_code ec;
    write(stream, msg, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class SyncWriteStream,
    bool isRequest, class Fields>
void
write(SyncWriteStream& stream,
    header<isRequest, Fields> const& msg,
        error_code& ec)
{
    static_assert(is_sync_write_stream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    multi_buffer b;
    {
        auto os = ostream(b);
        detail::write_start_line(os, msg);
        detail::write_fields(os, msg.fields);
        os << "\r\n";
    }
    boost::asio::write(stream, b.data(), ec);
}

template<class AsyncWriteStream,
    bool isRequest, class Fields,
        class WriteHandler>
async_return_type<
    WriteHandler, void(error_code)>
async_write(AsyncWriteStream& stream,
    header<isRequest, Fields> const& msg,
        WriteHandler&& handler)
{
    static_assert(is_async_write_stream<AsyncWriteStream>::value,
        "AsyncWriteStream requirements not met");
    async_completion<WriteHandler,
        void(error_code)> init{handler};
    multi_buffer b;
    {
        auto os = ostream(b);
        detail::write_start_line(os, msg);
        detail::write_fields(os, msg.fields);
        os << "\r\n";
    }
    detail::write_streambuf_op<AsyncWriteStream,
        handler_type<WriteHandler, void(error_code)>>{
            init.completion_handler, stream, std::move(b)};
    return init.result.get();
}

//------------------------------------------------------------------------------

namespace detail {

template<class Stream, class Handler,
    bool isRequest, class Body, class Fields,
    class ChunkDecorator>
class write_op
{
    struct data
    {
        int state = 0;
        Stream& s;
        write_stream<isRequest, Body, Fields,
            ChunkDecorator, handler_alloc<char, Handler>> ws;

        data(Handler& h, Stream& s_,
                message<isRequest, Body, Fields> const& m_)
            : s(s_)
            , ws(m_, handler_alloc<char, Handler>{h})
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_op(DeducedHandler&& h, Stream& s, Args&&... args)
        : d_(std::forward<DeducedHandler>(h),
            s, std::forward<Args>(args)...)
    {
    }

    void
    operator()(error_code ec);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->d_.handler()));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->d_.handler()));
    }

    friend
    bool asio_handler_is_continuation(write_op* op)
    {
        using boost::asio::asio_handler_is_continuation;
        return op->d_->state == 2 ||
            asio_handler_is_continuation(
                std::addressof(op->d_.handler()));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, write_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->d_.handler()));
    }
};

template<class Stream, class Handler,
    bool isRequest, class Body, class Fields, class ChunkDecorator>
void
write_op<Stream, Handler, isRequest, Body, Fields, ChunkDecorator>::
operator()(error_code ec)
{
    auto& d = *d_;
    if(ec)
        goto upcall;    
    switch(d.state)
    {
    case 0:
        d.state = 1;
        return d.ws.async_write_some(d.s, std::move(*this));
    case 1:
        d.state = 2;
    case 2:
        if(d.ws.is_done())
            break;
        return d.ws.async_write_some(d.s, std::move(*this));
    }
upcall:
    d_.invoke(ec);
}

} // detail

template<class SyncWriteStream,
    bool isRequest, class Body, class Fields>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg)
{
    static_assert(is_sync_write_stream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    error_code ec;
    write(stream, msg, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
}

template<class SyncWriteStream,
    bool isRequest, class Body, class Fields>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg,
        error_code& ec)
{
    static_assert(is_sync_write_stream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    auto ws = make_write_stream(msg);
    for(;;)
    {
        ws.write_some(stream, ec);
        if(ec)
            return;
        if(ws.is_done())
            break;
    }
}

template<class AsyncWriteStream,
    bool isRequest, class Body, class Fields,
        class WriteHandler>
async_return_type<
    WriteHandler, void(error_code)>
async_write(AsyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg,
        WriteHandler&& handler)
{
    static_assert(is_async_write_stream<AsyncWriteStream>::value,
        "AsyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    async_completion<WriteHandler,
        void(error_code)> init{handler};
    detail::write_op<AsyncWriteStream,
        handler_type<WriteHandler, void(error_code)>,
            isRequest, Body, Fields, null_chunk_decorator>{
                init.completion_handler, stream, msg}(
                    error_code{});
    return init.result.get();
}

//------------------------------------------------------------------------------

template<bool isRequest, class Fields>
std::ostream&
operator<<(std::ostream& os,
    header<isRequest, Fields> const& msg)
{
    beast::detail::sync_ostream oss{os};
    error_code ec;
    write(oss, msg, ec);
    if(ec)
        BOOST_THROW_EXCEPTION(system_error{ec});
    return os;
}

template<bool isRequest, class Body, class Fields>
std::ostream&
operator<<(std::ostream& os,
    message<isRequest, Body, Fields> const& msg)
{
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    beast::detail::sync_ostream oss{os};
    error_code ec;
    write(oss, msg, ec);
    if(ec && ec != boost::asio::error::eof)
        BOOST_THROW_EXCEPTION(system_error{ec});
    return os;
}

} // http
} // beast

#endif
