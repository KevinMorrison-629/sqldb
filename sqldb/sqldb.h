#pragma once
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
#include <memory>
#include <unordered_map>
#include <list>
#include <tuple> // Added for ORM mappings

namespace sqldb {

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



enum class SyncMode {
    OFF,
    NORMAL,
    FULL,
    EXTRA
};

struct Config {
    bool enableForeignKeys = true;
    bool enableWAL = true;
    SyncMode synchronous = SyncMode::NORMAL;
};

inline std::string quoteIdentifier(const std::string& id) {
    std::string escaped = id;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\"\"");
        pos += 2;
    }
    return "\"" + escaped + "\"";
}

// string representation for CREATE TABLE
inline std::string typeToString(SQLType t) {
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

// Helper for easier access
template<typename T>
T getCol(const Row& row, const std::string& key) {
    auto it = row.find(key);
    if (it == row.end()) throw std::runtime_error("Column not found: " + key);
    
    if (std::holds_alternative<T>(it->second)) {
        return std::get<T>(it->second);
    }
    
    // Coercions
    if constexpr (std::is_same_v<T, int>) {
        if (std::holds_alternative<long long>(it->second)) 
            return static_cast<int>(std::get<long long>(it->second));
    }
    if constexpr (std::is_same_v<T, long long>) {
        if (std::holds_alternative<int>(it->second)) 
            return static_cast<long long>(std::get<int>(it->second));
    }
    
    throw std::runtime_error("Column type mismatch: " + key);
}

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
    bool onDeleteCascade = false;
};

// Join Types
enum class JoinType {
    INNER,
    LEFT,
    RIGHT,
    CROSS
};

struct JoinClause {
    JoinType type;
    std::string table;
    std::string onCondition; // e.g., "users.id = posts.user_id"

    std::string getTypeString() const {
        switch (type) {
            case JoinType::INNER: return "INNER JOIN";
            case JoinType::LEFT:  return "LEFT JOIN";
            case JoinType::RIGHT: return "RIGHT JOIN"; // Note: SQLite only supports LEFT (and inner), RIGHT might not work directly in standard SQLite3 builds without extensions or workarounds, but we'll include the enum. Standard SQLite doesn't support RIGHT JOIN.
            case JoinType::CROSS: return "CROSS JOIN";
        }
        return "JOIN";
    }
};

struct QueryOptions {
    std::vector<std::string> columns; // Empty or {"*"} implies all.
    std::vector<JoinClause> joins;
    std::vector<std::string> groupBy; // Added for Grouping
    std::vector<Condition> having;    // Added for Filtering Groups
    std::string orderBy;
    bool orderDesc = false;
    int limit = -1;
    int offset = -1;
};

// ==========================================
// 2. Internal Context & RAII Helpers
// ==========================================

struct DBContext {
    sqlite3* db = nullptr;
    std::mutex mtx;

    // LRU Cache Data Structures
    // Use shared_ptr with custom deleter handling finalized statement
    using StmtPtr = std::shared_ptr<sqlite3_stmt>;
    using CacheEntry = std::pair<StmtPtr, std::list<std::string>::iterator>;
    std::unordered_map<std::string, CacheEntry> statementCache;
    std::list<std::string> lruList; // Front = MRU, Back = LRU
    const size_t MAX_CACHE_SIZE = 64;

