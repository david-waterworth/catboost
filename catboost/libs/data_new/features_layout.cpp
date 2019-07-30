#include "features_layout.h"

#include "util.h"

#include <catboost/libs/helpers/exception.h>
#include <catboost/libs/quantization_schema/schema.h>

#include <util/generic/algorithm.h>
#include <util/generic/scope.h>
#include <util/generic/xrange.h>
#include <util/generic/hash_set.h>
#include <util/string/join.h>

#include <algorithm>
#include <tuple>


using namespace NCB;

bool TFeatureMetaInfo::operator==(const TFeatureMetaInfo& rhs) const {
    return std::tie(Type, Name, IsIgnored, IsAvailable) ==
        std::tie(rhs.Type, rhs.Name, rhs.IsIgnored, rhs.IsAvailable);
}

TFeaturesLayout::TFeaturesLayout(const ui32 featureCount)
    : TFeaturesLayout(featureCount, {}, {}, {})
{}

TFeaturesLayout::TFeaturesLayout(
    const ui32 featureCount,
    const TVector<ui32>& catFeatureIndices,
    const TVector<ui32>& textFeatureIndices,
    const TVector<TString>& featureId)
{
    CheckDataSize(featureId.size(), (size_t)featureCount, "feature Ids", true, "feature count");

    ExternalIdxToMetaInfo.reserve((size_t)featureCount);
    for (auto externalFeatureIdx : xrange(featureCount)) {
        // cat feature will be set later
        ExternalIdxToMetaInfo.emplace_back(
            EFeatureType::Float,
            !featureId.empty() ? featureId[externalFeatureIdx] : ""
        );
    }
    for (auto catFeatureExternalIdx : catFeatureIndices) {
        CB_ENSURE(
            catFeatureExternalIdx < featureCount,
            "Cat feature index (" << catFeatureExternalIdx << ") is out of valid range [0,"
            << featureCount << ')'
        );
        ExternalIdxToMetaInfo[catFeatureExternalIdx].Type = EFeatureType::Categorical;
    }
    for (auto textFeatureExternalIdx : textFeatureIndices) {
        CB_ENSURE(
            textFeatureExternalIdx < featureCount,
            "Text feature index (" << textFeatureExternalIdx << ") is out of valid range [0,"
            << featureCount << ')'
        );
        ExternalIdxToMetaInfo[textFeatureExternalIdx].Type = EFeatureType::Text;
    }

    for (auto externalFeatureIdx : xrange(ExternalIdxToMetaInfo.size())) {
        switch (ExternalIdxToMetaInfo[externalFeatureIdx].Type) {
            case EFeatureType::Float: {
                FeatureExternalIdxToInternalIdx.push_back((ui32)FloatFeatureInternalIdxToExternalIdx.size());
                FloatFeatureInternalIdxToExternalIdx.push_back(externalFeatureIdx);
                break;
            }
            case EFeatureType::Categorical: {
                FeatureExternalIdxToInternalIdx.push_back((ui32)CatFeatureInternalIdxToExternalIdx.size());
                CatFeatureInternalIdxToExternalIdx.push_back(externalFeatureIdx);
                break;
            }
            case EFeatureType::Text: {
                FeatureExternalIdxToInternalIdx.push_back((ui32)TextFeatureInternalIdxToExternalIdx.size());
                TextFeatureInternalIdxToExternalIdx.push_back(externalFeatureIdx);
                break;
            }
        }
    }

    THashSet<TString> featureNames;
    for (const TString& name : featureId) {
        if (!name.empty()) {
            CB_ENSURE(
                !featureNames.contains(name),
                "All feature names should be different, but '" << name << "' used more than once."
            );
            featureNames.insert(name);
        }
    }
}

