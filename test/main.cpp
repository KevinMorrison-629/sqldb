#include <iostream>
#include "sqldb/sqldb.h"

int main() {
    try {
        std::cout << "Initializing Database..." << std::endl;
        // 1. Create Connection
        Database db("app_data.db");

        // 2. Define 'Users' Table
        // equivalent: CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT, score REAL);
        auto& users = db.defineTable("users");
        users.addColumn("id", SQLType::INTEGER, true, true)
             .addColumn("username", SQLType::TEXT)
             .addColumn("score", SQLType::REAL)
             .create();

        // 3. Define 'Posts' Table with Foreign Key
        // equivalent: ... FOREIGN KEY(user_id) REFERENCES users(id)
        auto& posts = db.defineTable("posts");
        posts.addColumn("id", SQLType::INTEGER, true, true)
             .addColumn("title", SQLType::TEXT)
             .addForeignKey("user_id", SQLType::INTEGER, "users", "id")
             .create();

        std::cout << "Tables created successfully." << std::endl;

        // 4. Create (Insert)
        std::cout << "Inserting users..." << std::endl;
        users.insert({ {"username", "Alice"}, {"score", 95.5} });
        users.insert({ {"username", "Bob"},   {"score", 80.0} });
        
        // Insert post for user 1 (Alice)
        // Note: We use '1' assuming Alice got ID 1. In real app, you'd query the ID first.
        posts.insert({ {"title", "Alice's First Post"}, {"user_id", 1} });

        // 5. Read (Select)
        std::cout << "\nReading Users with score > 90:" << std::endl;
        auto highScorers = users.select({ 
            Condition{"score", Op::GT, 90.0} 
        });

        for (const auto& row : highScorers) {
            // Helper to print std::variant
            std::cout << "User: " 
                      << std::get<std::string>(row.at("username")) 
                      << " (ID: " << std::get<long long>(row.at("id")) << ")" << std::endl;
        }

        // 6. Update
        std::cout << "\nUpdating Bob's score..." << std::endl;
        users.update(
            { {"score", 99.9} },            // Set values
            { Condition{"username", Op::EQ, "Bob"} } // Where clause
        );

        // 7. Verify Update
        auto bobRow = users.select({ Condition{"username", Op::EQ, "Bob"} });
        if(!bobRow.empty()) {
             std::cout << "Bob's new score: " << std::get<double>(bobRow[0].at("score")) << std::endl;
        }

        // 8. Delete
        std::cout << "\nDeleting Alice..." << std::endl;
        // This might fail if Foreign Keys are enforced and we didn't cascade delete, 
        // but for this example, we just show the syntax.
        try {
            users.remove({ Condition{"username", Op::EQ, "Alice"} });
            std::cout << "Alice deleted." << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Could not delete Alice (FK Constraint?): " << e.what() << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Database Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}