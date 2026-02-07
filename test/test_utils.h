#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include "sqldb/sqldb.h"

// ==========================================
// Utilities
// ==========================================

class Timer {
    std::chrono::high_resolution_clock::time_point start;
    std::string name;
public:
    Timer(const std::string& n) : name(n) {
        start = std::chrono::high_resolution_clock::now();
    }
    ~Timer() {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;
        std::cout << "[Timing] " << name << ": " << duration.count() << " ms" << std::endl;
    }
};

// ==========================================
// Data Structures & ORM
// ==========================================

struct UserStruct {
    long long id;
    std::string username;
    double score;
};

struct UserInput {
    std::string username;
    double score;
};

struct BenchUser {
    long long id;
    std::string username;
    std::string email;
    int age;
    double score;
};

// Map UserStruct to 'users' table
template<>
struct ORM<UserStruct> {
    static constexpr const char* table = "users";
    static auto map() {
        return std::make_tuple(
            orm_field(&UserStruct::id, "id"),
            orm_field(&UserStruct::username, "username"),
            orm_field(&UserStruct::score, "score")
        );
    }
};

// Map UserInput to 'users' table (for insertion without ID)
template<>
struct ORM<UserInput> {
    static constexpr const char* table = "users";
    static auto map() {
        return std::make_tuple(
            orm_field(&UserInput::username, "username"),
            orm_field(&UserInput::score, "score")
        );
    }
};

template<>
struct ORM<BenchUser> {
    static constexpr const char* table = "bench_users";
    static auto map() {
        return std::make_tuple(
            orm_field(&BenchUser::id, "id"),
            orm_field(&BenchUser::username, "username"),
            orm_field(&BenchUser::email, "email"),
            orm_field(&BenchUser::age, "age"),
            orm_field(&BenchUser::score, "score")
        );
    }
};

// ==========================================
// Test Module Declarations
// ==========================================

void test_basics(Database& db);
void test_orm(Database& db);
void test_advanced(Database& db);
void test_transactions(Database& db);
void test_performance(Database& db);