TFeaturesLayout::TFeaturesLayout(
    const TVector<TFloatFeature>& floatFeatures,
    const TVector<TCatFeature>& catFeatures)
{
    TFeatureMetaInfo defaultIgnoredMetaInfo(EFeatureType::Float, TString(), true);
    const ui32 internalOrExternalIndexPlaceholder = Max<ui32>();
    for (const TFloatFeature& floatFeature : floatFeatures) {
        CB_ENSURE(floatFeature.Position.FlatIndex >= 0, "floatFeature.Position.FlatIndex is negative");
        CB_ENSURE(floatFeature.Position.Index >= 0, "floatFeature.Position.Index is negative");
        if ((size_t)floatFeature.Position.FlatIndex >= ExternalIdxToMetaInfo.size()) {
            CB_ENSURE(
                (size_t)floatFeature.Position.FlatIndex < (size_t)Max<ui32>(),
                "floatFeature.Position.FlatIndex is greater than maximum allowed index: " << (Max<ui32>() - 1)
            );
            ExternalIdxToMetaInfo.resize(floatFeature.Position.FlatIndex + 1, defaultIgnoredMetaInfo);
            FeatureExternalIdxToInternalIdx.resize(floatFeature.Position.FlatIndex + 1, internalOrExternalIndexPlaceholder);
        }
        ExternalIdxToMetaInfo[floatFeature.Position.FlatIndex] =
            TFeatureMetaInfo(EFeatureType::Float, floatFeature.FeatureId);
        FeatureExternalIdxToInternalIdx[floatFeature.Position.FlatIndex] = floatFeature.Position.Index;
        if ((size_t)floatFeature.Position.Index >= FloatFeatureInternalIdxToExternalIdx.size()) {
            FloatFeatureInternalIdxToExternalIdx.resize((size_t)floatFeature.Position.Index + 1, internalOrExternalIndexPlaceholder);
        }
        FloatFeatureInternalIdxToExternalIdx[floatFeature.Position.Index] = floatFeature.Position.FlatIndex;
    }

    for (const TCatFeature& catFeature : catFeatures) {
        CB_ENSURE(catFeature.Position.FlatIndex >= 0, "catFeature.Position.FlatIndex is negative");
        CB_ENSURE(catFeature.Position.Index >= 0, "catFeature.Position.Index is negative");
        if ((size_t)catFeature.Position.FlatIndex >= ExternalIdxToMetaInfo.size()) {
            CB_ENSURE(
                (size_t)catFeature.Position.FlatIndex < (size_t)Max<ui32>(),
                "catFeature.Position.FlatIndex is greater than maximum allowed index: " << (Max<ui32>() - 1)
            );
            ExternalIdxToMetaInfo.resize(catFeature.Position.FlatIndex + 1, defaultIgnoredMetaInfo);
            FeatureExternalIdxToInternalIdx.resize(catFeature.Position.FlatIndex + 1, internalOrExternalIndexPlaceholder);
        }
        ExternalIdxToMetaInfo[catFeature.Position.FlatIndex] =
            TFeatureMetaInfo(EFeatureType::Categorical, catFeature.FeatureId);
        FeatureExternalIdxToInternalIdx[catFeature.Position.FlatIndex] = catFeature.Position.Index;
        if ((size_t)catFeature.Position.Index >= CatFeatureInternalIdxToExternalIdx.size()) {
            CatFeatureInternalIdxToExternalIdx.resize((size_t)catFeature.Position.Index + 1, internalOrExternalIndexPlaceholder);
        }
        CatFeatureInternalIdxToExternalIdx[catFeature.Position.Index] = catFeature.Position.FlatIndex;
    }
}

bool TFeaturesLayout::IsSupersetOf(const TFeaturesLayout& rhs) const {
    if (this == &rhs) { // shortcut
        return true;
    }

    const size_t rhsSize = rhs.ExternalIdxToMetaInfo.size();
    if (ExternalIdxToMetaInfo.size() < rhsSize) {
        return false;
    }
    return std::equal(
            rhs.ExternalIdxToMetaInfo.begin(),
            rhs.ExternalIdxToMetaInfo.end(),
            ExternalIdxToMetaInfo.begin()
        ) && std::equal(
            rhs.FeatureExternalIdxToInternalIdx.begin(),
            rhs.FeatureExternalIdxToInternalIdx.end(),
            FeatureExternalIdxToInternalIdx.begin()
        );
}


const TFeatureMetaInfo& TFeaturesLayout::GetInternalFeatureMetaInfo(
    ui32 internalFeatureIdx,
    EFeatureType type) const
{
    return ExternalIdxToMetaInfo[GetExternalFeatureIdx(internalFeatureIdx, type)];
}

TConstArrayRef<TFeatureMetaInfo> TFeaturesLayout::GetExternalFeaturesMetaInfo() const {
    return ExternalIdxToMetaInfo;
}

TString TFeaturesLayout::GetExternalFeatureDescription(ui32 internalFeatureIdx, EFeatureType type) const {
    return ExternalIdxToMetaInfo[GetExternalFeatureIdx(internalFeatureIdx, type)].Name;
}

