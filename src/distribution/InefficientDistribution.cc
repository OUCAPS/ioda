/*
 * (C) Copyright 2017-2019 UCAR
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#include "ioda/distribution/InefficientDistribution.h"

#include <algorithm>
#include <iostream>
#include <numeric>

#include "ioda/distribution/DistributionFactory.h"
#include "oops/util/DateTime.h"
#include "oops/util/Logger.h"

// -----------------------------------------------------------------------------
/*!
 * \brief Inefficient distribution
 *
 * \details This method distributes all observations to all processes.
 */

// -----------------------------------------------------------------------------

namespace ioda {

// -----------------------------------------------------------------------------
static DistributionMaker<InefficientDistribution> maker("InefficientDistribution");

// -----------------------------------------------------------------------------
InefficientDistribution::InefficientDistribution(const eckit::mpi::Comm & Comm,
                           const eckit::Configuration & config) :
                                             Distribution(Comm, config) {
  oops::Log::trace() << "InefficientDistribution constructed" << std::endl;
}

// -----------------------------------------------------------------------------
InefficientDistribution::~InefficientDistribution() {
  oops::Log::trace() << "InefficientDistribution destructed" << std::endl;
}

// -----------------------------------------------------------------------------
void InefficientDistribution::patchObs(std::vector<bool> & patchObsVec) const {
  // a copy of obs is present on all PEs but only rank=0 "owns" this obs. as patch
  size_t MyRank = comm_.rank();
  if (MyRank == 0) {
    std::fill(patchObsVec.begin(), patchObsVec.end(), true);
  } else {
    std::fill(patchObsVec.begin(), patchObsVec.end(), false);
  }
}

// -----------------------------------------------------------------------------
double InefficientDistribution::dot_product(
                const std::vector<double> &v1, const std::vector<double> &v2) const {
  return dot_productImpl(v1, v2);
}

// -----------------------------------------------------------------------------
double InefficientDistribution::dot_product(
                const std::vector<float> &v1, const std::vector<float> &v2) const {
  return dot_productImpl(v1, v2);
}

// -----------------------------------------------------------------------------
double InefficientDistribution::dot_product(
                const std::vector<int> &v1, const std::vector<int> &v2) const {
  return dot_productImpl(v1, v2);
}

template <typename T>
double InefficientDistribution::dot_productImpl(const std::vector<T> &v1,
                                                const std::vector<T> &v2) const {
  ASSERT(v1.size() == v2.size());
  T missingValue = util::missingValue(missingValue);
  double zz = 0.0;
  for (size_t jj = 0; jj < v1.size(); ++jj) {
    if (v1[jj] != missingValue && v2[jj] != missingValue) {
      zz += v1[jj] * v2[jj];
    }
  }
  return zz;
}

// -----------------------------------------------------------------------------
size_t InefficientDistribution::globalNumNonMissingObs(const std::vector<double> &v) const {
  return globalNumNonMissingObsImpl(v);
}

size_t InefficientDistribution::globalNumNonMissingObs(const std::vector<float> &v) const {
  return globalNumNonMissingObsImpl(v);
}

size_t InefficientDistribution::globalNumNonMissingObs(const std::vector<int> &v) const {
  return globalNumNonMissingObsImpl(v);
}

size_t InefficientDistribution::globalNumNonMissingObs(const std::vector<std::string> &v) const {
  return globalNumNonMissingObsImpl(v);
}

size_t InefficientDistribution::globalNumNonMissingObs(const std::vector<util::DateTime> &v) const {
  return globalNumNonMissingObsImpl(v);
}

template <typename T>
size_t InefficientDistribution::globalNumNonMissingObsImpl(const std::vector<T> &v) const {
  T missingValue = util::missingValue(missingValue);
  size_t nobs = 0;
  for (size_t jj = 0; jj < v.size(); ++jj) {
    if (v[jj] != missingValue) ++nobs;
  }
  return nobs;
}

// -----------------------------------------------------------------------------
size_t InefficientDistribution::globalUniqueConsecutiveLocationIndex(size_t loc) const {
  return loc;
}

// -----------------------------------------------------------------------------
}  // namespace ioda
