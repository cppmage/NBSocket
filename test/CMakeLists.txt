find_package(GTest REQUIRED)

add_executable(nbsocket_tests test_socket.cpp test_session_acceptor.cpp)

target_link_libraries(nbsocket_tests 
    PRIVATE 
        nbsocket 
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(nbsocket_tests)
