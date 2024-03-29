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
#include <beast/core/buffer_cat.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/ostream.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/multi_buffer.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/sync_ostream.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/asio/write.hpp>
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
        throw system_error{ec};
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

template<bool isRequest, class Body, class Fields>
struct write_preparation
{
    message<isRequest, Body, Fields> const& msg;
    typename Body::writer w;
    multi_buffer b;
    bool chunked;
    bool close;

    explicit
    write_preparation(
            message<isRequest, Body, Fields> const& msg_)
        : msg(msg_)
        , w(msg)
        , chunked(token_list{
            msg.fields["Transfer-Encoding"]}.exists("chunked"))
        , close(token_list{
            msg.fields["Connection"]}.exists("close") ||
                (msg.version < 11 && ! msg.fields.exists(
                    "Content-Length")))
    {
    }

    void
    init(error_code& ec)
    {
        w.init(ec);
        if(ec)
            return;

        auto os = ostream(b);
        write_start_line(os, msg);
        write_fields(os, msg.fields);
        os << "\r\n";
    }
};

template<class Stream, class Handler,
    bool isRequest, class Body, class Fields>
class write_op
{
    struct data
    {
        bool cont;
        Stream& s;
        // VFALCO How do we use handler_alloc in write_preparation?
        write_preparation<
            isRequest, Body, Fields> wp;
        int state = 0;

        data(Handler& handler, Stream& s_,
                message<isRequest, Body, Fields> const& m_)
            : s(s_)
            , wp(m_)
        {
            using boost::asio::asio_handler_is_continuation;
            cont = asio_handler_is_continuation(std::addressof(handler));
        }
    };

    class writef0_lambda
    {
        write_op& self_;

    public:
        explicit
        writef0_lambda(write_op& self)
            : self_(self)
        {
        }

        template<class ConstBufferSequence>
        void operator()(ConstBufferSequence const& buffers) const
        {
            auto& d = *self_.d_;
            // write header and body
            if(d.wp.chunked)
                boost::asio::async_write(d.s,
                    buffer_cat(d.wp.b.data(),
                        chunk_encode(false, buffers)),
                            std::move(self_));
            else
                boost::asio::async_write(d.s,
                    buffer_cat(d.wp.b.data(),
                        buffers), std::move(self_));
        }
    };

    class writef_lambda
    {
        write_op& self_;

    public:
        explicit
        writef_lambda(write_op& self)
            : self_(self)
        {
        }

        template<class ConstBufferSequence>
        void operator()(ConstBufferSequence const& buffers) const
        {
            auto& d = *self_.d_;
            // write body
            if(d.wp.chunked)
                boost::asio::async_write(d.s,
                    chunk_encode(false, buffers),
                        std::move(self_));
            else
                boost::asio::async_write(d.s,
                    buffers, std::move(self_));
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
        (*this)(error_code{}, 0, false);
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

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
        return op->d_->cont;
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
    bool isRequest, class Body, class Fields>
void
write_op<Stream, Handler, isRequest, Body, Fields>::
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
            d.wp.init(ec);
            if(ec)
            {
                // call handler
                d.state = 99;
                d.s.get_io_service().post(bind_handler(
                    std::move(*this), ec, 0, false));
                return;
            }
            d.state = 1;
            break;
        }

        case 1:
        {
            auto const result =
                d.wp.w.write(ec,
                    writef0_lambda{*this});
            if(ec)
            {
                // call handler
                d.state = 99;
                d.s.get_io_service().post(bind_handler(
                    std::move(*this), ec, false));
                return;
            }
            if(result)
                d.state = d.wp.chunked ? 4 : 5;
            else
                d.state = 2;
            return;
        }

        // sent header and body
        case 2:
            d.wp.b.consume(d.wp.b.size());
            d.state = 3;
            break;

        case 3:
        {
            auto const result =
                d.wp.w.write(ec,
                    writef_lambda{*this});
            if(ec)
            {
                // call handler
                d.state = 99;
                break;
            }
            if(result)
                d.state = d.wp.chunked ? 4 : 5;
            else
                d.state = 2;
            return;
        }

        case 4:
            // VFALCO Unfortunately the current interface to the
            //        Writer concept prevents us from coalescing the
            //        final body chunk with the final chunk delimiter.
            //
            // write final chunk
            d.state = 5;
            boost::asio::async_write(d.s,
                chunk_encode_final(), std::move(*this));
            return;

        case 5:
            if(d.wp.close)
            {
                // VFALCO TODO Decide on an error code
                ec = boost::asio::error::eof;
            }
            d.state = 99;
            break;
        }
    }
    d_.invoke(ec);
}

template<class SyncWriteStream, class DynamicBuffer>
class writef0_lambda
{
    DynamicBuffer const& sb_;
    SyncWriteStream& stream_;
    bool chunked_;
    error_code& ec_;

public:
    writef0_lambda(SyncWriteStream& stream,
            DynamicBuffer const& b, bool chunked, error_code& ec)
        : sb_(b)
        , stream_(stream)
        , chunked_(chunked)
        , ec_(ec)
    {
    }

    template<class ConstBufferSequence>
    void operator()(ConstBufferSequence const& buffers) const
    {
        // write header and body
        if(chunked_)
            boost::asio::write(stream_, buffer_cat(
                sb_.data(), chunk_encode(false, buffers)), ec_);
        else
            boost::asio::write(stream_, buffer_cat(
                sb_.data(), buffers), ec_);
    }
};

template<class SyncWriteStream>
class writef_lambda
{
    SyncWriteStream& stream_;
    bool chunked_;
    error_code& ec_;

public:
    writef_lambda(SyncWriteStream& stream,
            bool chunked, error_code& ec)
        : stream_(stream)
        , chunked_(chunked)
        , ec_(ec)
    {
    }

    template<class ConstBufferSequence>
    void operator()(ConstBufferSequence const& buffers) const
    {
        // write body
        if(chunked_)
            boost::asio::write(stream_,
                chunk_encode(false, buffers), ec_);
        else
            boost::asio::write(stream_, buffers, ec_);
    }
};

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
        throw system_error{ec};
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
    detail::write_preparation<isRequest, Body, Fields> wp(msg);
    wp.init(ec);
    if(ec)
        return;
    auto result = wp.w.write(
        ec, detail::writef0_lambda<
            SyncWriteStream, decltype(wp.b)>{
                stream, wp.b, wp.chunked, ec});
    if(ec)
        return;
    wp.b.consume(wp.b.size());
    if(! result)
    {
        detail::writef_lambda<SyncWriteStream> wf{
            stream, wp.chunked, ec};
        for(;;)
        {
            result = wp.w.write(ec, wf);
            if(ec)
                return;
            if(result)
                break;
        }
    }
    if(wp.chunked)
    {
        // VFALCO Unfortunately the current interface to the
        //        Writer concept prevents us from using coalescing the
        //        final body chunk with the final chunk delimiter.
        //
        // write final chunk
        boost::asio::write(stream, chunk_encode_final(), ec);
        if(ec)
            return;
    }
    if(wp.close)
    {
        // VFALCO TODO Decide on an error code
        ec = boost::asio::error::eof;
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
    detail::write_op<AsyncWriteStream, handler_type<
        WriteHandler, void(error_code)>, isRequest,
            Body, Fields>{init.completion_handler, stream, msg};
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
        throw system_error{ec};
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
        throw system_error{ec};
    return os;
}

} // http
} // beast

#endif
