//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_VERSION_HPP
#define BEAST_VERSION_HPP

#include <beast/config.hpp>

#define BEAST_QUOTE(arg) #arg
#define BEAST_STR(arg) BEAST_QUOTE(arg)

/** @def BEAST_API_VERSION 

    Identifies the API version of Beast.

    This is a simple integer that is incremented by one every time
    a set of code changes is merged to the master or develop branch.
*/
#define BEAST_VERSION 43

#define BEAST_VERSION_STRING "Beast/" BEAST_STR(BEAST_VERSION)

#endif

