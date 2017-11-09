/*******************************************************************************************************
*  Copyright 2017 Alliance for Sustainable Energy, LLC
*
*  NOTICE: This software was developed at least in part by Alliance for Sustainable Energy, LLC
*  (�Alliance�) under Contract No. DE-AC36-08GO28308 with the U.S. Department of Energy and the U.S.
*  The Government retains for itself and others acting on its behalf a nonexclusive, paid-up,
*  irrevocable worldwide license in the software to reproduce, prepare derivative works, distribute
*  copies to the public, perform publicly and display publicly, and to permit others to do so.
*
*  Redistribution and use in source and binary forms, with or without modification, are permitted
*  provided that the following conditions are met:
*
*  1. Redistributions of source code must retain the above copyright notice, the above government
*  rights notice, this list of conditions and the following disclaimer.
*
*  2. Redistributions in binary form must reproduce the above copyright notice, the above government
*  rights notice, this list of conditions and the following disclaimer in the documentation and/or
*  other materials provided with the distribution.
*
*  3. The entire corresponding source code of any redistribution, with or without modification, by a
*  research entity, including but not limited to any contracting manager/operator of a United States
*  National Laboratory, any institution of higher learning, and any non-profit organization, must be
*  made publicly available under this license for as long as the redistribution is made available by
*  the research entity.
*
*  4. Redistribution of this software, without modification, must refer to the software by the same
*  designation. Redistribution of a modified version of this software (i) may not refer to the modified
*  version by the same designation, or by any confusingly similar designation, and (ii) must refer to
*  the underlying software originally provided by Alliance as �System Advisor Model� or �SAM�. Except
*  to comply with the foregoing, the terms �System Advisor Model�, �SAM�, or any confusingly similar
*  designation may not be used to refer to any modified version of this software or any modified
*  version of the underlying software originally provided by Alliance without the prior written consent
*  of Alliance.
*
*  5. The name of the copyright holder, contributors, the United States Government, the United States
*  Department of Energy, or any of their employees may not be used to endorse or promote products
*  derived from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
*  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
*  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER,
*  CONTRIBUTORS, UNITED STATES GOVERNMENT OR UNITED STATES DEPARTMENT OF ENERGY, NOR ANY OF THEIR
*  EMPLOYEES, BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
*  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
*  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
*  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************************************/

#include "lib_physics.h"
#include "lib_util.h"
#include "lib_windwatts.h"
#include "lib_windwakemodel.h"

bool windTurbine::setPowerCurve(std::vector<double> windSpeeds, std::vector<double> powerOutput){
	if (windSpeeds.size() == powerOutput.size()) powerCurveArrayLength = windSpeeds.size();
	else{
		errDetails = "Turbine power curve array sizes are unequal.";
		return 0;
	}
	powerCurveWS = windSpeeds;
	powerCurveKW = powerOutput;
	densityCorrectedWS.resize(powerCurveArrayLength);
	powerCurveRPM.resize(powerCurveArrayLength);
	return 1;
}

double windTurbine::tipSpeedRatio(double windSpeed)
{	// find the rpm for this turbine at this wind speed
	double rpm = 0.0;
	if ((windSpeed > powerCurveWS[0]) && (windSpeed < powerCurveWS[powerCurveArrayLength - 1]))
	{
		int j = 1;
		while (powerCurveWS[j] <= windSpeed)
			j++; // find first m_adPowerCurveRPM > fWindSpeedAtHubHeight

		rpm = util::interpolate(powerCurveWS[j - 1], powerCurveRPM[j - 1], powerCurveWS[j], powerCurveRPM[j], windSpeed);
	}
	else if (windSpeed == powerCurveWS[powerCurveArrayLength - 1])
		rpm = powerCurveRPM[powerCurveArrayLength - 1]; // rpm -> zero if wind speed greater than maximum in the array

	// if rpm>0, calculate the tip speed ratio from it, otherwise, return a reasonable value
	return (rpm>0) ? rpm * rotorDiameter * physics::PI / (windSpeed*60.0) : 7.0;
}

