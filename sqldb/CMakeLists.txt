

find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(sqldb INTERFACE)

# Allow consumers to include headers via "sqldb/sqldb.h"
target_include_directories(sqldb INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(sqldb INTERFACE 
    unofficial::sqlite3::sqlite3
)
