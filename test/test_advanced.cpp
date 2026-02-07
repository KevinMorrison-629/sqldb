#include "test_utils.h"

void test_advanced(Database& db) {
    std::cout << "\n=== Testing Advanced Features ===" << std::endl;

    auto& users = db.getTable("users");
    auto& posts = db.getTable("posts");

    // 1. Indexing
    std::cout << "\n--- Indexing ---" << std::endl;
    users.createIndex("idx_username", "username", true);
    std::cout << "Index created on username." << std::endl;

    // 2. Joins
    std::cout << "\n--- Joins ---" << std::endl;
    // Insert a post for Bob so we can test the JOIN (Bob's ID: 2, exists from basics test)
    // We need Bob's ID.
    auto bobRows = users.select({ Condition{"username", Op::EQ, "Bob"} });
    if (!bobRows.empty()) {
        long long bobId = getCol<long long>(bobRows[0], "id");
        posts.insert({ {"title", "Bob's Thoughts"}, {"user_id", bobId} });
        posts.insert({ {"title", "Bob's Second Post"}, {"user_id", bobId} });
    }

    QueryOptions joinOpts;
    joinOpts.columns = {"users.username", "posts.title"};
    joinOpts.joins.push_back({JoinType::INNER, "posts", "users.id = posts.user_id"});
    
    auto joinResults = users.select({}, joinOpts);
    for (const auto& row : joinResults) {
        std::cout << "User: " << getCol<std::string>(row, "username") 
                  << " wrote: " << getCol<std::string>(row, "title") << std::endl;
    }

    // 3. Group By & Having
    std::cout << "\n--- Group By & Having ---" << std::endl;
    QueryOptions groupOpts;
    groupOpts.columns = {"users.username", "COUNT(posts.id)"};
    groupOpts.joins.push_back({JoinType::INNER, "posts", "users.id = posts.user_id"});
    groupOpts.groupBy.push_back("users.username");
    groupOpts.having.push_back(Condition{"COUNT(posts.id)", Op::GT, 1}); // Bob has 2 posts now

    auto groupResults = users.select({}, groupOpts);
    for (const auto& row : groupResults) {
        std::cout << "User: " << getCol<std::string>(row, "username") 
                  << " has " << getCol<long long>(row, "COUNT(posts.id)") << " posts." << std::endl;
    }
    
    // 4. Sanitization (Reserved Keywords)
    std::cout << "\n--- Sanitization ---" << std::endl;
    // 'group' is a reserved keyword in SQL
    auto& groupTable = db.defineTable("group");
    groupTable.addColumn("id", SQLType::INTEGER, true, true)
              .addColumn("order", SQLType::INTEGER) // 'order' is also reserved
              .create();
    
    groupTable.insert({ {"order", 1} });
    auto groupRows = groupTable.select({ Condition{"order", Op::EQ, 1} });
    if (groupRows.size() == 1) {
        std::cout << "Successfully queried table 'group' with column 'order'." << std::endl;
    } else {
         std::cerr << "Sanitization Test Failed!" << std::endl;
    }

    // 5. Constraints
    std::cout << "\n--- Constraints ---" << std::endl;
    auto& cTable = db.defineTable("constraints_test");
    cTable.addColumn("id", SQLType::INTEGER, true, true)
         .addColumn("unique_col", SQLType::TEXT) 
         .create();
    cTable.createIndex("idx_unique_col", "unique_col", true);

    try {
        cTable.insert({ {"unique_col", "duplicate"} });
        cTable.insert({ {"unique_col", "duplicate"} });
        std::cerr << "Unique Constraint Test Failed! Duplicate inserted." << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Unique Constraint Works (Caught: " << e.what() << ")" << std::endl;
    }

    // 6. BLOBs
    std::cout << "\n--- BLOB Data ---" << std::endl;
    auto& bTable = db.defineTable("blob_test");
    bTable.addColumn("id", SQLType::INTEGER, true, true)
          .addColumn("data", SQLType::BLOB)
          .create();

    std::vector<char> binaryData = {0, 1, 2, 3, 4, 127, -128, 55};
    bTable.insert({ {"data", binaryData} });

    auto bRows = bTable.select();
    std::vector<char> retrieved = getCol<std::vector<char>>(bRows[0], "data");

    if (retrieved == binaryData) {
        std::cout << "BLOB Data Integrity Verified." << std::endl;
    } else {
        std::cerr << "BLOB Data Mismatch!" << std::endl;
    }

    // 7. NULL and LIKE
    std::cout << "\n--- NULL and LIKE ---" << std::endl;
    auto& nTable = db.defineTable("null_like_test");
    nTable.addColumn("name", SQLType::TEXT)
          .addColumn("desc", SQLType::TEXT)
          .create();

    nTable.insert({ {"name", "NullItem"}, {"desc", nullptr} });
    nTable.insert({ {"name", "LikeItem"}, {"desc", "Hello World"} });

    // Verify NULL
    auto nullRows = nTable.select({ Condition{"name", Op::EQ, "NullItem"} });
    if (!nullRows.empty() && std::holds_alternative<std::nullptr_t>(nullRows[0]["desc"])) {
        std::cout << "NULL Retrieval Verified." << std::endl;
    } else {
        std::cerr << "NULL Retrieval Failed." << std::endl;
    }

    // Verify LIKE
    auto likeRows = nTable.select({ Condition{"desc", Op::LIKE, "Hello%"} });
    if (likeRows.size() == 1) {
        std::cout << "LIKE Operator Verified." << std::endl;
    } else {
        std::cerr << "LIKE Operator Failed." << std::endl;
    }
}
