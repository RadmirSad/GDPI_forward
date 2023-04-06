#include "goodbyedpi.h"
#include <stdint.h>
#include <minwindef.h>

typedef struct IpNode
{
    uint32_t ip;
    struct IpNode* m_next;
} IpNode;

typedef struct LimitedIpList
{
    const size_t m_ipLimit;
    size_t m_size;
    IpNode* m_head;
} LimitedIpList;

int Insert(uint32_t ip);
size_t Find(uint32_t ip);
void Erase(size_t index);
void ClearList();
void DebugPrintList();

void TestList();
