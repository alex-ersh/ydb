#include "mkql_wide_map.h"
#include <ydb/library/yql/minikql/computation/mkql_computation_node_holders.h>
#include <ydb/library/yql/minikql/computation/mkql_computation_node_codegen.h>
#include <ydb/library/yql/minikql/mkql_node_cast.h>
#include <ydb/library/yql/utils/cast.h>

namespace NKikimr {
namespace NMiniKQL {

using NYql::EnsureDynamicCast;

namespace {

class TExpandMapWrapper : public TStatelessWideFlowCodegeneratorNode<TExpandMapWrapper> {
using TBaseComputation = TStatelessWideFlowCodegeneratorNode<TExpandMapWrapper>;
public:
    TExpandMapWrapper(IComputationNode* flow, IComputationExternalNode* item, TComputationNodePtrVector&& newItems)
        :  TBaseComputation(flow), Flow(flow), Item(item), NewItems(std::move(newItems))
    {}

    EFetchResult DoCalculate(TComputationContext& ctx, NUdf::TUnboxedValue*const* output) const {
        if (auto item = Flow->GetValue(ctx); item.IsSpecial()) {
            return item.IsYield() ? EFetchResult::Yield : EFetchResult::Finish;
        } else {
            Item->SetValue(ctx, std::move(item));
        }

        for (const auto item : NewItems)
            if (const auto out = *output++)
                *out = item->GetValue(ctx);
        return EFetchResult::One;
    }
#ifndef MKQL_DISABLE_CODEGEN
    TGenerateResult DoGenGetValues(const TCodegenContext& ctx, BasicBlock*& block) const {
        auto& context = ctx.Codegen->GetContext();

        const auto codegenItem = dynamic_cast<ICodegeneratorExternalNode*>(Item);
        MKQL_ENSURE(codegenItem, "Item must be codegenerator node.");

        const auto item = GetNodeValue(Flow, ctx, block);

        const auto resultType = Type::getInt32Ty(context);
        const auto outres = SelectInst::Create(IsYield(item, block), ConstantInt::get(resultType, 0), ConstantInt::get(resultType, -1), "outres", block);

        const auto work = BasicBlock::Create(context, "work", ctx.Func);
        const auto pass = BasicBlock::Create(context, "pass", ctx.Func);
        const auto result = PHINode::Create(outres->getType(), 2, "result", pass);

        result->addIncoming(outres, block);

        BranchInst::Create(pass, work, IsSpecial(item, block), block);

        block = work;
        codegenItem->CreateSetValue(ctx, block, item);

        result->addIncoming(ConstantInt::get(resultType, 1), block);

        BranchInst::Create(pass, block);

        block = pass;

        TGettersList getters;
        getters.reserve(NewItems.size());
        std::transform(NewItems.cbegin(), NewItems.cend(), std::back_inserter(getters), [&](IComputationNode* node) {
            return [node](const TCodegenContext& ctx, BasicBlock*& block){ return GetNodeValue(node, ctx, block); };
        });
        return {result, std::move(getters)};
    }
#endif
private:
    void RegisterDependencies() const final {
        if (const auto flow = FlowDependsOn(Flow)) {
            Own(flow, Item);
            std::for_each(NewItems.cbegin(), NewItems.cend(), std::bind(&TExpandMapWrapper::DependsOn, flow, std::placeholders::_1));
        }
    }

    IComputationNode* const Flow;
    IComputationExternalNode *const Item;
    const TComputationNodePtrVector NewItems;
};

class TWideMapWrapper : public TStatelessWideFlowCodegeneratorNode<TWideMapWrapper> {
using TBaseComputation = TStatelessWideFlowCodegeneratorNode<TWideMapWrapper>;
public:
    TWideMapWrapper(IComputationWideFlowNode* flow, TComputationExternalNodePtrVector&& items, TComputationNodePtrVector&& newItems)
        : TBaseComputation(flow), Flow(flow), Items(std::move(items)), NewItems(std::move(newItems)), Fields(Items.size(), nullptr)
        , PasstroughtMap(GetPasstroughtMap(Items, NewItems)), ReversePasstroughtMap(GetPasstroughtMap(NewItems, Items))
    {}

