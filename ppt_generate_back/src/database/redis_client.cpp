#include "database/redis_client.h"

// 仅在 REDIS_ENABLED 时编译实际实现；否则头文件中的 inline stub 已覆盖全部接口。
#ifdef REDIS_ENABLED

#include <cstdlib>

#include <hiredis/hiredis.h>

#include "logger.h"

RedisClient::RedisClient(const std::string& host,
                         int port,
                         const std::string& password,
                         int db,
                         int pool_size,
                         int connect_timeout_ms,
                         int socket_timeout_ms)
    : password_(password) {
  sw::redis::ConnectionOptions opts;
  opts.host = host;
  opts.port = port;
  opts.db   = db;
  if (!password.empty()) {
    opts.password = password;
  }
  opts.connect_timeout = std::chrono::milliseconds(connect_timeout_ms);
  opts.socket_timeout  = std::chrono::milliseconds(socket_timeout_ms);

  sw::redis::ConnectionPoolOptions pool_opts;
  pool_opts.size = static_cast<std::size_t>(pool_size > 0 ? pool_size : 4);

  redis_ = std::make_unique<sw::redis::Redis>(opts, pool_opts);

  // 保存连接参数供 Ping() 使用 hiredis 做无密码预检
  host_ = host;
  port_ = port;
  connect_timeout_ms_ = connect_timeout_ms;
}

// ─── 连通性 ──────────────────────────────────────────────────────────────────

bool RedisClient::Ping() {
  // 始终先用 hiredis 裸连接做探活，绕过 redis-plus-plus 1.3.x 在无密码
  // Redis 上仍发送 AUTH "" 的问题。若 hiredis 探活成功，Redis++ 连接池
  // 后续操作才会真正建立；若有密码，hiredis 同样先做带密码认证。
  struct timeval tv;
  tv.tv_sec  = connect_timeout_ms_ / 1000;
  tv.tv_usec = (connect_timeout_ms_ % 1000) * 1000;
  if (tv.tv_sec == 0 && tv.tv_usec == 0) {
    tv.tv_sec = 1;
  }
  redisContext* ctx = redisConnectWithTimeout(host_.c_str(), port_, tv);
  if (ctx == nullptr || ctx->err) {
    std::string err = ctx ? ctx->errstr : "allocation failure";
    if (ctx) redisFree(ctx);
    Logger::Warn("[Redis] Ping failed: " + err);
    return false;
  }
  // 有密码时先发 AUTH
  if (!password_.empty()) {
    redisReply* auth_reply = static_cast<redisReply*>(
        redisCommand(ctx, "AUTH %s", password_.c_str()));
    bool auth_ok = (auth_reply != nullptr && auth_reply->type != REDIS_REPLY_ERROR);
    if (auth_reply) freeReplyObject(auth_reply);
    if (!auth_ok) {
      redisFree(ctx);
      Logger::Warn("[Redis] Ping failed: AUTH error");
      return false;
    }
  }
  redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "PING"));
  bool ok = (reply != nullptr && reply->type == REDIS_REPLY_STATUS &&
             std::string(reply->str) == "PONG");
  if (reply) freeReplyObject(reply);
  redisFree(ctx);
  if (!ok) {
    Logger::Warn("[Redis] Ping failed: no PONG");
  }
  return ok;
}

// ─── String ──────────────────────────────────────────────────────────────────

std::optional<std::string> RedisClient::Get(const std::string& key) {
  try {
    return redis_->get(key);
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] GET key=") + key + " : " + e.what());
    return std::nullopt;
  }
}

void RedisClient::SetEx(const std::string& key, const std::string& value, int ttl_sec) {
  try {
    redis_->setex(key, ttl_sec, value);
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] SETEX key=") + key + " : " + e.what());
  }
}

bool RedisClient::SetNxEx(const std::string& key, const std::string& value, int ttl_sec) {
  try {
    return redis_->set(key, value,
                       std::chrono::seconds(ttl_sec),
                       sw::redis::UpdateType::NOT_EXIST);
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] SETNXEX key=") + key + " : " + e.what());
    return false;
  }
}

long long RedisClient::Del(const std::string& key) {
  try {
    return redis_->del(key);
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] DEL key=") + key + " : " + e.what());
    return 0;
  }
}

bool RedisClient::Exists(const std::string& key) {
  try {
    return redis_->exists(key) > 0;
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] EXISTS key=") + key + " : " + e.what());
    return false;
  }
}

void RedisClient::Expire(const std::string& key, int ttl_sec) {
  try {
    redis_->expire(key, ttl_sec);
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] EXPIRE key=") + key + " : " + e.what());
  }
}

// ─── Hash ─────────────────────────────────────────────────────────────────────

void RedisClient::HMSet(const std::string& key,
                        const std::vector<std::pair<std::string, std::string>>& fields) {
  try {
    redis_->hmset(key, fields.begin(), fields.end());
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] HMSET key=") + key + " : " + e.what());
  }
}

std::optional<std::string> RedisClient::HGet(const std::string& key,
                                              const std::string& field) {
  try {
    return redis_->hget(key, field);
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] HGET key=") + key + " : " + e.what());
    return std::nullopt;
  }
}

std::unordered_map<std::string, std::string> RedisClient::HGetAll(const std::string& key) {
  try {
    std::unordered_map<std::string, std::string> result;
    redis_->hgetall(key, std::inserter(result, result.end()));
    return result;
  } catch (const std::exception& e) {
    Logger::Warn(std::string("[Redis] HGETALL key=") + key + " : " + e.what());
    return {};
  }
}

// ─── 私有工具 ────────────────────────────────────────────────────────────────

void RedisClient::SetExWithJitter(const std::string& key,
                                  const std::string& value,
                                  int base_ttl) {
  // ±10% 随机抖动，防止大批 Key 同时过期（雪崩）
  const int jitter_range = std::max(1, base_ttl / 10);
  const int jitter = static_cast<int>(std::rand() % (2 * jitter_range + 1)) - jitter_range;
  const int actual_ttl = std::max(1, base_ttl + jitter);
  SetEx(key, value, actual_ttl);
}

// ─── 高级：Cache-Aside + 防击穿 + 防穿透 ─────────────────────────────────────

std::string RedisClient::GetOrLoad(const std::string& key,
                                   int base_ttl,
                                   std::function<std::string()> loader,
                                   bool cache_null) {
  // 1. 读缓存
  auto cached = Get(key);
  if (cached) {
    if (*cached == kNullSentinel) return "";
    return *cached;
  }

  // 2. 尝试获取分布式锁（SETNX），防并发击穿
  const std::string lock_key = "lock:" + key;
  const bool got_lock = SetNxEx(lock_key, "1", 30);

  if (!got_lock) {
    // 未获锁：短暂等待后重试（最多 3 次，每次 150ms）
    for (int i = 0; i < 3; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      auto retry = Get(key);
      if (retry) {
        if (*retry == kNullSentinel) return "";
        return *retry;
      }
    }
    // 多次重试仍未命中，直接回源（兜底，不持锁写缓存）
    return loader();
  }

  // 3. 持锁，执行回源
  std::string val;
  try {
    val = loader();
  } catch (...) {
    Del(lock_key);
    throw;
  }

  if (val.empty()) {
    if (cache_null) {
      SetEx(key, kNullSentinel, 60);  // 防穿透：缓存空值 60s
    }
  } else {
    SetExWithJitter(key, val, base_ttl);
  }

  Del(lock_key);
  return val;
}

#endif  // REDIS_ENABLED
