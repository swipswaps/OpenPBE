/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2013 OpenFOAM Foundation
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

\*---------------------------------------------------------------------------*/

#include <string>
#include <sstream>
#include <cmath>

#include <boost/math/distributions/gamma.hpp>
#include <cassert>

#include "QMOM.H"
#include "addToRunTimeSelectionTable.H"

#include "PBESystems-internal.H"
#include "fvScalarMatrix.H"
#include "fvcSnGrad.H"
#include "fvcFlux.H"
#include "fvcAverage.H"
#include "fvm.H"
#include "Integrator.H"
#include "mathematicalConstants.H"

namespace Foam
{
namespace PBEMethods
{


defineTypeNameAndDebug(QMOM, 0);
addToRunTimeSelectionTable(PBEMethod, QMOM, dictionary);
// * * * * * * * * * * * * * * * Static Member Data  * * * * * * * * * * * * //


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //
using constant::mathematical::pi;

QMOM::QMOM
(
    const dictionary& pbeProperties,
    const phaseModel& phase
)
:
    PBEMethod(pbeProperties, phase),
    QMOMDict_(pbeProperties.subDict("QMOMCoeffs")),
    dispersedPhase_(phase),
    mesh_(dispersedPhase_.U().mesh()),
    moments_{{
    volScalarField
    (
        IOobject
        (
            "m0",
            mesh_.time().timeName(),
            mesh_,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_
    ),
    volScalarField
    (
        IOobject
        (
            "m1",
            mesh_.time().timeName(),
            mesh_,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_
    ),
    volScalarField
    (
        IOobject
        (
            "m2",
            mesh_.time().timeName(),
            mesh_,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_
    )
    }},
    d_
    (
        IOobject
        (
            "diameter",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        pow(6.0 / pi * moments_[1] / moments_[0], 1.0 / 3.0)
    ) ,
    gamma_k_
    (
        IOobject
        (
            "gamma_k",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("gamma_k", dimless, 0.0)
    ),
    gamma_theta_
    (
        IOobject
        (
            "gamma_theta",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("gamma_theta", dimless / dimVolume, 0.0)
    ),
    gamma_c0_
    (
        IOobject
        (
            "gamma_c0",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("c0", dimless, 0.0)
    ),
    Nf_(QMOMDict_.lookupOrDefault<scalar>("daughterDropletsNr",2.0)),
    maxD_
    (
        dimensionedScalar
        (
            "maxDiameter",
            dimLength,
            QMOMDict_.lookup("maxDiameter")
        )
    ),
    minD_
    (
        dimensionedScalar
        (
            "minDiameter",
            dimLength,
            QMOMDict_.lookup("minDiameter")
        )
    ),
    minGammaAlpha_
    (
        "minGammaAlpha",
        dimLength,
        QMOMDict_.lookupOrDefault<scalar>("minGammaAlpha", 1e-05)
    ),
    maxGammaBeta_(QMOMDict_.lookupOrDefault<scalar>("maxGammaBeta", 10.0)),
    maxDiameterMultiplicator_
    (
        QMOMDict_.lookupOrDefault<scalar>("maxDiameterMultiplicator", 10.0)
    ),
    integrationSteps_(QMOMDict_.lookupOrDefault<scalar>("integrationSteps", 10)),
    bList_(integrationSteps_),
    cList_(pow(integrationSteps_, 2)),
    gammaList_(integrationSteps_)
{
}

QMOM::~QMOM()
{

}
void QMOM::correct()
{
    Info<< "updating size moments" << endl;
    gamma_c0_ = moments_[0];
    gamma_k_ = pow(moments_[1] , 2)
        / (moments_[2] * moments_[0] - pow(moments_[1],2)
           + dimensionedScalar("small", dimensionSet(0,6,0,0,0), SMALL));

    gamma_theta_ = (moments_[0] * moments_[1]) 
        / max(
            moments_[0] * moments_[2] - pow(moments_[1], 2), 
            dimensionedScalar("small", dimensionSet(0,6,0,0,0), SMALL)
        );
    
    
    Info<< "gamma parameters:" << endl;
    printAvgMaxMin(gamma_c0_);
    printAvgMaxMin(gamma_k_);
    printAvgMaxMin(gamma_theta_);

    //TODO: get rid of 3
    std::array<volScalarField, 3> mSources_{{
        volScalarField
        (
            IOobject
            (
                "m0Source",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE
            ),
            momentSourceTerm(0)
            //dispersedPhase_.U().mesh(),
            //dimensionedScalar("m3Source", dimVolume / dimTime, 0.0)
        ),
        volScalarField
        (
            IOobject
            (
                "m1Source",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE
            ),
            momentSourceTerm(1)
        ),
        volScalarField
        (
            IOobject
            (
                "m2Source",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE
            ),
            momentSourceTerm(2)
        )
    }};

    Info<< "moment sources:" << endl;
    //TODO: is there a better way to assure that source terms on boundaries
    //are equal to 0?
    //maybe using internalField() instead of the whole field somewhere?
    //how source terms are handled in combustion/chemistry/turbulence codes in
    //OF
    for (auto& mSource : mSources_)
    {
        mSource.boundaryField() = 0;

        //fix for nan values
        forAll(mesh_.C(), celli)
        {
            auto& mSource_i = mSource[celli];
            if ( std::isnan(mSource_i) )
            {
                mSource_i = 0.0;
            }
        }

        printAvgMaxMin(mSource);
    }

    for (std::size_t i = 0; i<moments_.size(); ++i)
    {
        fvScalarMatrix mEqn
        (
            fvm::ddt(moments_[i])
            + fvm::div(dispersedPhase_.phi(),moments_[i])
            //+ fvm::div(limitedFlux_,moments_[i])
            ==
            mSources_[i]
        );
        mEqn.relax();
        mEqn.solve();
    }

    for (auto& moment : moments_)
    {
        moment = max(
            moment,
            dimensionedScalar(moment.name(), moment.dimensions(), SMALL)
        );
        //TODO: print a warning message
        printAvgMaxMin(moment);
    }

    d_ = pow(6.0 / pi * moments_[1] / moments_[0], 1.0/3.0);
    d_ = min(d_, maxD_);
    d_ = max(d_,  minD_);

    printAvgMaxMin(d_);
}

const volScalarField QMOM::d() const
{
    return d_;
}

// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

tmp<volScalarField> QMOM::momentSourceTerm(label momenti)
{
    volScalarField bS = breakupSourceTerm(momenti);
    volScalarField cS = coalescenceSourceTerm(momenti);
    Info<< "breakup (moment " << momenti << ") "
        << bS.weightedAverage(mesh_.V()).value() << endl;
    Info<< "coalescence (moment " << momenti << ") "
        << cS.weightedAverage(mesh_.V()).value() << endl;
    return cS + bS;// breakupSourceTerm(momenti);
}


tmp<volScalarField> QMOM::coalescenceSourceTerm(label momenti)
{
    dimensionedScalar xiDim = dimensionedScalar("xiDim", dimLength, 1.0);

    scalar dMean = max(d_.weightedAverage(mesh_.V()).value(), SMALL);
    scalar maxArg = maxDiameterMultiplicator_ * dMean ;
    label maxIter = integrationSteps_;
    dimensionedScalar dx = xiDim * maxArg / integrationSteps_;

    dimensionedScalar arg_i1 = xiDim;
    dimensionedScalar arg_j1 = xiDim;
    label iterator = 0;
    label iterator2 = 0;

    //value of the integral
    volScalarField sum
        (
            IOobject
            (
                "sum",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar
            (
                "sum", 
                pow(dimVolume, momenti - 1) / dimTime,
                0
            ) 
        );
    volScalarField sum_i0(sum);


    volScalarField sum2
        (
            IOobject
            (
                "Scoal",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar
            (
                "Scoal", 
                pow(dimVolume, momenti) / dimTime,
                0
            ) 
        );

    return tmp<volScalarField>( new volScalarField(sum2));

}

void QMOM::printAvgMaxMin(const volScalarField &v) const
{
    Info<< v.name() << ": avg, max,min "
        << v.weightedAverage(mesh_.V()).value()
        << ", " << max(v).value()
        << ", " << min(v).value() << endl;
}

using gammaDistribution = boost::math::gamma_distribution<>;
using boost::math::pdf;

tmp<volScalarField> QMOM::breakupSourceTerm(label momenti)
{

    //value of the integral
    volScalarField toReturn
        (
            IOobject
            (
                "Sbr",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar
            (
                "Sbr", 
                pow(dimVolume, momenti) /dimTime,
                0
            ) 
        );

    forAll(dispersedPhase_, celli)
    {
        gammaDistribution gamma(gamma_k_[celli], gamma_theta_[celli]);

        auto m0 = moments_[0][celli];

        auto breakupSourceIntegrand = [&](double xi){
            scalar breakupDeath = breakup_->S(xi).value() * m0 * pdf(gamma, xi);

            auto breakupBirthIntegrand = [&, xi](double xi_prime){
                return daughterParticleDistribution_->beta(xi, xi_prime).value()
                    * breakup_->S(xi_prime).value() * m0 * pdf(gamma, xi_prime);
            };

            double breakupBirth = integrate(breakupBirthIntegrand, xi);

            return pow(xi, momenti) * (breakupBirth - breakupDeath);
        };
        
        toReturn[celli] = integrate(breakupSourceIntegrand, 0.);
    }

    return tmp<volScalarField>( new volScalarField(toReturn));
}


}//end namespace PBEMethods
}//end namespace Foam

// ************************************************************************* //