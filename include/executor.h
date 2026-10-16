#ifndef MINIDB_EXECUTOR_H
#define MINIDB_EXECUTOR_H

#include "parser.h"
#include "storage.h"

typedef struct {
    DbStatus status;
    uint32_t rows; /* Affected rows, or rows successfully delivered before status. */
    StorageStats stats; /* Populated on successful COMMAND_STATS only. */
} ExecutionResult;

typedef struct {
    RecordVisitor record;
    BTreeNodeVisitor node;
    void *context;
} ExecutionSink;

/* No terminal I/O. NULL sink/callback discards values but still counts query rows.
 * Callbacks borrow rows/nodes, must not mutate storage, and may return an error.
 * Successful mutation is not undone if later presentation fails. */
ExecutionResult executor_execute(Storage *storage, const Command *command, const ExecutionSink *sink);

#endif
