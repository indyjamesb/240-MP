#include "MalProgressHook.h"

namespace mal {

namespace {
ProgressSink *g_sink = nullptr;
}

void setProgressSink(ProgressSink *sink)
{
    g_sink = sink;
}

void observeProgress(const ProgressSample &sample)
{
    if (g_sink)
        g_sink->observe(sample);
}

} // namespace mal
