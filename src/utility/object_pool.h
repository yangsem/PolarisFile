#ifndef __OBJECT_POOL_H
#define __OBJECT_POOL_H

#include <include/common.h>

#ifdef OS_WIN
#include <intrin.h>
#endif

namespace utility
{

    using BitSetType = uint64_t;

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

            bool IsEmpty() { return uCurrSize_ == BlockObjectSize; }
            bool IsFull() { return uCurrSize_ == 0; }
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
                        uint32_t uObjIdx = (i << 3) + idx;
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
                auto bitSetIndex = ObjIndex_ >> 3;
                auto bitIndex = ObjIndex_ & 0x0F;
                bitSetFree_[bitSetIndex] |= (1 << bitIndex);
                uCurrSize_--;
            }
        };

    public:
        CObjectPool() = default;
        ~CObjectPool() = default;

        int32_t Init(uint32_t uObjectSize)
        {
            UnInit();

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
            // 向后找下一个可用的块
            for (uint32_t i = m_uCurrIndex; i != m_uRear; i = GetNext(i))
            {
                if (m_lppBlocks[i] != nullptr && !m_lppBlocks[i]->IsEmpty())
                {
                    // 有上面两个判断，必定是成功，否则是程序异常
                    auto lpElemHead = m_lppBlocks[i]->GetObject(m_uObjectSize);
                    m_uCurrIndex = i;
                    return lpElemHead->pData_;
                }
            }

            // 向前找一个可用
            for (uint32_t i = m_uCurrIndex; i != m_uFront; i = GetPrev(i))
            {
                if (m_lppBlocks[i] != nullptr && !m_lppBlocks[i]->IsEmpty())
                {
                    // 有上面两个判断，必定是成功，否则是程序异常
                    auto lpElemHead = m_lppBlocks[i]->GetObject(m_uObjectSize);
                    m_uCurrIndex = i;
                    return lpElemHead->pData_;
                }
            }

            // 扩容
            auto lpBlock = Expand();
            if (lpBlock != nullptr)
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
            if (ptr == nullptr)
            {
                return;
            }

            auto lpElemHead = (ElemHead *)((uint8_t *)ptr - sizeof(ElemHead));
            auto lpOwnerBlock = (ObjectBlock *)lpElemHead->GetOwnerBlockPtr();
            lpOwnerBlock->ReleaseObject(lpElemHead);
            if (lpOwnerBlock->uIndex_ != m_uCurrIndex && lpOwnerBlock->IsFull())
            {
                m_lppBlocks[lpOwnerBlock->uIndex_] = nullptr;
                if (m_uRear == m_uFront)
                {
                    free(lpOwnerBlock);
                }
                else
                {
                    lpOwnerBlock->Reset(m_uRear);
                    m_lppBlocks[m_uRear] = lpOwnerBlock;
                    m_uRear = GetNext(m_uRear);
                }

                while (m_lppBlocks[m_uFront] == nullptr)
                {
                    m_uFront = GetNext(m_uFront);
                }
            }
        }

    private:
        uint32_t GetNext(uint32_t uIndex) { return (uIndex + 1) % m_uCapSize; }
        uint32_t GetPrev(uint32_t uIndex) { return (uIndex + m_uCapSize - 1) % m_uCapSize; }

        ObjectBlock *Expand()
        {
            if (m_uCurrSize == m_uCapSize)
            {
                auto uNewCap = m_uCapSize * 2;
                auto lppTmpBlocks = (ObjectBlock **)calloc(uNewCap, sizeof(ObjectBlock *));
                if (lppTmpBlocks == nullptr)
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

            auto lpNewBlock = (ObjectBlock *)malloc(sizeof(ObjectBlock) + m_uObjectSize * BlockObjectSize);
            if (lpNewBlock == nullptr)
            {
                return nullptr;
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
    };

} // end namespace utilitiy

#endif //__OBJECT_POOL_H
