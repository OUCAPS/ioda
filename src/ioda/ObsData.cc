/*
 * (C) Copyright 2017-2019 UCAR
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#include "ioda/ObsData.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <vector>

#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#include <boost/stacktrace.hpp>

#include "eckit/config/Configuration.h"
#include "eckit/exception/Exceptions.h"

#include "oops/parallel/mpi/mpi.h"
#include "oops/util/abor1_cpp.h"
#include "oops/util/DateTime.h"
#include "oops/util/Duration.h"
#include "oops/util/Logger.h"
#include "oops/util/Random.h"

#include "distribution/DistributionFactory.h"
#include "fileio/IodaIOfactory.h"

namespace ioda {

// -----------------------------------------------------------------------------
/*!
 * \details Config based constructor for an ObsData object. This constructor will read
 *          in from the obs file and transfer the variables into the obs container. Obs
 *          falling outside the DA timing window, specified by bgn and end, will be
 *          discarded before storing them in the obs container.
 *
 * \param[in] config ECKIT configuration segment holding obs types specs
 * \param[in] bgn    DateTime object holding the start of the DA timing window
 * \param[in] end    DateTime object holding the end of the DA timing window
 */
ObsData::ObsData(const eckit::Configuration & config,
                   const util::DateTime & bgn, const util::DateTime & end)
  : oops::ObsSpaceBase(config, bgn, end),
    winbgn_(bgn), winend_(end), commMPI_(oops::mpi::comm()),
    database_(), obsvars_()
{
  oops::Log::trace() << "ioda::ObsData config  = " << config << std::endl;

  obsname_ = config.getString("name");
  nwarns_fdtype_ = 0;
  distname_ = config.getString("distribution", "RoundRobin");

  eckit::LocalConfiguration varconfig(config, "simulate");
  obsvars_ = oops::Variables(varconfig);
  oops::Log::info() << obsname_ << " vars: " << obsvars_ << std::endl;

  // Initialize the obs space container
  if (config.has("ObsDataIn")) {
    // Initialize the container from an input obs file
    obs_group_variable_ = config.getString("ObsDataIn.obsgrouping.group_variable", "");
    obs_sort_variable_ = config.getString("ObsDataIn.obsgrouping.sort_variable", "");
    obs_sort_order_ = config.getString("ObsDataIn.obsgrouping.sort_order", "ascending");
    if ((obs_sort_order_ != "ascending") && (obs_sort_order_ != "descending")) {
      std::string ErrMsg =
        std::string("ObsData::ObsData: Must use one of 'ascending' or 'descending' ") +
        std::string("for the 'sort_order:' YAML configuration keyword.");
      ABORT(ErrMsg);
    }
    filein_ = config.getString("ObsDataIn.obsfile");
    oops::Log::trace() << obsname_ << " file in = " << filein_ << std::endl;
    InitFromFile(filein_);
    if (obs_sort_variable_ != "") {
      BuildSortedObsGroups();
    }
  } else if (config.has("Generate")) {
    // Initialize the container from the generateDistribution method
    eckit::LocalConfiguration genconfig(config, "Generate");
    generateDistribution(genconfig);
  } else {
    // Error - must have one of ObsDataIn or Generate
    std::string ErrorMsg =
      "ObsData::ObsData: Must use one of 'ObsDataIn' or 'Generate' in the YAML configuration.";
    ABORT(ErrorMsg);
  }

  // Check to see if an output file has been requested.
  if (config.has("ObsDataOut.obsfile")) {
    std::string filename = config.getString("ObsDataOut.obsfile");

    // Find the left-most dot in the file name, and use that to pick off the file name
    // and file extension.
    std::size_t found = filename.find_last_of(".");
    if (found == std::string::npos)
      found = filename.length();

    // Get the process rank number and format it
    std::ostringstream ss;
    ss << "_" << std::setw(4) << std::setfill('0') << comm().rank();

    // Construct the output file name
    fileout_ = filename.insert(found, ss.str());

    // Check to see if user is trying to overwrite an existing file. For now always allow
    // the overwrite, but issue a warning if we are about to clobber an existing file.
    std::ifstream infile(fileout_);
    if (infile.good() && (commMPI_.rank() == 0)) {
        oops::Log::warning() << "ioda::ObsData WARNING: Overwriting output file "
                             << fileout_ << std::endl;
      }
  } else {
    oops::Log::debug() << "ioda::ObsData output file is not required " << std::endl;
  }

  oops::Log::trace() << "ioda::ObsData contructed name = " << obsname() << std::endl;
}

// -----------------------------------------------------------------------------
/*!
 * \details Destructor for an ObsData object. This destructor will clean up the ObsData
 *          object and optionally write out the contents of the obs container into
 *          the output file. The save-to-file operation is invoked when an output obs
 *          file is specified in the ECKIT configuration segment associated with the
 *          ObsData object.
 */
ObsData::~ObsData() {
  oops::Log::trace() << "ioda::ObsData destructor begin" << std::endl;
  if (fileout_.size() != 0) {
    oops::Log::info() << obsname() << ": save database to " << fileout_ << std::endl;
    SaveToFile(fileout_);
  } else {
    oops::Log::info() << obsname() << " :  no output" << std::endl;
  }
  oops::Log::trace() << "ioda::ObsData destructor end" << std::endl;
}

// -----------------------------------------------------------------------------
// NOTES CONCERNING THE get_db methods below.
//
// What we want to end up with is deliberate conversion between double outside
// IODA to float in the ObsSpaceContainer database. This is done to save memory
// space because it is not necessary to store data with double precision.
//
// Given that, we want the structure of the get_db methods below to eventually
// look like that of the put_db methods. That is the overloaded functions of
// get_db simply call the templated get_db_helper method, ecexpt a float to double
// conversion is done in the get_db double case, and the get_db_helper method
// is a three-liner that just calls database_.LoadFromDb directly.
//
// Currently, we have some cases of the wrong data types trying to load from the
// database, such as trying to put QC marks (integers stored in the database)
// into a double in an ObsVector. To deal with this we've got checks for the
// proper data types in the get_db_helper method that will issue warnings if
// the variable we are trying to load into has a type that does not match the
// corresponding entry in the database. In these cases, after the warning is
// issued, a conversion is done including getting the missing value marks
// converted properly.
//
// The idea is to get all of these warnings that come up fixed, and once that
// is done, we will turn any mismatched types into an error and the program
// will abort. When we hit this point we should change the code in
// get_db_helper to:
//
//     std::string gname = (group.size() <= 0)? "GroupUndefined" : group;
//     std::vector<std::size_t> vshape(1, vsize);
//     database_.LoadFromDb(gname, name, vshape, &vdata[0]);
//
// I.e., make get_db_helper analogous to put_db_helper. Then we can get rid
// of the template specialization for the util::DateTime type. And we can
// allow the boost::bad_any_cast catch routine in ObsSpaceContainer.cc handle
// the type check (and abort if the types don't match).
//
// Note that when a variable is a double and the database has an int, this
// code is doing two conversions and is therefore inefficient. This is okay
// for now because once the variable type not matching the database type
// issues are fixed we will eliminate one of the conversions.
// -----------------------------------------------------------------------------
/*!
 * \brief transfer data from the obs container to vdata
 *
 * \details The following four get_db methods are the same except for the data type
 *          of the data being transferred (integer, float, double, DateTime). The
 *          caller needs to allocate the memory that the vdata parameter points to.
 *
 * \param[in]  group Name of container group (ObsValue, ObsError, MetaData, etc.)
 * \param[in]  name  Name of container variable
 * \param[in]  vsize Total number of elements in variable (i.e., length of vdata)
 * \param[out] vdata Pointer to memory where container data is being transferred to
 */

