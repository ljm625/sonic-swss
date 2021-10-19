#include <map>
#include <inttypes.h>

#include "switchorch.h"
#include "converter.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "macaddress.h"

using namespace std;
using namespace swss;

#define SAI_SWITCH_ATTR_CUSTOM_RANGE_BASE SAI_SWITCH_ATTR_CUSTOM_RANGE_START


extern sai_object_id_t gSwitchId;
extern sai_switch_api_t *sai_switch_api;
extern MacAddress gVxlanMacAddress;

const map<string, sai_switch_attr_t> switch_attribute_map =
{
    {"fdb_unicast_miss_packet_action",      SAI_SWITCH_ATTR_FDB_UNICAST_MISS_PACKET_ACTION},
    {"fdb_broadcast_miss_packet_action",    SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION},
    {"fdb_multicast_miss_packet_action",    SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_PACKET_ACTION},
    {"ecmp_hash_seed",                      SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED},
    {"lag_hash_seed",                       SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED},
    {"fdb_aging_time",                      SAI_SWITCH_ATTR_FDB_AGING_TIME},
    {"vxlan_port",                          SAI_SWITCH_ATTR_VXLAN_DEFAULT_PORT},
    {"vxlan_router_mac",                    SAI_SWITCH_ATTR_VXLAN_DEFAULT_ROUTER_MAC},
    {"ecmp_hash_offset",                      SAI_SWITCH_ATTR_EXT_ECMP_HASH_OFFSET},
    {"lag_hash_offset",                       SAI_SWITCH_ATTR_EXT_LAG_HASH_OFFSET}

};


typedef enum _sai_switch_attr_extensions_t
{
    /**
     * @brief List of ACL Field list
     *
     * The value is of type sai_s32_list_t where each list member is of type
     * sai_acl_table_attr_t. Only fields in the range SAI_ACL_TABLE_ATTR_FIELD_START
     * and SAI_ACL_TABLE_ATTR_FIELD_END as well any custom SAI_ACL_TABLE_ATTR_FIELD
     * are allowed. All other field types in sai_acl_table_attr_t are ignored.
     *
     * @type sai_s32_list_t
     * @flags CREATE_ONLY
     * @isvlan false
     */
    SAI_SWITCH_ATTR_EXT_ACL_FIELD_LIST = 0x10000000,

    /**
     * @brief Inject ECC error.
     *
     * When this value is set, ECC error initiate register will be set in HW.
     * As a result, ECC error will be generated. This feature is for testing and debug purpose.
     * If value is 1, 1 bit ECC error is generated and 2 for 2 bits error.
     *
     * @type sai_uint16_t
     * @flags CREATE_AND_SET
     * @isvlan false
     */
    SAI_SWITCH_ATTR_EXT_HW_ECC_ERROR_INITIATE,

    /**
     * @brief ECMP HASH offset.
     *
     * The value is of HASH offset value for ECMP.
     *
     * @type sai_uint8_t
     * @flags CREATE_AND_SET
     * @default 0
     */
    SAI_SWITCH_ATTR_EXT_ECMP_HASH_OFFSET,

    /**
     * @brief ECMP HASH offset.
     *
     * The value is of HASH offset value for LAG.
     *
     * @type sai_uint8_t
     * @flags CREATE_AND_SET
     * @default 0
     */
    SAI_SWITCH_ATTR_EXT_LAG_HASH_OFFSET,

    /**
     * @brief End of attributes
     */
    SAI_SWITCH_ATTR_EXT_END

} sai_switch_attr_extensions_t;


const map<string, sai_switch_attr_extensions_t> switch_attribute_ext_map =
{
    {"ecmp_hash_offset",                      SAI_SWITCH_ATTR_EXT_ECMP_HASH_OFFSET},
    {"lag_hash_offset",                       SAI_SWITCH_ATTR_EXT_LAG_HASH_OFFSET}
};


const map<string, sai_packet_action_t> packet_action_map =
{
    {"drop",    SAI_PACKET_ACTION_DROP},
    {"forward", SAI_PACKET_ACTION_FORWARD},
    {"trap",    SAI_PACKET_ACTION_TRAP}
};

SwitchOrch::SwitchOrch(DBConnector *db, string tableName, TableConnector switchTable):
        Orch(db, tableName),
        m_switchTable(switchTable.first, switchTable.second),
        m_db(db)
{
    m_restartCheckNotificationConsumer = new NotificationConsumer(db, "RESTARTCHECK");
    auto restartCheckNotifier = new Notifier(m_restartCheckNotificationConsumer, this, "RESTARTCHECK");
    Orch::addExecutor(restartCheckNotifier);
}

void SwitchOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        auto t = it->second;
        auto op = kfvOp(t);

        if (op == SET_COMMAND)
        {
            bool retry = false;

            for (auto i : kfvFieldsValues(t))
            {
                auto attribute = fvField(i);
                bool ext = false;

                if (switch_attribute_map.find(attribute) == switch_attribute_map.end())
                {
                    ext = true;
                    if (switch_attribute_ext_map.find(attribute) == switch_attribute_ext_map.end()){
                        SWSS_LOG_ERROR("Unsupported switch attribute %s", attribute.c_str());
                        break;
                    }
                }

                auto value = fvValue(i);

                sai_attribute_t attr;
                if(ext){
                    attr.id = switch_attribute_ext_map.at(attribute);

                }
                else{
                    attr.id = switch_attribute_map.at(attribute);
                }

                MacAddress mac_addr;
                bool invalid_attr = false;
                switch (attr.id)
                {
                    case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_PACKET_ACTION:
                    case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_PACKET_ACTION:
                    case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_PACKET_ACTION:
                        if (packet_action_map.find(value) == packet_action_map.end())
                        {
                            SWSS_LOG_ERROR("Unsupported packet action %s", value.c_str());
                            invalid_attr = true;
                            break;
                        }
                        attr.value.s32 = packet_action_map.at(value);
                        break;

                    case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
                    case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
                        attr.value.u32 = to_uint<uint32_t>(value);
                        break;

                    case SAI_SWITCH_ATTR_FDB_AGING_TIME:
                        attr.value.u32 = to_uint<uint32_t>(value);
                        break;

                    case SAI_SWITCH_ATTR_VXLAN_DEFAULT_PORT:
                        attr.value.u16 = to_uint<uint16_t>(value);
                        break;

                    case SAI_SWITCH_ATTR_VXLAN_DEFAULT_ROUTER_MAC:
                        mac_addr = value;
                        gVxlanMacAddress = mac_addr;
                        memcpy(attr.value.mac, mac_addr.getMac(), sizeof(sai_mac_t));
                        break;

                    case SAI_SWITCH_ATTR_EXT_ECMP_HASH_OFFSET:
                        SWSS_LOG_NOTICE("Updating ECMP HASH OFFSET");
                        attr.value.u8 = to_uint<uint8_t>(value);
                        break;

                    case SAI_SWITCH_ATTR_EXT_LAG_HASH_OFFSET:
                        SWSS_LOG_NOTICE("Updating LAG HASH OFFSET");
                        attr.value.u8 = to_uint<uint8_t>(value);
                        break;

                    default:
                        invalid_attr = true;
                        break;
                }
                if (invalid_attr)
                {
                    /* break from kfvFieldsValues for loop */
                    break;
                }

                sai_status_t status = sai_switch_api->set_switch_attribute(gSwitchId, &attr);
                if (status != SAI_STATUS_SUCCESS)
                {
                    SWSS_LOG_ERROR("Failed to set switch attribute %s to %s, rv:%d",
                            attribute.c_str(), value.c_str(), status);
                    retry = true;
                    break;
                }

                SWSS_LOG_NOTICE("Set switch attribute %s to %s", attribute.c_str(), value.c_str());
            }
            if (retry == true)
            {
                it++;
            }
            else
            {
                it = consumer.m_toSync.erase(it);
            }
        }
        else
        {
            SWSS_LOG_WARN("Unsupported operation");
            it = consumer.m_toSync.erase(it);
        }
    }
}

void SwitchOrch::doTask(NotificationConsumer& consumer)
{
    SWSS_LOG_ENTER();

    std::string op;
    std::string data;
    std::vector<swss::FieldValueTuple> values;

    consumer.pop(op, data, values);

    if (&consumer != m_restartCheckNotificationConsumer)
    {
        return;
    }

    m_warmRestartCheck.checkRestartReadyState = false;
    m_warmRestartCheck.noFreeze = false;
    m_warmRestartCheck.skipPendingTaskCheck = false;

    SWSS_LOG_NOTICE("RESTARTCHECK notification for %s ", op.c_str());
    if (op == "orchagent")
    {
        string s  =  op;

        m_warmRestartCheck.checkRestartReadyState = true;
        for (auto &i : values)
        {
            s += "|" + fvField(i) + ":" + fvValue(i);

            if (fvField(i) == "NoFreeze" && fvValue(i) == "true")
            {
                m_warmRestartCheck.noFreeze = true;
            }
            if (fvField(i) == "SkipPendingTaskCheck" && fvValue(i) == "true")
            {
                m_warmRestartCheck.skipPendingTaskCheck = true;
            }
        }
        SWSS_LOG_NOTICE("%s", s.c_str());
    }
}

void SwitchOrch::restartCheckReply(const string &op, const string &data, std::vector<FieldValueTuple> &values)
{
    NotificationProducer restartRequestReply(m_db, "RESTARTCHECKREPLY");
    restartRequestReply.send(op, data, values);
    checkRestartReadyDone();
}

bool SwitchOrch::setAgingFDB(uint32_t sec)
{
    sai_attribute_t attr;
    attr.id = SAI_SWITCH_ATTR_FDB_AGING_TIME;
    attr.value.u32 = sec;
    auto status = sai_switch_api->set_switch_attribute(gSwitchId, &attr);
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to set switch %" PRIx64 " fdb_aging_time attribute: %d", gSwitchId, status);
        return false;
    }
    SWSS_LOG_NOTICE("Set switch %" PRIx64 " fdb_aging_time %u sec", gSwitchId, sec);
    return true;
}

void SwitchOrch::set_switch_capability(const std::vector<FieldValueTuple>& values)
{
     m_switchTable.set("switch", values);
}