ui32 TFeaturesLayout::GetExternalFeatureIdx(ui32 internalFeatureIdx, EFeatureType type) const {
    switch (type) {
        case EFeatureType::Float:
            return FloatFeatureInternalIdxToExternalIdx[internalFeatureIdx];
        case EFeatureType::Categorical:
            return CatFeatureInternalIdxToExternalIdx[internalFeatureIdx];
        case EFeatureType::Text:
            return TextFeatureInternalIdxToExternalIdx[internalFeatureIdx];
    }
}

ui32 TFeaturesLayout::GetInternalFeatureIdx(ui32 externalFeatureIdx) const {
    Y_ASSERT(IsCorrectExternalFeatureIdx(externalFeatureIdx));
    return FeatureExternalIdxToInternalIdx[externalFeatureIdx];
}

EFeatureType TFeaturesLayout::GetExternalFeatureType(ui32 externalFeatureIdx) const {
    Y_ASSERT(IsCorrectExternalFeatureIdx(externalFeatureIdx));
    return ExternalIdxToMetaInfo[externalFeatureIdx].Type;
}

bool TFeaturesLayout::IsCorrectExternalFeatureIdx(ui32 externalFeatureIdx) const {
    return (size_t)externalFeatureIdx < ExternalIdxToMetaInfo.size();
}

bool TFeaturesLayout::IsCorrectInternalFeatureIdx(ui32 internalFeatureIdx, EFeatureType type) const {
    switch (type) {
        case EFeatureType::Float:
            return (size_t)internalFeatureIdx < FloatFeatureInternalIdxToExternalIdx.size();
        case EFeatureType::Categorical:
            return (size_t)internalFeatureIdx < CatFeatureInternalIdxToExternalIdx.size();
        case EFeatureType::Text:
            return (size_t)internalFeatureIdx < TextFeatureInternalIdxToExternalIdx.size();
    }
}

bool TFeaturesLayout::IsCorrectExternalFeatureIdxAndType(ui32 externalFeatureIdx, EFeatureType type) const {
    if ((size_t)externalFeatureIdx >= ExternalIdxToMetaInfo.size()) {
        return false;
    }
    return ExternalIdxToMetaInfo[externalFeatureIdx].Type == type;
}

ui32 TFeaturesLayout::GetFloatFeatureCount() const {
    // cast is safe because of size invariant established in constructors
    return (ui32)FloatFeatureInternalIdxToExternalIdx.size();
}

ui32 TFeaturesLayout::GetCatFeatureCount() const {
    // cast is safe because of size invariant established in constructors
    return (ui32)CatFeatureInternalIdxToExternalIdx.size();
}

ui32 TFeaturesLayout::GetTextFeatureCount() const {
    // cast is safe because of size invariant established in constructors
    return (ui32)TextFeatureInternalIdxToExternalIdx.size();
}

ui32 TFeaturesLayout::GetExternalFeatureCount() const {
    // cast is safe because of size invariant established in constructors
    return (ui32)ExternalIdxToMetaInfo.size();
}

ui32 TFeaturesLayout::GetFeatureCount(EFeatureType type) const {
    switch (type) {
        case EFeatureType::Float:
            return GetFloatFeatureCount();
        case EFeatureType::Categorical:
            return GetCatFeatureCount();
        case EFeatureType::Text:
            return GetTextFeatureCount();
    }
}

void TFeaturesLayout::IgnoreExternalFeature(ui32 externalFeatureIdx) {
    if (externalFeatureIdx >= ExternalIdxToMetaInfo.size()) {
        return;
    }

    auto& metaInfo = ExternalIdxToMetaInfo[externalFeatureIdx];
    metaInfo.IsIgnored = true;
    metaInfo.IsAvailable = false;
}

void TFeaturesLayout::IgnoreExternalFeatures(TConstArrayRef<ui32> ignoredFeatures) {
    for (auto ignoredFeature : ignoredFeatures) {
        IgnoreExternalFeature(ignoredFeature);
    }
}

void TFeaturesLayout::SetExternalFeatureAvailability(ui32 externalFeatureIdx, bool isAvailable) {
    if (externalFeatureIdx >= ExternalIdxToMetaInfo.size()) {
        return;
    }

    auto& metaInfo = ExternalIdxToMetaInfo[externalFeatureIdx];
    metaInfo.IsAvailable = isAvailable && !metaInfo.IsIgnored;
}

