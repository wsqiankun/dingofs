#include <bthread/types.h>
#include <bthread/unstable.h>
#include <bvar/bvar.h>
#include <bvar/passive_status.h>
#include <bvar/reducer.h>
#include <bvar/status.h>
#include <gflags/gflags_declare.h>

#include <ctime>
#include <memory>
#include <mutex>
#include <vector>

#include "common/status.h"

namespace dingofs {
namespace client {
namespace metrics {

class MemoryCollector;
using mallctl_fn = int (*)(const char* name, void* oldp, size_t* oldlenp,
                           void* newp, size_t newlen);

extern std::vector<std::pair<std::string, std::unique_ptr<MemoryCollector>>>
    g_target_collectors;

class MemoryCollector {
 public:
  MemoryCollector() = default;
  virtual ~MemoryCollector() = default;

  virtual Status Init() = 0;
  virtual void Record() = 0;
};

class JemallcStatsCollector : public MemoryCollector {
 public:
  Status Init() override;
  void Record() override;

 private:
  bool loaded_{false};
  mallctl_fn func_;
  bvar::Status<float> allocated_{0};
  bvar::Status<float> active_{0};
  bvar::Status<float> metadata_{0};
  bvar::Status<float> resident_{0};
  bvar::Status<float> mapped_{0};
  bvar::Status<float> retained_{0};
};

class MemoryMonitor {
 public:
  void Init();

  ~MemoryMonitor() {
    if (started_) Stop();
  }

  void Stop() {
    std::lock_guard<std::mutex> lg(mtx_);
    started_ = false;
    bthread_timer_del(timer_id_);
    LOG(INFO) << "Memory monitor stopped";
  }

 private:
  static void Run(void* arg);

  std::mutex mtx_;
  bool started_;
  std::vector<MemoryCollector*> collectors_;
  bthread_timer_t timer_id_;
};

}  // namespace metrics
}  // namespace client
}  // namespace dingofs
