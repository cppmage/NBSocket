add_executable(example
    example.cpp
)

target_link_libraries(example
    PRIVATE
        nbsocket
        Boost::fiber
)
