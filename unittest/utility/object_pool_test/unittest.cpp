#include <utility/object_pool.h>
#include <vector>
#include <thread>
#include <mutex>

using namespace utility;

struct ObjDemo
{
    ObjDemo(){}
    char a{'1'};
    uint8_t b{2};
    uint16_t c{3};
    int d{4};
    uint32_t e{5};
    uint64_t q{6};
    char f[64];
    
    void Set(char ch)
    {
        a = ch;
        b = 2;
        c = 3;
        d = 4;
        e = 5;
        q = 6;
        memset(f, ch, sizeof(f));
    }

    bool IsFOk()
    {
        for (int i = 0; i < sizeof(f); i++)
        {
            if (f[i] != a)
            {
                PRINT_ERROR("a = %c, f[%d] = %c", a, i, f[i]);
                return false;
            }
        }
        return true;
    }

    bool IsOk()
    {
        return b==2 && c == 3 && d == 4 && e == 5 && q == 6 && IsFOk();
    }
};

class TestHelper
{
public:
    TestHelper()
    {
        int iErrorNo = 0;
        iErrorNo = m_pool.Init(sizeof(ObjDemo));
        if (iErrorNo != 0)
        {
            PRINT_FAIL("pool Init Fail");
            exit(1);
        }
    }
    ~TestHelper()
    {
        m_pool.UnInit();
    }

public:
    CObjectPool m_pool;
};

void CaseOneByOne()
{
    PRINT_INFO("=================");
    TestHelper helper;
    auto pool = helper.m_pool;

    for (uint32_t i = 0; i < 10240; i++)
    {
        auto ptr = (ObjDemo *)pool.Get();
        if (ptr != nullptr)
        {
            ptr->Set('a');
            pool.Release(ptr);
        }
    }
    PRINT_INFO("=================");
}

void CaseManyGet2Release()
{
    PRINT_INFO("=================");
    TestHelper helper;
    auto pool = helper.m_pool;
    uint32_t count = 10240;
    auto ptrArr = (ObjDemo **)malloc(sizeof(ObjDemo *)*count);
    auto newCount = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        auto ptr = (ObjDemo *)pool.Get();
        if (ptr != nullptr)
        {
            ptr->Set('a');
            ptrArr[newCount++] = ptr; 
        }
    }
    for (uint32_t i = 0; i < newCount; i++)
    {
        if (!ptrArr[i]->IsOk())
        {
            PRINT_ERROR("ptrArr[%d] Is Not OK", i);
        }
        pool.Release(ptrArr[i]);
    }
    PRINT_INFO("=================");
}

void CaseMultiThreadOneByOne()
{
    PRINT_INFO("=================");
    TestHelper helper;
    auto pool = helper.m_pool;
    auto bRunning = true;
    std::mutex lock;

    auto func = [&]() {
        while (bRunning)
        {
            lock.lock();
            auto ptr = (ObjDemo *)pool.Get();
            lock.unlock();
            if (ptr != nullptr)
            {
                ptr->Set('c');
                usleep(1);
                if (!ptr->IsOk())
                {
                    PRINT_ERROR("ptr Is Not OK");
                }
                lock.lock();
                pool.Release(ptr);
                lock.unlock();
            }
            usleep(10);
        }
    };

    std::thread th[4];
    for (int i = 0; i < sizeof(th)/sizeof(std::thread); i++)
    {
        th[i] = std::thread(func);
    }

    for (uint32_t i = 10; i > 0; i--)
    {
        printf("%u...", i);
        fflush(stdout);
        sleep(1);
    }
    printf("\n");
    bRunning = false;

    for (int i = 0; i < sizeof(th)/sizeof(std::thread); i++)
    {
        if (th[i].joinable())
        {
            th[i].join();
        }
    }
    PRINT_INFO("=================");
}

int main(int argc, const char *argv[])
{
    CaseOneByOne();
    CaseManyGet2Release();
    CaseMultiThreadOneByOne();
    return 0;
}