void ObsData::get_db(const std::string & group, const std::string & name,
                      const std::size_t vsize, int vdata[]) const {
  get_db_helper<int>(group, name, vsize, vdata);
}

void ObsData::get_db(const std::string & group, const std::string & name,
                      const std::size_t vsize, float vdata[]) const {
  get_db_helper<float>(group, name, vsize, vdata);
}

void ObsData::get_db(const std::string & group, const std::string & name,
                      const std::size_t vsize, double vdata[]) const {
  // load the float values from the database and convert to double
  std::unique_ptr<float[]> FloatData(new float[vsize]);
  get_db_helper<float>(group, name, vsize, FloatData.get());
  ConvertVarType<float, double>(FloatData.get(), &vdata[0], vsize);
}

void ObsData::get_db(const std::string & group, const std::string & name,
                      const std::size_t vsize, util::DateTime vdata[]) const {
  get_db_helper<util::DateTime>(group, name, vsize, vdata);
}

/*!
 * \brief Helper method for get_db
 *
 * \details This method fills in the code for the four overloaded get_db methods.
 *          This method handles data type conversions, and the transfer of data
 *          from the container to vdata.
 *
 * NOTE: Some data type conversions are to be eventually eliminated, such as
 *       between double and int for QC marks, and warnings are issued when these occur.
 *       Once the need for these type conversions is eliminated across the whole system,
 *       these type mismatches will become errors and the type mismatch will need to be
 *       repaired.
 *
 * \param[in]  group Name of container group (ObsValue, ObsError, MetaData, etc.)
 * \param[in]  name  Name of container variable
 * \param[in]  vsize Total number of elements in variable (i.e., length of vdata)
 * \param[out] vdata Pointer to memory where container data is being transferred to
 */

template <typename DATATYPE>
void ObsData::get_db_helper(const std::string & group, const std::string & name,
                             const std::size_t vsize, DATATYPE vdata[]) const {
  std::string gname = (group.size() <= 0)? "GroupUndefined" : group;
  std::vector<std::size_t> vshape(1, vsize);

  // Check to see if the requested variable type matches the type stored in
  // the database. If these are different, issue warnings and do the conversion.
  const std::type_info & VarType = typeid(DATATYPE);
  const std::type_info & DbType = database_.dtype(gname, name);
  if (DbType == typeid(void)) {
    std::string ErrorMsg = "ObsData::get_db: " + name + " @ " + gname +
                           " not found in database.";
    ABORT(ErrorMsg);
  } else {
    // Check for type mis-match between var type and the type of the database
    // entry. If a conversion is necessary, do it and issue a warning so this
    // situation can be fixed.
    if (VarType == DbType) {
      database_.LoadFromDb(gname, name, vshape, &vdata[0]);
    } else {
      if (DbType == typeid(int)) {
        // Trying to load int into something else
        LoadFromDbConvert<int, DATATYPE>(gname, name, vshape, vsize, &vdata[0]);
      } else if (DbType == typeid(float)) {
        // Trying to load float into something else
        LoadFromDbConvert<float, DATATYPE>(gname, name, vshape, vsize, &vdata[0]);
      } else {
        // Let the bad cast check catch this one.
        database_.LoadFromDb(gname, name, vshape, &vdata[0]);
      }
    }
  }
}

/*!
 * \brief Helper method for get_db (specialized for DateTime type)
 *
 * \details This method fills in the code for the get_db methods that is transferring
 *          DateTime data. This is here because there isn't a way to convert between
 *          numeric data and DateTime objects. 
 *
 * \param[in]  group Name of container group (ObsValue, ObsError, MetaData, etc.)
 * \param[in]  name  Name of container variable
 * \param[in]  vsize Total number of elements in variable (i.e., length of vdata)
 * \param[out] vdata Pointer to memory where container data is being transferred to
 */
template <>
void ObsData::get_db_helper<util::DateTime>(const std::string & group,
         const std::string & name, const std::size_t vsize, util::DateTime vdata[]) const {
  std::string gname = (group.size() <= 0)? "GroupUndefined" : group;
  std::vector<std::size_t> vshape(1, vsize);
  database_.LoadFromDb(gname, name, vshape, &vdata[0]);
}

// -----------------------------------------------------------------------------
/*!
 * \brief transfer data from vdata to the obs container
 *
 * \details The following four put_db methods are the same except for the data type
 *          of the data being transferred (integer, float, double, DateTime). The
 *          caller needs to allocate and assign the memory that the vdata parameter
 *          points to.
 *
 * \param[in]  group Name of container group (ObsValue, ObsError, MetaData, etc.)
 * \param[in]  name  Name of container variable
 * \param[in]  vsize Total number of elements in variable (i.e., length of vdata)
 * \param[out] vdata Pointer to memory where container data is being transferred to
 */

void ObsData::put_db(const std::string & group, const std::string & name,
                      const std::size_t vsize, const int vdata[]) {
  put_db_helper<int>(group, name, vsize, vdata);
}

void ObsData::put_db(const std::string & group, const std::string & name,
                      const std::size_t vsize, const float vdata[]) {
  put_db_helper<float>(group, name, vsize, vdata);
}

void ObsData::put_db(const std::string & group, const std::string & name,
                      const std::size_t vsize, const double vdata[]) {
  // convert to float, then load into the database
  std::unique_ptr<float[]> FloatData(new float[vsize]);
  ConvertVarType<double, float>(&vdata[0], FloatData.get(), vsize);
  put_db_helper<float>(group, name, vsize, FloatData.get());
}

