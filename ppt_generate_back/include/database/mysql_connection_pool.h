#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include <mysql/mysql.h>

#include "app_config.h"

class MySQLConnectionPool {
 public:
  explicit MySQLConnectionPool(const DatabaseConfig& config);
  ~MySQLConnectionPool();

  MySQLConnectionPool(const MySQLConnectionPool&) = delete;
  MySQLConnectionPool& operator=(const MySQLConnectionPool&) = delete;

  MYSQL* Acquire();
  void Release(MYSQL* connection);

  class ScopedConnection {
   public:
    ScopedConnection(MySQLConnectionPool& pool, MYSQL* connection)
        : pool_(&pool), connection_(connection) {}
    ~ScopedConnection() {
      if (pool_ && connection_) {
        pool_->Release(connection_);
      }
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ScopedConnection(ScopedConnection&& other) noexcept {
      pool_ = other.pool_;
      connection_ = other.connection_;
      other.pool_ = nullptr;
      other.connection_ = nullptr;
    }

    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
      if (this != &other) {
        if (pool_ && connection_) {
          pool_->Release(connection_);
        }
        pool_ = other.pool_;
        connection_ = other.connection_;
        other.pool_ = nullptr;
        other.connection_ = nullptr;
      }
      return *this;
    }

    MYSQL* Get() const { return connection_; }

   private:
    MySQLConnectionPool* pool_;
    MYSQL* connection_;
  };

  ScopedConnection GetConnection() { return ScopedConnection(*this, Acquire()); }

 private:
  MYSQL* CreateConnection();

  DatabaseConfig config_;
  std::queue<MYSQL*> connections_;
  std::mutex mutex_;
  std::condition_variable condition_;
};
