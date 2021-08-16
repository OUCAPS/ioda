#pragma once
/*
 * (C) Copyright 2021 UCAR
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */
/** @file DataFromSQL.h
 * @brief implements ODC bindings
**/

#include <cctype>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

#include "ioda/defs.h"
#include "ioda/ObsGroup.h"
#include "unsupported/Eigen/CXX11/Tensor"

namespace ioda {
namespace Engines {
namespace ODC {

  // TODO(DJDavies2): Take these obsgroup and varno
  // definitions and encapsulate these into the YAML
  // file structure.
  static constexpr int obsgroup_scatwind  = 2;
  static constexpr int obsgroup_aircraft  = 4;
  static constexpr int obsgroup_sonde     = 5;
  static constexpr int obsgroup_atovs     = 7;
  static constexpr int obsgroup_airs      = 16;
  static constexpr int obsgroup_gpsro     = 18;
  static constexpr int obsgroup_ssmis     = 19;
  static constexpr int obsgroup_iasi      = 26;
  static constexpr int obsgroup_seviriclr = 27;
  static constexpr int obsgroup_amsr      = 29;
  static constexpr int obsgroup_abiclr    = 37;
  static constexpr int obsgroup_atms      = 38;
  static constexpr int obsgroup_cris      = 39;
  static constexpr int obsgroup_mwsfy3    = 44;
  static constexpr int obsgroup_ahiclr    = 51;
  static constexpr int obsgroup_mwri      = 55;
  static constexpr int obsgroup_gmilow    = 56;
  static constexpr int obsgroup_gmihigh   = 57;
  static constexpr int obsgroup_hiras     = 60;

  static constexpr int varno_dd               = 111;
  static constexpr int varno_rawbt            = 119;
  static constexpr int varno_bending_angle    = 162;
  static constexpr int varno_rawsca           = 233;
  static constexpr int varno_u_amb            = 242;
  static constexpr int varno_rawbt_hirs       = 248;
  static constexpr int varno_rawbt_amsu       = 249;
  static constexpr int varno_rawbt_amsr_89ghz = 267;
  static constexpr int varno_rawbt_mwts       = 274;
  static constexpr int varno_rawbt_mwhs       = 275;

  static constexpr int odb_type_int      = 1;
  static constexpr int odb_type_real     = 2;
  static constexpr int odb_type_string   = 3;
  static constexpr int odb_type_bitfield = 4;

  static constexpr float odb_missing_float = -2147483648.0f;
  static constexpr int odb_missing_int = 2147483647;

class DataFromSQL {
private:
  std::vector<std::string> columns_;
  std::vector<int> column_types_;
  std::vector<int> varnos_;
  std::vector<double> data_;
  size_t number_of_rows_           = 0;
  size_t number_of_metadata_rows_  = 0;
  size_t number_of_varnos_         = 0;
  int obsgroup_                    = 0;
  std::map<int, size_t> varnos_and_levels_;

  /// \brief Returns a count of the rows extracted by an sql
  /// \param sql The SQL to check
  static size_t countRows(const std::string& sql);

  /// \brief Returns the value for a particular row/column
  /// \param row Get data for this row
  /// \param column Get data for this column
  double getData(size_t row, size_t column) const;

  /// \brief Returns the value for a particular row/column (as name)
  /// \param row Get data for this row
  /// \param column Get data for this column
  double getData(size_t row, const std::string& column) const;

  /// \brief Set a value for a row,column element
  /// \param row The row to set
  /// \param column The column to set
  /// \param val Set this value
  void setData(size_t row, size_t column, double val);

  /// \brief Populate structure with data from an sql
  /// \param sql The SQL string to generate the data for the structure
  void setData(const std::string& sql);

  /// \brief Returns the number of rows for a particular varno
  /// \param varno The varno to check
  size_t numberOfRowsForVarno(int varno) const;

  /// \brief Returns true if a particular varno is present
  /// \param varno The varno to check
  bool hasVarno(int varno) const;

  /// \brief Returns the index of a specified column
  /// \param column The column to check
  int getColumnIndex(const std::string& column) const;

  /// \brief Returns the number of levels for each varno
  size_t numberOfLevels(int varno) const;

  /// \brief Returns data for a (metadata) column
  /// \param column Get data for this column
  Eigen::ArrayXf getMetadataColumn(std::string const& column) const;

  /// \brief Returns data for a (metadata) column
  /// \param column Get data for this column
  Eigen::ArrayXi getMetadataColumnInt(std::string const& column) const;

  /// \brief Returns data for a (metadata) column
  /// \param column Get data for this column
  std::vector<std::string> getMetadataStringColumn(std::string const& column) const;

  /// \brief Returns data for a varno for a varno column
  /// \param varno Get data for this varno
  /// \param column Get data for this column
  /// \param nchans Number of channels to store
  /// \param nchans_actual Actual number of channels
  Eigen::ArrayXf getVarnoColumn(const std::vector<int>& varnos, std::string const& column,
                                           const int nchans, const int nchans_actual) const;

public:
  /// \brief Simple constructor
  DataFromSQL();

  /// \brief Returns the number of "metadata" rows, i.e. hdr-type rows
  size_t numberOfMetadataRows() const;

  /// \brief Returns the dimensions for the ODB
  NewDimensionScales_t getVertcos() const;

  /// \brief Populate structure with data from specified columns, file and varnos
  /// \param columns List of columns to extract
  /// \param filename Extract from this file
  /// \param varnos List of varnos to extract
  void select(const std::vector<std::string>& columns, const std::string& filename,
              const std::vector<int>& varnos);

  /// \brief Returns a vector of date strings
  std::vector<std::string> getDates(std::string const& date_col, std::string const& time_col) const;

  /// \brief Returns a vector of columns
  const std::vector<std::string>& getColumns() const;

  /// \brief Returns an ioda variable for a specified column
  /// \param column Get data for this column
  ioda::Variable getIodaVariable(std::string const& column, ioda::ObsGroup og,
                                 ioda::VariableCreationParameters params) const;

  /// \brief Returns an ioda variable for a specified column
  ioda::Variable getIodaObsvalue(int varno, ioda::ObsGroup og,
                                 ioda::VariableCreationParameters params) const;

  /// \brief Returns the type of a specified column
  /// \param column The column to check
  int getColumnTypeByName(std::string const& column) const;

  /// \brief Returns the obsgroup number
  int getObsgroup() const;
};

}  // namespace ODC
}  // namespace Engines
}  // namespace ioda