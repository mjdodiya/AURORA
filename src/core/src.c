#include "aurora/src.h"

const char *aurora_src_class_name(enum aurora_src_class src_class)
{
    switch (src_class) {
    case AURORA_SRC_INTERACTIVE_UI:
        return "INTERACTIVE_UI";
    case AURORA_SRC_AI_INFERENCE:
        return "AI_INFERENCE";
    case AURORA_SRC_REALTIME_AUDIO:
        return "REALTIME_AUDIO";
    case AURORA_SRC_BACKGROUND_SYNC:
        return "BACKGROUND_SYNC";
    case AURORA_SRC_LOW_POWER_TASK:
        return "LOW_POWER_TASK";
    case AURORA_SRC_SECURITY_CRITICAL:
        return "SECURITY_CRITICAL";
    case AURORA_SRC_BATCH_COMPUTE:
        return "BATCH_COMPUTE";
    case AURORA_SRC_CLASS_COUNT:
        break;
    }
    return "UNKNOWN_SRC";
}

const char *aurora_memory_role_name(enum aurora_memory_role role)
{
    switch (role) {
    case AURORA_MEM_HOT_INTERACTIVE:
        return "HOT_INTERACTIVE";
    case AURORA_MEM_MODEL_WEIGHTS_READONLY:
        return "MODEL_WEIGHTS_READONLY";
    case AURORA_MEM_STREAMING_BUFFER:
        return "STREAMING_BUFFER";
    case AURORA_MEM_SECRET_TRANSIENT:
        return "SECRET_TRANSIENT";
    case AURORA_MEM_RECOMPUTABLE_CACHE:
        return "RECOMPUTABLE_CACHE";
    case AURORA_MEM_LOW_POWER_COLD:
        return "LOW_POWER_COLD";
    }
    return "UNKNOWN_MEMORY_ROLE";
}

int aurora_src_class_base_boost(enum aurora_src_class src_class)
{
    switch (src_class) {
    case AURORA_SRC_REALTIME_AUDIO:
        return 50;
    case AURORA_SRC_INTERACTIVE_UI:
        return 40;
    case AURORA_SRC_AI_INFERENCE:
        return 20;
    case AURORA_SRC_SECURITY_CRITICAL:
        return 10;
    case AURORA_SRC_BATCH_COMPUTE:
        return 0;
    case AURORA_SRC_BACKGROUND_SYNC:
        return -10;
    case AURORA_SRC_LOW_POWER_TASK:
        return -20;
    case AURORA_SRC_CLASS_COUNT:
        break;
    }
    return 0;
}

bool aurora_src_contract_valid(const struct aurora_src_contract *contract)
{
    if (contract == NULL) {
        return false;
    }

    if (contract->latency_us_target > 10u * 1000u * 1000u) {
        return false;
    }

    if (contract->cpu_burst_us_max > 10u * 1000u * 1000u) {
        return false;
    }

    return true;
}

const char *aurora_status_name(enum aurora_status status)
{
    switch (status) {
    case AURORA_OK:
        return "OK";
    case AURORA_ERR_INVALID:
        return "INVALID";
    case AURORA_ERR_DENIED:
        return "DENIED";
    case AURORA_ERR_FULL:
        return "FULL";
    case AURORA_ERR_NOT_FOUND:
        return "NOT_FOUND";
    case AURORA_ERR_EXPIRED:
        return "EXPIRED";
    case AURORA_ERR_RANGE:
        return "RANGE";
    }
    return "UNKNOWN_STATUS";
}