void windTurbine::turbinePower(double windVelocityAtDataHeight, double airDensity, double *turbineOutput, double *thrustCoefficient){
	if (!isInitialized()){
		errDetails = "windTurbine not initialized with necessary data";
		return;
	}

	*thrustCoefficient = 0.0;
	*turbineOutput = 0.0;

	//first, correct wind speeds in power curve for site air density. Using method 2 described in https://www.scribd.com/document/38818683/PO310-EWEC2010-Presentation
	std::vector <double> temp_ws;
	for (size_t i = 0; i < densityCorrectedWS.size(); i++)
		densityCorrectedWS[i] = powerCurveWS[i] * pow((physics::AIR_DENSITY_SEA_LEVEL / airDensity), (1.0 / 3.0));

	int i = 0;
	while (powerCurveKW[i] == 0)
		i++; //find the index of the first non-zero power output in the power curve
	//the cut-in speed is defined where the turbine FIRST STARTS TO TURN, not where it first generates electricity! Therefore, assume that the cut-in speed is actually 1 speed BELOW where power is generated.
	//this is consistent with the NREL Cost & Scaling model- if you specify a cut-in speed of 4 m/s, the power curve value at 4 m/s is 0, and it starts producing power at 4.25.
	//HOWEVER, if you specify the cut-in speed BETWEEN the wind speed bins, then this method would improperly assume that the cut-in speed is lower than it actually is. But given the 0.25 m/s size of the bins, that type of
	//specification would be false accuracy anyways, so we'll ignore it for now.
	cutInSpeed = densityCorrectedWS[i - 1];

	/*	//We will continue not to check cut-out speed because currently the model will interpolate between the last non-zero power point and zero, and we don't have a better definition of where the power cutoff should be.
	i = m_adPowerCurveKW.size() - 1; //last index in the array
	while (m_adPowerCurveKW[i] == 0)
	i--; //find the index of the last non-zero power output in the power curve
	m_dCutOutSpeed = m_adPowerCurveWS[i]; //unlike cut in speed, we want power to hard cut AFTER this wind speed value*/


	// If the wind speed measurement height (fDataHeight) differs from the turbine hub height (Hub_Ht), use the shear to correct it. 
	if (shearExponent > 1.0) shearExponent = 1.0 / 7.0;
	double fWindSpeedAtHubHeight = windVelocityAtDataHeight * pow(hubHeight / measurementHeight, shearExponent);

	// Find power from turbine power curve
	double out_pwr = 0.0;
	if ((fWindSpeedAtHubHeight > densityCorrectedWS[0]) && (fWindSpeedAtHubHeight < densityCorrectedWS[powerCurveArrayLength - 1]))
	{
		int j = 1;
		while (densityCorrectedWS[j] <= fWindSpeedAtHubHeight)
			j++; // find first m_adPowerCurveWS > fWindSpeedAtHubHeight

		out_pwr = util::interpolate(densityCorrectedWS[j - 1], powerCurveKW[j - 1], densityCorrectedWS[j], powerCurveKW[j], fWindSpeedAtHubHeight);
	}
	else if (fWindSpeedAtHubHeight == densityCorrectedWS[powerCurveArrayLength - 1])
		out_pwr = powerCurveKW[powerCurveArrayLength - 1];

	// Check against turbine cut-in speed
	if (fWindSpeedAtHubHeight < cutInSpeed) out_pwr = 0.0; //this is effectively redundant, because the power at the cut-in speed is defined to be 0, above, so anything below that will also be 0, but leave in for completeness


	//if (out_pwr > (m_dRatedPower * 0.001)) // if calculated power is > 0.1% of rating, set outputs
	if (out_pwr > 0)
	{
		out_pwr = out_pwr*(1.0 - lossesPercent) - lossesAbsolute;
		double pden = 0.5*airDensity*pow(fWindSpeedAtHubHeight, 3.0);
		double area = physics::PI / 4.0*rotorDiameter*rotorDiameter;
		double fPowerCoefficient = max_of(0.0, 1000.0*out_pwr / (pden*area));

		// set outputs to something other than zero
		*turbineOutput = out_pwr;
		if (fPowerCoefficient >= 0.0)
			*thrustCoefficient = max_of(0.0, -1.453989e-2 + 1.473506*fPowerCoefficient - 2.330823*pow(fPowerCoefficient, 2) + 3.885123*pow(fPowerCoefficient, 3));
	} // out_pwr > (rated power * 0.001)

	return;
}