    DBContext(const std::string& filename, const Config& config = {}) {
        if (sqlite3_open(filename.c_str(), &db) != SQLITE_OK) {
            std::string err = db ? sqlite3_errmsg(db) : "Unknown error";
             if (db) { sqlite3_close(db); db = nullptr; }
            throw std::runtime_error("Can't open database: " + err);
        }

        char* errMsg = nullptr;

        // 1. Foreign Keys
        std::string fkPragma = config.enableForeignKeys ? "PRAGMA foreign_keys = ON;" : "PRAGMA foreign_keys = OFF;";
        if (sqlite3_exec(db, fkPragma.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
             // Handle error or log
             if(errMsg) sqlite3_free(errMsg);
        }

        // 2. Journal Mode (WAL)
        if (config.enableWAL) {
             sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        }

        // 3. Synchronous Mode
        const char* syncPragma = "PRAGMA synchronous = NORMAL;";
        switch(config.synchronous) {
            case SyncMode::OFF: syncPragma = "PRAGMA synchronous = OFF;"; break;
            case SyncMode::FULL: syncPragma = "PRAGMA synchronous = FULL;"; break;
            case SyncMode::EXTRA: syncPragma = "PRAGMA synchronous = EXTRA;"; break;
            default: break; // Maintain NORMAL or default
        }
        sqlite3_exec(db, syncPragma, nullptr, nullptr, nullptr);
    }

    ~DBContext() {
        // Smart pointers clean up statements automatically when refcount hits 0.
        statementCache.clear();
        lruList.clear();

        if (db) {
            sqlite3_close(db);
        }
    }

    std::shared_ptr<sqlite3_stmt> getStatement(const std::string& sql) {
        auto it = statementCache.find(sql);
        if (it != statementCache.end()) {
            // Found! Move to front of LRU list (Mark as Recently Used)
            lruList.erase(it->second.second);
            lruList.push_front(sql);
            it->second.second = lruList.begin();
            return it->second.first;
        }

        // Not found. Check capacity.
        if (statementCache.size() >= MAX_CACHE_SIZE) {
            // Evict LRU (Back of list)
            std::string lruSql = lruList.back();
            auto cacheIt = statementCache.find(lruSql);
            if (cacheIt != statementCache.end()) {
                // Remove from map. Shared_ptr destructor handles finalize if it's the last ref.
                statementCache.erase(cacheIt);
            }
            lruList.pop_back();
        }

        // Create new
        sqlite3_stmt* rawStmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &rawStmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("Prepare failed: " + std::string(sqlite3_errmsg(db)) + " SQL: " + sql);
        }
        
        // Wrap in shared_ptr with custom deleter
        std::shared_ptr<sqlite3_stmt> stmt(rawStmt, [](sqlite3_stmt* s) {
            sqlite3_finalize(s);
        });

        // Add to cache (Front of list)
        lruList.push_front(sql);
        statementCache[sql] = {stmt, lruList.begin()};
        
        return stmt;
    }
};

class ScopedStmt {
    std::shared_ptr<sqlite3_stmt> stmt;
public:
    ScopedStmt(std::shared_ptr<DBContext> ctx, const std::string& sql) {
        stmt = ctx->getStatement(sql);
    }

    ~ScopedStmt() {
        if (stmt) {
            sqlite3_clear_bindings(stmt.get());
            sqlite3_reset(stmt.get());
        }
    }

    operator sqlite3_stmt*() const { return stmt.get(); }
    sqlite3_stmt* get() const { return stmt.get(); }
};

// ==========================================
// 1.5. ORM / Reflection Helpers
// ==========================================

template<typename Class, typename Member>
struct FieldMapping {
    Member Class::* ptr;
    std::string name;
};

template<typename Class, typename Member>
FieldMapping<Class, Member> orm_field(Member Class::* ptr, const std::string& name) {
    return {ptr, name};
}

// Default generic ORM trait - users specialize this
template<typename T>
struct ORM {
    // static constexpr const char* table = ...;
    // static auto map() { return std::make_tuple(...); }
};

// Helper for type coercion from SQLValue
template<typename T>
T fromSQLValue(const SQLValue& val, const std::string& colName) {
    if (std::holds_alternative<T>(val)) {
        return std::get<T>(val);
    }
    // Int <-> Long Long
    if constexpr (std::is_same_v<T, int>) {
        if (std::holds_alternative<long long>(val)) return static_cast<int>(std::get<long long>(val));
    }
    if constexpr (std::is_same_v<T, long long>) {
        if (std::holds_alternative<int>(val)) return static_cast<long long>(std::get<int>(val));
    }
    // Double <-> Float
    if constexpr (std::is_same_v<T, float>) {
        if (std::holds_alternative<double>(val)) return static_cast<float>(std::get<double>(val));
    }
    // std::string conversion if needed (e.g. from text)
    if constexpr (std::is_same_v<T, std::string>) {
         // Already handled by holds_alternative, but maybe text->blob?
    }
    
    throw std::runtime_error("Column type mismatch for column: " + colName);
}

// Helper to coerce types TO SQLValue (for insert/update)
template<typename T>
SQLValue toSQLValue(const T& val) {
    if constexpr (std::is_same_v<T, int>) return (long long)val;
    else if constexpr (std::is_same_v<T, float>) return (double)val;
    else return val; // rely on variant implicit constructor
}

template <typename T>
T rowToStruct(const Row& row) {
    T instance;
    auto mappings = ORM<T>::map();
    std::apply([&](const auto&... fields) {
        ((
            [&]{
                auto it = row.find(fields.name);
                if (it != row.end()) {
                    instance.*fields.ptr = fromSQLValue<std::decay_t<decltype(instance.*fields.ptr)>>(it->second, fields.name);
                }
            }()
        ), ...);
    }, mappings);
    return instance;
}

