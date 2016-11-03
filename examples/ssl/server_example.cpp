//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/unit_test/suite.hpp>

#include <beast/core/placeholders.hpp>
#include <beast/core/error.hpp>
#include <beast/http.hpp>
#include <beast/server/io_list.hpp>
#include <beast/server/io_threads.hpp>
#include <beast/server/ssl_socket.hpp>
#include <beast/unit_test/dstream.hpp>
#include <beast/test/sig_wait.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <memory>

/*  Notes
    
    * All I/O objects use a strand, whether they need it or not

    * You have to have OpenSSL whether you use it or not

    * io_list doesn't compose well, what if you want to track
      a subset of objects?
*/

namespace beast {

template<class Impl>
class basic_listener
    : public io_list::work
    , public std::enable_shared_from_this<basic_listener<Impl>>
{
protected:
    using socket_type =
        boost::asio::ip::tcp::socket;

    using strand_type =
        boost::asio::io_service::strand;

    using acceptor_type =
        boost::asio::ip::tcp::acceptor;

public:
    using endpoint_type =
        boost::asio::ip::tcp::endpoint;

    explicit
    basic_listener(boost::asio::io_service& ios)
        : sock_(ios)
        , strand_(ios)
        , acceptor_(ios)
    {
    }

    void
    open(endpoint_type const& ep, error_code& ec)
    {
        acceptor_.open(ep.protocol(), ec);
        if(ec)
            return;
        acceptor_.bind(ep, ec);
        if(ec)
            return;
        using boost::asio::socket_base;
        acceptor_.listen(
            socket_base::max_connections, ec);
        if(ec)
            return;
        namespace ph = beast::asio::placeholders;
        acceptor_.async_accept(sock_, strand_.wrap(
            std::bind(&basic_listener::on_accept,
                shared_from_this(), ph::error)));
    }

    void
    close() override
    {
        if(! strand_.running_in_this_thread())
            return strand_.post(std::bind(
                &basic_listener::close, shared_from_this()));
        error_code ec;
        acceptor_.close(ec);
    }

private:
    Impl&
    impl()
    {
        return static_cast<Impl&>(*this);
    }

    void
    on_accept(error_code const& ec)
    {
        if(ec == boost::asio::error::operation_aborted)
            return;
        socket_type sock{sock_.get_io_service()};
        std::swap(sock, sock_);
        impl().on_accept(std::move(sock), ec);
        if(! acceptor_.is_open())
            return;
        namespace ph = beast::asio::placeholders;
        acceptor_.async_accept(sock_, strand_.wrap(
            std::bind(&basic_listener::on_accept,
                shared_from_this(), ph::error)));
    }

    socket_type sock_;
    strand_type strand_;
    acceptor_type acceptor_;
};

//------------------------------------------------------------------------------

template<class Impl>
class basic_http_session
    : public io_list::work
    , public std::enable_shared_from_this<
        basic_http_session<Impl>>
{
protected:
    using strand_type =
        boost::asio::io_service::strand;

    using parser_type = http::parser_v1<
        true, http::string_body, http::headers>;

public:
    void
    run()
    {
        namespace ph = beast::asio::placeholders;
        http::async_parse(impl().stream(), rb_, p_,
            strand_.wrap(std::bind(
                &basic_http_session::on_parse,
                    shared_from_this(), ph::error_code)));
    }

    template<class ConstBufferSequence>
    void
    run(ConstBufferSequence const& buffers)
    {
        static_assert(
            is_ConstBufferSequence<ConstBufferSequence>::value,
                "ConstBufferSequence requirements not met");
        using boost::asio::buffer_copy;
        using boost::asio::buffer_size;
        rb_.commit(buffer_copy(
            rb.prepare(buffer_size(buffers)),
                buffers));
        run();
    }

    template<class Body, class Headers>
    void
    write(http::response<Body, Headers>&& m)
    {
        namespace ph = beast::asio::placeholders;
        async_write(impl().stream, std::move(m),
            strand_.wrap(std::bind(
                &basic_http_session::on_write,
                    shared_from_this(), ph::error_code)));
    }

private:
    template<class Stream, class Handler,
        bool isRequest, class Body, class Headers>
    class write_op
    {
        using alloc_type =
            handler_alloc<char, Handler>;

        struct data
        {
            Stream& s;
            http::message<isRequest, Body, Headers> m;
            Handler h;
            bool cont;

            template<class DeducedHandler>
            data(DeducedHandler&& h_, Stream& s_,
                    http::message<isRequest, Body, Headers>&& m_)
                : s(s_)
                , m(std::move(m_))
                , h(std::forward<DeducedHandler>(h_))
                , cont(boost_asio_handler_cont_helpers::
                    is_continuation(h))
            {
            }
        };

        std::shared_ptr<data> d_;

    public:
        write_op(write_op&&) = default;
        write_op(write_op const&) = default;

        template<class DeducedHandler, class... Args>
        write_op(DeducedHandler&& h, Stream& s, Args&&... args)
            : d_(std::allocate_shared<data>(alloc_type{h},
                std::forward<DeducedHandler>(h), s,
                    std::forward<Args>(args)...))
        {
            (*this)(error_code{}, false);
        }

        void
        operator()(error_code ec, bool again = true)
        {
            auto& d = *d_;
            d.cont = d.cont || again;
            if(! again)
            {
                http::async_write(d.s, d.m, std::move(*this));
                return;
            }
            d.h(ec);
        }

        friend
        void* asio_handler_allocate(
            std::size_t size, write_op* op)
        {
            return boost_asio_handler_alloc_helpers::
                allocate(size, op->d_->h);
        }

        friend
        void asio_handler_deallocate(
            void* p, std::size_t size, write_op* op)
        {
            return boost_asio_handler_alloc_helpers::
                deallocate(p, size, op->d_->h);
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
            return boost_asio_handler_invoke_helpers::
                invoke(f, op->d_->h);
        }
    };

    template<class Stream,
        bool isRequest, class Body, class Headers,
            class DeducedHandler>
    static
    void
    async_write(Stream& stream, http::message<
        isRequest, Body, Headers>&& msg,
            DeducedHandler&& handler)
    {
        write_op<Stream, typename std::decay<DeducedHandler>::type,
            isRequest, Body, Headers>{std::forward<DeducedHandler>(
                handler), stream, std::move(msg)};
    }

    Impl&
    impl()
    {
        return static_cast<Impl&>(*this);
    }

    void
    on_parse(error_code& ec)
    {
        if(ec == boost::asio::error::operation_aborted)
            return;
        if(ec)
        {
            return;
        }
        impl().on_request(p_.release());
    }

    streambuf rb_;
    strand_type strand_;
    parser_type p_;
};

//------------------------------------------------------------------------------

template<class Impl>
class basic_server
{
    io_list iov_;
    boost::asio::io_service& ios_;

public:
    basic_server(boost::asio::io_service& ios)
        : ios_(ios)
    {
    }

