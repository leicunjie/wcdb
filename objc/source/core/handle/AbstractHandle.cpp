/*
 * Tencent is pleased to support the open source community by making
 * WCDB available.
 *
 * Copyright (C) 2017 THL A29 Limited, a Tencent company.
 * All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *       https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <WCDB/AbstractHandle.hpp>
#include <WCDB/Assertion.hpp>
#include <WCDB/Notifier.hpp>
#include <WCDB/SQLite.h>
#include <WCDB/String.hpp>

namespace WCDB {

#pragma mark - Initialize
AbstractHandle::AbstractHandle()
: m_handle(nullptr), m_notification(this), m_nestedLevel(0), m_codeToBeIgnored(SQLITE_OK)
{
}

AbstractHandle::~AbstractHandle()
{
    close();
}

void **AbstractHandle::getRawHandle()
{
    return &m_handle;
}

#pragma mark - Global
void AbstractHandle::enableMultithread()
{
    sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
}

void AbstractHandle::setMemoryMapSize(int64_t defaultSizeLimit, int64_t maximumAllowedSizeLimit)
{
    sqlite3_config(SQLITE_CONFIG_MMAP_SIZE, defaultSizeLimit, maximumAllowedSizeLimit);
}

void AbstractHandle::enableMemoryStatus(bool enable)
{
    sqlite3_config(SQLITE_CONFIG_MEMSTATUS, enable);
}

void AbstractHandle::setNotificationForLog(const Log &log)
{
    sqlite3_config(SQLITE_CONFIG_LOG, log, nullptr);
}

void AbstractHandle::setNotificationWhenVFSOpened(const VFSOpen &vfsOpen)
{
    sqlite3_vfs *vfs = sqlite3_vfs_find(nullptr);
    vfs->xSetSystemCall(vfs, "open", (void (*)(void)) vfsOpen);
}

#pragma mark - Path
void AbstractHandle::setPath(const String &path)
{
    WCTRemedialAssert(!isOpened(), "Path can't be changed after opened.", return;);
    m_path = path;
    m_error.infos.set("Path", path);
}

const String &AbstractHandle::getPath() const
{
    return m_path;
}

String AbstractHandle::getSHMSubfix()
{
    return "-shm";
}

String AbstractHandle::getWALSubfix()
{
    return "-wal";
}

String AbstractHandle::getJournalSubfix()
{
    return "-journal";
}

#pragma mark - Basic
bool AbstractHandle::open()
{
    if (isOpened()) {
        return true;
    }
    int rc = sqlite3_open(m_path.c_str(), (sqlite3 **) &m_handle);
    if (rc != SQLITE_OK) {
        return error(rc);
    }
    return true;
}

bool AbstractHandle::isOpened() const
{
    return m_handle != nullptr;
}

void AbstractHandle::close()
{
    if (m_handle) {
        finalizeStatements();
        WCTRemedialAssert(m_nestedLevel == 0 && !isInTransaction(),
                          "Unpaired transaction.",
                          m_nestedLevel = 0;
                          rollbackTransaction(););
        m_notification.purge();
        // disable checkpoint when closing. If ones need a checkpoint, they should do it manually.
        constexpr const char *name = "close";
        m_notification.setNotificationWhenWillCheckpoint(
        std::numeric_limits<int>::min(),
        name,
        [](const String &) -> bool { return false; },
        true);
        sqlite3_close_v2((sqlite3 *) m_handle);
        m_handle = nullptr;
        m_notification.purge();
    }
}

bool AbstractHandle::execute(const String &sql)
{
    WCTInnerAssert(isOpened());
    int rc = sqlite3_exec((sqlite3 *) m_handle, sql.c_str(), nullptr, nullptr, nullptr);
    if (rc == SQLITE_OK) {
        return true;
    }
    return error(rc, sql);
}

int AbstractHandle::getExtendedErrorCode()
{
    WCTInnerAssert(isOpened());
    return sqlite3_extended_errcode((sqlite3 *) m_handle);
}

long long AbstractHandle::getLastInsertedRowID()
{
    WCTInnerAssert(isOpened());
    return sqlite3_last_insert_rowid((sqlite3 *) m_handle);
}

int AbstractHandle::getResultCode()
{
    WCTInnerAssert(isOpened());
    return sqlite3_errcode((sqlite3 *) m_handle);
}

const char *AbstractHandle::getErrorMessage()
{
    WCTInnerAssert(isOpened());
    return sqlite3_errmsg((sqlite3 *) m_handle);
}

int AbstractHandle::getChanges()
{
    WCTInnerAssert(isOpened());
    return sqlite3_changes((sqlite3 *) m_handle);
}

bool AbstractHandle::isReadonly()
{
    WCTInnerAssert(isOpened());
    return sqlite3_db_readonly((sqlite3 *) m_handle, NULL) == 1;
}

bool AbstractHandle::isInTransaction()
{
    WCTInnerAssert(isOpened());
    return sqlite3_get_autocommit((sqlite3 *) m_handle) == 0;
}

void AbstractHandle::interrupt()
{
    WCTInnerAssert(isOpened());
    sqlite3_interrupt((sqlite3 *) m_handle);
}

#pragma mark - Statement
HandleStatement *AbstractHandle::getStatement()
{
    m_handleStatements.push_back(HandleStatement(this));
    return &m_handleStatements.back();
}

void AbstractHandle::returnStatement(HandleStatement *handleStatement)
{
    if (handleStatement) {
        for (auto iter = m_handleStatements.begin(); iter != m_handleStatements.end(); ++iter) {
            if (&(*iter) == handleStatement) {
                m_handleStatements.erase(iter);
                return;
            }
        }
    }
}

void AbstractHandle::finalizeStatements()
{
    for (auto &handleStatement : m_handleStatements) {
        WCTRemedialAssert(!handleStatement.isPrepared(),
                          "Statement is not finalized.",
                          handleStatement.finalize(););
    }
}

#pragma mark - Meta
std::pair<bool, bool> AbstractHandle::tableExists(const String &table)
{
    HandleStatement *handleStatement = getStatement();
    bool result = false;
    do {
        StatementSelect statementSelect
        = StatementSelect().select(1).from(table).limit(0);
        markErrorAsIgnorable(SQLITE_ERROR);
        if (handleStatement->prepare(statementSelect)) {
            result = handleStatement->step();
            handleStatement->finalize();
        }
        markErrorAsUnignorable();
    } while (false);
    returnStatement(handleStatement);
    return { result || getResultCode() == SQLITE_ERROR, result };
}

std::pair<bool, std::set<String>>
AbstractHandle::getColumns(const Schema &schema, const String &table)
{
    WCDB::StatementPragma statement
    = StatementPragma().pragma(Pragma::tableInfo()).schema(schema).with(table);
    return getValues(statement, 1);
}

std::pair<bool, std::set<String>>
AbstractHandle::getValues(const Statement &statement, int index)
{
    HandleStatement *handleStatement = getStatement();
    bool done = false;
    std::set<String> values;
    do {
        if (handleStatement->prepare(statement)) {
            while (handleStatement->step(done) && !done) {
                values.emplace(handleStatement->getText(index));
            }
            handleStatement->finalize();
        }
    } while (false);
    returnStatement(handleStatement);
    if (!done) {
        values.clear();
    }
    return { done, std::move(values) };
}

#pragma mark - Transaction
const String &AbstractHandle::savepointPrefix()
{
    static const String s_savepointPrefix("WCDBSavepoint_");
    return s_savepointPrefix;
}

bool AbstractHandle::beginNestedTransaction()
{
    if (!isInTransaction()) {
        return beginTransaction();
    }
    String savepointName = savepointPrefix() + std::to_string(++m_nestedLevel);
    return execute(StatementSavepoint().savepoint(savepointName).getDescription());
}

bool AbstractHandle::commitOrRollbackNestedTransaction()
{
    if (m_nestedLevel == 0) {
        return commitOrRollbackTransaction();
    }
    String savepointName = savepointPrefix() + std::to_string(m_nestedLevel--);
    if (!execute(StatementRelease().release(savepointName).getDescription())) {
        markErrorAsIgnorable(-1);
        execute(StatementRollback().rollbackToSavepoint(savepointName).getDescription());
        markErrorAsUnignorable();
        return false;
    }
    return true;
}

void AbstractHandle::rollbackNestedTransaction()
{
    if (m_nestedLevel == 0) {
        return rollbackTransaction();
    }
    String savepointName = savepointPrefix() + std::to_string(m_nestedLevel--);
    markErrorAsIgnorable(-1);
    execute(StatementRollback().rollbackToSavepoint(savepointName).getDescription());
    markErrorAsUnignorable();
}

bool AbstractHandle::beginTransaction()
{
    return execute(StatementBegin().beginImmediate().getDescription());
}

bool AbstractHandle::commitOrRollbackTransaction()
{
    m_nestedLevel = 0;
    if (!execute(StatementCommit().commit().getDescription())) {
        markErrorAsIgnorable(-1);
        execute(StatementRollback().rollback().getDescription());
        markErrorAsUnignorable();
        return false;
    }
    return true;
}

void AbstractHandle::rollbackTransaction()
{
    m_nestedLevel = 0;
    markErrorAsIgnorable(-1);
    execute(StatementRollback().rollback().getDescription());
    markErrorAsUnignorable();
}

#pragma mark - Cipher
bool AbstractHandle::setCipherKey(const UnsafeData &data)
{
    WCTInnerAssert(isOpened());
    int rc = sqlite3_key((sqlite3 *) m_handle, data.buffer(), (int) data.size());
    if (rc == SQLITE_OK) {
        return true;
    }
    return error(rc);
}

#pragma mark - Notification
void AbstractHandle::setNotificationWhenSQLTraced(const String &name,
                                                  const SQLNotification &onTraced)
{
    WCTInnerAssert(isOpened());
    m_notification.setNotificationWhenSQLTraced(name, onTraced);
}

void AbstractHandle::setNotificationWhenPerformanceTraced(const String &name,
                                                          const PerformanceNotification &onTraced)
{
    WCTInnerAssert(isOpened());
    m_notification.setNotificationWhenPerformanceTraced(name, onTraced);
}

void AbstractHandle::setNotificationWhenCommitted(int order,
                                                  const String &name,
                                                  const CommittedNotification &onCommitted)
{
    WCTInnerAssert(isOpened());
    m_notification.setNotificationWhenCommitted(order, name, onCommitted);
}

void AbstractHandle::unsetNotificationWhenCommitted(const String &name)
{
    WCTInnerAssert(isOpened());
    m_notification.unsetNotificationWhenCommitted(name);
}

bool AbstractHandle::setNotificationWhenWillCheckpoint(
int order, const String &name, const WCDB::AbstractHandle::WillCheckpointNotification &willCheckpoint)
{
    WCTInnerAssert(isOpened());
    return m_notification.setNotificationWhenWillCheckpoint(order, name, willCheckpoint);
}

bool AbstractHandle::unsetNotificationWhenWillCheckpoint(const String &name)
{
    WCTInnerAssert(isOpened());
    return m_notification.unsetNotificationWhenWillCheckpoint(name);
}

bool AbstractHandle::setNotificationWhenCheckpointed(const String &name,
                                                     const CheckpointedNotification &checkpointed)
{
    WCTInnerAssert(isOpened());
    return m_notification.setNotificationWhenCheckpointed(name, checkpointed);
}

#pragma mark - Error
bool AbstractHandle::error(int rc, const String &sql)
{
    WCTInnerAssert(rc != SQLITE_OK);
    if (m_codeToBeIgnored >= 0 && rc != m_codeToBeIgnored) {
        // non-ignorable
        setupAndNotifyError(m_error, rc, sql);
        return false;
    } else {
        // ignorable
        Error error = m_error;
        setupAndNotifyError(error, rc, sql);
        return true;
    }
}

void AbstractHandle::setupAndNotifyError(Error &error, int rc, const String &sql)
{
    if (rc != SQLITE_MISUSE) {
        // extended error code will not be set in some case for misuse error
        error.setSQLiteCode(rc, getExtendedErrorCode());
    } else {
        error.setSQLiteCode(rc);
    }
    if (m_codeToBeIgnored >= 0 && rc != m_codeToBeIgnored) {
        error.level = Error::Level::Error;
    } else {
        error.level = Error::Level::Ignore;
    }
    const char *message = getErrorMessage();
    if (message) {
        error.message = message;
    } else {
        error.message = String::null();
    }
    error.infos.set("SQL", sql);
    Notifier::shared()->notify(error);
}

void AbstractHandle::markErrorAsIgnorable(int codeToBeIgnored)
{
    m_codeToBeIgnored = codeToBeIgnored;
}

void AbstractHandle::markErrorAsUnignorable()
{
    m_codeToBeIgnored = SQLITE_OK;
}

} //namespace WCDB
