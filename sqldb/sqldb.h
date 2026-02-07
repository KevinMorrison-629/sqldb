#include <iostream>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <variant>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <mutex>

// ==========================================
// 1. Type Definitions & Helpers
// ==========================================

// Supported SQL Types
enum class SQLType {
    INTEGER,
    TEXT,
    REAL,
    BLOB,
    NULL_VAL
};

// string representation for CREATE TABLE
std::string typeToString(SQLType t) {
    switch (t) {
        case SQLType::INTEGER: return "INTEGER";
        case SQLType::TEXT:    return "TEXT";
        case SQLType::REAL:    return "REAL";
        case SQLType::BLOB:    return "BLOB";
        default: return "NULL";
    }
}

// A variant to hold data passing in/out of the DB
using SQLValue = std::variant<std::nullptr_t, int, long long, double, std::string, std::vector<char>>;

// Helper to get string representation of a value (mostly for debugging)
struct ValueToStringVisitor {
    std::string operator()(std::nullptr_t) const { return "NULL"; }
    std::string operator()(int v) const { return std::to_string(v); }
    std::string operator()(long long v) const { return std::to_string(v); }
    std::string operator()(double v) const { return std::to_string(v); }
    std::string operator()(const std::string& v) const { return v; }
    std::string operator()(const std::vector<char>&) const { return "[BLOB]"; }
};

// Represents a single row: Column Name -> Value
using Row = std::map<std::string, SQLValue>;

// Represents a WHERE condition (e.g., id = 5)
enum class Op { EQ, NEQ, GT, LT, LIKE };

struct Condition {
    std::string column;
    Op op;
    SQLValue value;

    std::string getOpString() const {
        switch(op) {
            case Op::EQ: return "=";
            case Op::NEQ: return "!=";
            case Op::GT: return ">";
            case Op::LT: return "<";
            case Op::LIKE: return "LIKE";
        }
        return "=";
    }
};

// Schema Definition Structures
struct ColumnDef {
    std::string name;
    SQLType type;
    bool isPrimaryKey = false;
    bool isAutoIncrement = false;
    bool isNotNull = false;
    
    // Foreign Key support
    std::optional<std::string> foreignTable;
    std::optional<std::string> foreignColumn;
};

// ==========================================
// 2. The Table Class
// ==========================================

class Database; // Forward declaration

class Table {
private:
    std::string tableName;
    std::vector<ColumnDef> columns;
    sqlite3* dbHandle;
    std::mutex* dbMutex; // Pointer to the shared database mutex

    // Helper to bind a variant value to a prepared statement
    void bindValue(sqlite3_stmt* stmt, int index, const SQLValue& val) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                sqlite3_bind_null(stmt, index);
            } else if constexpr (std::is_same_v<T, int>) {
                sqlite3_bind_int(stmt, index, arg);
            } else if constexpr (std::is_same_v<T, long long>) {
                sqlite3_bind_int64(stmt, index, arg);
            } else if constexpr (std::is_same_v<T, double>) {
                sqlite3_bind_double(stmt, index, arg);
            } else if constexpr (std::is_same_v<T, std::string>) {
                sqlite3_bind_text(stmt, index, arg.c_str(), -1, SQLITE_TRANSIENT);
            } else if constexpr (std::is_same_v<T, std::vector<char>>) {
                sqlite3_bind_blob(stmt, index, arg.data(), static_cast<int>(arg.size()), SQLITE_TRANSIENT);
            }
        }, val);
    }

    // Helper to extract a value from a statement column
    SQLValue getColumnValue(sqlite3_stmt* stmt, int colIndex) {
        int type = sqlite3_column_type(stmt, colIndex);
        switch (type) {
            case SQLITE_INTEGER:
                return (long long)sqlite3_column_int64(stmt, colIndex);
            case SQLITE_FLOAT:
                return sqlite3_column_double(stmt, colIndex);
            case SQLITE_TEXT:
                return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, colIndex)));
            case SQLITE_BLOB: {
                const char* blob = reinterpret_cast<const char*>(sqlite3_column_blob(stmt, colIndex));
                int size = sqlite3_column_bytes(stmt, colIndex);
                return std::vector<char>(blob, blob + size);
            }
            case SQLITE_NULL:
            default:
                return nullptr;
        }
    }

