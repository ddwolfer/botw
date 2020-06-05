#pragma once

#include "KingSystem/Utils/StrTreeMap.h"

namespace ksys::act {

class BaseProc;

class BaseProcMapNode final : public util::StrTreeMapNode {
public:
    explicit BaseProcMapNode(BaseProc* proc) : mProc(proc) {}
    ~BaseProcMapNode() override;

    // NON_MATCHING: addressing mode for mPrev
    void erase_() override {
        if (mPrev)
            mPrev->mNext = mNext;

        if (mNext)
            mNext->mPrev = mPrev;

        StrTreeMapNode::erase_();
        mInserted = false;
        mPrev = mNext = nullptr;
    }

    util::StrTreeMapKey& key() { return mKey; }
    const util::StrTreeMapKey& key() const { return mKey; }
    BaseProc* proc() const { return mProc; }
    BaseProcMapNode* prev() const { return mPrev; }
    BaseProcMapNode* next() const { return mNext; }

    void add(BaseProcMapNode* node);

    bool isInserted() const { return mInserted; }
    void setInserted() { mInserted = true; }

private:
    BaseProc* mProc = nullptr;
    BaseProcMapNode* mPrev = nullptr;
    BaseProcMapNode* mNext = nullptr;
    bool mInserted = false;
};

class BaseProcMap : public util::StrTreeMap<BaseProcMapNode> {
public:
    void insert(BaseProcMapNode* node);

    void erase(BaseProcMapNode* node) {
        auto prev = node->prev();
        auto next = node->next();
        if (prev)
            node->erase_();
        else if (next)
            StrTreeMap::insert(next);
        else
            StrTreeMap::erase(node->key());
    }
};

}  // namespace ksys::act
