#ifndef __PERF_PROFILER_H
#define __PERF_PROFILER_H

#include <include/common.h>
#include <vector>

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
            if (m_lpAllocator != nullptr)
            {
                m_lpAllocator->SetObjSize(sizeof(TimePoint[StepTimePointSize]));
            }
            m_vecTimes.reserve(16);
            // m_lpTime = m_Time;
            // m_uSize = 0;
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

        static uint64_t GetTimeNano(timespec &ts)
        {
            return uint64_t(ts.tv_sec * 1000000000 + ts.tv_nsec);
        }

        static uint64_t GetTimeDiffNano(timespec &begin, timespec &end)
        {
            return uint64_t((end.tv_sec - begin.tv_sec) * 1000000000 + (end.tv_nsec - begin.tv_nsec));
        }

        void Add(const char *lpName, timespec &ts)
        {
            if (unlikely(m_lpTime == nullptr))
            {
                return;
            }

            if (unlikely(m_lpTime == m_Time && m_uSize >= sizeof(m_Time)/sizeof(TimePoint)) 
                || unlikely(m_uSize >= StepTimePointSize))
            {
                m_vecTimes.push_back(m_lpTime);
                m_lpTime = nullptr;
                m_uSize = 0;
                if (m_lpAllocator != nullptr)
                {
                    m_lpTime = m_lpAllocator->Get();
                }

                if (m_lpTime == nullptr)
                {
                    return;
                }
            }

            m_lpTime[m_uSize].lpName_ = lpName;
            m_lpTime[m_uSize].tsTime_ = ts;
            m_uSize++;
        }

        void Add(const char *lpName)
        {
            timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            Add(lpName, ts);
        }

        void Print(const char *szTip)
        {
            uint64_t uSum = 0;
            uint64_t uCount = 0;
            for (auto &item : m_vecTimes)
            {
                uint32_t uMaxSize = item == m_Time ? sizeof(m_Time)/sizeof(TimePoint) : StepTimePointSize;
                for (uint32_t i = 1; i < uMaxSize; i++)
                {
                    uSum += GetTimeDiffNano(item[i-1].tsTime_, item[i].tsTime_);
                    uCount++;
                }
            }
            for (uint32_t i = 1; i < m_uSize; i++)
            {
                uSum += GetTimeDiffNano(m_lpTime[i-1].tsTime_, m_lpTime[i].tsTime_);
                uCount++;
            }
            
            if (uCount == 0)
            {
                printf("empty statics!\n");
            }
            printf("\n%s: avg = %lu\n",szTip, uSum / uCount);
        }

        void Save(const char *szName)
        {
            auto lpFile = fopen(szName, "w");
            if (lpFile == nullptr) return;

            for (auto &item : m_vecTimes)
            {
                uint32_t uMaxSize = item == m_Time ? sizeof(m_Time)/sizeof(TimePoint) : StepTimePointSize;
                for (uint32_t i = 1; i < uMaxSize; i++)
                {
                    fprintf(lpFile, "%s, %lu\n", item[i].lpName_, GetTimeDiffNano(item[i-1].tsTime_, item[i].tsTime_));
                }
            }
            for (uint32_t i = 1; i < m_uSize; i++)
            {
                fprintf(lpFile, "%s, %lu\n", m_lpTime[i].lpName_, GetTimeDiffNano(m_lpTime[i-1].tsTime_, m_lpTime[i].tsTime_));
            }
            fclose(lpFile);
        }

    private:
        TimePoint m_Time[32];
        TimePoint *m_lpTime{m_Time};
        IObjAllocator *m_lpAllocator{nullptr};
        uint32_t m_uSize{0};
        uint32_t Reverse{0};
        std::vector<TimePoint *> m_vecTimes;
    };

} // end namespace utility

#endif //__PERF_PROFILER_H