TConstArrayRef<ui32> TFeaturesLayout::GetCatFeatureInternalIdxToExternalIdx() const {
    return CatFeatureInternalIdxToExternalIdx;
}

TConstArrayRef<ui32> TFeaturesLayout::GetTextFeatureInternalIdxToExternalIdx() const {
    return TextFeatureInternalIdxToExternalIdx;
}


bool TFeaturesLayout::operator==(const TFeaturesLayout& rhs) const {
    return std::tie(
            ExternalIdxToMetaInfo,
            FeatureExternalIdxToInternalIdx,
            CatFeatureInternalIdxToExternalIdx,
            FloatFeatureInternalIdxToExternalIdx,
            TextFeatureInternalIdxToExternalIdx
        ) == std::tie(
            rhs.ExternalIdxToMetaInfo,
            rhs.FeatureExternalIdxToInternalIdx,
            rhs.CatFeatureInternalIdxToExternalIdx,
            rhs.FloatFeatureInternalIdxToExternalIdx,
            rhs.TextFeatureInternalIdxToExternalIdx
        );
}


TVector<TString> TFeaturesLayout::GetExternalFeatureIds() const {
    TVector<TString> result;
    result.reserve(ExternalIdxToMetaInfo.size());
    for (const auto& metaInfo : ExternalIdxToMetaInfo) {
        result.push_back(metaInfo.Name);
    }
    return result;
}

void TFeaturesLayout::SetExternalFeatureIds(TConstArrayRef<TString> featureIds) {
    CheckDataSize(featureIds.size(), ExternalIdxToMetaInfo.size(), "feature names", false, "feature count");
    for (auto i : xrange(ExternalIdxToMetaInfo.size())) {
        ExternalIdxToMetaInfo[i].Name = featureIds[i];
    }
}

bool TFeaturesLayout::HasAvailableAndNotIgnoredFeatures() const {
    for (const auto& metaInfo : ExternalIdxToMetaInfo) {
        if (metaInfo.IsAvailable && !metaInfo.IsIgnored) {
            return true;
        }
    }
    return false;
}

void NCB::CheckCompatibleForApply(
    const TFeaturesLayout& learnFeaturesLayout,
    const TFeaturesLayout& applyFeaturesLayout,
    const TString& applyDataName
) {
    auto learnFeaturesMetaInfo = learnFeaturesLayout.GetExternalFeaturesMetaInfo();
    auto applyFeaturesMetaInfo = applyFeaturesLayout.GetExternalFeaturesMetaInfo();

    auto featuresIntersectionSize = Min(learnFeaturesMetaInfo.size(), applyFeaturesMetaInfo.size());

    size_t i = 0;
    for (; i < featuresIntersectionSize; ++i) {
        const auto& learnFeatureMetaInfo = learnFeaturesMetaInfo[i];
        const auto& applyFeatureMetaInfo = applyFeaturesMetaInfo[i];

        if (!learnFeatureMetaInfo.IsAvailable || learnFeatureMetaInfo.IsIgnored) {
            continue;
        }

        CB_ENSURE(
            applyFeatureMetaInfo.IsAvailable,
            "Feature #" << i
            << " is used in training data, but not available in " << applyDataName
        );
        CB_ENSURE(
            !applyFeatureMetaInfo.IsIgnored,
            "Feature #" << i
            << " is used in training data, but is ignored in " << applyDataName
        );
        CB_ENSURE(
            learnFeatureMetaInfo.Type == applyFeatureMetaInfo.Type,
            "Feature #" << i << " has '" << learnFeatureMetaInfo.Type << "' type in training data, but '"
            << applyFeatureMetaInfo.Type << "' type in " << applyDataName
        );
        CB_ENSURE(
            !learnFeatureMetaInfo.Name || !applyFeatureMetaInfo.Name ||
            (learnFeatureMetaInfo.Name == applyFeatureMetaInfo.Name),
            "Feature #" << i << " has '" << learnFeatureMetaInfo.Name << "' name in training data, but '"
            << applyFeatureMetaInfo.Name << "' name in " << applyDataName
        );
    }
    for (; i < learnFeaturesMetaInfo.size(); ++i) {
        CB_ENSURE(
            !learnFeaturesMetaInfo[i].IsAvailable || learnFeaturesMetaInfo[i].IsIgnored,
            "Feature #" << i
            << " is used in training data, but not available in " << applyDataName
        );
    }
}
