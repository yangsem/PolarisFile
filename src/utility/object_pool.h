#ifndef __OBJECT_POOL_H
#define __OBJECT_POOL_H

#include <functional>
#include <include/common.h>
#include <utility/perf_profiler.h>

#ifdef OS_WIN
#include <intrin.h>
#endif

namespace utility
{

    using BitSetType = uint64_t;
    constexpr uint16_t BitSetScale = (3 + 3); // 8 * 8 = 64 = sizeof(BitSetType)

    constexpr uint16_t BlockObjectSize = 1024; // 要能被64整除！
    constexpr uint32_t BlockBitSize = BlockObjectSize / (sizeof(BitSetType) * 8);
    constexpr uint32_t MinBlockCount = 128;

    class CObjectPool
    {
        struct ElemHead
        {
            uint64_t pOwnerBlock_ : 48; // 所属的块指针
            uint64_t uObjIndex_ : 16;   // 在块中的编号
            uint8_t pData_[];

            void Reset(uint16_t uObjIdx, uint64_t pOwnerBlock)
            {
                pOwnerBlock_ = pOwnerBlock;
                uObjIndex_ = uObjIdx;
            }

            void *GetOwnerBlockPtr() { return reinterpret_cast<void *>(pOwnerBlock_); }
            uint16_t GetObjIndex() { return (uint16_t)uObjIndex_; }
        };

        struct ObjectBlock
        {
            uint16_t uCurrSize_;                  // 当前被使用的数量
            uint8_t Reverse_[2];                   // padding
            uint32_t uIndex_;                     // 当前块在pool中索引
            BitSetType bitSetFree_[BlockBitSize]; // 被使用的置零，空闲的置1
            uint8_t pData_[];

            void Reset(uint32_t _uIndex)
            {
                uCurrSize_ = 0;
                Reverse_[0] = 0x7F;
                Reverse_[1] = 0x7F;
                uIndex_ = _uIndex;
                memset(bitSetFree_, 0XFF, sizeof(bitSetFree_));
            }

            // 块内元素被全部耗尽
            bool IsEmpty() { return uCurrSize_ == BlockObjectSize; }
            // 块内元素未被使用
            bool IsReFill() { return uCurrSize_ == 0; }
            uint32_t GetCurrSize() { return uCurrSize_; }

            ElemHead *GetObject(uint32_t uObjSize)
            {
                for (uint32_t i = 0; i < BlockBitSize; i++)
                {
                    if (bitSetFree_[i] != 0)
                    {
                        unsigned long idx = 0;
#ifdef _WIN32
                        _BitScanForward64(&idx, bitSetFree_[i]);
#else
                        idx = __builtin_ctzll(bitSetFree_[i]);
#endif
                        bitSetFree_[i] &= ~(BitSetType(1) << idx);
                        uCurrSize_++;
                        uint16_t uObjIdx = (i << BitSetScale) + idx;
                        auto lpElemHead = (ElemHead *)&pData_[uObjIdx * uObjSize];
                        lpElemHead->Reset(uObjIdx, (uint64_t)this);
                        return lpElemHead;
                    }
                }

                return nullptr;
            }

            void ReleaseObject(ElemHead *lpElemHead)
            {
                auto ObjIndex_ = lpElemHead->GetObjIndex();
                auto bitSetIndex = ObjIndex_ >> BitSetScale;
                auto bitIndex = ObjIndex_ & 0x0F;
                bitSetFree_[bitSetIndex] |= (1 << bitIndex);
                uCurrSize_--;
            }
        };

    public:
        CObjectPool() = default;
        ~CObjectPool() = default;

        int32_t Init(uint32_t uObjectSize, std::function<void(void*)> funcConstruct = nullptr)
        {
            UnInit();

            m_funcConstruct = funcConstruct;

            m_lppBlocks = (ObjectBlock **)calloc(MinBlockCount, sizeof(ObjectBlock *));
            if (m_lppBlocks == nullptr)
            {
                return 1;
            }

            m_uObjectSize = sizeof(ElemHead) + ALIGN8(uObjectSize);
            m_uCapSize = MinBlockCount;

            for (uint32_t i = 0; i < 16; i++)
            {
                if (Expand() == nullptr)
                {
                    return 1;
                }
            }

            return 0;
        }

        void UnInit()
        {
            if (m_lppBlocks != nullptr)
            {
                for (uint32_t i = 0; i < m_uCapSize; i++)
                {
                    if (m_lppBlocks[i] != nullptr)
                    {
                        free(m_lppBlocks[i]);
                    }
                }
                free(m_lppBlocks);
            }
        }

        void *Get()
        {
            // 向后找下一个可用的块，预期只有两次循环，curr和next
            for (uint32_t i = m_uCurrIndex; i != m_uRear; i = GetNext(i))
            {
                if (likely(m_lppBlocks[i] != nullptr && !m_lppBlocks[i]->IsEmpty()))
                {
                    // 有上面两个判断，必定是成功，否则是程序异常
                    auto lpElemHead = m_lppBlocks[i]->GetObject(m_uObjectSize);
                    m_uCurrIndex = i;
                    return lpElemHead->pData_;
                }
            }

            // 向前找一个可用
            auto end = GetPrev(m_uFront);
            for (uint32_t i = m_uCurrIndex; i != end; i = GetPrev(i))
            {
                if (likely(m_lppBlocks[i] != nullptr && !m_lppBlocks[i]->IsEmpty()))
                {
                    // 有上面两个判断，必定是成功，否则是程序异常
                    auto lpElemHead = m_lppBlocks[i]->GetObject(m_uObjectSize);
                    m_uCurrIndex = i;
                    return lpElemHead->pData_;
                }
            }

            // 扩容
            auto lpBlock = Expand();
            if (likely(lpBlock != nullptr))
            {
                m_uCurrIndex = lpBlock->uIndex_;
                auto lpElemHead = lpBlock->GetObject(m_uObjectSize);
                return lpElemHead->pData_;
            }

            // 内存申请不出来
            return nullptr;
        }