public:
    Table(std::string name, sqlite3* db, std::mutex* mtx) 
        : tableName(std::move(name)), dbHandle(db), dbMutex(mtx) {}

    // --------------------------------------------------------
    // Schema Definition Methods
    // --------------------------------------------------------
    Table& addColumn(const std::string& name, SQLType type, bool primaryKey = false, bool autoInc = false) {
        std::lock_guard<std::mutex> lock(*dbMutex);
        ColumnDef col;
        col.name = name;
        col.type = type;
        col.isPrimaryKey = primaryKey;
        col.isAutoIncrement = autoInc;
        columns.push_back(col);
        return *this;
    }

    Table& addForeignKey(const std::string& name, SQLType type, const std::string& refTable, const std::string& refCol) {
        std::lock_guard<std::mutex> lock(*dbMutex);
        ColumnDef col;
        col.name = name;
        col.type = type;
        col.foreignTable = refTable;
        col.foreignColumn = refCol;
        columns.push_back(col);
        return *this;
    }

    // Must be called to actually create the table in SQLite
    void create() {
        std::lock_guard<std::mutex> lock(*dbMutex);
        std::stringstream ss;
        ss << "CREATE TABLE IF NOT EXISTS " << tableName << " (";
        
        for (size_t i = 0; i < columns.size(); ++i) {
            const auto& col = columns[i];
            ss << col.name << " " << typeToString(col.type);

            if (col.isPrimaryKey) ss << " PRIMARY KEY";
            if (col.isAutoIncrement) ss << " AUTOINCREMENT";
            if (col.isNotNull) ss << " NOT NULL";

            if (col.foreignTable.has_value()) {
                ss << ", FOREIGN KEY(" << col.name << ") REFERENCES " 
                   << col.foreignTable.value() << "(" << col.foreignColumn.value() << ")";
            }

            if (i < columns.size() - 1) ss << ", ";
        }
        ss << ");";

        std::string sql = ss.str();
        char* errMsg = nullptr;
        int rc = sqlite3_exec(dbHandle, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            std::string err = errMsg;
            sqlite3_free(errMsg);
            throw std::runtime_error("Failed to create table " + tableName + ": " + err);
        }
    }

    // --------------------------------------------------------
    // CRUD Operations
    // --------------------------------------------------------

    // CREATE (Insert)
    void insert(const Row& row) {
        std::lock_guard<std::mutex> lock(*dbMutex);
        std::stringstream ss;
        ss << "INSERT INTO " << tableName << " (";
        
        std::vector<SQLValue> values;
        size_t idx = 0;
        for (const auto& [key, val] : row) {
            ss << key;
            values.push_back(val);
            if (idx < row.size() - 1) ss << ", ";
            idx++;
        }

        ss << ") VALUES (";
        for (size_t i = 0; i < values.size(); ++i) {
            ss << "?";
            if (i < values.size() - 1) ss << ", ";
        }
        ss << ");";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(dbHandle, ss.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Prepare failed: " + std::string(sqlite3_errmsg(dbHandle)));
        }

        for (int i = 0; i < values.size(); ++i) {
            bindValue(stmt, i + 1, values[i]);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(dbHandle);
            sqlite3_finalize(stmt);
            throw std::runtime_error("Insert failed: " + err);
        }

        sqlite3_finalize(stmt);
    }

    // READ (Select)
    std::vector<Row> select(const std::vector<Condition>& where = {}) {
        std::lock_guard<std::mutex> lock(*dbMutex);
        std::stringstream ss;
        ss << "SELECT * FROM " << tableName;
        
        if (!where.empty()) {
            ss << " WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                ss << where[i].column << " " << where[i].getOpString() << " ?";
                if (i < where.size() - 1) ss << " AND ";
            }
        }
        ss << ";";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(dbHandle, ss.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Prepare failed: " + std::string(sqlite3_errmsg(dbHandle)));
        }

        for (int i = 0; i < where.size(); ++i) {
            bindValue(stmt, i + 1, where[i].value);
        }

        std::vector<Row> results;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Row row;
            int colCount = sqlite3_column_count(stmt);
            for (int i = 0; i < colCount; ++i) {
                std::string name = sqlite3_column_name(stmt, i);
                row[name] = getColumnValue(stmt, i);
            }
            results.push_back(row);
        }

        sqlite3_finalize(stmt);
        return results;
    }

    // UPDATE
    void update(const Row& data, const std::vector<Condition>& where) {
        if (data.empty()) return;

        std::lock_guard<std::mutex> lock(*dbMutex);
        std::stringstream ss;
        ss << "UPDATE " << tableName << " SET ";
        
        std::vector<SQLValue> bindings;
        size_t idx = 0;
        for (const auto& [key, val] : data) {
            ss << key << " = ?";
            bindings.push_back(val);
            if (idx < data.size() - 1) ss << ", ";
            idx++;
        }

        if (!where.empty()) {
            ss << " WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                ss << where[i].column << " " << where[i].getOpString() << " ?";
                bindings.push_back(where[i].value);
                if (i < where.size() - 1) ss << " AND ";
            }
        }

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(dbHandle, ss.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Prepare failed: " + std::string(sqlite3_errmsg(dbHandle)));
        }

        for (int i = 0; i < bindings.size(); ++i) {
            bindValue(stmt, i + 1, bindings[i]);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(dbHandle);
            sqlite3_finalize(stmt);
            throw std::runtime_error("Update failed: " + err);
        }
        sqlite3_finalize(stmt);
    }

    // DELETE
    void remove(const std::vector<Condition>& where) {
        std::lock_guard<std::mutex> lock(*dbMutex);
        std::stringstream ss;
        ss << "DELETE FROM " << tableName;

        if (!where.empty()) {
            ss << " WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                ss << where[i].column << " " << where[i].getOpString() << " ?";
                if (i < where.size() - 1) ss << " AND ";
            }
        }

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(dbHandle, ss.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Prepare failed: " + std::string(sqlite3_errmsg(dbHandle)));
        }

        for (int i = 0; i < where.size(); ++i) {
            bindValue(stmt, i + 1, where[i].value);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(dbHandle);
            sqlite3_finalize(stmt);
            throw std::runtime_error("Delete failed: " + err);
        }
        sqlite3_finalize(stmt);
    }
};

