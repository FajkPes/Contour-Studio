#pragma once
#include <algorithm>
#include <cstdint>
#include <exception>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace contour::detail {
// Reuse Windows' process worker pool instead of starting new OS threads at each
// image pass. Calls may overlap; each call owns its contexts and waits for them.
template <class Action> void parallelRanges(uint32_t first, uint32_t last, size_t pixels, Action action) {
    if (first >= last)
        return;
    static const uint32_t available = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    const uint32_t workers = pixels < 65536 || last - first < 8 ? 1 : available;
    if (workers == 1) {
        action(first, last);
        return;
    }
    struct Context {
        Action *action;
        uint32_t first, last;
        std::exception_ptr error;
    };
    std::vector<Context> contexts;
    contexts.reserve(workers - 1);
    for (uint32_t i = 1; i < workers; ++i)
        contexts.push_back(
            {&action, first + (last - first) * i / workers, first + (last - first) * (i + 1) / workers, {}});
    auto run = [](Context &context) {
        try {
            (*context.action)(context.first, context.last);
        } catch (...) {
            context.error = std::current_exception();
        }
    };
#ifdef _WIN32
    struct Work {
        PTP_WORK handle{};
        Work() = default;
        Work(const Work &) = delete;
        Work &operator=(const Work &) = delete;
        ~Work() {
            if (handle) {
                WaitForThreadpoolWorkCallbacks(handle, FALSE);
                CloseThreadpoolWork(handle);
            }
        }
    };
    std::vector<Work> jobs(workers - 1);
    for (size_t i = 0; i < contexts.size(); ++i) {
        jobs[i].handle = CreateThreadpoolWork(
            [](PTP_CALLBACK_INSTANCE, void *raw, PTP_WORK) {
                auto &context = *static_cast<Context *>(raw);
                try {
                    (*context.action)(context.first, context.last);
                } catch (...) {
                    context.error = std::current_exception();
                }
            },
            &contexts[i], nullptr);
        if (jobs[i].handle)
            SubmitThreadpoolWork(jobs[i].handle);
        else
            run(contexts[i]);
    }
#else
    std::vector<std::jthread> jobs;
    for (auto &context : contexts)
        jobs.emplace_back([&run, &context] { run(context); });
#endif
    action(first, first + (last - first) / workers);
    jobs.clear();
    for (auto &context : contexts)
        if (context.error)
            std::rethrow_exception(context.error);
}
template <class Action> void parallelRows(uint32_t first, uint32_t last, size_t pixels, Action action) {
    parallelRanges(first, last, pixels, [&](uint32_t begin, uint32_t end) {
        for (auto row = begin; row < end; ++row)
            action(row);
    });
}
} // namespace contour::detail
