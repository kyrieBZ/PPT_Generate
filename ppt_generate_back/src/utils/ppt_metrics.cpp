#include "utils/ppt_metrics.h"

namespace PptMetrics {

namespace {
std::atomic<std::uint64_t> g_generation_total{0};
std::atomic<std::uint64_t> g_generation_success{0};
std::atomic<std::uint64_t> g_generation_failed{0};
std::atomic<std::uint64_t> g_last_duration_ms{0};
}  // namespace

std::atomic<std::uint64_t>& GenerationTotal() { return g_generation_total; }
std::atomic<std::uint64_t>& GenerationSuccess() { return g_generation_success; }
std::atomic<std::uint64_t>& GenerationFailed() { return g_generation_failed; }
std::atomic<std::uint64_t>& LastGenerationDurationMs() { return g_last_duration_ms; }

}  // namespace PptMetrics
