#include "test_utils.h"

void test_orm(Database& db) {
    std::cout << "\n=== Testing ORM Struct Mapping ===" << std::endl;
    
    // Insert using struct
    std::cout << "Inserting Charlie via ORM..." << std::endl;
    UserInput charlie = {"Charlie", 88.5};
    db.getTable("users").insert(charlie); // Uses generic insert

    // Select using Struct (Table method)
    std::cout << "Selecting all users as structs:" << std::endl;
    auto allUsers = db.getTable("users").query<UserStruct>();
    for (const auto& u : allUsers) {
        std::cout << "  [ORM] User: " << u.username << ", Score: " << u.score << ", ID: " << u.id << std::endl;
    }
    
    // Select using Struct (Database method)
    std::cout << "Selecting high scorers via db.query<UserStruct>..." << std::endl;
    auto bestUsers = db.query<UserStruct>({ Condition{"score", Op::GT, 90.0} });
    for (const auto& u : bestUsers) { // Should be Bob(99.9)
         std::cout << "  [DB-ORM] Found: " << u.username << std::endl;
    }
}
