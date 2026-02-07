#include "test_utils.h"

void test_basics(Database& db) {
    std::cout << "\n=== Testing Basic CRUD Operations ===" << std::endl;
    
    // 1. Define 'Users' Table
    auto& users = db.defineTable("users");
    users.addColumn("id", SQLType::INTEGER, true, true)
         .addColumn("username", SQLType::TEXT)
         .addColumn("score", SQLType::REAL)
         .create();

    // 2. Define 'Posts' Table with Foreign Key
    auto& posts = db.defineTable("posts");
    posts.addColumn("id", SQLType::INTEGER, true, true)
         .addColumn("title", SQLType::TEXT)
         .addForeignKey("user_id", SQLType::INTEGER, "users", "id", true)
         .create();

    std::cout << "Tables created successfully." << std::endl;

    // 3. Create (Insert)
    std::cout << "Inserting users..." << std::endl;
    long long aliceId = users.insert({ {"username", "Alice"}, {"score", 95.5} });
    users.insert({ {"username", "Bob"},   {"score", 80.0} });
    
    // Insert post for user 1 (Alice) using returned ID
    posts.insert({ {"title", "Alice's First Post"}, {"user_id", aliceId} });
    
    // 4. Read (Select)
    std::cout << "Reading Users with score > 90 (Ordered by Score DESC):" << std::endl;
    QueryOptions opts;
    opts.orderBy = "score";
    opts.orderDesc = true;
    
    auto highScorers = users.select({ 
        Condition{"score", Op::GT, 90.0} 
    }, opts);

    for (const auto& row : highScorers) {
        std::cout << "User: " 
                  << getCol<std::string>(row, "username") 
                  << " (ID: " << getCol<long long>(row, "id") << ")" << std::endl;
    }

    // 5. Update
    std::cout << "Updating Bob's score..." << std::endl;
    users.update(
        { {"score", 99.9} },
        { Condition{"username", Op::EQ, "Bob"} }
    );

    // Verify Update
    auto bobRow = users.select({ Condition{"username", Op::EQ, "Bob"} });
    if(!bobRow.empty()) {
         std::cout << "Bob's new score: " << getCol<double>(bobRow[0], "score") << std::endl;
    }

    // 6. Delete
    std::cout << "Deleting Alice..." << std::endl;
    users.remove({ Condition{"username", Op::EQ, "Alice"} });
    std::cout << "Alice deleted." << std::endl;
    
    // Verify cascade delete
    auto alicePosts = posts.select({ Condition{"title", Op::EQ, "Alice's First Post"} });
    if (alicePosts.empty()) {
         std::cout << "Alice's posts were automatically deleted." << std::endl;
    } else {
         std::cerr << "Error: Alice's posts still exist!" << std::endl;
    }
}
