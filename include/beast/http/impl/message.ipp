//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_MESSAGE_IPP
#define BEAST_HTTP_IMPL_MESSAGE_IPP

#include <beast/core/error.hpp>
#include <beast/core/string_view.hpp>
#include <beast/http/concepts.hpp>
#include <beast/http/rfc7230.hpp>
#include <beast/core/detail/ci_char_traits.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <boost/assert.hpp>
#include <boost/optional.hpp>
#include <stdexcept>

namespace beast {
namespace http {

template<class Fields>
void
swap(
    header<true, Fields>& m1,
    header<true, Fields>& m2)
{
    using std::swap;
    swap(m1.version, m2.version);
    swap(m1.fields, m2.fields);
}

template<class Fields>
void
swap(
    header<false, Fields>& a,
    header<false, Fields>& b)
{
    using std::swap;
    swap(a.version, b.version);
    swap(a.status, b.status);
    swap(a.fields, b.fields);
}

template<bool isRequest, class Body, class Fields>
void
swap(
    message<isRequest, Body, Fields>& m1,
    message<isRequest, Body, Fields>& m2)
{
    using std::swap;
    swap(m1.base(), m2.base());
    swap(m1.body, m2.body);
}

template<bool isRequest, class Fields>
bool
is_keep_alive(header<isRequest, Fields> const& msg)
{
    BOOST_ASSERT(msg.version == 10 || msg.version == 11);
    if(msg.version == 11)
    {
        if(token_list{msg.fields["Connection"]}.exists("close"))
            return false;
        return true;
    }
    if(token_list{msg.fields["Connection"]}.exists("keep-alive"))
        return true;
    return false;
}

template<bool isRequest, class Fields>
bool
is_upgrade(header<isRequest, Fields> const& msg)
{
    BOOST_ASSERT(msg.version == 10 || msg.version == 11);
    if(msg.version == 10)
        return false;
    if(token_list{msg.fields["Connection"]}.exists("upgrade"))
        return true;
    return false;
}

namespace detail {

struct prepare_info
{
    boost::optional<connection> connection_value;
    boost::optional<std::uint64_t> content_length;
};

template<bool isRequest, class Body, class Fields>
inline
void
prepare_options(prepare_info& pi,
    message<isRequest, Body, Fields>& msg)
{
    beast::detail::ignore_unused(pi, msg);
}

template<bool isRequest, class Body, class Fields>
void
prepare_option(prepare_info& pi,
    message<isRequest, Body, Fields>& msg,
        connection value)
{
    beast::detail::ignore_unused(msg);
    pi.connection_value = value;
}

template<
    bool isRequest, class Body, class Fields,
    class Opt, class... Opts>
void
prepare_options(prepare_info& pi,
    message<isRequest, Body, Fields>& msg,
        Opt&& opt, Opts&&... opts)
{
    prepare_option(pi, msg, opt);
    prepare_options(pi, msg,
        std::forward<Opts>(opts)...);
}

template<bool isRequest, class Body, class Fields>
void
prepare_content_length(prepare_info& pi,
    message<isRequest, Body, Fields> const& msg,
        std::true_type)
{
    typename Body::writer w(msg);
    // VFALCO This is a design problem!
    error_code ec;
    w.init(ec);
    if(ec)
        throw system_error{ec};
    pi.content_length = w.content_length();
}

template<bool isRequest, class Body, class Fields>
void
prepare_content_length(prepare_info& pi,
    message<isRequest, Body, Fields> const& msg,
        std::false_type)
{
    beast::detail::ignore_unused(msg);
    pi.content_length = boost::none;
}

} // detail

template<
    bool isRequest, class Body, class Fields,
    class... Options>
void
prepare(message<isRequest, Body, Fields>& msg,
    Options&&... options)
{
    using beast::detail::make_exception;

    // VFALCO TODO
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    detail::prepare_info pi;
    detail::prepare_content_length(pi, msg,
        detail::has_content_length<typename Body::writer>{});
    detail::prepare_options(pi, msg,
        std::forward<Options>(options)...);

    if(msg.fields.exists("Connection"))
        throw make_exception<std::invalid_argument>(
            "prepare called with Connection field set", __FILE__, __LINE__);

    if(msg.fields.exists("Content-Length"))
        throw make_exception<std::invalid_argument>(
            "prepare called with Content-Length field set", __FILE__, __LINE__);

    if(token_list{msg.fields["Transfer-Encoding"]}.exists("chunked"))
        throw make_exception<std::invalid_argument>(
            "prepare called with Transfer-Encoding: chunked set", __FILE__, __LINE__);

    if(pi.connection_value != connection::upgrade)
    {
        if(pi.content_length)
        {
            struct set_field
            {
                void
                operator()(message<true, Body, Fields>& msg,
                    detail::prepare_info const& pi) const
                {
                    using beast::detail::ci_equal;
                    if(*pi.content_length > 0 ||
                        ci_equal(msg.method(), "POST"))
                    {
                        msg.fields.insert(
                            "Content-Length", *pi.content_length);
                    }
                }

                void
                operator()(message<false, Body, Fields>& msg,
                    detail::prepare_info const& pi) const
                {
                    if((msg.status / 100 ) != 1 &&
                        msg.status != 204 &&
                        msg.status != 304)
                    {
                        msg.fields.insert(
                            "Content-Length", *pi.content_length);
                    }
                }
            };
            set_field{}(msg, pi);
        }
        else if(msg.version >= 11)
        {
            msg.fields.insert("Transfer-Encoding", "chunked");
        }
    }

    auto const content_length =
        msg.fields.exists("Content-Length");

    if(pi.connection_value)
    {
        switch(*pi.connection_value)
        {
        case connection::upgrade:
            msg.fields.insert("Connection", "upgrade");
            break;

        case connection::keep_alive:
            if(msg.version < 11)
            {
                if(content_length)
                    msg.fields.insert("Connection", "keep-alive");
            }
            break;

        case connection::close:
            if(msg.version >= 11)
                msg.fields.insert("Connection", "close");
            break;
        }
    }

    // rfc7230 6.7.
    if(msg.version < 11 && token_list{
            msg.fields["Connection"]}.exists("upgrade"))
        throw make_exception<std::invalid_argument>(
            "invalid version for Connection: upgrade", __FILE__, __LINE__);
}

namespace detail {

template<class = void>
string_view
reason_string(int status)
{
    switch(status)
    {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";

    case 306: return "<reserved>";
    default:
        break;
    }
    return "<unknown-status>";
}

} // detail

inline
string_view const
reason_string(int status)
{
    return detail::reason_string(status);
}

} // http
} // beast

#endif
