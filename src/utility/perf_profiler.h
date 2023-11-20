#ifndef __PERF_PROFILER_H
#define __PERF_PROFILER_H

#include <include/common.h>
#include <vector>

#define GetTimeDiff(begin, end) uint64_t((end.tv_sec - begin.tv_sec) * (1000 * 1000 * 1000) + (end.tv_nsec - begin.tv_nsec));
#define BeginPerfTest(name) CPerfProfilerWrap ts_begin_##name, ts_end_##name; clock_gettime(CLOCK_MONOTONIC, &ts_begin_##name);
#define EndPerfTest(name, channal) clock_gettime(CLOCK_MONOTONIC, &ts_end_##name); fprintf(channal, #name " = %lu\n", GetTimeDiff(ts_begin_##name, ts_end_##name));

namespace utility
{
    constexpr uint32_t StepTimePointSize = 256;
    class CPerfProfiler
    {
    public:
        struct TimePoint
        {
            const char *lpName_;
            timespec tsTime_{0};
        };

        class IObjAllocator
        {
        protected:
            virtual ~IObjAllocator() = default;

        public:
            virtual int32_t SetObjSize(uint32_t uObjSize) = 0;
            virtual TimePoint *Get() = 0;
            virtual void *Release(TimePoint *ptr) = 0;
        };

    public:
        CPerfProfiler(IObjAllocator *lpAllocator = nullptr) : m_lpAllocator(lpAllocator)
        {
            if (m_lpAllocator != nullptr && m_lpAllocator->SetObjSize(sizeof(TimePoint[StepTimePointSize])) != 0)
            {
                m_lpAllocator = nullptr;
            }
        }

        ~CPerfProfiler()
        {
            for (auto &item : m_vecTimes)
            {
                if (item != m_Time)
                {
                    if (m_lpAllocator != nullptr)
                    {
                        m_lpAllocator->Release(item);
                    }
                }
            }
        }

        static void GetTime(timespec &ts)
        {
            clock_gettime(CLOCK_MONOTONIC, &ts);
        }

        static uint64_t GetTimeNano(timespec &ts)
        {
            return uint64_t(ts.tv_sec * 1000000000 + ts.tv_nsec);
        }

        static uint64_t GetTimeDiffNano(timespec &begin, timespec &end)
        {
            return uint64_t((end.tv_sec - begin.tv_sec) * 1000000000 + (end.tv_nsec - begin.tv_nsec));
        }

        void Add(const char *lpName)
        {
            if (unlikely(m_lpTime == nullptr))
            {
                return;
            }

            if (unlikely(m_lpTime == m_Time && m_uSize >= sizeof(m_Time)/sizeof(TimePoint)) 
                || unlikely(m_uSize >= StepTimePointSize))
            {
                if (unlikely(Expand() == nullptr))
                {
                    return;
                }
            }

            m_lpTime[m_uSize].lpName_ = lpName;
            GetTime(m_lpTime[m_uSize].tsTime_);
            m_uSize++;
        }

        uint32_t GetSize() { return m_vecTimes.size() * StepTimePointSize + m_uSize; }

        TimePoint *At(uint32_t i)
        {
            if (i >= GetSize())
            {
                return nullptr;
            }

            auto uVecSize = m_vecTimes.size() * StepTimePointSize;
            if (i < uVecSize)
            {
                return &m_vecTimes[i / StepTimePointSize][i % StepTimePointSize];
            }

            return &m_lpTime[i - uVecSize];
        }

    private:
        TimePoint *Expand()
        {
            timespec tmpTs;
            GetTime(tmpTs);

            m_vecTimes.push_back(m_lpTime);
            m_lpTime = nullptr;
            m_uSize = 0;
            
            if (m_lpAllocator != nullptr)
            {
                m_lpTime = m_lpAllocator->Get();
            }
            if (m_lpTime != nullptr)
            {
                m_lpTime[m_uSize].lpName_ = "__pf_expand";
                m_lpTime[m_uSize].tsTime_ = tmpTs;
                m_uSize++;
                return m_lpTime;
            }
            
            return nullptr;
        }

    private:
        TimePoint m_Time[StepTimePointSize];
        TimePoint *m_lpTime{m_Time};
        IObjAllocator *m_lpAllocator{nullptr};
        uint32_t m_uSize{0};
        uint32_t Reverse{0};
        std::vector<TimePoint *> m_vecTimes;
    };

    class CPerfProfilerWrap
    {
    public:
        CPerfProfilerWrap(CPerfProfiler::IObjAllocator *lpAllocator = nullptr) : m_perf(lpAllocator)
        {
            m_perf.Add("begin");
        }

        ~CPerfProfilerWrap() = default;

        void Add(const char *lpName) { m_perf.Add(lpName); }

        void Print(const char *szTip)
        {
            uint64_t uSum = 0;
            uint64_t uCount = 0;
            auto uSize = m_perf.GetSize();
            for (uint32_t i = 1; i < uSize; i++)
            {
                uSum += CPerfProfiler::GetTimeDiffNano(m_perf.At(i - 1)->tsTime_, m_perf.At(i)->tsTime_);
                uCount++;
            }
            
            if (uCount == 0)
            {
                printf("empty statics!\n");
            }
            else
            {
                printf("\n%s: avg = %lu\n",szTip, uSum / uCount);
            }
        }

        void Save(const char *szName)
        {
            auto lpFile = fopen(szName, "w");
            if (lpFile == nullptr)
            {
                return;
            }

            auto uSize = m_perf.GetSize();
            for (uint32_t i = 1; i < uSize; i++)
            {
                fprintf(lpFile, "%s, %lu\n",m_perf.At(i)->lpName_,  CPerfProfiler::GetTimeDiffNano(m_perf.At(i - 1)->tsTime_, m_perf.At(i)->tsTime_));
            }

            fclose(lpFile);
        }

    private:
        CPerfProfiler m_perf;
    };

} // end namespace utility

#endif //__PERF_PROFILER_H
