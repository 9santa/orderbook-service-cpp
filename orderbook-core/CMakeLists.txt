add_library(orderbook_core
    src/order_book.cpp
)

target_include_directories(orderbook_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(orderbook_core PUBLIC cxx_std_20)
target_compile_options(orderbook_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(orderbook_core_tests
    tests/order_book_tests.cpp
)

target_link_libraries(orderbook_core_tests PRIVATE orderbook_core)
target_compile_options(orderbook_core_tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME orderbook_core_tests COMMAND orderbook_core_tests)