/// Calculates the velocity deficit (% reduction in wind speed) and the turbulence intensity (TI) due to an upwind turbine.
double simpleWakeModel::velDeltaPQ(double radiiCrosswind, double axialDistInRadii, double thrustCoeff, double *newTurbulenceIntensity)
{
	if (radiiCrosswind > 20.0 || *newTurbulenceIntensity <= 0.0 || axialDistInRadii <= 0.0 || thrustCoeff <= 0.0)
		return 0.0;

	double fAddedTurbulence = (thrustCoeff / 7.0)*(1.0 - (2.0 / 5.0)*log(2.0*axialDistInRadii)); // NOTE that this equation does not account for how far over the turbine is!!
	*newTurbulenceIntensity = sqrt(pow(fAddedTurbulence, 2.0) + pow(*newTurbulenceIntensity, 2.0));

	double AA = pow(*newTurbulenceIntensity, 2.0) * pow(axialDistInRadii, 2.0);
	double fExp = max_of(-99.0, (-pow(radiiCrosswind, 2.0) / (2.0*AA)));
	double dVelocityDeficit = (thrustCoeff / (4.0*AA))*exp(fExp);
	return max_of(min_of(dVelocityDeficit, 1.0), 0.0); // limit result from zero to one
}

void simpleWakeModel::wakeCalculations(const double , const double distanceDownwind[], const double distanceCrosswind[],
	double[], double thrust[], double windSpeed[], double turbulenceIntensity[])
{
	for (size_t i = 1; i < nTurbines; i++) // loop through all turbines, starting with most upwind turbine. i=0 has already been done
	{
		double dDeficit = 1;
		for (size_t j = 0; j < i; j++) // loop through all turbines upwind of turbine[i]
		{
			// distance downwind (axial distance) = distance from turbine j to turbine i along axis of wind direction (units of wind turbine blade radii)
			double fDistanceDownwind = fabs(distanceDownwind[j] - distanceDownwind[i]);

			// separation crosswind (radial distance) between turbine j and turbine i (units of wind turbine blade radii)
			double fDistanceCrosswind = fabs(distanceCrosswind[j] - distanceCrosswind[i]);

			// Calculate the wind speed reduction and turbulence at turbine i, due to turbine j
			// Both the velocity deficit (vdef) and the turbulence intensity (TI) are accumulated over the j loop
			// vdef is accumulated in the code below, using dDeficit
			// TI is accumulated in vel_delta_PQ (it will either add to current TI, or return the same value)
			double vdef = velDeltaPQ(fDistanceCrosswind, fDistanceDownwind, thrust[j], &turbulenceIntensity[i]);
			dDeficit *= (1.0 - vdef);
		}
		windSpeed[i] = windSpeed[i] * dDeficit;
	}
}


/// Returns the area of overlap, NOT a fraction
double parkWakeModel::circle_overlap(double dist_center_to_center, double rad1, double rad2)
{	// Source: http://mathworld.wolfram.com/Circle-CircleIntersection.html, equation 14
	if (dist_center_to_center<0 || rad1<0 || rad2<0)
		return 0;

	if (dist_center_to_center >= rad1 + rad2)
		return 0;

	if (rad1 >= dist_center_to_center + rad2)
		return physics::PI * pow(rad2, 2); // overlap = area of circle 2

	if (rad2 >= dist_center_to_center + rad1)
		return physics::PI * pow(rad1, 2); // overlap = area of circle 1 ( if rad1 is turbine, it's completely inside wake)

	double t1 = pow(rad1, 2) * acos((pow(dist_center_to_center, 2) + pow(rad1, 2) - pow(rad2, 2)) / (2 * dist_center_to_center*rad1));
	double t2 = pow(rad2, 2) * acos((pow(dist_center_to_center, 2) + pow(rad2, 2) - pow(rad1, 2)) / (2 * dist_center_to_center*rad2));
	double t3 = 0.5 * sqrt((-dist_center_to_center + rad1 + rad2) * (dist_center_to_center + rad1 - rad2) * (dist_center_to_center - rad1 + rad2) * (dist_center_to_center + rad1 + rad2));

	return t1 + t2 - t3;
}