template <typename T>
Row structToRow(const T& instance) {
    Row row;
    auto mappings = ORM<T>::map();
    std::apply([&](const auto&... fields) {
        ((
            row[fields.name] = toSQLValue(instance.*fields.ptr)
        ), ...);
    }, mappings);
    return row;
}

// ==========================================
// 2. The Table Class
// ==========================================

class Database; // Forward declaration

class Table {
private:
    std::string tableName;
    std::vector<ColumnDef> columns;
    std::shared_ptr<DBContext> ctx; // Shared ownership logic

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
    Table(std::string name, std::shared_ptr<DBContext> context) 
        : tableName(std::move(name)), ctx(std::move(context)) {}

    // --------------------------------------------------------
    // Schema Definition Methods
    // --------------------------------------------------------
    Table& addColumn(const std::string& name, SQLType type, bool primaryKey = false, bool autoInc = false) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ColumnDef col;
        col.name = name;
        col.type = type;
        col.isPrimaryKey = primaryKey;
        col.isAutoIncrement = autoInc;
        columns.push_back(col);
        return *this;
    }

    Table& addForeignKey(const std::string& name, SQLType type, const std::string& refTable, const std::string& refCol, bool onDeleteCascade = false) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ColumnDef col;
        col.name = name;
        col.type = type;
        col.foreignTable = refTable;
        col.foreignColumn = refCol;
        col.onDeleteCascade = onDeleteCascade;
        columns.push_back(col);
        return *this;
    }

    // Create an Index
    void createIndex(const std::string& indexName, const std::string& column, bool unique = false) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        std::stringstream ss;
        ss << "CREATE ";
        if (unique) ss << "UNIQUE ";
        ss << "INDEX IF NOT EXISTS " << quoteIdentifier(indexName) << " ON " << quoteIdentifier(tableName) << "(" << quoteIdentifier(column) << ");";
        
        std::string sql = ss.str();
        char* errMsg = nullptr;
        if (sqlite3_exec(ctx->db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
             std::string err = errMsg ? errMsg : "Unknown error";
             if(errMsg) sqlite3_free(errMsg);
             throw std::runtime_error("Failed to create index " + indexName + ": " + err);
        }
    }

    // Must be called to actually create the table in SQLite
    void create() {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        std::stringstream ss;
        ss << "CREATE TABLE IF NOT EXISTS " << quoteIdentifier(tableName) << " (";
        
        for (size_t i = 0; i < columns.size(); ++i) {
            const auto& col = columns[i];
            ss << quoteIdentifier(col.name) << " " << typeToString(col.type);

            if (col.isPrimaryKey) ss << " PRIMARY KEY";
            if (col.isAutoIncrement) ss << " AUTOINCREMENT";
            if (col.isNotNull) ss << " NOT NULL";

            if (col.foreignTable.has_value()) {
                ss << ", FOREIGN KEY(" << quoteIdentifier(col.name) << ") REFERENCES " 
                   << quoteIdentifier(col.foreignTable.value()) << "(" << quoteIdentifier(col.foreignColumn.value()) << ")";
                if (col.onDeleteCascade) {
                    ss << " ON DELETE CASCADE";
                }
            }

            if (i < columns.size() - 1) ss << ", ";
        }
        ss << ");";

        std::string sql = ss.str();
        char* errMsg = nullptr;
        int rc = sqlite3_exec(ctx->db, sql.c_str(), nullptr, nullptr, &errMsg);
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
    // Returns the last inserted row ID
    long long insert(const Row& row) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        std::stringstream ss;
        ss << "INSERT INTO " << quoteIdentifier(tableName) << " (";
        
        std::vector<SQLValue> values;
        size_t idx = 0;
        for (const auto& [key, val] : row) {
            ss << quoteIdentifier(key);
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

        ScopedStmt stmt(ctx, ss.str());

        for (int i = 0; i < values.size(); ++i) {
            bindValue(stmt, i + 1, values[i]);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            throw std::runtime_error("Insert failed: " + std::string(sqlite3_errmsg(ctx->db)));
        }

        return sqlite3_last_insert_rowid(ctx->db);
    }

    // READ (Select)
    std::vector<Row> select(const std::vector<Condition>& where = {}, const QueryOptions& opts = {}) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        std::stringstream ss;
        
        ss << "SELECT ";
        if (opts.columns.empty()) {
            ss << "*";
        } else {
            for (size_t i = 0; i < opts.columns.size(); ++i) {
                // If column alias or complex expression, user must handle manually or we expand parser.
                // For now, assuming direct column names or "table.col".
                // Simple heuristic: if it contains space or function parens, don't quote.
                // Otherwise split by '.' and quote parts.
                std::string col = opts.columns[i];
                if (col.find_first_of(" (") == std::string::npos) {
                     size_t dot = col.find('.');
                     if (dot != std::string::npos) {
                         ss << quoteIdentifier(col.substr(0, dot)) << "." << quoteIdentifier(col.substr(dot+1));
                     } else {
                         ss << quoteIdentifier(col);
                     }
                } else {
                    ss << col; // Leave as is if complex
                }

                if (i < opts.columns.size() - 1) ss << ", ";
            }
        }
        
        ss << " FROM " << quoteIdentifier(tableName);
        
        // Append Joins
        for (const auto& join : opts.joins) {
            ss << " " << join.getTypeString() << " " << quoteIdentifier(join.table) 
               << " ON " << join.onCondition; // onCondition is raw SQL for now
        }
        
        if (!where.empty()) {
            ss << " WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                ss << quoteIdentifier(where[i].column) << " " << where[i].getOpString() << " ?";
                if (i < where.size() - 1) ss << " AND ";
            }
        }

        if (!opts.groupBy.empty()) {
            ss << " GROUP BY ";
            for (size_t i = 0; i < opts.groupBy.size(); ++i) {
                ss << quoteIdentifier(opts.groupBy[i]);
                if (i < opts.groupBy.size() - 1) ss << ", ";
            }
        }

        if (!opts.having.empty()) {
            ss << " HAVING ";
            for (size_t i = 0; i < opts.having.size(); ++i) {
                // Heuristic: if contains space or paren, likely a function (COUNT(x)), don't quote
                std::string col = opts.having[i].column;
                if (col.find_first_of(" (") == std::string::npos) {
                    ss << quoteIdentifier(col);
                } else {
                    ss << col;
                }
                
                ss << " " << opts.having[i].getOpString() << " ?";
                if (i < opts.having.size() - 1) ss << " AND ";
            }
        }

        if (!opts.orderBy.empty()) {
             // Heuristic quote for orderBy like columns
             std::string order = opts.orderBy;
             if (order.find_first_of(" (") == std::string::npos) {
                 size_t dot = order.find('.');
                 if (dot != std::string::npos) {
                     ss << " ORDER BY " << quoteIdentifier(order.substr(0, dot)) << "." << quoteIdentifier(order.substr(dot+1));
                 } else {
                     ss << " ORDER BY " << quoteIdentifier(order);
                 }
             } else {
                 ss << " ORDER BY " << order;
             }
             ss << (opts.orderDesc ? " DESC" : " ASC");
        }
        if (opts.limit >= 0) {
            ss << " LIMIT " << opts.limit;
        }
        if (opts.offset >= 0) {
            ss << " OFFSET " << opts.offset;
        }
        ss << ";";

        ScopedStmt stmt(ctx, ss.str());

        int bindIdx = 1;
        for (const auto& cond : where) {
            bindValue(stmt, bindIdx++, cond.value);
        }
        for (const auto& cond : opts.having) {
            bindValue(stmt, bindIdx++, cond.value);
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

        return results;
    }

    // UPDATE
    void update(const Row& data, const std::vector<Condition>& where) {
        if (data.empty()) return;

        std::lock_guard<std::mutex> lock(ctx->mtx);
        std::stringstream ss;
        ss << "UPDATE " << quoteIdentifier(tableName) << " SET ";
        
        std::vector<SQLValue> bindings;
        size_t idx = 0;
        for (const auto& [key, val] : data) {
            ss << quoteIdentifier(key) << " = ?";
            bindings.push_back(val);
            if (idx < data.size() - 1) ss << ", ";
            idx++;
        }

        if (!where.empty()) {
            ss << " WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                ss << quoteIdentifier(where[i].column) << " " << where[i].getOpString() << " ?";
                bindings.push_back(where[i].value);
                if (i < where.size() - 1) ss << " AND ";
            }
        }

        ScopedStmt stmt(ctx, ss.str());

        for (int i = 0; i < bindings.size(); ++i) {
            bindValue(stmt, i + 1, bindings[i]);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            throw std::runtime_error("Update failed: " + std::string(sqlite3_errmsg(ctx->db)));
        }
    }

    // DELETE
    void remove(const std::vector<Condition>& where) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        std::stringstream ss;
        ss << "DELETE FROM " << quoteIdentifier(tableName);

        if (!where.empty()) {
            ss << " WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                ss << quoteIdentifier(where[i].column) << " " << where[i].getOpString() << " ?";
                if (i < where.size() - 1) ss << " AND ";
            }
        }

        ScopedStmt stmt(ctx, ss.str());

        for (int i = 0; i < where.size(); ++i) {
            bindValue(stmt, i + 1, where[i].value);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            throw std::runtime_error("Delete failed: " + std::string(sqlite3_errmsg(ctx->db)));
        }
    }

    // --------------------------------------------------------
    // ORM / Struct Mapping API
    // --------------------------------------------------------

    // Template-based Select
    // Template-based Select (Renamed to query to avoid overload strictness issues)
    template<typename T>
    std::vector<T> query(const std::vector<Condition>& where = {}, const QueryOptions& opts = {}) {
        auto rows = this->select(where, opts);
        std::vector<T> results;
        results.reserve(rows.size());
        for (const auto& r : rows) {
            results.push_back(rowToStruct<T>(r));
        }
        return results;
    }

    // Template-based Insert
    // Note: This will attempt to insert ALL fields in the struct mapping.
    // If 'id' is autoincrement and 0 in struct, you might want to exclude it manually 
    // or handle it by having a specialized 'insert struct' without ID.
    // For now, we insert everything defined in the map.
    template<typename T>
    long long insert(const T& obj) {
        return this->insert(structToRow(obj));
    }
};

