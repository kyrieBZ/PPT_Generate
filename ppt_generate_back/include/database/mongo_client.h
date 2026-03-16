#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

/**
 * MongoClient — 对 mongocxx 的轻量封装。
 *
 * 设计原则：
 *  1. 接口使用 nlohmann::json 作为文档载体，与项目现有风格一致。
 *  2. 所有操作均 try-catch，失败时返回 false / 空容器，由调用方处理。
 *  3. 未安装 mongo-cxx-driver 时（MONGO_ENABLED 未定义），所有方法为空操作 stub，
 *     编译不受影响，系统自动降级为无持久化模式。
 */

#ifdef MONGO_ENABLED
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/uri.hpp>
#endif

class MongoClient {
 public:
#ifdef MONGO_ENABLED
  MongoClient(const std::string& uri, const std::string& db_name);

  bool IsConnected() const { return connected_; }

  /**
   * 插入一条文档。
   * @param collection  集合名
   * @param doc         nlohmann::json 对象（不含 _id，由 MongoDB 自动生成）
   * @return 成功返回 true
   */
  bool InsertOne(const std::string& collection, const nlohmann::json& doc);

  /**
   * 查询文档列表。
   * @param collection  集合名
   * @param filter      过滤条件，如 {{"session_id", "xxx"}}
   * @param sort        排序，如 {{"timestamp", 1}}（1=升序，-1=降序）
   * @param limit       最大返回条数，0 表示不限制
   * @return 匹配的文档列表（每条为 nlohmann::json）
   */
  std::vector<nlohmann::json> Find(const std::string& collection,
                                   const nlohmann::json& filter,
                                   const nlohmann::json& sort = {},
                                   int limit = 0);

  /**
   * 更新一条文档（使用 $set 操作符）。
   * @param collection  集合名
   * @param filter      过滤条件
   * @param update_doc  要 $set 的字段，如 {{"updated_at", "..."}}
   * @return 成功返回 true
   */
  bool UpdateOne(const std::string& collection,
                 const nlohmann::json& filter,
                 const nlohmann::json& update_doc);

  /**
   * 删除匹配的所有文档。
   * @return 成功返回 true
   */
  bool DeleteMany(const std::string& collection, const nlohmann::json& filter);

  /**
   * 计数。
   */
  std::int64_t Count(const std::string& collection, const nlohmann::json& filter);

 private:
  // mongocxx::instance 必须全局唯一，用 static 保证
  static mongocxx::instance& GetInstance();
  std::string db_name_;
  std::unique_ptr<mongocxx::pool> pool_;
  bool connected_ = false;

#else  // ── Stub：无 mongo-cxx-driver 时的空实现 ──────────────────────────────

  MongoClient(const std::string& /*uri*/, const std::string& /*db_name*/) {}

  bool IsConnected() const { return false; }

  bool InsertOne(const std::string& /*collection*/,
                 const nlohmann::json& /*doc*/) {
    return false;
  }

  std::vector<nlohmann::json> Find(const std::string& /*collection*/,
                                   const nlohmann::json& /*filter*/,
                                   const nlohmann::json& /*sort*/ = {},
                                   int /*limit*/ = 0) {
    return {};
  }

  bool UpdateOne(const std::string& /*collection*/,
                 const nlohmann::json& /*filter*/,
                 const nlohmann::json& /*update_doc*/) {
    return false;
  }

  bool DeleteMany(const std::string& /*collection*/,
                  const nlohmann::json& /*filter*/) {
    return false;
  }

  std::int64_t Count(const std::string& /*collection*/,
                     const nlohmann::json& /*filter*/) {
    return 0;
  }

#endif  // MONGO_ENABLED
};
