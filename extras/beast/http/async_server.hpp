//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLE_HTTP_ASYNC_SERVER_H_INCLUDED
#define BEAST_EXAMPLE_HTTP_ASYNC_SERVER_H_INCLUDED

#include "file_body.hpp"
#include "mime_type.hpp"

#include <beast/http.hpp>
#include <beast/core/placeholders.hpp>
#include <beast/core/streambuf.hpp>
#include <boost/asio.hpp>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace beast {
namespace http {

/** Asynchronous HTTP/1 server.

    This implements an asynchronous HTTP server with
    one listening port. To use this class, derive from
    it using the curiously recurring template pattern,
    and provide the member functions @ref on_request and
    @ref on_accept.

    Example:
    @code
    struct MyServer : beast::http::async_server<MyServer>
    {
        template<class Session>
        void 
        on_accept(Session& session);

        template<class Session, class Body, class Headers>
        void
        on_request(Session& session,
            beast::http::request_v1<Body, Headers>&& req);
    };
    @endcode
*/
template<class Derived>
class async_server
{
public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    using req_type = request_v1<string_body>;

    async_server(endpoint_type const& ep,
            std::size_t threads, std::string const& root);

    ~async_server();

    template<class... Args>
    void
    log(Args const&... args);

private:
    class session;

    void
    log_args()
    {
    }

    template<class Arg, class... Args>
    void
    log_args(Arg const& arg, Args const&... args)
    {
        std::cerr << arg;
        log_args(args...);
    }

    void
    fail(error_code ec, std::string what)
    {
        log(what, ": ", ec.message(), "\n");
    }

    void
    on_accept(error_code ec);

private:
    Derived&
    impl()
    {
        return *static_cast<Derived*>(this);
    }

    std::mutex m_;
    bool log_ = true;
    boost::asio::io_service ios_;
    boost::asio::ip::tcp::acceptor acceptor_;
    socket_type sock_;
    std::string root_;
    std::vector<std::thread> thread_;
    endpoint_type ep_;
};

template<class Derived>
async_server<Derived>::
async_server(endpoint_type const& ep,
        std::size_t threads, std::string const& root)
    : acceptor_(ios_)
    , sock_(ios_)
    , root_(root)
{
    acceptor_.open(ep.protocol());
    acceptor_.bind(ep);
    acceptor_.listen(
        boost::asio::socket_base::max_connections);
    acceptor_.async_accept(sock_, ep_,
        std::bind(&async_server::on_accept, this,
            beast::asio::placeholders::error));
    thread_.reserve(threads);
    for(std::size_t i = 0; i < threads; ++i)
        thread_.emplace_back(
            [&] { ios_.run(); });
}

template<class Derived>
async_server<Derived>::
~async_server()
{
    error_code ec;
    ios_.dispatch(
        [&]{ acceptor_.close(ec); });
    for(auto& t : thread_)
        t.join();
}

template<class Derived>
template<class... Args>
void
async_server<Derived>::
log(Args const&... args)
{
    if(log_)
    {
        std::lock_guard<std::mutex> lock(m_);
        log_args(args...);
    }
}

template<class Derived>
void
async_server<Derived>::
on_accept(error_code ec)
{
    if(! acceptor_.is_open())
        return;
    if(ec)
        return fail(ec, "accept");
    socket_type sock(std::move(sock_));
    acceptor_.async_accept(sock_,
        std::bind(&async_server::on_accept, this,
            asio::placeholders::error));
    std::make_shared<session>(std::move(sock), *this)->run();
}

//------------------------------------------------------------------------------

template<class Derived>
class async_server<Derived>::session
    : public std::enable_shared_from_this<session>
{
public:
    session(socket_type&& sock, async_server& server)
        : sock_(std::move(sock))
        , server_(server)
        , strand_(sock_.get_io_service())
    {
        static int n = 0;
        id_ = ++n;
    }

    /** Close the session.

        This function closes the session. The call will
        return immediately.
    */
    void
    close()
    {
        if(! strand_.running_in_this_thread())
            return server_.ios_.post(strand_.wrap(
                std::bind(&close, shared_from_this())));
        error_code ec;
        sock_.close(ec);
        // VFALCO What to do with ec?
    }

    /** Detach the session.

        This keeps the session alive without any active I/O.
        Callers can use this to defer request fulfillment to
        foreign threads.
    */
    std::shared_ptr<session>
    detach()
    {
        return shared_from_this();
    }

    /** Send a response.

        This function sends the specified response to the remote
        host.

        If the status code is 100, a subsequent read will not be
        issued when the response completes sending. The caller is
        responsible for eventually sending a resposne with a non-100
        status code.

        When the response completes sending, and the status code is
        not 100, the connection will be closed if necessary,
        depending on the HTTP version and contents of the header
        fields.

        @param res The response to send, which must be move constructible.
    */
    template<class Body, class Headers>
    void
    write(http::response_v1<Body, Headers>&& res)
    {
        static_assert(std::is_move_constructible<decltype(res)>::value,
            "MoveConstructible requirements not met");
        async_write(sock_, std::move(res),
            std::bind(&session::on_write, shared_from_this(),
                asio::placeholders::error));
    }

private:
    session(session&&) = default;
    session(session const&) = default;
    session& operator=(session&&) = delete;
    session& operator=(session const&) = delete;

    void
    fail(error_code ec, std::string what)
    {
        if(ec != boost::asio::error::operation_aborted)
            server_.log("#", id_, " ", what, ": ", ec.message(), "\n");
    }

    void
    run()
    {
        do_read();
    }

    void
    do_read()
    {
        async_read(sock_, sb_, req_, strand_.wrap(
            std::bind(&session::on_read, shared_from_this(),
                asio::placeholders::error)));
    }

    void
    on_read(error_code const& ec)
    {
        if(ec)
            return fail(ec, "read");
        if(! version_)
        {
            version_ = req_.version;
        }
        else if(version_ != req_.version)
        {
            // Invalid: mismatch in HTTP request version
        }
        server_.impl().on_request(*this, std::move(req_));
    }

    void
    on_write(error_code ec)
    {
        if(ec)
            fail(ec, "write");
        do_read();
    }

private:
    friend class async_server<Derived>;

    template<class Stream, class Handler,
        class Body, class Headers>
    class write_op;

    template<class Stream,
        class Body, class Headers, class DeducedHandler>
    void
    async_write(Stream& stream,
        response_v1<Body, Headers>&& msg, DeducedHandler&& handler)
    {
        write_op<Stream, typename std::decay<DeducedHandler>::type,
            Body, Headers>{std::forward<DeducedHandler>(
                handler), stream, std::move(msg)};
    }

    int id_;
    int version_ = 0;
    streambuf sb_;
    socket_type sock_;
    async_server& server_;
    boost::asio::io_service::strand strand_;
    req_type req_;
};

//------------------------------------------------------------------------------

template<class Derived>
template<class Stream, class Handler,
    class Body, class Headers>
class async_server<Derived>::session::write_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        Stream& s;
        response_v1<Body, Headers> m;
        Handler h;
        bool cont;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, Stream& s_,
                response_v1<Body, Headers>&& m_)
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
    operator()(error_code ec, bool again = true);

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

