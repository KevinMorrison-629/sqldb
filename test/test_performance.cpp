#include "test_utils.h"

void test_performance(Database& db) {
    std::cout << "\n=== Performance & Timing Tests ===" << std::endl;
    
    // We use defineTable here, which links to BenchUser ORM if we use it, 
    // but here we are using manual table definition for clarity in the test.
    auto& users = db.defineTable("bench_users");
    users.addColumn("id", SQLType::INTEGER, true, true)
         .addColumn("username", SQLType::TEXT)
         .addColumn("email", SQLType::TEXT)
         .addColumn("age", SQLType::INTEGER)
         .addColumn("score", SQLType::REAL)
         .create();

    const int ROW_COUNT = 10000; // Adjust as needed
    std::cout << "Inserting " << ROW_COUNT << " rows inside a transaction..." << std::endl;

    {
        Timer t("Bulk Insert");
        auto txn = db.transaction();
        for (int i = 0; i < ROW_COUNT; ++i) {
            users.insert({
                {"username", "User" + std::to_string(i)},
                {"email", "user" + std::to_string(i) + "@example.com"},
                {"age", i % 100},
                {"score", (double)(i % 1000) / 10.0}
            });
        }
        txn.commit();
    }

    // Test Select Performance without Index
    std::cout << "Querying without index..." << std::endl;
    {
        Timer t("Select (No Index)");
        auto result = users.select({ Condition{"username", Op::EQ, "User5000"} });
    }

    // Add Index
    std::cout << "Creating index on username..." << std::endl;
    {
        Timer t("Create Index");
        users.createIndex("idx_bench_username", "username", true);
    }

    // Test Select Performance with Index
    std::cout << "Querying with index..." << std::endl;
    {
        Timer t("Select (With Index)");
        auto result = users.select({ Condition{"username", Op::EQ, "User5000"} });
    }
    
    // Complex Query with Group By
    std::cout << "Complex Query (Group By Age)..." << std::endl;
    {
        Timer t("Group By Query");
        QueryOptions opts;
        opts.columns = {"age", "count(id)"}; // count(id) is aggregate
        opts.groupBy.push_back("age");
        // We aren't checking result correctness here, just timing execution
        auto result = users.select({}, opts);
    }
}
