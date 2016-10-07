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
#include <beast/unit_test/dstream.hpp>
#include <beast/test/sig_wait.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <memory>

namespace beast {

class io_object
{
public:
    virtual ~io_object() = default;
    virtual void close() = 0;
};

template<class Impl>
class basic_listener :
    public io_object,
    public io_list::work,
    public std::enable_shared_from_this<basic_listener<Impl>>
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
        return iov_.template emplace<T>(
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

    beast::http::request_v1<beast::http::empty_body> req;
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
    beast::http::response_v1<beast::http::streambuf_body> resp;
    beast::http::read(sock, sb, resp);
    beast::unit_test::dstream dout{std::cout};
    dout << resp;
}

int main()
{
    using namespace beast;
    io_threads iot;
    server s{iot.get_io_service()};
    auto sl = s.emplace<listener>(s.get_io_service());

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
