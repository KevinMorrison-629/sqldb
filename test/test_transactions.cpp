#include "test_utils.h"

void test_transactions(Database& db) {
    std::cout << "\n=== Testing Transaction Support ===" << std::endl;
    auto& table = db.defineTable("txn_test");
    table.addColumn("id", SQLType::INTEGER, true, true)
         .addColumn("val", SQLType::INTEGER)
         .create();

    // 1. Successful commit
    std::cout << "Testing Commit..." << std::endl;
    {
        auto txn = db.transaction();
        table.insert({ {"val", 100} });
        txn.commit();
    }
    auto rows = table.select({ Condition{"val", Op::EQ, 100} });
    if (rows.size() == 1) {
        std::cout << "Commit Works." << std::endl;
    } else {
        std::cerr << "Commit Failed!" << std::endl;
    }

    // 2. Rollback (Destructor)
    std::cout << "Testing Rollback (via Destructor)..." << std::endl;
    int countBefore = table.select().size();
    {
        auto txn = db.transaction();
        table.insert({ {"val", 200} });
        // No commit() -> Rollback
    }
    
    int countAfter = table.select().size();
    if (countAfter == countBefore) {
        std::cout << "Rollback Works. Row count unchanged." << std::endl;
    } else {
        std::cerr << "Rollback Failed! Rows increased." << std::endl;
    }

    // 3. Explicit Rollback
    std::cout << "Testing Explicit Rollback..." << std::endl;
    {
        auto txn = db.transaction();
        table.insert({ {"val", 300} });
        txn.rollback();
    }
    if (table.select({ Condition{"val", Op::EQ, 300} }).empty()) {
         std::cout << "Explicit Rollback Works." << std::endl;
    } else {
         std::cerr << "Explicit Rollback Failed." << std::endl;
    }
}