// ==========================================
// 3. The Database Manager
// ==========================================

class Database {
private:
    sqlite3* db;
    std::map<std::string, Table> tables;
    std::mutex dbMutex; // Mutex to synchronize access to the database connection

public:
    Database(const std::string& filename) {
        // Note: We use dbMutex to protect the opening, though in constructor it's technically
        // single-threaded unless the object is global. Good practice to lock anyway if re-entrant.
        // But mutex members are not initialized until constructor body, so we trust constructor is unique.
        if (sqlite3_open(filename.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("Can't open database: " + std::string(sqlite3_errmsg(db)));
        }
        // Enable Foreign Keys (SQLite defaults to off)
        char* errMsg = nullptr;
        sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, &errMsg);
        if (errMsg) sqlite3_free(errMsg);
    }

    ~Database() {
        if (db) {
            sqlite3_close(db);
        }
    }

    // Start defining a new table
    Table& defineTable(const std::string& name) {
        std::lock_guard<std::mutex> lock(dbMutex);
        // Construct table in map using piecewise construction, passing the mutex pointer
        auto res = tables.emplace(std::piecewise_construct, 
                                  std::forward_as_tuple(name), 
                                  std::forward_as_tuple(name, db, &dbMutex));
        return res.first->second;
    }

    // Retrieve an existing table wrapper
    Table& getTable(const std::string& name) {
        std::lock_guard<std::mutex> lock(dbMutex);
        auto it = tables.find(name);
        if (it == tables.end()) {
            throw std::runtime_error("Table not defined in wrapper: " + name);
        }
        return it->second;
    }
};