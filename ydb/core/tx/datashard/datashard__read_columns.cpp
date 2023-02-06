#include "datashard_impl.h"
#include <ydb/core/formats/factory.h>
#include <util/string/vector.h>

namespace NKikimr {
namespace NDataShard {

using namespace NTabletFlatExecutor;

class TTxReleaseSnaphotReference : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
    TSnapshotKey SnapshotKey;

public:
    TTxReleaseSnaphotReference(TDataShard* self, const TSnapshotKey& snapshotKey)
        : TTransactionBase<TDataShard>(self)
        , SnapshotKey(snapshotKey)
    {}

    TTxType GetTxType() const override { return TXTYPE_READ_COLUMNS; }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        Self->GetSnapshotManager().ReleaseReference(SnapshotKey, txc.DB, ctx.Now());
        return true;
    }

    void Complete(const TActorContext &ctx) override {
        Y_UNUSED(ctx);
    }
};


struct TKeyBoundary {
    TSerializedCellVec Key;
    bool Inclusive;
};

class TReadColumnsScan : public INoTxScan {
    const TActorId ReplyTo;
    const TActorId DatashardActorId;
    const TString TableName;
    const ui64 TabletId;
    const TKeyBoundary From;
    const TKeyBoundary To;
    const TVector<NTable::TTag> ValueColumns;
    const TVector<NScheme::TTypeInfo> ValueColumnTypes;
    const ui64 RowsLimit = 100000;
    const ui64 BytesLimit = 1024*1024;
    const TKeyBoundary ShardEnd;
    TMaybe<TSnapshotKey> SnapshotKey;

    std::unique_ptr<IBlockBuilder> BlockBuilder;
    ui64 Rows = 0;
    ui64 Bytes = 0;
    bool ShardFinished = true;
    TString LastKeySerialized;
    TAutoPtr<TEvDataShard::TEvReadColumnsResponse> Result;

    IDriver *Driver = nullptr;
    TIntrusiveConstPtr<TScheme> Scheme;

public:
    TReadColumnsScan(const TKeyBoundary& keyFrom,
                     const TKeyBoundary& keyTo,
                     const TVector<NTable::TTag>& valueColumns,
                     const TVector<NScheme::TTypeInfo> valueColumnTypes,
                     std::unique_ptr<IBlockBuilder>&& blockBuilder,
                     ui64 rowsLimit, ui64 bytesLimit,
                     const TKeyBoundary& shardEnd,
                     const TActorId& replyTo,
                     const TActorId& datashardActorId,
                     TMaybe<TSnapshotKey> snapshotKey,
                     const TString& tableName,
                     ui64 tabletId)
        : ReplyTo(replyTo)
        , DatashardActorId(datashardActorId)
        , TableName(tableName)
        , TabletId(tabletId)
        , From(keyFrom)
        , To(keyTo)
        , ValueColumns(valueColumns)
        , ValueColumnTypes(valueColumnTypes)
        , RowsLimit(rowsLimit)
        , BytesLimit(bytesLimit)
        , ShardEnd(shardEnd)
        , SnapshotKey(snapshotKey)
        , BlockBuilder(std::move(blockBuilder))
    {}

    TInitialState Prepare(IDriver* driver, TIntrusiveConstPtr<TScheme> scheme) noexcept override {
        Driver = driver;
        Scheme = std::move(scheme);

        TInitialState hello;
        hello.Scan = EScan::Reset;
        return hello;
    }

    EScan Seek(TLead& lead, ui64 seq) noexcept override {
        Y_VERIFY(seq == 0, "Unexpected repeated Seek");

        lead.To(ValueColumns, From.Key.GetCells(), From.Inclusive ? NTable::ESeek::Lower : NTable::ESeek::Upper);
        lead.Until(To.Key.GetCells(), To.Inclusive);

        return EScan::Feed;
    }

    EScan Feed(TArrayRef<const TCell> key, const TRow& row) noexcept override {
        const auto& keyTypes = Scheme->Keys->BasicTypes();

        Y_VERIFY(key.size() == keyTypes.size());
        Y_VERIFY((*row).size() == ValueColumnTypes.size());

        TDbTupleRef rowKey(keyTypes.data(), key.data(), keyTypes.size());
        TDbTupleRef rowValues(ValueColumnTypes.data(), (*row).data(), ValueColumnTypes.size());

        BlockBuilder->AddRow(rowKey, rowValues);

        Rows++;
        Bytes = BlockBuilder->Bytes();

        if (Rows >= RowsLimit || Bytes >= BytesLimit) {
            ShardFinished = false;
            LastKeySerialized = TSerializedCellVec::Serialize(key);
            return EScan::Final;
        }

        return EScan::Feed;
    }

