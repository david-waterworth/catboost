#pragma once

#include "approx_calcer_helpers.h"
#include "approx_updater_helpers.h"
#include "error_functions.h"

#include <catboost/libs/data_types/query.h>
#include <catboost/libs/options/restrictions.h>


class IDerCalcer;

namespace NPar {
    class TLocalExecutor;
}


void CalculateDersForQueries(
    const TVector<double>& approxes,
    const TVector<double>& approxesDelta,
    const TVector<float>& targets,
    const TVector<float>& weights,
    const TVector<TQueryInfo>& queriesInfo,
    const IDerCalcer& error,
    int queryStartIndex,
    int queryEndIndex,
    TArrayRef<TDers> approxDers,
    ui64 randomSeed,
    NPar::TLocalExecutor* localExecutor
);

void AddLeafDersForQueries(
    const TVector<TDers>& weightedDers,
    const TVector<TIndexType>& indices,
    const TVector<float>& weights,
    const TVector<TQueryInfo>& queriesInfo,
    int queryStartIndex,
    int queryEndIndex,
    ELeavesEstimation estimationMethod,
    int iteration,
    TVector<TSum>* buckets,
    NPar::TLocalExecutor* localExecutor
);
