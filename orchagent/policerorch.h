#pragma once

#include <string>
#include <map>
#include <string>
#include <unordered_map>

#include "orch.h"
#include "portsorch.h"
#include "redisapi.h"
#include "redisclient.h"

using namespace std;

#define POLICER_COUNTER_FLEX_COUNTER_GROUP "POLICER_STAT_COUNTER"

typedef map<string, sai_object_id_t> PolicerTable;
typedef map<string, int> PolicerRefCountTable;

class PolicerOrch : public Orch
{
public:
    PolicerOrch(DBConnector* db, string tableName);

    bool policerExists(const string &name);
    bool getPolicerOid(const string &name, sai_object_id_t &oid);

    bool increaseRefCount(const string &name);
    bool decreaseRefCount(const string &name);
    void generatePolicerCounterIdList(void);

private:
    virtual void doTask(Consumer& consumer);
    void initFlexCounterGroupTable(void);

    PolicerTable m_syncdPolicers;
    PolicerRefCountTable m_policerRefCounts;
    unique_ptr<DBConnector> m_flexCounterDb;
    unique_ptr<ProducerTable> m_flexCounterGroupTable;
    unique_ptr<ProducerTable> m_flexCounterTable;

    unique_ptr<DBConnector> m_countersDb;
    RedisClient m_countersDbRedisClient;

};
