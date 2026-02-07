#include <iostream>
#include <cstdio>
#include "test_utils.h"

int main() {
    const std::string dbFile = "test_suite.db";
    std::remove(dbFile.c_str()); // Clean slate

    std::cout << "Starting Comprehensive SQLDB Test Suite..." << std::endl;

    try {
        Database db(dbFile);

        test_basics(db);
        test_orm(db);
        test_advanced(db); // Covers Joins, GroupBy, Indexing, Constraints, Blob
        test_transactions(db); // Covers Rollback/Commit explicitly
        test_performance(db);

    } catch (const std::exception& e) {
        std::cerr << "Test Suite Failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\nAll Tests Completed Successfully." << std::endl;
    return 0;
}