    EFetchResult DoCalculate(TComputationContext& ctx, NUdf::TUnboxedValue*const* output) const {
        for (auto i = 0U; i < Fields.size(); ++i)
            if (Items[i]->GetDependencesCount() > 0U)
                Fields[i] = &Items[i]->RefValue(ctx);
            else if (const auto& map = PasstroughtMap[i])
                if (const auto out = output[*map])
                    Fields[i] = out;

        if (const auto result = Flow->FetchValues(ctx, Fields.data()); EFetchResult::One != result)
            return result;

        for (auto i = 0U; i < NewItems.size(); ++i) {
            if (const auto out = output[i]) {
                if (const auto& map = ReversePasstroughtMap[i]) {
                    if (const auto from = *map; !Items[from]->GetDependencesCount()) {
                        if (const auto first = *PasstroughtMap[from]; first != i)
                            *out = *output[first];
                        continue;
                    }
                }

                *out = NewItems[i]->GetValue(ctx);
            }
        }
        return EFetchResult::One;
    }
#ifndef MKQL_DISABLE_CODEGEN
    TGenerateResult DoGenGetValues(const TCodegenContext& ctx, BasicBlock*& block) const {
        auto& context = ctx.Codegen->GetContext();

        const auto valueType = Type::getInt128Ty(context);

        const auto result = GetNodeValues(Flow, ctx, block);

        const auto good = CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_SGT, result.first, ConstantInt::get(result.first->getType(), 0), "good", block);

        const auto work = BasicBlock::Create(context, "work", ctx.Func);
        const auto pass = BasicBlock::Create(context, "pass", ctx.Func);

        BranchInst::Create(work, pass, good, block);

        block = work;

        for (auto i = 0U; i < Items.size(); ++i)
            if (Items[i]->GetDependencesCount() > 0U)
                EnsureDynamicCast<ICodegeneratorExternalNode*>(Items[i])->CreateSetValue(ctx, block, result.second[i](ctx, block));

        BranchInst::Create(pass, block);

        block = pass;

        TGettersList getters;
        getters.reserve(NewItems.size());
        for (auto i = 0U; i < NewItems.size(); ++i) {
            if (const auto map = ReversePasstroughtMap[i])
                getters.emplace_back(result.second[*map]);
            else
                getters.emplace_back([node=NewItems[i]](const TCodegenContext& ctx, BasicBlock*& block){ return GetNodeValue(node, ctx, block); });
        };
        return {result.first, std::move(getters)};

    }
#endif
private:
    void RegisterDependencies() const final {
        if (const auto flow = FlowDependsOn(Flow)) {
            std::for_each(Items.cbegin(), Items.cend(), std::bind(&TWideMapWrapper::Own, flow, std::placeholders::_1));
            std::for_each(NewItems.cbegin(), NewItems.cend(), std::bind(&TWideMapWrapper::DependsOn, flow, std::placeholders::_1));
        }
    }

    IComputationWideFlowNode* const Flow;
    const TComputationExternalNodePtrVector Items;
    const TComputationNodePtrVector NewItems;
    const TPasstroughtMap PasstroughtMap, ReversePasstroughtMap;

    mutable std::vector<NUdf::TUnboxedValue*> Fields;
};

class TNarrowMapWrapper : public TStatelessFlowCodegeneratorNode<TNarrowMapWrapper> {
using TBaseComputation = TStatelessFlowCodegeneratorNode<TNarrowMapWrapper>;
public:
    TNarrowMapWrapper(EValueRepresentation kind, IComputationWideFlowNode* flow, TComputationExternalNodePtrVector&& items, IComputationNode* newItem)
        : TBaseComputation(flow, kind)
        , Flow(flow)
        , Items(std::move(items))
        , NewItem(newItem)
        , PasstroughItem(GetPasstroughtMap({NewItem}, Items).front())
        , Fields(Items.size(), nullptr)
    {}

    NUdf::TUnboxedValuePod DoCalculate(TComputationContext& ctx) const {
        for (auto i = 0U; i < Fields.size(); ++i)
            if (NewItem == Items[i] || Items[i]->GetDependencesCount() > 0U)
                Fields[i] = &Items[i]->RefValue(ctx);

        switch (const auto result = Flow->FetchValues(ctx, Fields.data())) {
            case EFetchResult::Finish:
                return NUdf::TUnboxedValuePod::MakeFinish();
            case EFetchResult::Yield:
                return NUdf::TUnboxedValuePod::MakeYield();
            case EFetchResult::One:
                return NewItem->GetValue(ctx).Release();
        }
    }
#ifndef MKQL_DISABLE_CODEGEN
    Value* DoGenerateGetValue(const TCodegenContext& ctx, BasicBlock*& block) const {
        auto& context = ctx.Codegen->GetContext();

        const auto getres = GetNodeValues(Flow, ctx, block);

        const auto yield = CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_EQ, getres.first, ConstantInt::get(getres.first->getType(), 0), "yield", block);
        const auto good = CmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_SGT, getres.first, ConstantInt::get(getres.first->getType(), 0), "good", block);

        const auto outres = SelectInst::Create(yield, GetYield(context), GetFinish(context), "outres", block);

        const auto work = BasicBlock::Create(context, "work", ctx.Func);
        const auto pass = BasicBlock::Create(context, "pass", ctx.Func);

        const auto result = PHINode::Create(outres->getType(), 2, "result", pass);
        result->addIncoming(outres, block);