/// Calculate the change in wind speed due to wake effects of upwind turbine
double parkWakeModel::delta_V_Park(double Uo, double Ui, double distCrosswind, double distDownwind, double dRadiusUpstream, double dRadiusDownstream, double dThrustCoeff)
{
	// bound the coeff of thrust
	double Ct = max_of(min_of(0.999, dThrustCoeff), minThrustCoeff);

	double k = wakeDecayCoefficient;

	double dRadiusOfWake = dRadiusUpstream + (k * distDownwind); // radius of circle formed by wake from upwind rotor
	double dAreaOverlap = circle_overlap(distCrosswind, dRadiusDownstream, dRadiusOfWake);

	// if there is no overlap, no impact on wind speed
	if (dAreaOverlap <= 0.0) return Uo;

	double dDef = (1 - sqrt(1 - Ct)) * pow(dRadiusUpstream / dRadiusOfWake, 2) * (dAreaOverlap / (physics::PI*dRadiusDownstream*dRadiusDownstream));

	return Ui * (1.0 - dDef);
}

/// Deficit at a downwind turbine is the minimum speed found from all the upwind turbine impacts
void parkWakeModel::wakeCalculations(/*INPUTS */ const double , const double aDistanceDownwind[], const double aDistanceCrosswind[],
	/*OUTPUTS*/ double [], double Thrust[], double adWindSpeed[], double[])
{
	double dTurbineRadius = rotorDiameter / 2;

	for (size_t i = 1; i < nTurbines; i++) // downwind turbines, i=0 has already been done
	{
		double dNewSpeed = adWindSpeed[0];
		for (size_t j = 0; j < i; j++) // upwind turbines
		{
			double fDistanceDownwindMeters = dTurbineRadius*fabs(aDistanceDownwind[i] - aDistanceDownwind[j]);
			double fDistanceCrosswindMeters = dTurbineRadius*fabs(aDistanceCrosswind[i] - aDistanceCrosswind[j]);

			// Calculate the wind speed reduction at turbine i, due turbine [j]
			// keep this new speed if it's less than any other calculated speed
			dNewSpeed = min_of(dNewSpeed, delta_V_Park(adWindSpeed[0], adWindSpeed[j], fDistanceCrosswindMeters, fDistanceDownwindMeters, dTurbineRadius, dTurbineRadius, Thrust[j]));
		}
		adWindSpeed[i] = dNewSpeed;
	}
}


double eddyViscosityWakeModel::getVelocityDeficit(int upwindTurbine, double axialDistanceInDiameters)
{	
	// if we're too close, it's just the initial deficit (simplification, but model isn't valid closer than MIN_DIAM_EV to upwind turbine)
	double dDistPastMin = axialDistanceInDiameters - MIN_DIAM_EV; // in diameters
	if (dDistPastMin < 0.0)
		return rotorDiameter * matEVWakeDeficits.at(upwindTurbine, 0);

	double dDistInResolutionUnits = dDistPastMin / axialResolution;
	size_t iLowerIndex = (size_t)dDistInResolutionUnits;
	size_t iUpperIndex = iLowerIndex + 1;

	if (iUpperIndex >= matEVWakeDeficits.ncols())
		return 0.0;

	dDistInResolutionUnits -= iLowerIndex;

	return (matEVWakeDeficits.at(upwindTurbine, iLowerIndex) * (1.0 - dDistInResolutionUnits)) + (matEVWakeDeficits.at(upwindTurbine, iUpperIndex) * dDistInResolutionUnits);	// in meters
}

double eddyViscosityWakeModel::wakeDeficit(int upwindTurbine, double distCrosswind, double distDownwind)
{
	double dDef = getVelocityDeficit(upwindTurbine, distDownwind);
	if (dDef <= 0.0)
		return 0.0;

	double dSteps = 25.0;
	double dCrossWindDistanceInMeters = distCrosswind * rotorDiameter;
	double dWidth = getWakeWidth(upwindTurbine, distDownwind);
	double dRadius = rotorDiameter / 2.0;
	double dStep = rotorDiameter / dSteps;

	double dTotal = 0.0;
	for (double y = dCrossWindDistanceInMeters - dRadius; y <= dCrossWindDistanceInMeters + dRadius; y += dStep)
	{
		dTotal += dDef * exp(-3.56*(((y*y)) / (dWidth*dWidth)));  // exp term ranges from >zero to one
	}

	dTotal /= (dSteps + 1.0); // average of all terms above will be zero to dDef

	return dTotal;
}

