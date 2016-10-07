//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_SERVER_IO_THREADS_HPP
#define BEAST_SERVER_IO_THREADS_HPP

#include <boost/asio/io_service.hpp>
#include <boost/optional.hpp>
#include <thread>
#include <vector>

namespace beast {

/** A set of threads calling `io_service::run`.
*/
class io_threads
{
    std::vector<std::thread> v_;
    boost::asio::io_service ios_;
    boost::optional<
        boost::asio::io_service::work> work_;

public:
    explicit
    io_threads(std::size_t n = 1)
        : work_(ios_)
    {
        v_.reserve(n);
        while(n--)
            v_.emplace_back(
                [&]{ ios_.run(); });
    }
    
    ~io_threads()
    {
        work_ = boost::none;
        for(auto& t : v_)
            t.join();
    }

    boost::asio::io_service&
    get_io_service()
    {
        return ios_;
    }
};

} // beast

#endif