        void Release(void *ptr)
        {
            if (unlikely(ptr == nullptr))
            {
                return;
            }

            auto lpElemHead = (ElemHead *)((uint8_t *)ptr - sizeof(ElemHead));
            auto lpOwnerBlock = (ObjectBlock *)lpElemHead->GetOwnerBlockPtr();
            lpOwnerBlock->ReleaseObject(lpElemHead);
            if (unlikely(lpOwnerBlock->IsReFill() && lpOwnerBlock->uIndex_ != m_uCurrIndex))
            {
                m_lppBlocks[lpOwnerBlock->uIndex_] = nullptr;
                // 存在有人长期不释放内存，那就要整理内存
                if (unlikely(m_uRear == m_uFront))
                {
                    CompactFrontBlock(); // m_uFront至少前进一格，因为当前要释放的块位置置空了
                }

                lpOwnerBlock->Reset(m_uRear);
                m_lppBlocks[m_uRear] = lpOwnerBlock;
                m_uRear = GetNext(m_uRear);
                // 提前调整范围，减少get的搜索范围
                while (m_lppBlocks[m_uFront] == nullptr && m_uFront != m_uCurrIndex)
                {
                    m_uFront = GetNext(m_uFront);
                }
            }
        }

    private:
        uint32_t GetNext(uint32_t uIndex) { return (uIndex + 1) % m_uCapSize; }
        uint32_t GetPrev(uint32_t uIndex) { return (uIndex + m_uCapSize - 1) % m_uCapSize; }

        // 整理用过的内存块
        void CompactFrontBlock()
        {
            auto begin = GetPrev(m_uCurrIndex);
            auto end = GetPrev(m_uFront);
            auto slow = m_uCurrIndex;
            // 遍历 [front, curr) 范围，收缩所有非null的块
            for (uint32_t fast = begin; fast != end; fast = GetPrev(fast))
            {
                auto lpCurBlock = m_lppBlocks[fast];
                if (lpCurBlock != nullptr && slow != fast)
                {
                    slow = GetPrev(slow);
                    lpCurBlock->Reset(slow);
                    m_lppBlocks[slow] = lpCurBlock;
                    m_lppBlocks[fast] = nullptr;
                }
            }
            m_uFront = slow;
        }

        void WarnUp(void *ptr, uint32_t uSize)
        {
            uint32_t uPageSize = 0;
#ifdef OS_WIN
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            uPageSize = (uint32_t)si.dwPageSize;
#else
            uPageSize = (uint32_t)sysconf(_SC_PAGESIZE);
#endif
            auto addr = (uint8_t *)ptr;
            for (uint32_t offset = 0; offset < uSize; offset += uPageSize)
            {
                addr[offset] = 0x00;
            }
            addr[uSize - 1] = 0x00;
        }

        ObjectBlock *Expand()
        {
            if (unlikely(m_uCurrSize == m_uCapSize))
            {
                auto uNewCap = m_uCapSize * 2;
                auto lppTmpBlocks = (ObjectBlock **)calloc(uNewCap, sizeof(ObjectBlock *));
                if (unlikely(lppTmpBlocks == nullptr))
                {
                    return nullptr;
                }

                int uNewRear = 0;
                for (uint32_t i = 0; i < m_uCapSize; i++)
                {
                    if (m_lppBlocks[i] != nullptr)
                    {
                        m_lppBlocks[i]->uIndex_ = uNewRear;
                        lppTmpBlocks[uNewRear++] = m_lppBlocks[i];
                    }
                }

                m_lppBlocks = lppTmpBlocks;
                m_uCapSize = uNewCap;
                m_uFront = 0;
                m_uRear = uNewRear;
                m_uCurrIndex = 0;
            }

            if (unlikely(m_uRear == m_uFront))
            {
                CompactFrontBlock(); // 这里一定可以整理出空位，因为槽位没满，所有可以进行下一步
            }

            uint32_t uBlockSize = sizeof(ObjectBlock) + m_uObjectSize * BlockObjectSize;
            auto lpNewBlock = (ObjectBlock *)malloc(uBlockSize);
            if (unlikely(lpNewBlock == nullptr))
            {
                return nullptr;
            }

            if (m_funcConstruct != nullptr)
            {
                for (uint32_t i = 0; i < BlockObjectSize; i++)
                {
                    auto ptr = (ElemHead *)lpNewBlock->pData_[i + m_uObjectSize];
                    m_funcConstruct(ptr->pData_);
                }
            }
            else
            {
                WarnUp(lpNewBlock, uBlockSize);
            }

            lpNewBlock->Reset(m_uRear);
            m_lppBlocks[m_uRear] = lpNewBlock;
            m_uRear = GetNext(m_uRear);
            m_uCurrSize++;
            return lpNewBlock;
        }

    private:
        uint32_t m_uObjectSize{0};
        uint32_t m_uCurrIndex{0};
        uint32_t m_uCurrSize{0};
        uint32_t m_uFront{0};
        uint32_t m_uRear{0};
        uint32_t m_uCapSize{0};
        ObjectBlock **m_lppBlocks{nullptr};
        std::function<void(void*)> m_funcConstruct;
    };

} // end namespace utilitiy

#endif //__OBJECT_POOL_H