    TAutoPtr<IDestructable> Finish(EAbort reason) noexcept override {
        Result = new TEvDataShard::TEvReadColumnsResponse(TabletId);

        if (reason == EAbort::None) {
            TString buffer = BlockBuilder->Finish();
            buffer.resize(BlockBuilder->Bytes());
            BlockBuilder.reset();

            Result->Record.SetBlocks(buffer);
            Result->Record.SetLastKey(ShardFinished ? ShardEnd.Key.GetBuffer() : LastKeySerialized);
            Result->Record.SetLastKeyInclusive(ShardFinished ? ShardEnd.Inclusive : true);
            Result->Record.SetEndOfShard(ShardFinished);

            LOG_DEBUG_S(*TlsActivationContext, NKikimrServices::TX_DATASHARD, TabletId
                        << " Read columns scan result for table [" << TableName << "]: "
                        << Rows << " rows, " << Bytes << " bytes (event size "
                        << Result->Record.GetBlocks().size() << ") shardFinished: " << ShardFinished);
        } else {
            LOG_NOTICE_S(*TlsActivationContext, NKikimrServices::TX_DATASHARD, TabletId
                        << " Read columns scan failed for table [" << TableName << "]");

            Result->Record.SetStatus(NKikimrTxDataShard::TError::WRONG_SHARD_STATE);
            Result->Record.SetErrorDescription("Scan aborted");
        }

        TlsActivationContext->Send(new IEventHandle(ReplyTo, TActorId(), Result.Release()));
        TlsActivationContext->Send(new IEventHandle(DatashardActorId, TActorId(), new TDataShard::TEvPrivate::TEvScanStats(Rows, Bytes)));

        return this;
    }

    EScan Exhausted() noexcept override {
        return EScan::Final;
    }

    void Describe(IOutputStream& str) const noexcept override {
        str << "ReadColumnsScan table: ["<< TableName << "]shard: " << TabletId;
    }

    void OnFinished(TDataShard* self) override {
        if (SnapshotKey) {
            self->Execute(new TTxReleaseSnaphotReference(self, *SnapshotKey));
        }
    }
};


class TDataShard::TTxReadColumns : public NTabletFlatExecutor::TTransactionBase<TDataShard> {
private:
    TEvDataShard::TEvReadColumnsRequest::TPtr Ev;
    TAutoPtr<TEvDataShard::TEvReadColumnsResponse> Result;
    TSmallVec<TRawTypeValue> KeyFrom;
    TSmallVec<TRawTypeValue> KeyTo;
    bool InclusiveFrom;
    bool InclusiveTo;
    ui64 RowsLimit = 100000;
    ui64 BytesLimit = 1024*1024;
    ui64 Restarts = 0;
    TRowVersion ReadVersion = TRowVersion::Max();

public:
    TTxReadColumns(TDataShard* ds, TEvDataShard::TEvReadColumnsRequest::TPtr ev)
        : TBase(ds)
        , Ev(ev)
    {
        if (Ev->Get()->Record.HasSnapshotStep() && Ev->Get()->Record.HasSnapshotTxId()) {
            ReadVersion.Step = Ev->Get()->Record.GetSnapshotStep();
            ReadVersion.TxId = Ev->Get()->Record.GetSnapshotTxId();
        }
    }

    TTxType GetTxType() const override { return TXTYPE_READ_COLUMNS; }

    bool Precharge(NTable::TDatabase& db, ui32 localTid, const TVector<NTable::TTag>& valueColumns) {
        bool ready =  db.Precharge(localTid,
                                   KeyFrom,
                                   KeyTo,
                                   valueColumns,
                                   0,
                                   RowsLimit, BytesLimit,
                                   NTable::EDirection::Forward, ReadVersion);
        return ready;
    }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        // FIXME: we need to transform HEAD into some non-repeatable snapshot here
        if (!ReadVersion.IsMax() && Self->GetVolatileTxManager().HasVolatileTxsAtSnapshot(ReadVersion)) {
            Self->GetVolatileTxManager().AttachWaitingSnapshotEvent(
                ReadVersion,
                std::unique_ptr<IEventHandle>(Ev.Release()));
            Result.Destroy();
            return true;
        }

