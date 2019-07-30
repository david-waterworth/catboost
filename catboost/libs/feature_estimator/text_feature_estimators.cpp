#include "text_feature_estimators.h"
#include "base_text_feature_estimator.h"

#include <catboost/libs/text_features/text_feature_calcers.h>
#include <catboost/libs/text_processing/embedding.h>
#include <catboost/libs/text_processing/embedding_loader.h>

#include <catboost/libs/options/enum_helpers.h>

#include <util/generic/set.h>


using namespace NCB;

namespace {
    class TNaiveBayesEstimator final: public TBaseEstimator<TMultinomialNaiveBayes, TNaiveBayesVisitor> {
    public:
        TNaiveBayesEstimator(
            TTextClassificationTargetPtr target,
            TTextDataSetPtr learnTexts,
            TArrayRef<TTextDataSetPtr> testText)
            : TBaseEstimator(std::move(target), std::move(learnTexts), testText)
        {
        }

        TEstimatedFeaturesMeta FeaturesMeta() const override {
            TEstimatedFeaturesMeta meta;
            meta.FeaturesCount = TMultinomialNaiveBayes::FeatureCount(GetTarget().NumClasses);
            meta.Type.resize(meta.FeaturesCount, EFeatureCalcerType::NaiveBayes);
            return meta;
        }

        TMultinomialNaiveBayes CreateFeatureCalcer() const override {
            return TMultinomialNaiveBayes(GetTarget().NumClasses);
        }

        TNaiveBayesVisitor CreateCalcerVisitor() const override {
            return TNaiveBayesVisitor();
        };
    };

    class TBM25Estimator final: public TBaseEstimator<TBM25, TBM25Visitor> {
    public:
        TBM25Estimator(
            TTextClassificationTargetPtr target,
            TTextDataSetPtr learnTexts,
            TArrayRef<TTextDataSetPtr> testText)
            : TBaseEstimator(std::move(target), std::move(learnTexts), testText)
        {
        }

        TEstimatedFeaturesMeta FeaturesMeta() const override {
            TEstimatedFeaturesMeta meta;
            meta.FeaturesCount = TBM25::FeatureCount(GetTarget().NumClasses);
            meta.Type.resize(meta.FeaturesCount, EFeatureCalcerType::BM25);
            return meta;
        }

        TBM25 CreateFeatureCalcer() const override {
            return TBM25(GetTarget().NumClasses);
        }

        TBM25Visitor CreateCalcerVisitor() const override {
            return TBM25Visitor();
        };
    };

    class TEmbeddingOnlineFeaturesEstimator final:
        public TBaseEstimator<TEmbeddingOnlineFeatures, TEmbeddingFeaturesVisitor> {
    public:
        TEmbeddingOnlineFeaturesEstimator(
            TEmbeddingPtr embedding,
            TTextClassificationTargetPtr target,
            TTextDataSetPtr learnTexts,
            TArrayRef<TTextDataSetPtr> testText,
            const TSet<EFeatureCalcerType>& enabledTypes)
            : TBaseEstimator(std::move(target), std::move(learnTexts), std::move(testText))
            , Embedding(std::move(embedding))
            , ComputeCosDistance(enabledTypes.contains(EFeatureCalcerType::CosDistanceWithClassCenter))
            , ComputeGaussianHomoscedatic(enabledTypes.contains(EFeatureCalcerType::GaussianHomoscedasticModel))
            , ComputeGaussianHeteroscedatic(enabledTypes.contains(EFeatureCalcerType::GaussianHeteroscedasticModel))
        {}

        TEstimatedFeaturesMeta FeaturesMeta() const override {
            TEstimatedFeaturesMeta meta;
            meta.FeaturesCount = TEmbeddingOnlineFeatures::FeatureCount(
                GetTarget().NumClasses,
                ComputeCosDistance,
                ComputeGaussianHomoscedatic,
                ComputeGaussianHeteroscedatic
            );

            for (ui32 classIdx = 0; classIdx < GetTarget().NumClasses; ++classIdx) {
                if (ComputeCosDistance) {
                    meta.Type.push_back(EFeatureCalcerType::CosDistanceWithClassCenter);
                }
                if (ComputeGaussianHomoscedatic) {
                    meta.Type.push_back(EFeatureCalcerType::GaussianHomoscedasticModel);
                }
                if (ComputeGaussianHeteroscedatic) {
                    meta.Type.push_back(EFeatureCalcerType::GaussianHeteroscedasticModel);
                }
            }
            return meta;
        }

        TEmbeddingOnlineFeatures CreateFeatureCalcer() const override {
            return TEmbeddingOnlineFeatures(
                GetTarget().NumClasses,
                Embedding,
                ComputeCosDistance,
                ComputeGaussianHomoscedatic,
                ComputeGaussianHeteroscedatic
            );
        }

        TEmbeddingFeaturesVisitor CreateCalcerVisitor() const override {
            return TEmbeddingFeaturesVisitor(GetTarget().NumClasses, Embedding->Dim());
        }

    private:
        TEmbeddingPtr Embedding;
        bool ComputeCosDistance = false;
        bool ComputeGaussianHomoscedatic = false;
        bool ComputeGaussianHeteroscedatic = false;
    };

