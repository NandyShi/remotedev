//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_EMPTY_BODY_HPP
#define BEAST_HTTP_EMPTY_BODY_HPP

#include <beast/config.hpp>
#include <beast/http/message.hpp>
#include <boost/optional.hpp>

namespace beast {
namespace http {

/** An empty message body

    Meets the requirements of @b Body.

    @note This body type may only be written, not read.
*/
struct empty_body
{
    /// The type of the `message::body` member
    struct value_type
    {
    };

#if BEAST_DOXYGEN
private:
#endif

    struct writer
    {
        using is_deferred = std::false_type;

        using const_buffers_type =
            boost::asio::null_buffers;

        template<bool isRequest, class Fields>
        explicit
        writer(message<
            isRequest, empty_body, Fields> const&)
        {
        }

        void
        init(error_code&)
        {
        }

        std::uint64_t
        content_length() const
        {
            return 0;
        }

        template<class WriteFunction>
        bool
        write(error_code&, WriteFunction&& wf) noexcept
        {
            return true;
        }

        boost::optional<std::pair<const_buffers_type, bool>>
        read(error_code& ec)
        {
            return boost::none;
        }
    };
};

} // http
} // beast

#endif
