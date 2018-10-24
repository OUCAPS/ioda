/*
 * (C) Copyright 2017 UCAR
 * 
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0. 
 */

#ifndef IODA_OBSVECTOR_H_
#define IODA_OBSVECTOR_H_

#include <ostream>
#include <string>

#include "Fortran.h"
#include "oops/util/ObjectCounter.h"
#include "oops/util/Printable.h"

namespace ioda {
  class ObsSpace;

//-----------------------------------------------------------------------------
/*! \brief ObsVector class to handle vectors in observation space for IODA
 *
 *  \details This class holds observation vector data. Examples of an obs vector
 *           are the y vector and the H(x) vector. The methods of this class
 *           that implement vector operations (e.g., bitwise add, bitwise subtract,
 *           dot product) are capable of handling missing values in the obs data.
 */

class ObsVector : public util::Printable,
                  private util::ObjectCounter<ObsVector> {
 public:
  static const std::string classname() {return "ioda::ObsVector";}

  explicit ObsVector(const ObsSpace &);
  ObsVector(const ObsVector &, const bool copy = true);
  ~ObsVector();

  ObsVector & operator = (const ObsVector &);
  ObsVector & operator*= (const double &);
  ObsVector & operator+= (const ObsVector &);
  ObsVector & operator-= (const ObsVector &);
  ObsVector & operator*= (const ObsVector &);
  ObsVector & operator/= (const ObsVector &);

  void zero();
  void axpy(const double &, const ObsVector &);
  void invert();
  void random();
  double dot_product_with(const ObsVector &) const;
  double rms() const;

  std::size_t size() const {return values_.size();}

  const double & toFortran() const {return values_[0];}
  double & toFortran() {return values_[0];}

// I/O
  void read(const std::string &);
  void save(const std::string &) const;

 private:
  void print(std::ostream &) const;

  /*! \brief Associate ObsSpace object */
  const ObsSpace & obsdb_;

  /*! \brief Vector data */
  std::vector<double> values_;
};
// -----------------------------------------------------------------------------

}  // namespace ioda

#endif  // IODA_OBSVECTOR_H_