        Result = new TEvDataShard::TEvReadColumnsResponse(Self->TabletID());

        bool useScan = Self->ReadColumnsScanEnabled;

        if (Self->IsFollower()) {
            NKikimrTxDataShard::TError::EKind status = NKikimrTxDataShard::TError::OK;
            TString errMessage;

            if (!Self->SyncSchemeOnFollower(txc, ctx, status, errMessage))
                return false;

            if (status != NKikimrTxDataShard::TError::OK) {
                SetError(status, errMessage);
                return true;
            }

            if (!ReadVersion.IsMax()) {
                NIceDb::TNiceDb db(txc.DB);
                TRowVersion lastCompleteTx;
                if (!TDataShard::SysGetUi64(db, Schema::Sys_LastCompleteStep, lastCompleteTx.Step))
                    return false;
                if (!TDataShard::SysGetUi64(db, Schema::Sys_LastCompleteTx, lastCompleteTx.TxId))
                    return false;

                if (ReadVersion > lastCompleteTx) {
                    SetError(NKikimrTxDataShard::TError::WRONG_SHARD_STATE,
                             TStringBuilder() << "RO replica last version " << lastCompleteTx
                             << " lags behind the requested snapshot " << ReadVersion
                             << " shard " << Self->TabletID());
                    return true;
                }
            }

            useScan = false;
        }

        LOG_DEBUG_S(ctx, NKikimrServices::TX_DATASHARD, Self->TabletID() << " Read columns: " << Ev->Get()->Record);

        if (Self->State != TShardState::Ready &&
            Self->State != TShardState::Readonly)
        {
            SetError(NKikimrTxDataShard::TError::WRONG_SHARD_STATE,
                        Sprintf("Wrong shard state: %s tablet id: %" PRIu64,
                                DatashardStateName(Self->State).c_str(), Self->TabletID()));
            return true;
        }

        const ui64 tableId = Ev->Get()->Record.GetTableId();
        if (Ev->Get()->Record.GetMaxBytes()) {
            BytesLimit = Ev->Get()->Record.GetMaxBytes();
        }

        if (!Self->TableInfos.contains(tableId)) {
            SetError(NKikimrTxDataShard::TError::SCHEME_ERROR, Sprintf("Unknown table id %" PRIu64, tableId));
            return true;
        }

        TMaybe<TSnapshotKey> snapshotKey;
        if (!ReadVersion.IsMax()) {
            // FIXME: protocol needs a full table id (both owner id and path id)
            ui64 ownerId = Self->GetPathOwnerId();
            snapshotKey = TSnapshotKey(ownerId, tableId, ReadVersion.Step, ReadVersion.TxId);

            // Check if readVersion is a valid snapshot
            if (!Self->GetSnapshotManager().FindAvailable(*snapshotKey)) {
                SetError(NKikimrTxDataShard::TError::SNAPSHOT_NOT_EXIST,
                    TStringBuilder() << "Table id " << tableId << " has no snapshot at " << ReadVersion
                         << " shard " << Self->TabletID() << (Self->IsFollower() ? " RO replica" : ""));
                return true;
            }
        }

        const TUserTable& tableInfo = *Self->TableInfos[tableId];
        if (tableInfo.IsBackup) {
            SetError(NKikimrTxDataShard::TError::SCHEME_ERROR, "Cannot read from a backup table");
            return true;
        }

        const ui32 localTableId = tableInfo.LocalTid;
        THashMap<TString, ui32> columnsByName;
        for (const auto& col : tableInfo.Columns) {
            columnsByName[col.second.Name] = col.first;
        }

        TString format = "clickhouse_native";
        if (Ev->Get()->Record.HasFormat()) {
            format = Ev->Get()->Record.GetFormat();
        }
        std::unique_ptr<IBlockBuilder> blockBuilder = AppData()->FormatFactory->CreateBlockBuilder(format);
        if (!blockBuilder) {
            SetError(NKikimrTxDataShard::TError::BAD_ARGUMENT,
                     Sprintf("Unsupported block format \"%s\"", format.data()));
            return true;
        }

