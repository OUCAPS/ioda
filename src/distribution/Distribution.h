/*
 * (C) Copyright 2017 UCAR
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#ifndef DISTRIBUTION_DISTRIBUTION_H_
#define DISTRIBUTION_DISTRIBUTION_H_

#include <vector>

#include "eckit/config/Configuration.h"
#include "eckit/exception/Exceptions.h"
#include "eckit/geometry/Point2.h"
#include "eckit/mpi/Comm.h"
#include "oops/util/missingValues.h"

namespace util {
class DateTime;
}

namespace ioda {

// ---------------------------------------------------------------------
/*!
 * \brief class for distributing obs across multiple process elements
 *
 * \details This Distribution class is a base class where various subclasses of
 *          this class define different methods for distributing obs.
 *
 *           The subclasses of this base class need to implement all pure virtual functions.
 *           The client will use the isMyRecord method to determine what records to keep
 *           when reading in observations.
 *
 *           for distributions sensitive to lat/lon locations of the ob.
 *           assignRecord() must be called before isMyRecord or isMyPatchRecord 
 *           can be called 
 *
 *           for distributions with observations duplicated on multiple PEs 
 *           (currently Inefficient and Halo) the following terminology/logic is used:
 *           the set of obs on each PE is called halo obs. (as in within a halo of some location)
 *           obs in the halo set can be duplicated across multiple PEs
 *           the subset of halo obs. (patch obs.) "belong" only to this PE and are used to 
 *           compute reduce operations without duplication. 
 *           patch obs form complete, non-overlapping partition of the global set of obs. 
 *
 * \author Xin Zhang (JCSDA)
 */

/*! \fn bool isMyRecord(std::size_t RecNum)
 *  \brief The client will use the isMyRecord method to determine what records to 
 *         keep when reading in observations.
 *  \param RecNum Record Number       
 */

/*! \fn void computePatchLocs(const std::size_t nglocs)
 *  \brief computes internal index (if needed) of patch locations for this PE
 *         assumed that this function is called before dot_product, globalNumNonMissingObs
 *  \param nglocs total number of global locations
*/

/*! \fn void assignRecord(const std::size_t RecNum, const std::size_t LocNum,
                          const eckit::geometry::Point2 & point)
 *  \brief for Halo distribution, computes if this recNumber locNumber belong to this PE
 *         empty function for other distributions
 *  \param point Point2 object describing location of this LocNum/RecNum
 */

/*! \fn void patchObs(std::vector<bool> & vecIn) 
 *  \brief returns vector<bool> that is true for patch obs on this PE and false otherwise
 *         currently only used in tests 
 *  \param  vecIn(nobs) preallocated vector of length == to number of obs on this PE
 */

/*! \fn double dot_product(const std::vector<double> &v1, const std::vector<double> &v2)
 *  \brief computes dot product between two vectors of obs distributed accross PEs
 *         assumes that data vectors have the following layout values[iloc*nvars + ivar] 
 */

/*! \fn size_t globalNumNonMissingObs(const std::vector<double> &v) const
 *  \brief Counts unique non-missing observations in \p v.
 *
 *  \param v
 *    A vector assumed to contain observations of a single variable or interleaved observations of
 *    multiple variables (so that the observations of variable `ivar` at location `iloc` in the
 *    halo of this PE is stored at `v[iloc * nvars + ivar]`, where `nvars` is the number of
 *    variables stored in `v`).
 *
 *  \return The number of unique observations on all PEs set to something else than the missing
 *  value indicator. "Unique" means that observations taken at locations belonging to the halos of
 *  multiple PEs are counted only once.
 */

class Distribution {
 public:
    explicit Distribution(const eckit::mpi::Comm & Comm,
                          const eckit::Configuration & config);
    virtual ~Distribution();

    virtual bool isMyRecord(std::size_t RecNum) const = 0;
    virtual void computePatchLocs(const std::size_t nglocs) {}
    virtual void assignRecord(const std::size_t RecNum, const std::size_t LocNum,
              const eckit::geometry::Point2 & point) {}
    virtual void patchObs(std::vector<bool> &) const = 0;


