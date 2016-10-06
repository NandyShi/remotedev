//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/unit_test/suite.hpp>

#include <beast/core/placeholders.hpp>
#include <beast/core/error.hpp>
#include <beast/server/io_list.hpp>
#include <boost/asio.hpp>
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

//------------------------------------------------------------------------------

class server_test : public unit_test::suite
{
public:
    void
    testServer()
    {
        boost::asio::io_service ios;
        server s{ios};
        auto sl = std::make_shared<listener>(ios);

        error_code ec;
        using address_type = boost::asio::ip::address;
        using endpoint_type = boost::asio::ip::tcp::endpoint;
        sl->open(endpoint_type{
            address_type::from_string("127.0.0.1"), 6000}, ec);
    }

    void run() override
    {
        testServer();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(server,core,beast);

} // beast