template<class Derived>
template<class Stream, class Handler,
    class Body, class Headers>
void
async_server<Derived>::session::
write_op<Stream, Handler, Body, Headers>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    if(! again)
    {
        beast::http::async_write(d.s, d.m, std::move(*this));
        return;
    }
    d.h(ec);
}

//------------------------------------------------------------------------------

class async_file_server
    : public async_server<async_file_server>
{
    std::string root_;

public:
    async_file_server(endpoint_type const& ep,
            std::size_t threads, std::string const& root)
        : async_server<async_file_server>(ep, threads, root)
        , root_(root)
    {
    }

    template<class Session, class Body, class Headers>
    void
    on_request(Session& session,
        request_v1<Body, Headers>&& req)
    {
        auto path = req.url;
        if(path == "/")
            path = "/index.html";
        path = root_ + path;
        if(! boost::filesystem::exists(path))
        {
            response_v1<string_body> res;
            res.status = 404;
            res.reason = "Not Found";
            res.version = req.version;
            res.headers.insert("Server", "async_server");
            res.headers.insert("Content-Type", "text/html");
            res.body = "The file '" + path + "' was not found";
            prepare(res);
            session.write(std::move(res));
            return;
        }
        try
        {
            response_v1<file_body> res;
            res.status = 200;
            res.reason = "OK";
            res.version = req.version;
            res.headers.insert("Server", "async_server");
            res.headers.insert("Content-Type", mime_type(path));
            res.body = path;
            prepare(res);
            session.write(std::move(res));
        }
        catch(std::exception const& e)
        {
            response_v1<string_body> res;
            res.status = 500;
            res.reason = "Internal Error";
            res.version = req.version;
            res.headers.insert("Server", "async_server");
            res.headers.insert("Content-Type", "text/html");
            res.body =
                std::string{"An internal error occurred"} + e.what();
            prepare(res);
            session.write(std::move(res));
        }
    }
};


} // http
} // beast

#endif