double eddyViscosityWakeModel::getWakeWidth(int upwindTurbine, double axialDistanceInDiameters)
{	
	// if we're too close, it's just the initial wake width
	double dDistPastMin = axialDistanceInDiameters - MIN_DIAM_EV; // in diameters
	if (dDistPastMin < 0.0)
		return rotorDiameter * matEVWakeWidths.at(upwindTurbine, 0);

	double dDistInResolutionUnits = dDistPastMin / axialResolution;
	int iLowerIndex = (int)dDistInResolutionUnits;
	size_t iUpperIndex = iLowerIndex + 1;
	dDistInResolutionUnits -= iLowerIndex;

	if (iUpperIndex >= matEVWakeWidths.ncols())
		return 0.0;

	return rotorDiameter * max_of(1.0, (matEVWakeWidths.at(upwindTurbine, iLowerIndex) * (1.0 - dDistInResolutionUnits) + matEVWakeWidths.at(upwindTurbine, iUpperIndex) * dDistInResolutionUnits));	// in meters
}

double eddyViscosityWakeModel::addedTurbulenceIntensity(double Ct, double deltaX)
{
	if (deltaX == 0) return 0.0; 
	return max_of(0.0, (Ct / 7.0)*(1.0 - (2.0 / 5.0)*log(deltaX / rotorDiameter)));
}

void eddyViscosityWakeModel::nearWakeRegionLength(double U, double Ii, double Ct, double, VMLN& vmln)
{
	// Ii is incident TI in percent at upstream turbine
	Ct = max_of(min_of(0.999, Ct), minThrustCoeff);

	// these formulae can be found in Wind Energy Handbook by Bossanyi, pages 36 and 37
	// although there are errors in that book so it has been supplemented from the original work  
	// by Vermeulen, P.E.J.  TNO - report
	double dr_dx;

	double m = 1.0 / sqrt(1.0 - Ct);

	double r0 = 0.5*rotorDiameter*sqrt((m + 1.0) / 2.0);

	double t1 = sqrt(0.214 + 0.144*m);
	double t2 = sqrt(0.134 + 0.124*m);

	double n = (t1*(1.0 - t2)) / ((1.0 - t1)*t2);

	double dr_dx_A = Ii < 2.0 ? 0.05*Ii : 0.025*Ii + 0.05; // from original TNO report

	double dr_dx_M = ((1.0 - m)*sqrt(1.49 + m)) / ((1.0 + m)*9.76);

	double dr_dx_L = 0.012*(double)nBlades * wTurbine->tipSpeedRatio(U);

	dr_dx = sqrt(dr_dx_A*dr_dx_A + dr_dx_M*dr_dx_M + dr_dx_L*dr_dx_L);		// wake growth rate

	/////////////////////////////////////////////////////////

	vmln.m = m;

	vmln.diam = rotorDiameter;

	vmln.Xh = r0 / (dr_dx); // end of region 1

	vmln.Xn = n*vmln.Xh;	// end of region 2

	return;

	//	this part not fully used just now but its coded and it could be used in future enhancements
	//vmln.Xf = 5.0*vmln.Xn; // end of region 3
	//vmln.Ro = r0;
	//double c1 = 0.416 + 0.134*m;
	//double c2 = 0.021*(1.0+0.8*m-0.45*m*m);
	//vmln.Rh = r0*((-c1+sqrt(c1*c1+4.0*c2))/(2.0*c2)); // A
	//vmln.Rn = r0 + n*(vmln.Rh-r0);
	//vmln.dUc_Uinf_Xn = ((m-1.0)/m)*((-0.258*m + sqrt(0.066564*m*m + 0.536*(1.0-m)*(vmln.Ro/vmln.Rn)*(vmln.Ro/vmln.Rn)))/(0.268*(1.0-m))); // A-16
	//vmln.Rf = m_dRotorDiameter*(sqrt(m*m-1.0)/0.882*m) * (1.0/sqrt(0.353*vmln.dUc_Uinf_Xn - 0.0245*vmln.dUc_Uinf_Xn*vmln.dUc_Uinf_Xn));
}

double eddyViscosityWakeModel::simpleIntersect(double distToCenter, double radiusTurbine, double radiusWake)
{	// returns the fraction of overlap, NOT an area
	if (distToCenter<0 || radiusTurbine<0 || radiusWake<0)
		return 0;

	if (distToCenter > radiusTurbine + radiusWake)
		return 0;

	if (radiusWake >= distToCenter + radiusTurbine)
		return 1; // turbine completely inside wake

	return min_of(1.0, max_of(0.0, (radiusTurbine + radiusWake - distToCenter) / (2 * radiusTurbine)));
}

