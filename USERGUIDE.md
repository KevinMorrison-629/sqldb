# sqldb User Guide

This guide provides comprehensive documentation for using **sqldb**, a modern C++17 wrapper for SQLite3.

---

## Table of Contents
1.  [Getting Started](#getting-started)
2.  [Core Concepts](#core-concepts)
3.  [Schema Definition](#schema-definition)
4.  [Basic Operations](#basic-operations)
5.  [Advanced Selecting & Filtering](#advanced-selecting--filtering)
6.  [ORM (Object-Relational Mapping)](#orm-object-relational-mapping)
7.  [Transactions](#transactions)
8.  [Configuration](#configuration)

---

## Getting Started

### Prerequisites
- C++17 or newer
- CMake 3.21 or newer
- SQLite3 development libraries

### Integration via CMake
Add `sqldb` as a subdirectory in your `CMakeLists.txt`:

```cmake
add_subdirectory(sqldb)
target_link_libraries(your_target PRIVATE sqldb sqlite3)
```

Ensure SQLite3 is found:
```cmake
find_package(SQLite3 REQUIRED)
target_link_libraries(your_target PRIVATE SQLite::SQLite3)
```

---

## Core Concepts

### Database Handle
To start, create a `Database` object. This manages the connection and thread safety.

```cpp
#include "sqldb/sqldb.h"

// Opens (or creates) "app.db"
Database db("app.db");

// You can also specify config options:
Config cfg;
cfg.enableWAL = true; // Write-Ahead Logging for better concurrency
Database db("app.db", cfg);
```

### Table Representation
Use `db.defineTable("name")` to create a schema definition, or `db.getTable("name")` to access an existing table.

```cpp
auto& users = db.defineTable("users"); // Returns a Table&
```

### Types & Variants
`sqldb` uses `std::variant` (aliased as `SQLValue`) to handle data safely. Supported types:
- `SQLType::INTEGER` -> `long long` (or `int`)
- `SQLType::REAL`    -> `double` (or `float`)
- `SQLType::TEXT`    -> `std::string`
- `SQLType::BLOB`    -> `std::vector<char>`
- `SQLType::NULL_VAL` -> `nullptr_t`

---

## Schema Definition

You can define tables fluently using method chaining. Call `.create()` at the end to execute the SQL.

### Methods
- `addColumn(name, type, [isPrimaryKey], [isAutoIncrement])`
- `addForeignKey(name, type, refTable, refColumn, [onDeleteCascade])`
- `createIndex(indexName, columnName, [unique])`
- `create()`: Executes the schema creation.

### Example

```cpp
// 1. Users Table
auto& users = db.defineTable("users");
users.addColumn("id", SQLType::INTEGER, true, true)
     .addColumn("username", SQLType::TEXT)
     .addColumn("email", SQLType::TEXT)
     .create();

// 2. Posts Table with Foreign Key
auto& posts = db.defineTable("posts");
posts.addColumn("id", SQLType::INTEGER, true, true)
     .addColumn("title", SQLType::TEXT)
     .addColumn("content", SQLType::TEXT)
     .addForeignKey("user_id", SQLType::INTEGER, "users", "id", true) // CASCADE delete
     .create();

// 3. Create Index
users.createIndex("idx_email", "email", true); // UNIQUE index
```

---

## Basic Operations

### Insert
Provide a `Row` (map of column name -> value) to insert data.

```cpp
long long newId = users.insert({ 
    {"username", "Alice"}, 
    {"email", "alice@example.com"} 
});
```

### Select
Returns `std::vector<Row>`.

```cpp
// Select all
auto allUsers = users.select(); 

// Access columns safely with helper
for(const auto& row : allUsers) {
    std::cout << getCol<std::string>(row, "username") << "\n";
}
```

### Update
Requires data to update and a WHERE condition.

```cpp
users.update(
    { {"email", "newalice@example.com"} },      // SET
    { Condition{"username", Op::EQ, "Alice"} }  // WHERE
);
```

### Delete

```cpp
users.remove({ 
    Condition{"username", Op::EQ, "Alice"} 
});
```

---

## Advanced Selecting & Filtering

You can perform complex queries using `Condition` and `QueryOptions`.

### Filtering (WHERE)
`Condition` struct takes: `column`, `Op` (operator), and `value`.
Operators: `EQ` (=), `NEQ` (!=), `GT` (>), `LT` (<), `LIKE`.

```cpp
auto olderUsers = users.select({
    Condition{"age", Op::GT, 21},
    Condition{"active", Op::EQ, 1}
});
```

### Sorting, Limiting, Grouping
Use `QueryOptions` for ORDER BY, LIMIT, OFFSET, GROUP BY.

```cpp
QueryOptions opts;
opts.orderBy = "score";
opts.orderDesc = true;
opts.limit = 10;
opts.groupBy = {"department"}; // Group by department

auto topUsers = users.select({}, opts);
```

### Joins
You can join tables using `QueryOptions`.

```cpp
QueryOptions opts;
opts.joins.push_back({
    JoinType::INNER, 
    "posts", 
    "users.id = posts.user_id"
});
// Select specific columns to avoid collisions
opts.columns = {"users.username", "posts.title"};

auto results = users.select({ Condition{"users.id", Op::EQ, 1} }, opts);
```

---

## ORM (Object-Relational Mapping)

`sqldb` allows mapping C++ structs directly to database tables.

### 1. Define your Struct
```cpp
struct User {
    long long id;
    std::string name;
    double score;
};
```

### 2. Specialize `ORM<T>`
You must tell `sqldb` how to map the struct members to columns. This MUST be done in the global scope or outside the function.

```cpp
template<>
struct ORM<User> {
    static constexpr const char* table = "users"; // Table name
    static auto map() {
        return std::make_tuple(
            orm_field(&User::id, "id"),
            orm_field(&User::name, "username"),
            orm_field(&User::score, "score")
        );
    }
};
```

### 3. Use Typed Methods
Now you can use `insert(struct)` and `query<T>()`.

```cpp
// Insert
User u = {0, "Bob", 88.5}; // ID is ignored if auto-increment
db.getTable("users").insert(u);

// Query
std::vector<User> results = db.query<User>({ 
    Condition{"score", Op::GT, 50.0} 
});

for(const auto& user : results) {
    std::cout << user.name << ": " << user.score << "\n";
}
```

---

## Transactions

Wrap multiple operations in a transaction for atomicity. If an exception occurs, `sqldb` expects you to catch it and rollback.

```cpp
db.beginTransaction();
try {
    users.insert({ {"username", "Charlie"} });
    posts.insert({ {"user_id", 1}, {"title", "Hello"} });
    
    db.commit(); // Save changes
} catch (const std::exception& e) {
    db.rollback(); // Revert changes
    std::cerr << "Transaction failed: " << e.what() << std::endl;
}
```

---

## Configuration

The `Config` struct allows tuning SQLite behavior.

```cpp
struct Config {
    bool enableForeignKeys = true;       // PRAGMA foreign_keys = ON
    bool enableWAL = true;               // PRAGMA journal_mode = WAL
    SyncMode synchronous = SyncMode::NORMAL; // PRAGMA synchronous
};
```

- **WAL (Write-Ahead Logging)**: parallelize readers and writers.
- **Synchronous**: Controls how often SQLite writes to disk. `NORMAL` is a safe default for WAL mode. `OFF` is faster but less safe.
