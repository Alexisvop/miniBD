#include "status.h"

const char *db_status_message(DbStatus status)
{
    switch (status) {
    case DB_OK: return "success";
    case DB_IO_ERROR: return "I/O error (open, read, write, sync, or close failed)";
    case DB_PERMISSION_DENIED: return "permission denied or read-only filesystem";
    case DB_NO_MEMORY: return "memory allocation failed";
    case DB_LIMIT_REACHED: return "page or record limit reached";
    case DB_INVALID_FORMAT: return "invalid or truncated database format";
    case DB_UNSUPPORTED_VERSION: return "unsupported database format version";
    case DB_INVALID_RECORD: return "invalid record";
    case DB_DUPLICATE_ID: return "duplicate ID";
    case DB_NOT_FOUND: return "record not found";
    }
    return "unknown database error";
}
