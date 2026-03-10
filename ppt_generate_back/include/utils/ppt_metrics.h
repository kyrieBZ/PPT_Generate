#pragma once

#include <cstdint>
#include <atomic>

namespace PptMetrics {

/** 异步 PPT 生成任务入队总数 */
std::atomic<std::uint64_t>& GenerationTotal();
/** 生成成功数 */
std::atomic<std::uint64_t>& GenerationSuccess();
/** 生成失败数 */
std::atomic<std::uint64_t>& GenerationFailed();
/** 最近一次生成耗时（毫秒），0 表示暂无 */
std::atomic<std::uint64_t>& LastGenerationDurationMs();

inline void IncGenerationTotal() { GenerationTotal().fetch_add(1, std::memory_order_relaxed); }
inline void IncGenerationSuccess() { GenerationSuccess().fetch_add(1, std::memory_order_relaxed); }
inline void IncGenerationFailed() { GenerationFailed().fetch_add(1, std::memory_order_relaxed); }
inline void SetLastGenerationDurationMs(std::uint64_t ms) {
  LastGenerationDurationMs().store(ms, std::memory_order_relaxed);
}

}  // namespace PptMetrics
