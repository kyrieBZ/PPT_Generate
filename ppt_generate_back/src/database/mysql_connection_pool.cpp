#include "database/mysql_connection_pool.h"

#include <stdexcept>
#include <utility>

#include "logger.h"

namespace {
bool g_mysql_initialized = false;
std::mutex g_mysql_init_mutex;

void EnsureMysqlLibraryInitialized() {
  std::lock_guard<std::mutex> lock(g_mysql_init_mutex);
  if (!g_mysql_initialized) {
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
      throw std::runtime_error("mysql_library_init failed");
    }
    g_mysql_initialized = true;
  }
}
}

MySQLConnectionPool::MySQLConnectionPool(const DatabaseConfig& config) : config_(config) {
  EnsureMysqlLibraryInitialized();
  for (std::size_t i = 0; i < config_.pool_size; ++i) {
    connections_.push(CreateConnection());
  }
  Logger::Info("MySQL connection pool initialized with size: " + std::to_string(config_.pool_size));
}

MySQLConnectionPool::~MySQLConnectionPool() {
  while (!connections_.empty()) {
    MYSQL* conn = connections_.front();
    connections_.pop();
    mysql_close(conn);
  }
}

MYSQL* MySQLConnectionPool::CreateConnection() {
  MYSQL* connection = mysql_init(nullptr);
  if (!connection) {
    throw std::runtime_error("mysql_init failed");
  }

  mysql_options(connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");

  if (!mysql_real_connect(connection,
                          config_.host.c_str(),
                          config_.user.c_str(),
                          config_.password.c_str(),
                          config_.name.c_str(),
                          config_.port,
                          nullptr,
                          CLIENT_MULTI_STATEMENTS)) {
    std::string error = mysql_error(connection);
    mysql_close(connection);
    throw std::runtime_error("mysql_real_connect failed: " + error);
  }

  return connection;
}

MYSQL* MySQLConnectionPool::Acquire() {
  std::unique_lock<std::mutex> lock(mutex_);
  condition_.wait(lock, [this]() { return !connections_.empty(); });
  MYSQL* connection = connections_.front();
  connections_.pop();
  return connection;
}

void MySQLConnectionPool::Release(MYSQL* connection) {
  if (!connection) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  connections_.push(connection);
  condition_.notify_one();
}
