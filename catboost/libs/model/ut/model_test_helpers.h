#pragma once

#include <catboost/libs/data_new/data_provider.h>
#include <catboost/libs/model/model.h>

TFullModel TrainFloatCatboostModel(int iterations = 5, int seed = 123);

NCB::TDataProviderPtr GetAdultPool();

TFullModel SimpleFloatModel(size_t treeCount = 1);

TFullModel SimpleDeepTreeModel(size_t treeDepth = 10);

TFullModel SimpleAsymmetricModel();

TFullModel MultiValueFloatModel();

// Deterministically train model that has only 3 categorical features.
TFullModel TrainCatOnlyModel();

