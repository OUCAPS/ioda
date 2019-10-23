/*
 * (C) Copyright 2017-2019 UCAR
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#ifndef FILEIO_NETCDFIO_H_
#define FILEIO_NETCDFIO_H_

#include <map>
#include <string>
#include <vector>

#include "fileio/IodaIO.h"
#include "oops/util/DateTime.h"
#include "oops/util/ObjectCounter.h"


////////////////////////////////////////////////////////////////////////
// Implementation of IodaIO for netcdf.
////////////////////////////////////////////////////////////////////////

// Forward declarations
namespace eckit {
  class Configuration;
}

namespace ioda {

/*! \brief Implementation of IodaIO for netcdf.
 *
 * \details The NetcdfIO class defines the constructor and methods for netcdf
 *          file access. These fill in the abstract base class IodaIO methods.
 *
 * \author Stephen Herbener (JCSDA)
 */
class NetcdfIO : public IodaIO,
                 private util::ObjectCounter<NetcdfIO> {
 public:
  /*!
   * \brief classname method for object counter
   *
   * \details This method is supplied for the ObjectCounter base class.
   *          It defines a name to identify an object of this class
   *          for reporting by OOPS.
   */
  static const std::string classname() {return "ioda::NetcdfIO";}

  NetcdfIO(const std::string & FileName, const std::string & FileMode,
           const std::size_t & Nlocs, const std::size_t & Nrecs,
           const std::size_t & Nvars);
  ~NetcdfIO();

  void ReadVar(const std::string & GroupName, const std::string & VarName,
               const std::vector<std::size_t> & VarShape, int * VarData);
  void ReadVar(const std::string & GroupName, const std::string & VarName,
               const std::vector<std::size_t> & VarShape, float * VarData);
  void ReadVar(const std::string & GroupName, const std::string & VarName,
               const std::vector<std::size_t> & VarShape, double * VarData);
  void ReadVar(const std::string & GroupName, const std::string & VarName,
               const std::vector<std::size_t> & VarShape, char * VarData);

  void WriteVar(const std::string & GroupName, const std::string & VarName,
                const std::vector<std::size_t> & VarShape, int * VarData);
  void WriteVar(const std::string & GroupName, const std::string & VarName,
                const std::vector<std::size_t> & VarShape, float * VarData);
  void WriteVar(const std::string & GroupName, const std::string & VarName,
                const std::vector<std::size_t> & VarShape, char * VarData);

 private:
  // For the oops::Printable base class
  void print(std::ostream & os) const;

  void CheckNcCall(int RetCode, std::string & ErrorMsg);

  bool NcAttrExists(const int & AttrOwnerId, const std::string & AttrName);

  std::string FormNcVarName(const std::string & GroupName, const std::string & VarName);

  void CreateNcDim(const std::string DimName, const std::size_t DimSize);

  int GetStringDimBySize(const std::size_t DimSize);

  void ReadConvertDateTime(std::string GroupName, std::string VarName, char * VarData);

  template <typename DataType>
  void ReadVar_helper(const std::string & GroupName, const std::string & VarName,
                      const std::vector<std::size_t> & VarShape, DataType * VarData);

  void ReadNcVarFill(const int & VarId, const std::string & VarName, int * VarData,
                     int & FillValue);
  void ReadNcVarFill(const int & VarId, const std::string & VarName, float * VarData,
                     float & FillValue);
  void ReadNcVarFill(const int & VarId, const std::string & VarName, double * VarData,
                     double & FillValue);
  void ReadNcVarFill(const int & VarId, const std::string & VarName, char * VarData,
                     char & FillValue);

  template <typename DataType>
  void WriteVar_helper(const std::string & GroupName, const std::string & VarName,
                       const std::vector<std::size_t> & VarShape, DataType * VarData);

  // Data members
  /*!
   * \brief netcdf file id
   *
   * \details This data member holds the file id of the open netcdf file.
   *          It gives access to the dimensions, attributes and variables in
   *          the netcdf file.
   */
  int ncid_;

  /*!
   * \brief count of undefined group names
   *
   * \details This data member holds a count of the number of variables in the
   *          netcdf file that are missing a group name (@GroupName suffix).
   */
  std::size_t num_missing_gnames_;

  /*!
   * \brief offset time flag
   *
   * \details This data member is a flag indicating the existence of the
   *          offset time variable in the netcdf file.
   */
  bool have_offset_time_;

  /*!
   * \brief date time flag
   *
   * \details This data member is a flag indicating the existence of the
   *          date_time variable in the netcdf file.
   */
  bool have_date_time_;
};

}  // namespace ioda

#endif  // FILEIO_NETCDFIO_H_