// ==========================================
// 3. The Database Manager
// ==========================================

class Database {
private:
    std::shared_ptr<DBContext> ctx;
    std::map<std::string, Table> tables;

public:
    Database(const std::string& filename, const Config& config = {}) {
        ctx = std::make_shared<DBContext>(filename, config);
    }

    // Default destructor is fine now, shared_ptr handles cleanup
    ~Database() = default;

    // Start defining a new table
    Table& defineTable(const std::string& name) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        // Construct table in map using piecewise construction
        // Use operator[] or emplace. 
        // We need to pass the shared_ptr context to the Table constructor.
        auto res = tables.emplace(std::piecewise_construct, 
                                  std::forward_as_tuple(name), 
                                  std::forward_as_tuple(name, ctx));
        return res.first->second;
    }

    // Retrieve an existing table wrapper
    Table& getTable(const std::string& name) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        auto it = tables.find(name);
        if (it == tables.end()) {
            throw std::runtime_error("Table not defined in wrapper: " + name);
        }
        return it->second;
    }
    
    // ORM Helper: Select directly from Database using Struct type to identify Table
    // ORM Helper: Select directly from Database using Struct type to identify Table
    template<typename T>
    std::vector<T> query(const std::vector<Condition>& where = {}, const QueryOptions& opts = {}) {
        return getTable(ORM<T>::table).query<T>(where, opts);
    }

    template<typename T>
    long long insert(const T& obj) {
        return getTable(ORM<T>::table).insert(obj);
    }

    // ==========================================
    // Transaction Support
    // ==========================================
    void beginTransaction() {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        char* errMsg = nullptr;
        if (sqlite3_exec(ctx->db, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
             std::string err = errMsg ? errMsg : "Unknown error";
             if(errMsg) sqlite3_free(errMsg);
             throw std::runtime_error("Begin Transaction failed: " + err);
        }
    }

    void commit() {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        char* errMsg = nullptr;
        if (sqlite3_exec(ctx->db, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
             std::string err = errMsg ? errMsg : "Unknown error";
             if(errMsg) sqlite3_free(errMsg);
             throw std::runtime_error("Commit failed: " + err);
        }
    }

    void rollback() {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        char* errMsg = nullptr;
        // Rollback shouldn't generally throw, but we report errors
        if (sqlite3_exec(ctx->db, "ROLLBACK;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
             std::string err = errMsg ? errMsg : "Unknown error";
             if(errMsg) sqlite3_free(errMsg);
             std::cerr << "Rollback failed: " << err << std::endl;
        }
    }

    // ==========================================
    // RAII Transaction Helper
    // ==========================================
    struct TransactionGuard {
        Database& db;
        bool finished = false;

        TransactionGuard(Database& _db) : db(_db) {
            db.beginTransaction();
        }

        ~TransactionGuard() {
            if (!finished) {
                try {
                    db.rollback();
                } catch (...) {
                    // Destructor must not throw
                }
            }
        }

        void commit() {
            if (finished) return;
            db.commit();
            finished = true;
        }

        void rollback() {
            if (finished) return;
            db.rollback();
            finished = true;
        }
    };

    // Factory method
    TransactionGuard transaction() {
        return TransactionGuard(*this);
    }
};
} // namespace sqldb