        BranchInst::Create(work, pass, good, block);

        block = work;

        if (const auto passtrough = PasstroughItem) {
            result->addIncoming(getres.second[*passtrough](ctx, block), block);
        } else {
            for (auto i = 0U; i < Items.size(); ++i)
                if (Items[i]->GetDependencesCount() > 0U)
                    EnsureDynamicCast<ICodegeneratorExternalNode*>(Items[i])->CreateSetValue(ctx, block, getres.second[i](ctx, block));

            result->addIncoming(GetNodeValue(NewItem, ctx, block), block);
        }

        BranchInst::Create(pass, block);

        block = pass;
        return result;
    }
#endif
private:
    void RegisterDependencies() const final {
        if (const auto flow = FlowDependsOn(Flow)) {
            std::for_each(Items.cbegin(), Items.cend(), std::bind(&TNarrowMapWrapper::Own, flow, std::placeholders::_1));
            DependsOn(flow, NewItem);
        }
    }

    IComputationWideFlowNode* const Flow;
    const TComputationExternalNodePtrVector Items;
    IComputationNode* const NewItem;

    const std::optional<size_t> PasstroughItem;

    mutable std::vector<NUdf::TUnboxedValue*> Fields;
};

}

IComputationNode* WrapExpandMap(TCallable& callable, const TComputationNodeFactoryContext& ctx) {
    const auto width = AS_TYPE(TTupleType, AS_TYPE(TFlowType, callable.GetType()->GetReturnType())->GetItemType())->GetElementsCount();
    MKQL_ENSURE(callable.GetInputsCount() == width + 2U, "Expected two or more args.");
    const auto flow = LocateNode(ctx.NodeLocator, callable, 0U);

    TComputationNodePtrVector newItems(width, nullptr);
    ui32 index = 1U;
    std::generate(newItems.begin(), newItems.end(), [&](){ return LocateNode(ctx.NodeLocator, callable, ++index); });

    const auto itemArg = LocateExternalNode(ctx.NodeLocator, callable, 1U);
    return new TExpandMapWrapper(flow, itemArg, std::move(newItems));
}

IComputationNode* WrapWideMap(TCallable& callable, const TComputationNodeFactoryContext& ctx) {
    MKQL_ENSURE(callable.GetInputsCount() > 0U, "Expected argument.");
    const auto inputWidth = AS_TYPE(TTupleType, AS_TYPE(TFlowType, callable.GetInput(0U).GetStaticType())->GetItemType())->GetElementsCount();
    const auto outputWidth = AS_TYPE(TTupleType, AS_TYPE(TFlowType, callable.GetType()->GetReturnType())->GetItemType())->GetElementsCount();
    MKQL_ENSURE(callable.GetInputsCount() == inputWidth + outputWidth + 1U, "Wrong signature.");

    const auto flow = LocateNode(ctx.NodeLocator, callable, 0U);
    if (const auto wide = dynamic_cast<IComputationWideFlowNode*>(flow)) {
        TComputationNodePtrVector newItems(outputWidth, nullptr);
        ui32 index = inputWidth;
        std::generate(newItems.begin(), newItems.end(), [&](){ return LocateNode(ctx.NodeLocator, callable, ++index); });

        TComputationExternalNodePtrVector args(inputWidth, nullptr);
        index = 0U;
        std::generate(args.begin(), args.end(), [&](){ return LocateExternalNode(ctx.NodeLocator, callable, ++index); });

        return new TWideMapWrapper(wide, std::move(args), std::move(newItems));
    }

    THROW yexception() << "Expected wide flow.";
}

IComputationNode* WrapNarrowMap(TCallable& callable, const TComputationNodeFactoryContext& ctx) {
    MKQL_ENSURE(callable.GetInputsCount() > 1U, "Expected two or more args.");
    const auto width = AS_TYPE(TTupleType, AS_TYPE(TFlowType, callable.GetInput(0U).GetStaticType())->GetItemType())->GetElementsCount();
    MKQL_ENSURE(callable.GetInputsCount() == width + 2U, "Wrong signature.");
    const auto flow = LocateNode(ctx.NodeLocator, callable, 0U);
    if (const auto wide = dynamic_cast<IComputationWideFlowNode*>(flow)) {
        const auto newItem = LocateNode(ctx.NodeLocator, callable, callable.GetInputsCount() - 1U);

        TComputationExternalNodePtrVector args(width, nullptr);
        ui32 index = 0U;
        std::generate(args.begin(), args.end(), [&](){ return LocateExternalNode(ctx.NodeLocator, callable, ++index); });

        return new TNarrowMapWrapper(GetValueRepresentation(callable.GetType()->GetReturnType()), wide, std::move(args), newItem);
    }

    THROW yexception() << "Expected wide flow.";
}

}
}
