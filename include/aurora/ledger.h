#ifndef AURORA_LEDGER_H
#define AURORA_LEDGER_H

#include "aurora/types.h"

enum aurora_ledger_category {
    AURORA_LEDGER_PSL = 1,
    AURORA_LEDGER_ASO = 2,
    AURORA_LEDGER_SCHED = 3,
    AURORA_LEDGER_FALLBACK = 4,
    AURORA_LEDGER_SRC = 5,
    AURORA_LEDGER_MEMORY = 6
};

enum aurora_ledger_decision {
    AURORA_DECISION_ACCEPT = 1,
    AURORA_DECISION_REJECT = 2,
    AURORA_DECISION_CLAMP = 3,
    AURORA_DECISION_OBSERVE = 4,
    AURORA_DECISION_DISABLE = 5
};

struct aurora_ledger_record {
    uint64_t seq;
    aurora_time_t timestamp_ns;
    enum aurora_ledger_category category;
    enum aurora_ledger_decision decision;
    int pid;
    char reason[AURORA_MESSAGE_MAX];
};

struct aurora_policy_ledger {
    struct aurora_ledger_record records[AURORA_LEDGER_CAPACITY];
    size_t count;
    uint64_t next_seq;
    uint64_t dropped;
};

void aurora_ledger_init(struct aurora_policy_ledger *ledger);
void aurora_ledger_append(
    struct aurora_policy_ledger *ledger,
    aurora_time_t now_ns,
    enum aurora_ledger_category category,
    enum aurora_ledger_decision decision,
    int pid,
    const char *reason);
const char *aurora_ledger_category_name(enum aurora_ledger_category category);
const char *aurora_ledger_decision_name(enum aurora_ledger_decision decision);

#endif
