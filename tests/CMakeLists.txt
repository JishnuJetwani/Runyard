add_executable(domain_test domain_test.cpp)
target_link_libraries(domain_test PRIVATE runyard_domain GTest::gtest_main)
gtest_discover_tests(domain_test)
