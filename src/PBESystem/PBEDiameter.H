/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2012 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Class
    Foam::PBEDiameter

Description
    Incompressible multi-phase mixture with built in solution for the
    phase fractions with interface compression for interface-capturing.

    Derived from transportModel so that it can be unsed in conjunction with
    the incompressible turbulence models.

    Surface tension and contact-angle is handled for the interface
    between each phase-pair.

SourceFiles
    PBEDiameter.C

\*---------------------------------------------------------------------------*/

#ifndef PBEDiameter_H
#define PBEDiameter_H

#include "diameterModel.H"
#include "PBEMethod.H"
#include "autoPtr.H"
//#include "breakupKernels/breakupKernel.H"
//#include "coalescenceKernels/coalescenceKernel.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                      Class PBEDiameter Declaration
\*---------------------------------------------------------------------------*/
namespace diameterModels
{

class PBEDiameter
:
    public diameterModel
{
 private:

  //- PBE solution method
  autoPtr<PBEMethod> method_;

  //- The Sauter-mean diameter of the phase
  volScalarField d_;

 public:

  //- Runtime type information
  TypeName("PBEDiameter");

  // Constructors

  //- Construct from components
  PBEDiameter
  (
      const dictionary& diameterProperties,
      const phaseModel& phase
  );

  //- Destructor
  virtual ~PBEDiameter()
  {}

  // Member Functions

  //- Dummy correct
  void correct()
  {
      method_->correct();
      d_ = method_->d();
  }

  //- Return the Sauter-mean diameter
  virtual tmp<volScalarField> d() const
  {
      return d_;
  }

  //- Read base transportProperties dictionary
  virtual bool read(const dictionary& phaseProperties)
  {
      //TODO: Finish read function
      return true;
  };
};

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace diameterModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
