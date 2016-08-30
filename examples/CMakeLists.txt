# Part of Beast

GroupSources(include/beast beast)
GroupSources(extras/beast extras)

GroupSources(examples "/")

add_executable (http-crawl
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    urls_large_data.hpp
    urls_large_data.cpp
    http_crawl.cpp
)

if (NOT WIN32)
    target_link_libraries(http-crawl ${Boost_LIBRARIES} Threads::Threads)
endif()

add_executable (http-server
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    file_body.hpp
    mime_type.hpp
    http_server.cpp
)

if (NOT WIN32)
    target_link_libraries(http-server ${Boost_LIBRARIES} Threads::Threads)
endif()

add_executable (http-example
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    http_example.cpp
)

if (NOT WIN32)
    target_link_libraries(http-example ${Boost_LIBRARIES} Threads::Threads)
endif()

add_executable (websocket-example
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    websocket_example.cpp
)

if (NOT WIN32)
    target_link_libraries(websocket-example ${Boost_LIBRARIES} Threads::Threads)
endif()
