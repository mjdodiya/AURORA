#include "aurora/ledger.h"

#include <stdio.h>
#include <string.h>

void aurora_ledger_init(struct aurora_policy_ledger *ledger)
{
    if (ledger == NULL) {
        return;
    }
    memset(ledger, 0, sizeof(*ledger));
    ledger->next_seq = 1u;
}

void aurora_ledger_append(
    struct aurora_policy_ledger *ledger,
    aurora_time_t now_ns,
    enum aurora_ledger_category category,
    enum aurora_ledger_decision decision,
    int pid,
    const char *reason)
{
    if (ledger == NULL) {
        return;
    }

    if (ledger->count >= AURORA_LEDGER_CAPACITY) {
        memmove(
            &ledger->records[0],
            &ledger->records[1],
            sizeof(ledger->records[0]) * (AURORA_LEDGER_CAPACITY - 1u));
        ledger->count = AURORA_LEDGER_CAPACITY - 1u;
        ledger->dropped++;
    }

    struct aurora_ledger_record *record = &ledger->records[ledger->count++];
    record->seq = ledger->next_seq++;
    record->timestamp_ns = now_ns;
    record->category = category;
    record->decision = decision;
    record->pid = pid;

    if (reason != NULL) {
        (void)snprintf(record->reason, sizeof(record->reason), "%s", reason);
    } else {
        record->reason[0] = '\0';
    }
}

const char *aurora_ledger_category_name(enum aurora_ledger_category category)
{
    switch (category) {
    case AURORA_LEDGER_PSL:
        return "PSL";
    case AURORA_LEDGER_ASO:
        return "ASO";
    case AURORA_LEDGER_SCHED:
        return "SCHED";
    case AURORA_LEDGER_FALLBACK:
        return "FALLBACK";
    case AURORA_LEDGER_SRC:
        return "SRC";
    case AURORA_LEDGER_MEMORY:
        return "MEMORY";
    }
    return "UNKNOWN_CATEGORY";
}

const char *aurora_ledger_decision_name(enum aurora_ledger_decision decision)
{
    switch (decision) {
    case AURORA_DECISION_ACCEPT:
        return "ACCEPT";
    case AURORA_DECISION_REJECT:
        return "REJECT";
    case AURORA_DECISION_CLAMP:
        return "CLAMP";
    case AURORA_DECISION_OBSERVE:
        return "OBSERVE";
    case AURORA_DECISION_DISABLE:
        return "DISABLE";
    }
    return "UNKNOWN_DECISION";
}