    // operations for computing RMSE and globalNumNonMissingObs in presence of overlapping obs.
    virtual double dot_product(const std::vector<double> &v1, const std::vector<double> &v2)
                      const = 0;
    virtual double dot_product(const std::vector<float> &v1, const std::vector<float> &v2)
                      const = 0;
    virtual double dot_product(const std::vector<int> &v1, const std::vector<int> &v2)
                      const = 0;

    virtual size_t globalNumNonMissingObs(const std::vector<double> &v) const = 0;
    virtual size_t globalNumNonMissingObs(const std::vector<float> &v) const = 0;
    virtual size_t globalNumNonMissingObs(const std::vector<int> &v) const = 0;
    virtual size_t globalNumNonMissingObs(const std::vector<std::string> &v) const = 0;
    virtual size_t globalNumNonMissingObs(const std::vector<util::DateTime> &v) const = 0;

    /*!
     * \brief All reduce operation on a scalar
     *
     * \param[inout] x
     *   On input: a scalar associated with observations held by the calling process.
     *   On output: a result of reduction operation on \p x passed by all calling processes.
     * \param[in] op
     *   Reduction operation (e.g. eckit::mpi::sum(), eckit::mpi::min(), eckit::mpi::max()
     */
    virtual void allReduceInPlace(double &x, eckit::mpi::Operation::Code op) const = 0;
    virtual void allReduceInPlace(float &x, eckit::mpi::Operation::Code op) const = 0;
    virtual void allReduceInPlace(int &x, eckit::mpi::Operation::Code op) const = 0;
    virtual void allReduceInPlace(size_t &x, eckit::mpi::Operation::Code op) const = 0;

    /*!
     * \brief All reduce operation on a vector
     *
     * \param[inout] x
     *   On input: a standard vector associated with observations held by the calling process.
     *   On output: a result of reduction operation on \p x passed by all calling processes.
     * \param[in] op
     *   Reduction operation (e.g. eckit::mpi::sum(), eckit::mpi::min(), eckit::mpi::max()
     */
    virtual void allReduceInPlace(std::vector<double> &x, eckit::mpi::Operation::Code op) const = 0;
    virtual void allReduceInPlace(std::vector<float> &x, eckit::mpi::Operation::Code op) const = 0;
    virtual void allReduceInPlace(std::vector<size_t> &x, eckit::mpi::Operation::Code op) const = 0;

    /*!
     * \brief Gather observation data from all processes and deliver the combined data to
     * all processes.
     *
     * \param[inout] x
     *   On input: a vector whose ith element is associated with the ith observation held by the
     *   calling process.
     *   On output: a concatenation of the vectors `x` passed by all calling processes, in the order
     *   of process ranks, with duplicates removed (i.e. if any observations are duplicated across
     *   multiple processes, the elements of `x` corresponding to these data are included only
     *   once).
     */
    virtual void allGatherv(std::vector<size_t> &x) const = 0;
    virtual void allGatherv(std::vector<int> &x) const = 0;
    virtual void allGatherv(std::vector<float> &x) const = 0;
    virtual void allGatherv(std::vector<double> &x) const = 0;
    virtual void allGatherv(std::vector<util::DateTime> &x) const = 0;
    virtual void allGatherv(std::vector<std::string> &x) const = 0;

    /*!
     * \brief Map the index of a location held on the calling process to the index of the
     * corresponding element of any vector produced by allGatherv().
     */
    virtual size_t globalUniqueConsecutiveLocationIndex(size_t loc) const = 0;

    // return the name of the distribution
    virtual std::string name() const = 0;

    /// Deprecated accessor to MPI communicator (added temporarily, to be removed soon, May 2021)
    const eckit::mpi::Comm & comm() const {return comm_;}

 protected:
     /*! \brief Local MPI communicator */
     const eckit::mpi::Comm & comm_;
};

}  // namespace ioda

#endif  // DISTRIBUTION_DISTRIBUTION_H_
