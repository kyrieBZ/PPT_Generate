#include "database/mongo_client.h"

#include "logger.h"

#ifdef MONGO_ENABLED

#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/exception/exception.hpp>

namespace {

// nlohmann::json → bsoncxx document
bsoncxx::document::value JsonToBson(const nlohmann::json& j) {
  return bsoncxx::from_json(j.dump());
}

// bsoncxx document view → nlohmann::json
nlohmann::json BsonToJson(bsoncxx::document::view view) {
  return nlohmann::json::parse(bsoncxx::to_json(view));
}

}  // namespace

mongocxx::instance& MongoClient::GetInstance() {
  static mongocxx::instance instance{};
  return instance;
}

MongoClient::MongoClient(const std::string& uri, const std::string& db_name)
    : db_name_(db_name) {
  try {
    GetInstance();  // 确保 instance 已初始化
    mongocxx::uri mongo_uri(uri);
    pool_ = std::make_unique<mongocxx::pool>(mongo_uri);
    // 做一次连通性测试
    auto conn = pool_->acquire();
    auto db   = (*conn)[db_name_];
    db.list_collections().begin();  // 触发真实连接
    connected_ = true;
    Logger::Info("MongoDB connected: " + uri + " / " + db_name);
  } catch (const mongocxx::exception& e) {
    Logger::Error(std::string("MongoDB connection failed: ") + e.what());
    connected_ = false;
  } catch (const std::exception& e) {
    Logger::Error(std::string("MongoDB init error: ") + e.what());
    connected_ = false;
  }
}

bool MongoClient::InsertOne(const std::string& collection,
                             const nlohmann::json& doc) {
  if (!connected_ || !pool_) return false;
  try {
    auto conn = pool_->acquire();
    auto coll = (*conn)[db_name_][collection];
    auto bson = JsonToBson(doc);
    auto result = coll.insert_one(bson.view());
    return result.has_value();
  } catch (const std::exception& e) {
    Logger::Error(std::string("MongoClient::InsertOne error: ") + e.what());
    return false;
  }
}

std::vector<nlohmann::json> MongoClient::Find(const std::string& collection,
                                               const nlohmann::json& filter,
                                               const nlohmann::json& sort,
                                               int limit) {
  std::vector<nlohmann::json> results;
  if (!connected_ || !pool_) return results;
  try {
    auto conn = pool_->acquire();
    auto coll = (*conn)[db_name_][collection];

    auto filter_bson = JsonToBson(filter);

    mongocxx::options::find opts;
    bsoncxx::document::value sort_bson_val = bsoncxx::from_json("{}");
    if (!sort.empty()) {
      sort_bson_val = JsonToBson(sort);
      opts.sort(sort_bson_val.view());
    }
    if (limit > 0) {
      opts.limit(static_cast<std::int64_t>(limit));
    }

    auto cursor = coll.find(filter_bson.view(), opts);
    for (const auto& doc : cursor) {
      try {
        results.push_back(BsonToJson(doc));
      } catch (...) {}
    }
  } catch (const std::exception& e) {
    Logger::Error(std::string("MongoClient::Find error: ") + e.what());
  }
  return results;
}

bool MongoClient::UpdateOne(const std::string& collection,
                             const nlohmann::json& filter,
                             const nlohmann::json& update_doc) {
  if (!connected_ || !pool_) return false;
  try {
    auto conn = pool_->acquire();
    auto coll = (*conn)[db_name_][collection];

    auto filter_bson = JsonToBson(filter);
    // 包装成 { $set: update_doc }
    nlohmann::json set_doc = {{"$set", update_doc}};
    auto update_bson = JsonToBson(set_doc);

    auto result = coll.update_one(filter_bson.view(), update_bson.view());
    return result.has_value();
  } catch (const std::exception& e) {
    Logger::Error(std::string("MongoClient::UpdateOne error: ") + e.what());
    return false;
  }
}

bool MongoClient::DeleteMany(const std::string& collection,
                              const nlohmann::json& filter) {
  if (!connected_ || !pool_) return false;
  try {
    auto conn = pool_->acquire();
    auto coll = (*conn)[db_name_][collection];

    auto filter_bson = JsonToBson(filter);
    auto result = coll.delete_many(filter_bson.view());
    return result.has_value();
  } catch (const std::exception& e) {
    Logger::Error(std::string("MongoClient::DeleteMany error: ") + e.what());
    return false;
  }
}

std::int64_t MongoClient::Count(const std::string& collection,
                                 const nlohmann::json& filter) {
  if (!connected_ || !pool_) return 0;
  try {
    auto conn = pool_->acquire();
    auto coll = (*conn)[db_name_][collection];
    auto filter_bson = JsonToBson(filter);
    return coll.count_documents(filter_bson.view());
  } catch (const std::exception& e) {
    Logger::Error(std::string("MongoClient::Count error: ") + e.what());
    return 0;
  }
}

#endif  // MONGO_ENABLED