    class TBagOfWordsEstimator final : public IFeatureEstimator {
    public:
        TBagOfWordsEstimator(
            TTextDataSetPtr learnTexts,
            TArrayRef<TTextDataSetPtr> testTexts)
            : LearnTexts({learnTexts})
            , TestTexts(testTexts.begin(), testTexts.end())
            , Dictionary(learnTexts->GetDictionary())
        {}

        TEstimatedFeaturesMeta FeaturesMeta() const override {
            const ui32 featureCount = Dictionary.Size();
            TEstimatedFeaturesMeta meta;
            meta.Type = TVector<EFeatureCalcerType>(featureCount, EFeatureCalcerType::BoW);
            meta.FeaturesCount = featureCount;
            meta.UniqueValuesUpperBoundHint = TVector<ui32>(featureCount, 2);
            return meta;
        }

        void ComputeFeatures(TCalculatedFeatureVisitor learnVisitor,
                             TConstArrayRef<TCalculatedFeatureVisitor> testVisitors,
                             NPar::TLocalExecutor* executor) const override {
            Calc(*executor, MakeConstArrayRef(LearnTexts), {learnVisitor});
            Calc(*executor, MakeConstArrayRef(TestTexts), testVisitors);
        }


    protected:

        void Calc(NPar::TLocalExecutor& executor,
                  TConstArrayRef<TTextDataSetPtr> dataSets,
                  TConstArrayRef<TCalculatedFeatureVisitor> visitors) const {
            const ui32 featuresCount = Dictionary.Size();

            // TODO(d-kruchinin, noxoomo) better implementation:
            // add MaxRam option + bit mask compression for block on m features
            // estimation of all features in one pass

            for (ui32 id = 0; id < dataSets.size(); ++id) {
                const auto& ds = *dataSets[id];
                const ui64 samplesCount = ds.SamplesCount();

                //one-by-one, we don't want to acquire unnecessary RAM for very sparse features
                TVector<float> singleFeature(samplesCount);
                for (ui32 tokenId = 0; tokenId < featuresCount; ++tokenId) {
                    NPar::ParallelFor(
                        executor, 0, samplesCount, [&](ui32 line) {
                            const bool hasToken = ds.GetText(line).Has(TTokenId(tokenId));
                            if (hasToken) {
                                singleFeature[line] = 1.0;
                            }
                        }
                    );
                    visitors[id](tokenId, singleFeature);
                }
            }
        }

    private:
        TVector<TTextDataSetPtr> LearnTexts;
        TVector<TTextDataSetPtr> TestTexts;
        const IDictionary& Dictionary;
    };
}

TVector<TOnlineFeatureEstimatorPtr> NCB::CreateEstimators(
    TConstArrayRef<EFeatureCalcerType> type,
    TEmbeddingPtr embedding,
    TTextClassificationTargetPtr target,
    TTextDataSetPtr learnTexts,
    TArrayRef<TTextDataSetPtr> testText) {
    TSet<EFeatureCalcerType> typesSet(type.begin(), type.end());

    TVector<TOnlineFeatureEstimatorPtr> estimators;

    if (typesSet.contains(EFeatureCalcerType::NaiveBayes)) {
        estimators.push_back(new TNaiveBayesEstimator(target, learnTexts, testText));
    }
    if (typesSet.contains(EFeatureCalcerType::BM25)) {
        estimators.push_back(new TBM25Estimator(target, learnTexts, testText));
    }
    TSet<EFeatureCalcerType> embeddingEstimators = {
        EFeatureCalcerType::GaussianHomoscedasticModel,
        EFeatureCalcerType::GaussianHeteroscedasticModel,
        EFeatureCalcerType::CosDistanceWithClassCenter
    };

    TSet<EFeatureCalcerType> enabledEmbeddingCalculators;
    SetIntersection(
        typesSet.begin(), typesSet.end(),
        embeddingEstimators.begin(), embeddingEstimators.end(),
        std::inserter(enabledEmbeddingCalculators, enabledEmbeddingCalculators.end()));

    if (!enabledEmbeddingCalculators.empty()) {
        estimators.push_back(new TEmbeddingOnlineFeaturesEstimator(embedding, target, learnTexts, testText, enabledEmbeddingCalculators));
    }
    return estimators;
}

TVector<TFeatureEstimatorPtr> NCB::CreateEstimators(
    TConstArrayRef<EFeatureCalcerType> types,
    TEmbeddingPtr embedding,
    TTextDataSetPtr learnTexts,
    TArrayRef<TTextDataSetPtr> testText) {
    Y_UNUSED(embedding);
    TSet<EFeatureCalcerType> typesSet(types.begin(), types.end());
    TVector<TFeatureEstimatorPtr> estimators;
    if (typesSet.contains(EFeatureCalcerType::BoW)) {
        estimators.push_back(new TBagOfWordsEstimator(learnTexts, testText));
    }
    return estimators;
}
