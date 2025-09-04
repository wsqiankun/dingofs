
#include "memory_monitor.h"

#include <butil/time.h>
#include <dlfcn.h>
#include <glog/logging.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/status.h"
#include "options/client/option.h"

namespace dingofs {
namespace client {
namespace metrics {

namespace fs = std::filesystem;

std::vector<std::pair<std::string, std::unique_ptr<MemoryCollector>>>
    g_target_collectors;
// REGISTER_MEMORY_COLLECTOR("sys_mem", SysMemCollector);
// REGISTER_MEMORY_COLLECTOR("je_mem", JemallcStatsCollector);

void MemoryMonitor::Init() {
  std::lock_guard<std::mutex> lg(mtx_);

  //  g_target_collectors.emplace_back("sys_mem",
  //                                   std::make_unique<SysMemCollector>());
  g_target_collectors.emplace_back("je_mem",
                                   std::make_unique<JemallcStatsCollector>());

  for (auto& collector : g_target_collectors) {
    auto s = collector.second->Init();
    LOG(INFO) << collector.first << " init " << s.ToString();
    if (s.ok()) {
      collectors_.push_back(collector.second.get());
    }
  }

  bthread_timer_add(
      &timer_id_,
      butil::seconds_from_now(FLAGS_client_memory_stats_interval_secs), Run,
      this);

  started_ = true;

  LOG(INFO) << "Memory monitor started with collect interval:"
            << FLAGS_client_memory_stats_interval_secs << "s";
}

void MemoryMonitor::Run(void* arg) {
  MemoryMonitor* monitor = static_cast<MemoryMonitor*>(arg);
  std::lock_guard<std::mutex> lg(monitor->mtx_);
  if (!monitor->started_) {
    return;
  }

  for (auto* collector : monitor->collectors_) {
    collector->Record();
  }
  bthread_timer_add(
      &monitor->timer_id_,
      butil::seconds_from_now(FLAGS_client_memory_stats_interval_secs), Run,
      arg);
}

Status JemallcStatsCollector::Init() {
  allocated_.expose_as("client_jemalloc_mem_stats_", "allocated_MB");
  active_.expose_as("client_jemalloc_mem_stats_", "active_MB");
  metadata_.expose_as("client_jemalloc_mem_stats_", "metadata_MB");
  resident_.expose_as("client_jemalloc_mem_stats_", "resident_MB");
  mapped_.expose_as("client_jemalloc_mem_stats_", "mapped_MB");
  retained_.expose_as("client_jemalloc_mem_stats_", "retained_MB");

  // try direct get symbol
  func_ = reinterpret_cast<mallctl_fn>(dlsym(RTLD_DEFAULT, "mallctl"));
  if (func_) {
    loaded_ = true;
    return Status::OK();
  }

  // try get symbol from dynamic libray
  const char* jemalloc_libs[] = {"libjemalloc.so", "libjemalloc.so.2",
                                 "libjemalloc.so.1",
                                 "libjemalloc.dylib",  // macOS
                                 nullptr};

  for (int i = 0; jemalloc_libs[i] != nullptr; ++i) {
    auto* jemalloc_handle = dlopen(jemalloc_libs[i], RTLD_LAZY | RTLD_LOCAL);
    if (jemalloc_handle) {
      func_ = reinterpret_cast<mallctl_fn>(dlsym(jemalloc_handle, "mallctl"));
      if (func_) {
        LOG(INFO) << "find mallctl sym from lib" << jemalloc_libs[i];
        loaded_ = true;
        return Status::OK();
      }
      dlclose(jemalloc_handle);
      jemalloc_handle = nullptr;
    }
  }

  return Status::NotFound("can't find mallctl sym");
}

void JemallcStatsCollector::Record() {
  uint64_t value, len;

  if (!loaded_) return;

  VLOG(6) << "collect jemalloc stats";

  len = sizeof(value);
  if (func_("stats.allocated", &value, &len, nullptr, 0) == 0) {
    allocated_.set_value(1.0 * value / (1024 * 1024));
  }
  if (func_("stats.active", &value, &len, nullptr, 0) == 0) {
    active_.set_value(1.0 * value / (1024 * 1024));
  }
  if (func_("stats.metadata", &value, &len, nullptr, 0) == 0) {
    metadata_.set_value(1.0 * value / (1024 * 1024));
  }
  if (func_("stats.resident", &value, &len, nullptr, 0) == 0) {
    resident_.set_value(1.0 * value / (1024 * 1024));
  }
  if (func_("stats.retained", &value, &len, nullptr, 0) == 0) {
    retained_.set_value(1.0 * value / (1024 * 1024));
  }
  if (func_("stats.mapped", &value, &len, nullptr, 0) == 0) {
    mapped_.set_value(1.0 * value / (1024 * 1024));
  }
}

}  // namespace metrics
}  // namespace client
}  // namespace dingofs