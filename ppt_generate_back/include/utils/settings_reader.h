#pragma once

#include <string>
#include <memory>

#include "database/mysql_connection_pool.h"

/**
 * SettingsReader — 运行时动态读取 system_settings 表中的配置项。
 *
 * 每次调用直接查一条 SQL，适合低频控制路径（注册、生成请求入口等）。
 * 若表不存在或查询失败，返回提供的默认值，保证降级安全。
 */
namespace SettingsReader {

/**
 * 读取字符串配置项。
 * 若键不存在或查询失败，返回 default_value。
 */
std::string GetString(MySQLConnectionPool& pool,
                      const std::string&   key,
                      const std::string&   default_value = "");

/**
 * 读取布尔配置项（存储值为 "true"/"false"/"1"/"0"）。
 * 若键不存在或查询失败，返回 default_value。
 */
bool GetBool(MySQLConnectionPool& pool,
             const std::string&   key,
             bool                 default_value = true);

/**
 * 读取整数配置项。
 * 若键不存在或查询失败，返回 default_value。
 */
int GetInt(MySQLConnectionPool& pool,
           const std::string&   key,
           int                  default_value = 0);

}  // namespace SettingsReader
