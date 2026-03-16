#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef REDIS_ENABLED
#include <chrono>
#include <thread>
#include <sw/redis++/redis++.h>
#endif

/**
 * RedisClient — 对 redis-plus-plus 的轻量封装。
 *
 * 设计原则：
 *  1. 所有操作均 try-catch，Redis 故障时返回空值/false，系统降级到 MySQL。
 *  2. GetOrLoad 实现分布式锁防缓存击穿（SETNX + TTL 自动抖动防雪崩）。
 *  3. 空值用 "__NULL__" 哨兵缓存，防止穿透。
 *  4. 未安装 redis-plus-plus 时（REDIS_ENABLED 未定义），所有方法为空操作 stub，
 *     系统自动降级到纯 MySQL 模式，编译不受影响。
 */
class RedisClient {
 public:
  /** Redis 空值哨兵，用于防穿透。 */
  static constexpr const char* kNullSentinel = "__NULL__";

#ifdef REDIS_ENABLED
  RedisClient(const std::string& host,
              int port,
              const std::string& password = "",
              int db = 0,
              int pool_size = 8,
              int connect_timeout_ms = 200,
              int socket_timeout_ms = 500);

  // ─── 连通性 ──────────────────────────────────────────────────────────────
  bool Ping();

  // ─── String ──────────────────────────────────────────────────────────────
  std::optional<std::string> Get(const std::string& key);
  void SetEx(const std::string& key, const std::string& value, int ttl_sec);
  /** SET key value EX ttl NX（原子，key 不存在时才写）。 */
  bool SetNxEx(const std::string& key, const std::string& value, int ttl_sec);
  long long Del(const std::string& key);
  bool Exists(const std::string& key);
  void Expire(const std::string& key, int ttl_sec);

  // ─── Hash（PPT 生成状态实时更新）────────────────────────────────────────
  void HMSet(const std::string& key,
             const std::vector<std::pair<std::string, std::string>>& fields);
  std::optional<std::string> HGet(const std::string& key, const std::string& field);
  std::unordered_map<std::string, std::string> HGetAll(const std::string& key);

  // ─── 高级：Cache-Aside + 防击穿 + 防穿透 ────────────────────────────────
  /**
   * 读取缓存，未命中时调 loader() 回填。
   * @param base_ttl  基础 TTL（秒），实际值加 ±10% 随机抖动（防雪崩）
   * @param loader    回源函数，返回空字符串视为"数据不存在"
   * @param cache_null true 时对不存在数据缓存哨兵（防穿透，TTL=60s）
   */
  std::string GetOrLoad(const std::string& key,
                        int base_ttl,
                        std::function<std::string()> loader,
                        bool cache_null = true);

 private:
  void SetExWithJitter(const std::string& key, const std::string& value, int base_ttl);

  std::unique_ptr<sw::redis::Redis> redis_;
  std::string password_;
  std::string host_;
  int port_{6379};
  int connect_timeout_ms_{1000};

#else  // ── Stub：无 Redis 库时的空实现，保证编译不报错 ────────────────────

  RedisClient(const std::string& /*host*/,
              int /*port*/,
              const std::string& /*password*/ = "",
              int /*db*/ = 0,
              int /*pool_size*/ = 8,
              int /*connect_timeout_ms*/ = 200,
              int /*socket_timeout_ms*/ = 500) {}

  bool Ping() { return false; }

  std::optional<std::string> Get(const std::string&) { return std::nullopt; }
  void SetEx(const std::string&, const std::string&, int) {}
  bool SetNxEx(const std::string&, const std::string&, int) { return false; }
  long long Del(const std::string&) { return 0; }
  bool Exists(const std::string&) { return false; }
  void Expire(const std::string&, int) {}

  void HMSet(const std::string&,
             const std::vector<std::pair<std::string, std::string>>&) {}
  std::optional<std::string> HGet(const std::string&, const std::string&) {
    return std::nullopt;
  }
  std::unordered_map<std::string, std::string> HGetAll(const std::string&) {
    return {};
  }

  std::string GetOrLoad(const std::string&,
                        int,
                        std::function<std::string()> loader,
                        bool = true) {
    return loader();  // 无缓存，直接回源
  }

#endif  // REDIS_ENABLED
};