    boost::asio::io_service&
    get_io_service()
    {
        return ios_;
    }

    template<class T, class... Args>
    std::shared_ptr<T>
    emplace(Args&&... args)
    {
        return iov_.template emplace<T>(ios_,
            std::forward<Args>(args)...);
    }
};

//------------------------------------------------------------------------------

struct listener : basic_listener<listener>
{
    template<class... Args>
    explicit
    listener(Args&&... args)
        : basic_listener<listener>(
            std::forward<Args>(args)...)
    {
    }

    void
    on_accept(socket_type&& sock, error_code const& ec)
    {
    }
};

struct server : basic_server<server>
{
    template<class... Args>
    explicit
    server(Args&&... args)
        : basic_server<server>(
            std::forward<Args>(args)...)
    {
    }
};

} // beast

//------------------------------------------------------------------------------

void doRequest()
{
    std::string const host = "127.0.0.1";
    boost::asio::io_service ios;
    boost::asio::ip::tcp::resolver r{ios};
    boost::asio::ip::tcp::socket sock{ios};
    boost::asio::connect(sock,
        r.resolve(boost::asio::ip::tcp::resolver::query{host, "6000"}));

    beast::http::request<beast::http::empty_body> req;
    req.method = "GET";
    req.url = "/";
    req.version = 11;
    req.headers.replace("Host", host + ":" +
        std::to_string(sock.remote_endpoint().port()));
    req.headers.replace("User-Agent", "Beast");
    beast::http::prepare(req);
    beast::http::write(sock, req);

    // Receive and print HTTP response using beast
    beast::streambuf sb;
    beast::http::response<beast::http::streambuf_body> resp;
    beast::http::read(sock, sb, resp);
    beast::unit_test::dstream dout{std::cout};
    dout << resp;
}

int main()
{
    using namespace beast;
    io_threads iot;
    server s{iot.get_io_service()};
    auto sl = s.emplace<listener>();

    error_code ec;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    sl->open(endpoint_type{
        address_type::from_string("127.0.0.1"), 6000}, ec);

    try
    {
        doRequest();
    }
    catch(std::exception const& e)
    {
        beast::unit_test::dstream dout{std::cout};
        dout << e.what() << "\n";
    }
};
