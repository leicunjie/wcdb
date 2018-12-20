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

#ifndef _WCDB_HANDLESTATEMENT_HPP
#define _WCDB_HANDLESTATEMENT_HPP

#include <WCDB/HandleRelated.hpp>
#include <WCDB/WINQ.h>

namespace WCDB {

class HandleStatement final : public HandleRelated {
public:
    ~HandleStatement();

    bool prepare(const Statement &statement);
    bool isPrepared() const;

    bool step(bool &done);
    bool step();
    void finalize();
    void reset();

    using Integer32 = ColumnTypeInfo<ColumnType::Integer32>::UnderlyingType;
    using Integer64 = ColumnTypeInfo<ColumnType::Integer64>::UnderlyingType;
    using Text = ColumnTypeInfo<ColumnType::Text>::UnderlyingType;
    using Float = ColumnTypeInfo<ColumnType::Float>::UnderlyingType;
    using BLOB = ColumnTypeInfo<ColumnType::BLOB>::UnderlyingType;

    void bindInteger32(const Integer32 &value, int index);
    void bindInteger64(const Integer64 &value, int index);
    void bindDouble(const Float &value, int index);
    void bindText(const Text &value, int index);
    void bindBLOB(const BLOB &value, int index);
    void bindNull(int index);

    Integer32 getInteger32(int index);
    Integer64 getInteger64(int index);
    Float getDouble(int index);
    Text getText(int index);
    const BLOB getBLOB(int index);

    ColumnType getType(int index);

    int getColumnCount();
    const UnsafeString getOriginColumnName(int index);
    const UnsafeString getColumnName(int index);
    const UnsafeString getColumnTableName(int index);

    bool isReadonly();
    bool isPrepared();

protected:
    HandleStatement(AbstractHandle *handle);

    bool prepare(const String &sql);
    friend class AbstractHandle;
    void *m_stmt;
};

} //namespace WCDB

#endif /* _WCDB_HANDLESTATEMENT_HPP */