void ObsData::put_db(const std::string & group, const std::string & name,
                      const std::size_t vsize, const util::DateTime vdata[]) {
  put_db_helper<util::DateTime>(group, name, vsize, vdata);
}

/*!
 * \brief Helper method for put_db
 *
 * \details This method fills in the code for the four overloaded put_db methods.
 *          This method handles data type conversions, and the transfer of data
 *          from vdata to the container
 *
 * NOTE: Some data type conversions are to be eventually eliminated, such as
 *       between double and int for QC marks, and warnings are issued when these occur.
 *       Once the need for these type conversions is eliminated across the whole system,
 *       these type mismatches will become errors and the type mismatch will need to be
 *       repaired.
 *
 * \param[in]  group Name of container group (ObsValue, ObsError, MetaData, etc.)
 * \param[in]  name  Name of container variable
 * \param[in]  vsize Total number of elements in variable (i.e., length of vdata)
 * \param[out] vdata Pointer to memory where container data is being transferred to
 */

template <typename DATATYPE>
void ObsData::put_db_helper(const std::string & group, const std::string & name,
                             const std::size_t vsize, const DATATYPE vdata[]) {
  std::string gname = (group.size() <= 0)? "GroupUndefined" : group;
  std::vector<std::size_t> vshape(1, vsize);
  database_.StoreToDb(gname, name, vshape, &vdata[0]);
}

// -----------------------------------------------------------------------------
/*!
 * \details This method checks for the existence of the group, name combination
 *          in the obs container. If the combination exists, "true" is returned,
 *          otherwise "false" is returned.
 */