        // TODO: check schemas

        TSerializedCellVec fromKeyCells(Ev->Get()->Record.GetFromKey());
        KeyFrom.clear();
        for (ui32 i = 0; i < fromKeyCells.GetCells().size(); ++i) {
            KeyFrom.push_back(TRawTypeValue(fromKeyCells.GetCells()[i].AsRef(), tableInfo.KeyColumnTypes[i]));
        }
        KeyFrom.resize(tableInfo.KeyColumnTypes.size());
        InclusiveFrom = Ev->Get()->Record.GetFromKeyInclusive();
        KeyTo.clear();
        InclusiveTo = true;

        TSerializedCellVec toKeyCells;

        if (!useScan) {
            // Use histogram to limit the range for single request
            const auto& sizeHistogram = tableInfo.Stats.DataStats.DataSizeHistogram;
            auto histIt = LowerBound(sizeHistogram.begin(), sizeHistogram.end(), fromKeyCells,
                       [&tableInfo] (const NTable::TBucket& bucket, const TSerializedCellVec& key) {
                TSerializedCellVec bk(bucket.EndKey);
                return CompareTypedCellVectors(
                            bk.GetCells().data(), key.GetCells().data(),
                            tableInfo.KeyColumnTypes.data(),
                            bk.GetCells().size(), key.GetCells().size()) < 0;
            });

            if (histIt != sizeHistogram.end() && ++histIt != sizeHistogram.end()) {
                toKeyCells.Parse(histIt->EndKey);
                for (ui32 i = 0; i < toKeyCells.GetCells().size(); ++i) {
                    KeyTo.push_back(TRawTypeValue(toKeyCells.GetCells()[i].AsRef(), tableInfo.KeyColumnTypes[i]));
                }
            }
        }

        TVector<NTable::TTag> valueColumns;
        TVector<NScheme::TTypeInfo> valueColumnTypes;
        TVector<std::pair<TString, NScheme::TTypeInfo>> columns;

        if (Ev->Get()->Record.GetColumns().empty()) {
            SetError(NKikimrTxDataShard::TError::BAD_ARGUMENT, "Empty column list");
            return true;
        }

        for (const auto& col : Ev->Get()->Record.GetColumns()) {
            if (!columnsByName.contains(col)) {
                SetError(NKikimrTxDataShard::TError::SCHEME_ERROR,
                         Sprintf("Unknown column: %s", col.data()));
                return true;
            }

            NTable::TTag colId = columnsByName[col];
            valueColumns.push_back(colId);
            valueColumnTypes.push_back(tableInfo.Columns.at(colId).Type);
            columns.push_back({col, tableInfo.Columns.at(colId).Type});
        }

        ui64 rowsPerBlock = Ev->Get()->Record.GetMaxRows() ? Ev->Get()->Record.GetMaxRows() : 64000;
        ui64 bytesPerBlock = 64000;

        TString err;
        if (!blockBuilder->Start(columns, rowsPerBlock, bytesPerBlock, err)) {
            SetError(NKikimrTxDataShard::TError::BAD_ARGUMENT,
                     Sprintf("Failed to create block builder \"%s\"", err.data()));
            return true;
        }

        tableInfo.Stats.AccessTime = TAppData::TimeProvider->Now();

        if (useScan) {
            if (snapshotKey) {
                if (!Self->GetSnapshotManager().AcquireReference(*snapshotKey)) {
                    SetError(NKikimrTxDataShard::TError::SNAPSHOT_NOT_EXIST,
                        TStringBuilder() << "Table id " << tableId << " has no snapshot at " << ReadVersion
                             << " shard " << Self->TabletID() << (Self->IsFollower() ? " RO replica" : ""));
                    return true;
                }
            }

            auto* scan = new TReadColumnsScan(TKeyBoundary{fromKeyCells, InclusiveFrom},
                                              TKeyBoundary{toKeyCells, InclusiveTo},
                                              valueColumns, valueColumnTypes,
                                              std::move(blockBuilder), RowsLimit, BytesLimit,
                                              TKeyBoundary{tableInfo.Range.To, tableInfo.Range.ToInclusive},
                                              Ev->Sender, ctx.SelfID,
                                              snapshotKey,
                                              tableInfo.Path,
                                              Self->TabletID());
            auto opts = TScanOptions()
                    .SetResourceBroker("scan", 10)
                    .SetSnapshotRowVersion(ReadVersion)
                    .SetActorPoolId(Self->ReadColumnsScanInUserPool ? AppData(ctx)->UserPoolId : AppData(ctx)->BatchPoolId)
                    .SetReadAhead(512*1024, 1024*1024)
                    .SetReadPrio(TScanOptions::EReadPrio::Low);

            ui64 cookie = -1; // Should be ignored
            Self->QueueScan(localTableId, scan, cookie, opts);

            Result.Destroy(); // Scan is now responsible for sending the result

            return true;
        }

