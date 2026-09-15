#include <stdio.h>

#ifdef BEST_RECORD_RENAMED
#define BEST_RECORD_FUNCTION best_record_fixture_renamed
#else
#define BEST_RECORD_FUNCTION best_record_fixture
#endif

#ifdef BEST_RECORD_UNSIGNED_PRIORITY
typedef unsigned int priority_type;
#else
typedef int priority_type;
#endif

struct BestRecordTask {
    const char *name;
    priority_type priority;
    _Bool done;
};

#ifdef BEST_RECORD_VOLATILE_TASKS
#define TASK_QUALIFIER volatile
#else
#define TASK_QUALIFIER const
#endif

static const struct BestRecordTask *BEST_RECORD_FUNCTION(
    TASK_QUALIFIER struct BestRecordTask *tasks, int count)
{
    int i;
    const struct BestRecordTask *best;

    best = 0;
#ifdef BEST_RECORD_EXTRA_CFG
    if (count < 0)
        return 0;
#endif
    for (i = 0; i < count; ++i) {
#ifdef BEST_RECORD_ALTERNATE_PREDICATE
        if (tasks[i].done == 0 &&
#else
        if (!tasks[i].done &&
#endif
#ifdef BEST_RECORD_LOWEST_PRIORITY
            (best == 0 || tasks[i].priority < best->priority))
#else
            (best == 0 || tasks[i].priority > best->priority))
#endif
#ifdef BEST_RECORD_VOLATILE_TASKS
            best = (const struct BestRecordTask *)&tasks[i];
#else
            best = &tasks[i];
#endif
    }
    return best;
}

static int record_code(const struct BestRecordTask *task)
{
    if (task == 0)
        return 0;
    return task->name[0] * 31 + task->priority;
}

int main(void)
{
    struct BestRecordTask tasks[] = {
        { "parse", 2, 1 },
        { "build", 3, 1 },
        { "run", 5, 0 },
        { "log", 1, 0 }
    };
    const struct BestRecordTask *first;
    const struct BestRecordTask *second;
    const struct BestRecordTask *none;
    int checksum;

    first = BEST_RECORD_FUNCTION(tasks, 4);
    tasks[2].done = 1;
    second = BEST_RECORD_FUNCTION(tasks, 4);
    tasks[3].done = 1;
    none = BEST_RECORD_FUNCTION(tasks, 4);
    checksum = record_code(first) * 7 + record_code(second) * 3;
    printf(
        "best record first=%s second=%s none=%d checksum=%d\n",
        first != 0 ? first->name : "none",
        second != 0 ? second->name : "none",
        none == 0, checksum);
#ifdef BEST_RECORD_LOWEST_PRIORITY
    return checksum != -32046 || none != 0;
#else
    return checksum != -30716 || none != 0;
#endif
}