double eddyViscosityWakeModel::totalTurbulenceIntensity(double ambientTI, double additionalTI, double Uo, double Uw, double partial)
{
	if (Uw <= 0.0)
		return ambientTI;

	double f = max_of(0.0, ambientTI*ambientTI + additionalTI*additionalTI);
	f = sqrt(f)*Uo / Uw;
	return (1.0 - partial)*ambientTI + partial*f;
	//	return f;
}

/// Simplified Eddy-Viscosity model as per "Simplified Solution To The Eddy Viscosity Wake Model" - 2009 by Dr Mike Anderson of RES
void eddyViscosityWakeModel::wakeCalculations(/*INPUTS */ const double air_density, const double aDistanceDownwind[], const double aDistanceCrosswind[],
	/*OUTPUTS*/ double power[], double Thrust[], double adWindSpeed[], double aTurbulence_intensity[])
{
	double dTurbineRadius = rotorDiameter / 2;
	matEVWakeDeficits.fill(0.0);
	matEVWakeWidths.fill(0.0);
	std::vector<VMLN> vmln(nTurbines);
	std::vector<double> Iamb(nTurbines, turbulenceCoeff);

	// Note that this 'i' loop starts with i=0, which is necessary to initialize stuff for turbine[0]
	for (size_t i = 0; i<nTurbines; i++) // downwind turbines, but starting with most upwind and working downwind
	{
		double dDeficit = 0, Iadd = 0, dTotalTI = aTurbulence_intensity[i];
		//		double dTOut=0, dThrustCoeff=0;
		for (size_t j = 0; j<i; j++) // upwind turbines - turbines upwind of turbine[i]
		{
			// distance downwind = distance from turbine i to turbine j along axis of wind direction
			double dDistAxialInDiameters = fabs(aDistanceDownwind[i] - aDistanceDownwind[j]) / 2.0;
			if (abs(dDistAxialInDiameters) <= 0.0001)
				continue; // if this turbine isn't really upwind, move on to the next

			// separation crosswind between turbine i and turbine j
			double dDistRadialInDiameters = fabs(aDistanceCrosswind[i] - aDistanceCrosswind[j]) / 2.0;

			double dWakeRadiusMeters = getWakeWidth((int)j, dDistAxialInDiameters);  // the radius of the wake
			if (dWakeRadiusMeters <= 0.0)
				continue;

			// calculate the wake deficit
			double dDef = wakeDeficit((int)j, dDistRadialInDiameters, dDistAxialInDiameters);
			double dWindSpeedWaked = adWindSpeed[0] * (1 - dDef); // wind speed = free stream * (1-deficit)

			// keep it if it's bigger
			dDeficit = max_of(dDeficit, dDef);

			Iadd = addedTurbulenceIntensity( Thrust[j], dDistAxialInDiameters*rotorDiameter );

			double dFractionOfOverlap = simpleIntersect(dDistRadialInDiameters*rotorDiameter, dTurbineRadius, dWakeRadiusMeters);
			dTotalTI = max_of(dTotalTI, totalTurbulenceIntensity(aTurbulence_intensity[i], Iadd, adWindSpeed[0], dWindSpeedWaked, dFractionOfOverlap));
		}
		// use the max deficit found to calculate the turbine output
		adWindSpeed[i] = adWindSpeed[0] * (1 - dDeficit);
		aTurbulence_intensity[i] = dTotalTI;
		wTurbine->turbinePower(adWindSpeed[i], air_density, &power[i], &Thrust[i]);

		//if (Power[0] < 0.0)
		//	Eff[i] = 0.0;
		//else
		//	Eff[i] = 100.0*(Power[i] + 0.0001) / (Power[0] + 0.0001);

		//// now that turbine[i] wind speed, output, thrust, etc. have been calculated, calculate wake characteristics for it, because downwind turbines will need the info
		//if (!fill_turbine_wake_arrays_for_EV((int)i, adWindSpeed[0], adWindSpeed[i], Power[i], Thrust[i], aTurbulence_intensity[i], fabs(aDistanceDownwind[m_iNumberOfTurbinesInFarm - 1] - aDistanceDownwind[i])*dTurbineRadius))
		//{
		//	if (m_sErrDetails.length() == 0) m_sErrDetails = "Could not calculate the turbine wake arrays in the Eddy-Viscosity model.";
		//}
		nearWakeRegionLength(adWindSpeed[i], Iamb[i], Thrust[i], air_density, vmln[i]);
	}
}

