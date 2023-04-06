#include "ip_list.h"
#include <ws2tcpip.h>
#include <WinSock2.h>

#define DEBUG_EXIT_TEST(errorMessage) DebugExitTest(__FUNCTION__, errorMessage)

static LimitedIpList GlobalIpList = { 6, 0, NULL };

// List's methods

int Insert(uint32_t ip)
{
    IpNode* newNode = (IpNode*)malloc(sizeof(IpNode));
    newNode->ip = ip;
    newNode->m_next = GlobalIpList.m_head;
    ++GlobalIpList.m_size;
    GlobalIpList.m_head = newNode;
    if (GlobalIpList.m_size > GlobalIpList.m_ipLimit)
    {
        Erase(GlobalIpList.m_size - 1);
        return TRUE;
    }

    return FALSE;
}

size_t Find(uint32_t ip)
{
    IpNode* node = GlobalIpList.m_head;
    for (size_t i = 0; i < GlobalIpList.m_size; ++i, node = node->m_next)
        if (node->ip == ip)
            return i;

    return GlobalIpList.m_ipLimit;
}

void Erase(size_t index)
{
    if (index >= GlobalIpList.m_size)
        return;
    IpNode* node = GlobalIpList.m_head;

    if (!index)
    {
        GlobalIpList.m_head = node->m_next;
    }
    else
    {
        IpNode* prevNode = NULL;
        for (size_t i = 0;
            i < index;
            ++i,
            prevNode = node, node = node->m_next)
            ;
        prevNode->m_next = node ? node->m_next : NULL;
    }

    free(node);
    --GlobalIpList.m_size;
}

void ClearList()
{
    GlobalIpList.m_size = 0;
    IpNode* node = GlobalIpList.m_head;
    while (node)
    {
        IpNode* nextNode = node->m_next;
        free(node);
        node = nextNode;
    }
    GlobalIpList.m_head = NULL;
}

void DebugPrintList()
{
    debug("List's state: size = %u, ipLimit = %u, head -> ", GlobalIpList.m_size, GlobalIpList.m_ipLimit);
    IpNode* node = GlobalIpList.m_head;
    if (!GlobalIpList.m_size)
        debug("NULL\n");
    for (size_t i = 0; i < GlobalIpList.m_size; ++i)
    {
        char addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(node->ip), addr, sizeof(addr));
        debug("[%s] -> ", addr);
        if (node->m_next)
            node = node->m_next;
        else
            debug("NULL\n");
    }
}

// Test methods

void DebugExitTest(const char* functionName, const char* errorExplanation)
{
    debug("%s: %s", functionName, errorExplanation);
    debug("\nPlease, close the program!");
    while (TRUE);
}

void TestOnlyInsert()
{
    if (Insert(3))
        DEBUG_EXIT_TEST("Insert(3) returned TRUE, expected FALSE");
    if(Insert(2))
        DEBUG_EXIT_TEST("Insert(2) returned TRUE, expected FALSE");
    if(Insert(1))
        DEBUG_EXIT_TEST("Insert(1) returned TRUE, expected FALSE");

    if (GlobalIpList.m_size != 3)
        DEBUG_EXIT_TEST("Size is not equal to 3 after 3 insertions!");

    if(!GlobalIpList.m_head)
        DEBUG_EXIT_TEST("Something wrong with list: head is NULL");

    if(GlobalIpList.m_head->ip != 1)
        DEBUG_EXIT_TEST("Something wrong with ip: ip != 1");

    if(!GlobalIpList.m_head->m_next || GlobalIpList.m_head->m_next->ip != 2)
        DEBUG_EXIT_TEST("Something wrong with 2 element");

    if (!GlobalIpList.m_head->m_next->m_next || GlobalIpList.m_head->m_next->m_next->ip != 3)
        DEBUG_EXIT_TEST("Something wrong with 3 element");

    ClearList();
}