        // TODO: make sure KeyFrom and KeyTo properly reference non-inline cells data

        if (!Precharge(txc.DB, localTableId, valueColumns))
            return false;

        size_t rows = 0;
        size_t bytes = 0;
        bool shardFinished = false;

        {
            NTable::TKeyRange iterRange;
            iterRange.MinKey = KeyFrom;
            iterRange.MinInclusive = InclusiveFrom;

            auto iter = txc.DB.IterateRange(localTableId, iterRange, valueColumns, ReadVersion);

            TString lastKeySerialized;
            bool lastKeyInclusive = true;
            while (iter->Next(NTable::ENext::All) == NTable::EReady::Data) {
                TDbTupleRef rowKey = iter->GetKey();
                lastKeySerialized = TSerializedCellVec::Serialize(rowKey.Cells());

                // Compare current row with right boundary
                int cmp = -1;// CompareTypedCellVectors(tuple.Columns, KeyTo.data(), tuple.Types, KeyTo.size());

                if (cmp == 0 && KeyTo.size() < rowKey.ColumnCount) {
                    cmp = -1;
                }
                if (InclusiveTo) {
                    if (cmp > 0)
                        break; // Stop iff greater(cmp > 0)
                } else {
                    if (cmp >= 0)
                        break; // Stop iff equal(cmp == 0) or greater(cmp > 0)
                }

                // Skip erased row
                if (iter->Row().GetRowState() == NTable::ERowOp::Erase) {
                    continue;
                }

                TDbTupleRef rowValues = iter->GetValues();

                blockBuilder->AddRow(rowKey, rowValues);

                rows++;
                bytes = blockBuilder->Bytes();

                if (rows >= RowsLimit || bytes >= BytesLimit)
                    break;
            }

            // We don't want to do many restarts if pages weren't precharged
            // So we just return whatever we read so far and the client can request more rows
            if (iter->Last() == NTable::EReady::Page && rows < 1000 && bytes < 100000 && Restarts < 1) {
                ++Restarts;
                return false;
            }

            if (iter->Last() == NTable::EReady::Gone) {
                shardFinished = true;
                lastKeySerialized = tableInfo.Range.To.GetBuffer();
                lastKeyInclusive = tableInfo.Range.ToInclusive;
            }

            TString buffer = blockBuilder->Finish();
            buffer.resize(blockBuilder->Bytes());

            Result->Record.SetBlocks(buffer);
            Result->Record.SetLastKey(lastKeySerialized);
            Result->Record.SetLastKeyInclusive(lastKeyInclusive);
            Result->Record.SetEndOfShard(shardFinished);
        }

        Self->IncCounter(COUNTER_READ_COLUMNS_ROWS, rows);
        Self->IncCounter(COUNTER_READ_COLUMNS_BYTES, bytes);

        LOG_DEBUG_S(ctx, NKikimrServices::TX_DATASHARD, Self->TabletID()
                    << " Read columns result for table [" << tableInfo.Path << "]: "
                    << rows << " rows, " << bytes << " bytes (event size "
                    << Result->Record.GetBlocks().size() << ") shardFinished: " << shardFinished);

        return true;
    }

    void Complete(const TActorContext& ctx) override {
        if (Result) {
            ctx.Send(Ev->Sender, Result.Release());
        }
    }

private:
    void SetError(ui32 status, TString descr) {
        Result->Record.SetStatus(status);
        Result->Record.SetErrorDescription(descr);
    }
};

void TDataShard::Handle(TEvDataShard::TEvReadColumnsRequest::TPtr& ev, const TActorContext& ctx) {
    Executor()->Execute(new TTxReadColumns(this, ev), ctx);
}

}}
