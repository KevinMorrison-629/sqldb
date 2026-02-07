add_executable(test 
    main_test.cpp
    test_basics.cpp
    test_orm.cpp
    test_advanced.cpp
    test_transactions.cpp
    test_performance.cpp
)
target_link_libraries(test PRIVATE sqldb)