bool ObsData::has(const std::string & group, const std::string & name) const {
  return database_.has(group, name);
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the number of unique locations in the input
 *          obs file. Note that nlocs from the obs container may be smaller
 *          than nlocs from the input obs file due to the removal of obs outside
 *          the DA timing window and/or due to distribution of obs across
 *          multiple process elements.
 */
std::size_t ObsData::gnlocs() const {
  return gnlocs_;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the number of unique locations in the obs
 *          container. Note that nlocs from the obs container may be smaller
 *          than nlocs from the input obs file due to the removal of obs outside
 *          the DA timing window and/or due to distribution of obs across
 *          multiple process elements.
 */
std::size_t ObsData::nlocs() const {
  return nlocs_;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the number of unique records in the obs
 *          container. A record is an atomic unit of locations that belong
 *          together such as a single radiosonde sounding.
 */
std::size_t ObsData::nrecs() const {
  return nrecs_;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the number of unique variables in the obs
 *          container. "Variables" refers to the quantities that can be
 *          assimilated as opposed to meta data.
 */
std::size_t ObsData::nvars() const {
  return nvars_;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns a reference to the record number vector
 *          data member. This is for read only access.
 */
const std::vector<std::size_t> & ObsData::recnum() const {
  return recnums_;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns a reference to the index vector
 *          data member. This is for read only access.
 */
const std::vector<std::size_t> & ObsData::index() const {
  return indx_;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the begin iterator associated with the
 *          recidx_ data member.
 */
const ObsData::RecIdxIter ObsData::recidx_begin() const {
  return recidx_.begin();
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the end iterator associated with the
 *          recidx_ data member.
 */
const ObsData::RecIdxIter ObsData::recidx_end() const {
  return recidx_.end();
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns a boolean value indicating whether the
 *          given record number exists in the recidx_ data member.
 */
bool ObsData::recidx_has(const std::size_t RecNum) const {
  RecIdxIter irec = recidx_.find(RecNum);
  return (irec != recidx_.end());
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the current record number, pointed to by the
 *          given iterator, from the recidx_ data member.
 */
std::size_t ObsData::recidx_recnum(const RecIdxIter & Irec) const {
  return Irec->first;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the current vector, pointed to by the
 *          given iterator, from the recidx_ data member.
 */
const std::vector<std::size_t> & ObsData::recidx_vector(const RecIdxIter & Irec) const {
  return Irec->second;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the current vector, pointed to by the
 *          given iterator, from the recidx_ data member.
 */
const std::vector<std::size_t> & ObsData::recidx_vector(const std::size_t RecNum) const {
  RecIdxIter Irec = recidx_.find(RecNum);
  if (Irec == recidx_.end()) {
    std::string ErrMsg =
      "ObsData::recidx_vector: Record number, " + std::to_string(RecNum) +
      ", does not exist in record index map.";
    ABORT(ErrMsg);
  }
  return Irec->second;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method returns the all of the record numbers from the
 *          recidx_ data member (ie, all the key values) in a vector.
 */
std::vector<std::size_t> ObsData::recidx_all_recnums() const {
  std::vector<std::size_t> RecNums(nrecs_);
  std::size_t recnum = 0;
  for (RecIdxIter Irec = recidx_.begin(); Irec != recidx_.end(); ++Irec) {
    RecNums[recnum] = Irec->first;
    recnum++;
  }
  return RecNums;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method will generate a set of latitudes and longitudes of which
 *          can be used for testing without reading in an obs file. Two latitude
 *          values, two longitude values, the number of locations (nobs keyword)
 *          and an optional random seed are specified in the configuration given
 *          by the conf parameter. Random locations between the two latitudes and
 *          two longitudes are generated and stored in the obs container as meta data.
 *          Random time stamps that fall inside the given timing window (which is
 *          specified in the configuration file) are alos generated and stored
 *          in the obs container as meta data.
 *
 * \param[in] conf ECKIT configuration segment built from an input configuration file.
 */
void ObsData::generateDistribution(const eckit::Configuration & conf) {
  gnlocs_  = conf.getInt("nobs");
  float lat1 = conf.getFloat("lat1");
  float lat2 = conf.getFloat("lat2");
  float lon1 = conf.getFloat("lon1");
  float lon2 = conf.getFloat("lon2");

  // Make the random_seed keyword optional. Can spec it for testing to get repeatable
  // values, and the user doesn't have to spec it if they want subsequent runs to
  // use different random sequences.
  unsigned int ran_seed;
  if (conf.has("random_seed")) {
    ran_seed = conf.getInt("random_seed");
  } else {
    ran_seed = std::time(0);  // based on the current date/time.
  }

  // number of variables specified in simulate section
  nvars_ = obsvars_.size();

  // Create the MPI Distribution
  // The default constructor for std::unique_ptr generates a null ptr which
  // can be tested in GenMpiDistribution.
  std::unique_ptr<IodaIO> NoIO;
  GenMpiDistribution(NoIO);

  // Use the following formula to generate random lat, lon and time values.
  //
  //   val = val1 + (random_number_between_0_and_1 * (val2-val1))
  //
  // where val2 > val1.
  //
  // Create a list of random values between 0 and 1 to be used for genearting
  // random lat, lon and time vaules.
  //
  // Use different seeds for lat and lon so that in the case where lat and lon ranges
  // are the same, you get a different sequences for lat compared to lon.
  //
  // Have rank 0 generate the full length random sequences, and then
  // broadcast these to the other ranks. This ensures that every rank
  // contains the same random sequences. If all ranks generated their
  // own sequences, which they could do, the sequences between ranks
  // would be different in the case where random_seed is not specified.
  std::vector<float> RanVals(gnlocs_, 0.0);
  std::vector<float> RanVals2(gnlocs_, 0.0);
  if (comm().rank() == 0) {
    util::UniformDistribution<float> RanUD(gnlocs_, 0.0, 1.0, ran_seed);
    util::UniformDistribution<float> RanUD2(gnlocs_, 0.0, 1.0, ran_seed+1);

    RanVals = RanUD.data();
    RanVals2 = RanUD2.data();
  }
  comm().broadcast(RanVals, 0);
  comm().broadcast(RanVals2, 0);

  // Form the ranges val2-val for lat, lon, time
  float LatRange = lat2 - lat1;
  float LonRange = lon2 - lon1;
  util::Duration WindowDuration(this->windowEnd() - this->windowStart());
  float TimeRange = static_cast<float>(WindowDuration.toSeconds());

  // Read obs errors (one for each variable)
  const std::vector<float> err = conf.getFloatVector("obs_errors");
  ASSERT(nvars_ == err.size());

  // Create vectors for lat, lon, time, fill them with random values
  // inside their respective ranges, and put results into the obs container.
  std::vector<float> latitude(nlocs_);
  std::vector<float> longitude(nlocs_);
  std::vector<util::DateTime> obs_datetimes(nlocs_);

  util::Duration DurZero(0);
  util::Duration DurOneSec(1);
  for (std::size_t ii = 0; ii < nlocs_; ii++) {
    std::size_t index = indx_[ii];
    latitude[ii] = lat1 + (RanVals[index] * LatRange);
    longitude[ii] = lon1 + (RanVals2[index] * LonRange);

    // Currently the filter for time stamps on obs values is:
    //
    //     windowStart < ObsTime <= windowEnd
    //
    // If we get a zero OffsetDt, then change it to 1 second so that the observation
    // will remain inside the timing window.
    util::Duration OffsetDt(static_cast<int64_t>(RanVals[index] * TimeRange));
    if (OffsetDt == DurZero) {
      OffsetDt = DurOneSec;
    }
    obs_datetimes[ii] = this->windowStart() + OffsetDt;
  }

  put_db("MetaData", "datetime", nlocs_, obs_datetimes.data());
  put_db("MetaData", "latitude", nlocs_, latitude.data());
  put_db("MetaData", "longitude", nlocs_, longitude.data());
  for (std::size_t ivar = 0; ivar < nvars_; ivar++) {
    std::vector<float> obserr(nlocs_, err[ivar]);
    put_db("ObsError", obsvars_[ivar], nlocs_, obserr.data());
  }
}

// -----------------------------------------------------------------------------
/*!
 * \details This method provides a way to print an ObsData object in an output
 *          stream. It simply prints a dummy message for now.
 */
void ObsData::print(std::ostream & os) const {
  os << "ObsData::print not implemented";
}

// -----------------------------------------------------------------------------
/*!
 * \details This method will initialize the obs container from the input obs file.
 *          All the variables from the input file will be read in and loaded into
 *          the obs container. Obs that fall outside the DA timing window will be
 *          filtered out before loading into the container. This method will also
 *          apply obs distribution across multiple process elements. For these reasons,
 *          the number of locations in the obs container may be smaller than the
 *          number of locations in the input obs file.
 *
 * \param[in] filename Path to input obs file
 */
void ObsData::InitFromFile(const std::string & filename) {
  oops::Log::trace() << "ioda::ObsData opening file: " << filename << std::endl;

  // Open the file for reading and record nlocs and nvars from the file.
  std::unique_ptr<IodaIO> fileio {ioda::IodaIOfactory::Create(filename, "r")};
  gnlocs_ = fileio->nlocs();

  // Create the MPI distribution
  GenMpiDistribution(fileio);

  // Reject observations that fall outside the DA timing window. Do this by removing
  // any locations from indx_ and recnums_ that fall outside the window.
  ApplyTimingWindow(fileio);

  // Read in all variables from the file and store them into the database.
  nvars_ = 0;
  for (IodaIO::GroupIter igrp = fileio->group_begin();
                         igrp != fileio->group_end(); ++igrp) {
    std::string GroupName = fileio->group_name(igrp);
    for (IodaIO::VarIter ivar = fileio->var_begin(igrp);
                         ivar != fileio->var_end(igrp); ++ivar) {
      std::string VarName = fileio->var_name(ivar);
      std::string FileVarType = fileio->var_dtype(ivar);

      // nvars_ is equal to the number of variables in the ObsValue group
      if (GroupName == "ObsValue") {
        nvars_++;
      }

      // VarShape, VarSize hold dimension sizes from file.
      // AdjVarShape, AdjVarSize hold dimension sizes needed when the
      // fisrt dimension is nlocs in size. Ie, all variables with nlocs
      // as the first dimension need to be distributed across that dimension.
      std::vector<std::size_t> VarShape = fileio->var_shape(ivar);
      std::size_t VarSize =
        std::accumulate(VarShape.begin(), VarShape.end(), 1, std::multiplies<std::size_t>());

      // Get the desired data type for the database.
      std::string DbVarType = DesiredVarType(GroupName, FileVarType);

      // Read the variable from the file and transfer it to the database.
      if (FileVarType == "int") {
        std::unique_ptr<int[]> FileData(new int[VarSize]);
        fileio->ReadVar(GroupName, VarName, VarShape, FileData.get());

        std::unique_ptr<int[]> IndexedData;
        std::vector<std::size_t> IndexedShape;
        std::size_t IndexedSize;
        ApplyDistIndex<int>(FileData, VarShape, IndexedData, IndexedShape, IndexedSize);
        database_.StoreToDb(GroupName, VarName, IndexedShape, IndexedData.get());
      } else if (FileVarType == "float") {
        std::unique_ptr<float[]> FileData(new float[VarSize]);
        fileio->ReadVar(GroupName, VarName, VarShape, FileData.get());

        std::unique_ptr<float[]> IndexedData;
        std::vector<std::size_t> IndexedShape;
        std::size_t IndexedSize;
        ApplyDistIndex<float>(FileData, VarShape, IndexedData, IndexedShape, IndexedSize);

        if (DbVarType == "int") {
          ConvertStoreToDb<float, int>(GroupName, VarName, IndexedShape,
                                     IndexedSize, IndexedData.get());
        } else {
          database_.StoreToDb(GroupName, VarName, IndexedShape, IndexedData.get());
        }
      } else if (FileVarType == "double") {
        // Convert double to float before storing into the database.
        std::unique_ptr<double[]> FileData(new double[VarSize]);
        fileio->ReadVar(GroupName, VarName, VarShape, FileData.get());

        std::unique_ptr<double[]> IndexedData;
        std::vector<std::size_t> IndexedShape;
        std::size_t IndexedSize;
        ApplyDistIndex<double>(FileData, VarShape, IndexedData, IndexedShape, IndexedSize);

        ConvertStoreToDb<double, float>(GroupName, VarName, IndexedShape,
                                     IndexedSize, IndexedData.get());
      } else if (FileVarType == "char") {
        // Convert the char array to a vector of strings. If we are working
        // on the variable "datetime", then convert the strings to DateTime
        // objects.
        std::unique_ptr<char[]> FileData(new char[VarSize]);
        fileio->ReadVar(GroupName, VarName, VarShape, FileData.get());

        std::unique_ptr<char[]> IndexedData;
        std::vector<std::size_t> IndexedShape;
        std::size_t IndexedSize;
        ApplyDistIndex<char>(FileData, VarShape, IndexedData, IndexedShape, IndexedSize);

        std::vector<std::string> StringData =
               CharArrayToStringVector(IndexedData.get(), IndexedShape);
        std::vector<std::size_t> AdjVarShape;
        std::size_t AdjVarSize = 1;
        for (std::size_t j = 0; j < (IndexedShape.size()-1); j++) {
          AdjVarShape.push_back(IndexedShape[j]);
          AdjVarSize *= IndexedShape[j];
        }

        if (VarName == "datetime") {
          std::vector<util::DateTime> DtData(AdjVarSize);
          for (std::size_t j = 0; j < AdjVarSize; j++) {
            util::DateTime TempDt(StringData[j]);
            DtData[j] = TempDt;
          }
          database_.StoreToDb(GroupName, VarName, AdjVarShape, DtData.data());
        } else {
          database_.StoreToDb(GroupName, VarName, AdjVarShape, StringData.data());
        }
      } else if (commMPI_.rank() == 0) {
        oops::Log::warning() << "ioda::IodaIO::InitFromFile: Unrecognized file data type: "
                             << FileVarType << std::endl;
        oops::Log::warning() << "  File IO Currently supports data types int, float and char."
                             << std::endl;
        oops::Log::warning() << "  Skipping read of " << VarName << " @ " << GroupName
                             << " from the input file." << std::endl;
      }
    }
  }
  oops::Log::trace() << "ioda::ObsSpaceContainer opening file ends " << std::endl;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method generates an list of indices with their corresponding
 *          record numbers, where the indices denote which locations are to be
 *          read into this process element.
 *
 *          This routine sets up record grouping, and is also responsible for
 *          setting the nrecs_, nlocs_, indx_ and recnums_ data members.
 *
 * \param[in] FileIO File id (pointer to IodaIO object)
 */
void ObsData::GenMpiDistribution(const std::unique_ptr<IodaIO> & FileIO) {
  // Apply the MPI distribution. If we are initializing from a file (FileIO is
  // not null), then generate records numbers based on the specified variable
  // in the input file. Otherwise, use default grouping.
  std::unique_ptr<DistributionFactory> distFactory;
  std::unique_ptr<Distribution> dist;
  if (FileIO) {
    // Grouping based on variable in the input file.
    std::vector<std::size_t> Records(gnlocs_);
    GenRecordNumbers(FileIO, Records);
    dist.reset(distFactory->createDistribution(comm(), gnlocs_, distname_, Records));
  } else {
    // Default grouping (every location is a separate record)
    dist.reset(distFactory->createDistribution(comm(), gnlocs_, distname_));
  }
  dist->distribution();

  // The Distribution::distribution() method calculates data needed for
  // the nrecs_, nlocs_, indx_ and recnums_ data members.
  nlocs_ = dist->nlocs();
  nrecs_ = dist->nrecs();
  indx_ = dist->index();
  recnums_ = dist->recnum();
}

// -----------------------------------------------------------------------------
/*!
 * \details This method calculates the record numbers according to the specs in
 *          the YAML file. If the specification of a variable that denotes the
 *          grouping is present, then that variable is read from the input file
 *          and the records numbers are based on the unique values in that variable.
 *          If the specification is not present, then the record numbers are set
 *          to 0..(nlocs-1) which makes each location a separate record effectively
 *          disabling grouping.
 *
 * \param[in] FileIO File id (pointer to IodaIO object)
 * \param[out] Records Vector of record numbers
 */
void ObsData::GenRecordNumbers(const std::unique_ptr<IodaIO> & FileIO,
                                std::vector<std::size_t> & Records) const {
  // Collect the group and variable names that came from the configuration
  std::string GroupName = "MetaData";
  std::string VarName = obs_group_variable_;

  // Construct the group numbers
  if (VarName.empty()) {
    // Grouping is not specified, so place 0..(nlocs-1) in the Records vector.
    // This effectively disables grouping (each location is a separate group).
    std::iota(Records.begin(), Records.end(), 0);
  } else {
    // Grouping is based on GroupName, VarName. Read in the variable and make
    // two passes through the values. First pass is to determine the unique
    // values of which group numbers will be assigned 0..(number_of_unique_vals-1).
    // Second pass is to generate the group numbers in the same order as the
    // values occur in the variable read in.
    std::string VarType = FileIO->var_dtype(GroupName, VarName);
    std::vector<std::size_t> VarShape = FileIO->var_shape(GroupName, VarName);
    std::size_t VarSize =
      std::accumulate(VarShape.begin(), VarShape.end(), 1, std::multiplies<std::size_t>());

    if (VarType == "int") {
      std::vector<int> FileData(VarSize);
      FileIO->ReadVar(GroupName, VarName, VarShape, FileData.data());
      GenRnumsFromVar<int>(FileData, Records);
    } else if (VarType == "float") {
      std::vector<float> FileData(VarSize);
      FileIO->ReadVar(GroupName, VarName, VarShape, FileData.data());
      GenRnumsFromVar<float>(FileData, Records);
    } else if (VarType == "char") {
      std::vector<char> FileData(VarSize);
      FileIO->ReadVar(GroupName, VarName, VarShape, FileData.data());
      std::vector<std::string> StringData = CharArrayToStringVector(FileData.data(), VarShape);
      GenRnumsFromVar<std::string>(StringData, Records);
    }
  }
}

// -----------------------------------------------------------------------------
/*!
 * \details This method will generate a set of unique group numbers that go
 *          in sequence from 0 to number_of_unique_values-1. All of the unique
 *          values in VarData are extracted and assigned a unique group number.
 *
 * \param[in] VarData Vector of variable data.
 * \param[out] Records Vector of group numbers.
 */
template<typename DATATYPE>
void ObsData::GenRnumsFromVar(const std::vector<DATATYPE> & VarData,
                               std::vector<std::size_t> & Records) {
  typedef std::map<DATATYPE, std::size_t> VGMap;
  typedef typename VGMap::iterator VGMapIter;
  // Form a map from VarData values to group number.
  //
  // First, collect all of the unique key values. Then go back and fill in the
  // values in the map with numbers going from 0 to number_of_unique_values-1.
  VGMap ValueToGroupNum;
  for (std::size_t i = 0; i < VarData.size(); i++) {
    ValueToGroupNum[VarData[i]] = 0;
  }

  std::size_t Gnum = 0;
  for (VGMapIter imap = ValueToGroupNum.begin();
                 imap != ValueToGroupNum.end(); ++imap) {
    ValueToGroupNum[imap->first] = Gnum;
    Gnum++;
  }

  // Use the map to translate the VarData values into their associated group
  // numbers
  for (std::size_t i = 0; i < VarData.size(); i++) {
    Records[i] = ValueToGroupNum[VarData[i]];
  }
}

// -----------------------------------------------------------------------------
/*!
 * \details This method will save the contents of the obs container into the
 *          given file. Currently, all variables in the obs container are written
 *          into the file. This may change in the future where we can select which
 *          variables we want saved.
 *
 * \param[in] file_name Path to output obs file.
 */
void ObsData::ApplyTimingWindow(const std::unique_ptr<IodaIO> & FileIO) {
  // Read in the datetime values and filter out any variables outside the
  // timing window.
  std::unique_ptr<char[]> DtCharArray(new char[gnlocs_ * 20]);
  std::vector<std::size_t> DtShape{ gnlocs_, 20 };

  // Look for datetime@MetaData first, then datetime@GroupUndefined
  std::string DtGroupName = "MetaData";
  std::string DtVarName = "datetime";
  if (!FileIO->grp_var_exists(DtGroupName, DtVarName)) {
    DtGroupName = "GroupUndefined";
    if (!FileIO->grp_var_exists(DtGroupName, DtVarName)) {
      std::string ErrorMsg = "ObsData::InitFromFile: datetime information is not available";
      ABORT(ErrorMsg);
    }
  }
  FileIO->ReadVar(DtGroupName, DtVarName, DtShape, DtCharArray.get());
  std::vector<std::string> DtStrings =
           CharArrayToStringVector(DtCharArray.get(), DtShape);

  std::size_t Index;
  std::size_t RecNum;
  std::set<std::size_t> UniqueRecNums;
  std::vector<std::size_t> NewIndices;
  std::vector<std::size_t> NewRecNums;
  for (std::size_t ii = 0; ii < nlocs_; ii++) {
    Index = indx_[ii];
    RecNum = recnums_[ii];
    util::DateTime TestDt(DtStrings[Index]);
    if ((TestDt > winbgn_) && (TestDt <= winend_)) {
      // Inside the DA time window, keep this index
      // and associated record number
      NewIndices.push_back(Index);
      NewRecNums.push_back(RecNum);
      UniqueRecNums.insert(RecNum);
    }
  }

  // Save adjusted counts, etc.
  nlocs_ = NewIndices.size();
  nrecs_ = UniqueRecNums.size();
  indx_ = NewIndices;
  recnums_ = NewRecNums;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method will construct a data structure that holds the
 *          location order within each group sorted by the values of
 *          the specified sort variable.
 */
void ObsData::BuildSortedObsGroups() {
  typedef std::map<std::size_t, std::vector<std::pair<float, std::size_t>>> TmpRecIdxMap;
  typedef TmpRecIdxMap::iterator TmpRecIdxIter;

  // Get the sort variable from the data store, and convert to a vector of floats.
  std::vector<float> SortValues(nlocs_);
  if (obs_sort_variable_ == "datetime") {
    std::vector<util::DateTime> Dates(nlocs_);
    get_db("MetaData", obs_sort_variable_, nlocs_, Dates.data());
    for (std::size_t iloc = 0; iloc < nlocs_; iloc++) {
      SortValues[iloc] = (Dates[iloc] - Dates[0]).toSeconds();
    }
  } else {
    get_db("MetaData", obs_sort_variable_, nlocs_, SortValues.data());
  }

  // Construct a temporary structure to do the sorting, then transfer the results
  // to the data member recidx_.
  TmpRecIdxMap TmpRecIdx;
  for (size_t iloc = 0; iloc < nlocs_; iloc++) {
    TmpRecIdx[recnums_[iloc]].push_back(std::make_pair(SortValues[iloc], iloc));
  }

  for (TmpRecIdxIter irec = TmpRecIdx.begin(); irec != TmpRecIdx.end(); ++irec) {
    if (obs_sort_order_ == "ascending") {
      sort(irec->second.begin(), irec->second.end());
    } else {
      // Use a lambda function to access the std::pair greater-than operator to
      // implement a descending order sort.
      sort(irec->second.begin(), irec->second.end(),
           [](const std::pair<float, std::size_t> & p1,
              const std::pair<float, std::size_t> & p2){ return(p1 > p2); } );
    }
  }

  // Copy indexing to the recidx_ data member.
  for (TmpRecIdxIter irec = TmpRecIdx.begin(); irec != TmpRecIdx.end(); ++irec) {
    recidx_[irec->first].resize(irec->second.size());
    for (std::size_t iloc = 0; iloc < irec->second.size(); iloc++) {
      recidx_[irec->first][iloc] = irec->second[iloc].second;
    }
  }
}

// -----------------------------------------------------------------------------
/*!
 * \details This method will save the contents of the obs container into the
 *          given file. Currently, all variables in the obs container are written
 *          into the file. This may change in the future where we can select which
 *          variables we want saved.
 *
 * \param[in] file_name Path to output obs file.
 */
void ObsData::SaveToFile(const std::string & file_name) {
  // Open the file for output
  std::unique_ptr<IodaIO> fileio
    {ioda::IodaIOfactory::Create(file_name, "W", nlocs_, nrecs_, nvars_)};

  // List all records and write out the every record
  for (ObsSpaceContainer::VarIter ivar = database_.var_iter_begin();
    ivar != database_.var_iter_end(); ++ivar) {
    std::string GroupName = database_.var_iter_gname(ivar);
    std::string VarName = database_.var_iter_vname(ivar);
    const std::type_info & VarType = database_.var_iter_type(ivar);
    std::vector<std::size_t> VarShape = database_.var_iter_shape(ivar);
    std::size_t VarSize = database_.var_iter_size(ivar);

    if (VarType == typeid(int)) {
      std::unique_ptr<int[]> VarData(new int[VarSize]);
      database_.LoadFromDb(GroupName, VarName, VarShape, VarData.get());
      fileio->WriteVar(GroupName, VarName, VarShape, VarData.get());
    } else if (VarType == typeid(float)) {
      std::unique_ptr<float[]> VarData(new float[VarSize]);
      database_.LoadFromDb(GroupName, VarName, VarShape, VarData.get());
      fileio->WriteVar(GroupName, VarName, VarShape, VarData.get());
    } else if (VarType == typeid(std::string)) {
      std::vector<std::string> VarData(VarSize, "");
      database_.LoadFromDb(GroupName, VarName, VarShape, VarData.data());

      // Get the shape needed for the character array, which will be a 2D array.
      // The total number of char elelments will be CharShape[0] * CharShape[1].
      std::vector<std::size_t> CharShape = CharShapeFromStringVector(VarData);
      std::unique_ptr<char[]> CharData(new char[CharShape[0] * CharShape[1]]);
      StringVectorToCharArray(VarData, CharShape, CharData.get());
      fileio->WriteVar(GroupName, VarName, CharShape, CharData.get());
    } else if (VarType == typeid(util::DateTime)) {
      util::DateTime TempDt("0000-01-01T00:00:00Z");
      std::vector<util::DateTime> VarData(VarSize, TempDt);
      database_.LoadFromDb(GroupName, VarName, VarShape, VarData.data());

      // Convert the DateTime vector to a string vector, then save into the file.
      std::vector<std::string> StringVector(VarSize, "");
      for (std::size_t i = 0; i < VarSize; i++) {
        StringVector[i] = VarData[i].toString();
      }
      std::vector<std::size_t> CharShape = CharShapeFromStringVector(StringVector);
      std::unique_ptr<char[]> CharData(new char[CharShape[0] * CharShape[1]]);
      StringVectorToCharArray(StringVector, CharShape, CharData.get());
      fileio->WriteVar(GroupName, VarName, CharShape, CharData.get());
    } else if (commMPI_.rank() == 0) {
      oops::Log::warning() << "ObsData::SaveToFile: Unrecognized data type: "
                           << VarType.name() << std::endl;
      oops::Log::warning() << "  ObsSpaceContainer currently supports data types "
                           << "int, float and char." << std::endl;
      oops::Log::warning() << "  Skipping save of " << VarName << " @ " << GroupName
                           << " from the input file." << std::endl;
    }
  }
}

// -----------------------------------------------------------------------------
/*!
 * \details This method handles the data type conversion when transferring data from
 *          VarData into the obs container. This method is called for the cases where
 *          an undesired data type mismatch could occurr during a put_db call. Therefore
 *          this method issues a warning about the conversion. Once the type mismatches
 *          are fixed, this method will be eliminated and the type mismatches that it
 *          is handling will become errors.
 *
 * \param[in] GroupName Name of group in the obs container
 * \param[in] VarName   Name of variable in the obs container
 * \param[in] VarShape  Shape (dimension sizes) of variable
 * \param[in] VarSize   Total number of elements of variable
 * \param[in] VarData   Pointer to memory to be stored in the obs container
 */
template<typename VarType, typename DbType>
void ObsData::ConvertStoreToDb(const std::string & GroupName, const std::string & VarName,
                   const std::vector<std::size_t> & VarShape, const std::size_t VarSize,
                   const VarType * VarData) {
  // Print a warning so we know to fix this situation. Limit the warnings to one per
  // ObsData instantiation (roughly one per file).
  std::string VarTypeName = TypeIdName(typeid(VarType));
  std::string DbTypeName = TypeIdName(typeid(DbType));
  nwarns_fdtype_++;
  if ((commMPI_.rank() == 0) && (nwarns_fdtype_ == 1)) {
    oops::Log::warning() << "ObsData::ConvertStoreToDb: WARNING: input file contains "
                         << "unexpected data type: " << VarTypeName << std::endl
                         << "  Input file: " << filein_ << std::endl << std::endl;
  }

  std::unique_ptr<DbType[]> DbData(new DbType[VarSize]);
  ConvertVarType<VarType, DbType>(VarData, DbData.get(), VarSize);
  database_.StoreToDb(GroupName, VarName, VarShape, DbData.get());
}

// -----------------------------------------------------------------------------
/*!
 * \details This method handles the data type conversion when transferring data from
 *          obs container into VarData. This method is called for the cases where
 *          an undesired data type mismatch could occurr during a get_db call. Therefore
 *          this method issues a warning about the conversion. Once the type mismatches
 *          are fixed, this method will be eliminated and the type mismatches that it
 *          is handling will become errors.
 *
 * \param[in] GroupName Name of group in the obs container
 * \param[in] VarName   Name of variable in the obs container
 * \param[in] VarShape  Shape (dimension sizes) of variable
 * \param[in] VarSize   Total number of elements of variable
 * \param[in] VarData   Pointer to memory to be loaded from the obs container
 */
template<typename DbType, typename VarType>
void ObsData::LoadFromDbConvert(const std::string & GroupName, const std::string & VarName,
                   const std::vector<std::size_t> & VarShape, const std::size_t VarSize,
                   VarType * VarData) const {
  // Print a warning so we know to fix this situation
  std::string VarTypeName = TypeIdName(typeid(VarType));
  std::string DbTypeName = TypeIdName(typeid(DbType));
  if (commMPI_.rank() == 0) {
    oops::Log::warning() << "ObsData::LoadFromDbConvert: WARNING: Variable type does not "
                         << "match that of the database entry." << std::endl
                         << "  Input file: " << filein_ << std::endl
                         << "  Variable: " << VarName << " @ " << GroupName << std::endl
                         << "  Type of variable: " << VarTypeName << std::endl
                         << "  Type of database entry: " << DbTypeName << std::endl
                         << "Converting data, including missing marks, from "
                         << DbTypeName << " to " << VarTypeName << std::endl << std::endl;
    oops::Log::warning() << "ObsData::LoadFromDbConvert: STACKTRACE:" << std::endl
                         << boost::stacktrace::stacktrace() << std::endl;
  }

  std::unique_ptr<DbType[]> DbData(new DbType[VarSize]);
  database_.LoadFromDb(GroupName, VarName, VarShape, DbData.get());
  ConvertVarType<DbType, VarType>(DbData.get(), VarData, VarSize);
}

// -----------------------------------------------------------------------------
/*!
 * \details This method applys the distribution index on data read from the input obs file.
 *          This method will allocate the memory for the IndexedData pointer since it
 *          is also calculating the shape and size of the IndexedData memory. 
 *          It is expected that when this method is called that the distribution index will
 *          have the process element and DA timing window effects accounted for.
 *
 * \param[in]  FullData     Pointer to the data read in from the input obs file (all locations)
 * \param[in]  FullShape    Shape (dimension sizes) of FullData
 * \param[out] IndexedData  Pointer to memory for the filtered data (selected locations)
 * \param[out] IndexedShape Shape of IndexedData
 * \param[out] IndexedSize  Total number of elements in IndexedData
 */
template<typename VarType>
void ObsData::ApplyDistIndex(std::unique_ptr<VarType[]> & FullData,
                              const std::vector<std::size_t> & FullShape,
                              std::unique_ptr<VarType[]> & IndexedData,
                              std::vector<std::size_t> & IndexedShape,
                              std::size_t & IndexedSize) const {
  IndexedShape = FullShape;
  IndexedSize = 1;
  // Apply the distribution index only when the first dimension size (FullShape[0])
  // is equal to gnlocs_. Ie, only apply the index to obs data (values, errors
  // and QC marks) and metadata related to the obs data.
  if (FullShape[0] == gnlocs_) {
    // Need to compute the new Shape and size. Keep track of the number of items
    // between each element of the first dimension (IndexIncrement). IndexIncrement
    // will define the space between each element of the first dimension, which will
    // be general no matter how many dimensions in the variable.
    IndexedShape[0] = nlocs_;
    std::size_t IndexIncrement = 1;
    for (std::size_t i = 0; i < IndexedShape.size(); i++) {
      IndexedSize *= IndexedShape[i];
      if (i > 0) {
        IndexIncrement *= IndexedShape[i];
      }
    }

    IndexedData.reset(new VarType[IndexedSize]);
    for (std::size_t i = 0; i < IndexedShape[0]; i++) {
      for (std::size_t j = 0; j < IndexIncrement; j++) {
        std::size_t isrc = (indx_[i] * IndexIncrement) + j;
        std::size_t idest = (i * IndexIncrement) + j;
        IndexedData.get()[idest] = FullData.get()[isrc];
      }
    }
  } else {
    // Transfer the full data pointer to the indexed data pointer
    for (std::size_t i = 0; i < IndexedShape.size(); i++) {
      IndexedSize *= IndexedShape[i];
    }
    IndexedData.reset(FullData.release());
  }
}

// -----------------------------------------------------------------------------
/*!
 * \details This method will return the desired numeric data type for variables
 *          read from the input obs file. The rule for now is any variable
 *          in the group "PreQC" is to be an integer, and any variable that is
 *          a double is to be a float (single precision). For cases outside of this
 *          rule, the data type from the file is used.
 *
 * \param[in] GroupName   Name of obs container group
 * \param[in] FileVarType Name of the data type of the variable from the input obs file
 */
std::string ObsData::DesiredVarType(std::string & GroupName, std::string & FileVarType) {
  // By default, make the DbVarType equal to the FileVarType
  // Exceptions are:
  //   Force the group "PreQC" to an integer type.
  //   Force double to float.
  std::string DbVarType = FileVarType;

  if (GroupName == "PreQC") {
    DbVarType = "int";
  } else if (FileVarType == "double") {
    DbVarType = "float";
  }

  return DbVarType;
}

// -----------------------------------------------------------------------------
/*!
 * \details This method will perform numeric data type conversions. The caller needs
 *          to allocate memory for the converted data (ToVar). This method is aware
 *          of the IODA missing values and will convert these appropriately. For
 *          example when converting double to float, all double missing values will
 *          be replaced with float missing values during the conversion.
 *
 * \param[in]  FromVar Pointer to memory of variable we are converting from
 * \param[out] ToVar   Pointer to memory of variable we are converting to
 * \param[in]  VarSize Total number of elements in FromVar and ToVar.
 */
template<typename FromType, typename ToType>
void ObsData::ConvertVarType(const FromType * FromVar, ToType * ToVar,
                              const std::size_t VarSize) {
  std::string FromTypeName = TypeIdName(typeid(FromType));
  std::string ToTypeName = TypeIdName(typeid(ToType));
  const FromType FromMiss = util::missingValue(FromMiss);
  const ToType ToMiss = util::missingValue(ToMiss);

  // It is assumed that the caller has allocated memory for both input and output
  // variables.
  //
  // In any type change, the missing values need to be switched.
  //
  // Allow type changes between numeric types (int, float, double). These can
  // be handled with the standard conversions.
  bool FromTypeOkay = ((typeid(FromType) == typeid(int)) ||
                       (typeid(FromType) == typeid(float)) ||
                       (typeid(FromType) == typeid(double)));

  bool ToTypeOkay = ((typeid(ToType) == typeid(int)) ||
                     (typeid(ToType) == typeid(float)) ||
                     (typeid(ToType) == typeid(double)));

  if (FromTypeOkay && ToTypeOkay) {
    for (std::size_t i = 0; i < VarSize; i++) {
      if (FromVar[i] == FromMiss) {
        ToVar[i] = ToMiss;
      } else {
        ToVar[i] = FromVar[i];
      }
    }
  } else {
    std::string ErrorMsg = "Unsupported variable data type conversion: " +
       FromTypeName + " to " + ToTypeName;
    ABORT(ErrorMsg);
  }
}

// -----------------------------------------------------------------------------
/*!
 * \details This method provides a means for printing Jo in
 *          an output stream. For now a dummy message is printed.
 */
void ObsData::printJo(const ObsVector & dy, const ObsVector & grad) {
  oops::Log::info() << "ObsData::printJo not implemented" << std::endl;
}

// -----------------------------------------------------------------------------

}  // namespace ioda