# sqldb

**sqldb** is a modern, lightweight, and thread-safe C++17 wrapper for SQLite3. It provides an intuitive, fluent API for schema definition, type-safe data handling using `std::variant`, and a powerful ORM-like struct mapping system.

## Features

- **RAII Resource Management**: Automatically handles database connections and prepared statements.
- **Thread Safety**: Built-in mutex protection for concurrent access.
- **Fluent Schema Builder**: easily define tables, columns, foreign keys, and indexes.
- **Type Safety**: Uses `std::variant` to handle SQL types (INTEGER, TEXT, REAL, BLOB, NULL) safely.
- **ORM Support**: Map C++ structs directly to database tables for seamless inserts and queries.
- **Transactions**: Explicit `beginTransaction`, `commit`, and `rollback` support.
- **Advanced Querying**: Support for `JOIN`, `GROUP BY`, `HAVING`, and complex `WHERE` conditions.
- **Configuration**: Easy setup for WAL mode, foreign keys, and synchronous settings.

## Requirements

- **C++17** or later
- **CMake 3.21+**
- **SQLite3** (via system install or vcpkg)

## Installation

You can integrate `sqldb` into your project using CMake. 

1.  **Clone the repository**:
    ```bash
    git clone https://github.com/yourusername/sqldb.git
    cd sqldb
    ```

2.  **Build with CMake**:
    ```bash
    cmake -B build
    cmake --build build
    ```

3.  **Run Tests**:
    ```bash
    cd build && ctest
    ```

For detailed integration instructions, see the [User Guide](USERGUIDE.md).

## Quick Start

Here is a simple example showing how to define a table, insert data, and query it.

```cpp
#include "sqldb/sqldb.h"

int main() {
    // 1. Open Database
    Database db("my_app.db");

    // 2. Define Schema
    auto& users = db.defineTable("users");
    users.addColumn("id", SQLType::INTEGER, true, true)     // Primary Key, Auto Increment
         .addColumn("username", SQLType::TEXT)
         .addColumn("score", SQLType::REAL)
         .create();

    // 3. Insert Data
    long long aliceId = users.insert({ 
        {"username", "Alice"}, 
        {"score", 95.5} 
    });

    // 4. Query Data
    auto results = users.select({ 
        Condition{"score", Op::GT, 90.0} // WHERE score > 90.0
    });

    for (const auto& row : results) {
        std::cout << "User: " << getCol<std::string>(row, "username") << std::endl;
    }

    return 0;
}
```

## Documentation

For full API documentation, advanced usage, and ORM examples, please refer to the **[USERGUIDE.md](USERGUIDE.md)**.