void TestMaxInsert()
{
    Insert(7);
    Insert(6);
    Insert(5);
    Insert(4);
    int insert3 = Insert(3);
    int insert2 = Insert(2);
    int insert1 = Insert(1);

    if(insert3 || insert2 || !insert1)
        DEBUG_EXIT_TEST("Some of inserts returned wrong result code!");

    printf("%u\n", GlobalIpList.m_size);

    if (GlobalIpList.m_size != 6)
        DEBUG_EXIT_TEST("Size is not equal to 6 after 7 insertions!");
    IpNode* head = GlobalIpList.m_head;
    size_t testIp = 1;
    for (; testIp < 7 && head && head->ip == testIp;
        ++testIp, head = head->m_next)
        ;

    if (testIp != 7)
    {
        const char testIpStr = testIp + '0';
        char errStr[37] = "Wrong ip instead of ";
        strcat_s(errStr, 1, &testIpStr);
        DEBUG_EXIT_TEST(errStr);
    }

    ClearList();
}

void TestFind()
{
    Insert(3);
    Insert(2);
    Insert(1);
    Insert(0);

    uint32_t testIp = 0;
    for (size_t index = Find(testIp);
        testIp < 4 && index == testIp;
        index = Find(++testIp))
        ;

    if (testIp != 4)
    {
        /*const char debugValues[] = { GlobalIpList.m_head->ip + '0', ' ', GlobalIpList.m_head->m_next->ip + '0', ' ', 
            GlobalIpList.m_head->m_next->m_next->ip + '0', ' ', GlobalIpList.m_head->m_next->m_next->m_next->ip + '0', ' ', GlobalIpList.m_size + '0', '\0'};
        debug("%s\n", debugValues);*/
        char errStr[26] = "Wrong ip ( ) instead of ";
        errStr[10] = (short)Find(testIp) + '0';
        errStr[24] = (short)testIp + '0';
        errStr[25] = '\0';
        DEBUG_EXIT_TEST(errStr);
    }

    if (Find(testIp) != GlobalIpList.m_ipLimit)
        DEBUG_EXIT_TEST("Returned wrong index after Find");

    ClearList();
}

void TestErase()
{
    Insert(3);
    Insert(2);
    Insert(1);
    Insert(0);

    Erase(3);
    if (GlobalIpList.m_size != 3)
        DEBUG_EXIT_TEST("Wrong size after \'Erase(3)\'");
    if (!GlobalIpList.m_head || GlobalIpList.m_head->ip != 0 || !GlobalIpList.m_head->m_next || GlobalIpList.m_head->m_next->ip != 1
        || !GlobalIpList.m_head->m_next->m_next || GlobalIpList.m_head->m_next->m_next->ip != 2 || GlobalIpList.m_head->m_next->m_next->m_next)
        DEBUG_EXIT_TEST("Wrong list\'s state after  \'Erase(3)\', expected HEAD->[0]->[1]->[2]->NULL");

    Erase(1);
    if (GlobalIpList.m_size != 2)
        DEBUG_EXIT_TEST("Wrong size after \'Erase(1)\'");
    if(!GlobalIpList.m_head || GlobalIpList.m_head->ip != 0 || !GlobalIpList.m_head->m_next || GlobalIpList.m_head->m_next->ip != 2
        || GlobalIpList.m_head->m_next->m_next)
        DEBUG_EXIT_TEST("Wrong list\'s state after  \'Erase(1)\', expected HEAD->[0]->[2]->NULL");

    Erase(0);
    if(GlobalIpList.m_size != 1)
        DEBUG_EXIT_TEST("Wrong size after \'Erase(0)\', expected = 1");
    if(!GlobalIpList.m_head || GlobalIpList.m_head->ip != 2 || GlobalIpList.m_head->m_next)
        DEBUG_EXIT_TEST("Wrong list\'s state after  \'Erase(0)\', expected HEAD->[2]->NULL");

    Erase(0);
    if (GlobalIpList.m_size != 0)
        DEBUG_EXIT_TEST("Wrong size after \'Erase(0)\', expected = 0");
    if(GlobalIpList.m_head)
        DEBUG_EXIT_TEST("Wrong list\'s state after  \'Erase(0)\', expected HEAD->NULL");

    Erase(4);
    if (GlobalIpList.m_size != 0)
        DEBUG_EXIT_TEST("Wrong size after \'Erase(4)\', expected = 0");
    if (GlobalIpList.m_head)
        DEBUG_EXIT_TEST("Wrong list\'s state after  \'Erase(4)\', expected HEAD->NULL");

    ClearList();
}

void TestList()
{
    if (GlobalIpList.m_ipLimit != 6)
        DEBUG_EXIT_TEST("IpLimit wasn't equal to 6");

    TestOnlyInsert();
    TestFind();
    TestErase();
    TestMaxInsert();
}
