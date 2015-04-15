#include <math.h>

#include "lib_battery.h"
/* 
Define Capacity Model 
*/

capacity_t::capacity_t(double q)
{
	_q0 = q;
	_qmax = q;
	_qmax0 = q;
	_I = 0.;
	_P = 0.;

	// Initialize SOC to 100, DOD to 0
	_SOC = 100;
	_DOD = 0;
	_DOD_prev = 0;

	// Initialize charging states
	_prev_charge = DISCHARGE;
	_chargeChange = false;
}
void capacity_t::check_charge_change()
{
	int charging = NO_CHARGE;

	// charge state 
	if (_I < 0)
		charging = CHARGE;
	else if (_I > 0)
		charging = DISCHARGE;

	// Check if charge changed 
	_chargeChange = false;
	if ((charging != _prev_charge) && (charging != NO_CHARGE) && (_prev_charge != NO_CHARGE))
	{
		_chargeChange = true;
		_prev_charge = charging;
	}
}
void capacity_t::update_SOC(double q0)
{
	_SOC = 100.*(q0 / _qmax);
	_DOD = 100. - _SOC;
}
void capacity_t::update_SOC()
{
	_SOC = 100.*(_q0 / _qmax);
	_DOD = 100. - _SOC;
}
bool capacity_t::chargeChanged(){return _chargeChange;}
double capacity_t::SOC(){ return _SOC; }
double capacity_t::DOD(){ return _DOD; }
double capacity_t::prev_DOD(){ return _DOD_prev; }
double capacity_t::q0(){ return _q0; }
double capacity_t::qmax(){ return _qmax; }
double capacity_t::I(){ return _I; }
double capacity_t::P(){ return _P; }


/*
Define KiBam Capacity Model
*/
capacity_kibam_t::capacity_kibam_t(double q20, double t1, double q1, double q10) :
capacity_t(q20)
{
	_q10 = q10;
	_q20 = q20;
	_I20 = q20/20.;

	// parameters for c, k calculation
	_q1 = q1;
	_q2 = q10;
	_t1 = t1;
	_t2 = 10.;
	_F1 = q1 / q20; // use t1, 20
	_F2 = q1 / q10;  // use t1, 10

	// compute the parameters
	parameter_compute();
	_qmax0 = _qmax;

	// Assume initial current is 20 hour discharge current
	// Assume initial charge is 20 capacity
	double T = _q0 / _I20;
	_qmaxI = qmax_of_i_compute(T);
	_q0 = _q20;

	// Initialize charge quantities.  
	// Assumes battery is initially fully charged
	_q1_0 = _q0*_c;
	_q2_0 = _q0 - _q1_0;
}

double capacity_kibam_t::c_compute(double F, double t1, double t2, double k_guess)
{
	double num = F*(1 - exp(-k_guess*t1))*t2 - (1 - exp(-k_guess*t2))*t1;
	double denom = F*(1 - exp(-k_guess*t1))*t2 - (1 - exp(-k_guess*t2))*t1 - k_guess*F*t1*t2 + k_guess*t1*t2;
	return (num / denom);
}

double capacity_kibam_t::q1_compute(double q10, double q0, double dt, double I)
{
	double A = q10*exp(-_k*dt);
	double B = (q0*_k*_c - I)*(1 - exp(-_k*dt)) / _k;
	double C = I*_c*(_k*dt - 1 + exp(-_k*dt)) / _k;
	return (A + B - C);
}

double capacity_kibam_t::q2_compute(double q20, double q0, double dt, double I)
{
	double A = q20*exp(-_k*dt);
	double B = q0*(1 - _c)*(1 - exp(-_k*dt));
	double C = I*(1 - _c)*(_k*dt - 1 + exp(-_k*dt)) / _k;
	return (A + B - C);
}

double capacity_kibam_t::Icmax_compute(double q10, double q0, double dt)
{
	double num = -_k*_c*_qmax + _k*q10*exp(-_k*dt) + q0*_k*_c*(1 - exp(-_k*dt));
	double denom = 1 - exp(-_k*dt) + _c*(_k*dt - 1 + exp(-_k*dt));
	return (num / denom);
}

double capacity_kibam_t::Idmax_compute(double q10, double q0, double dt)
{
	double num = _k*q10*exp(-_k*dt) + q0*_k*_c*(1 - exp(-_k*dt));
	double denom = 1 - exp(-_k*dt) + _c*(_k*dt - 1 + exp(-_k*dt));
	return (num / denom);
}

double capacity_kibam_t::qmax_compute()
{
	double num = _q20*((1 - exp(-_k * 20)) * (1 - _c) + _k*_c * 20);
	double denom = _k*_c * 20;
	return (num / denom);
}

double capacity_kibam_t::qmax_of_i_compute(double T)
{
	return ((_qmax*_k*_c*T) / (1 -exp(-_k*T) + _c*(_k*T - 1 + exp(-_k*T))));
}
void capacity_kibam_t::parameter_compute()
{
	double k_guess = 0.;
	double c1 = 0.;
	double c2 = 0.;
	double minRes = 10000.;

	for (int i = 0; i < 5000; i++)
	{
		k_guess = i*0.001;
		c1 = c_compute(_F1, _t1, 20, k_guess);
		c2 = c_compute(_F2, _t1, _t2, k_guess);

		if (fabs(c1 - c2) < minRes)
		{
			minRes = fabs(c1 - c2);
			_k = k_guess;
			_c = 0.5*(c1 + c2);
		}
	}
	_qmax = qmax_compute();
}

void capacity_kibam_t::updateCapacity(double P, voltage_t * voltage_model, double dt, int cycles)
{
	_DOD_prev = _DOD;
	double V = voltage_model->battery_voltage();
	_P = P;
	_I = _P / V;
	double Idmax = 0.;
	double Icmax = 0.;
	double Id = 0.;
	double Ic = 0.;
	double q1 = 0.;
	double q2 = 0.;

	if (_I > 0)
	{
		Idmax = Idmax_compute(_q1_0, _q0, dt);
		Id = fmin(_I, Idmax);
		_I = Id;
	}
	else if (_I < 0 )
	{
		Icmax = Icmax_compute(_q1_0, _q0, dt);
		Ic = -fmin(fabs(_I), fabs(Icmax));
		_I = Ic;
	}

	/*
	// Loop so we don't discharge much beyond cut-off voltage
	// double v_dt = 1. / 60.;
	// int n = dt / v_dt;
	// int n = 1; // need to determine if this is a good way to implement

	// for (int ii = 0; ii != n; ii++)
	// {
		if (voltage_model->cutoff_voltage() > voltage_model->cell_voltage())
			break;
	*/
		// new charge levels
		q1 = q1_compute(_q1_0, _q0, dt, _I);
		q2 = q2_compute(_q2_0, _q0, dt, _I);

		// update max charge at this current
		if (fabs(_I) > 0)
			_qmaxI = qmax_of_i_compute(fabs(_qmaxI / _I));

		// update the SOC
		_SOC = ((q1 + q2) / _qmax) * 100;
		
		// due to dynamics, it's possible SOC could be slightly above 1 or below 0
		if (_SOC > 100.)
			_SOC = 100.;
		else if (_SOC < 0.)
			_SOC = 0.;

		_DOD = 100. - _SOC;

		// update internal variables 
		_q1_0 = q1;
		_q2_0 = q2;
		_q0 = q1 + q2;

		check_charge_change(); 

		// update voltage
		voltage_model->updateVoltage(this, dt);
	// }
}
void capacity_kibam_t::updateCapacityForThermal(double capacity_percent)
{
	_q0 *= capacity_percent*0.01;
	_q1_0 *= capacity_percent*0.01;
	_q2_0 *= capacity_percent*0.01;
	update_SOC();
}
void capacity_kibam_t::updateCapacityForLifetime(double capacity_percent)
{
	_q0 *= capacity_percent*0.01;
	_q1_0 *= capacity_percent*0.01;
	_q2_0 *= capacity_percent*0.01;
	_qmax = _qmax0* capacity_percent*0.01;
	update_SOC();
}

double capacity_kibam_t::q1(){ return _q1_0; }
double capacity_kibam_t::q2(){ return _q2_0; }
double capacity_kibam_t::qmaxI(){return _qmaxI;}
double capacity_kibam_t::q10(){ return _q10; }
double capacity_kibam_t::q20(){return _q20;}


/*
Define Lithium Ion capacity model
*/
capacity_lithium_ion_t::capacity_lithium_ion_t(double q) :capacity_t(q){};
capacity_lithium_ion_t::~capacity_lithium_ion_t(){}

void capacity_lithium_ion_t::updateCapacity(double P, voltage_t * voltage_model, double dt, int cycles)
{
	_DOD_prev = _DOD;
	double q0_old = _q0;

	// currently just a tank of coloumbs
	 double V = voltage_model->battery_voltage();
	_I = P / V;
	_P = P;

	/*
	// Loop so we don't discharge much beyond cut-off voltage
	// double v_dt = 1.;// / 60.;
	// int n = dt / v_dt;
	// int n = 1; // need to determine if this is a good way to implement

	// for (int ii = 0; ii != n; ii++)
	{
		if (voltage_model->cutoff_voltage() > voltage_model->cell_voltage())
			break;
	*/
		// update charge ( I > 0 discharging, I < 0 charging)
		_q0 -= _I*dt;

		// check if overcharged
		if (_q0 > _qmax)
		{
			_I = -(_qmax - q0_old) / dt;
			_P = _I*V;
			_q0 = _qmax;
		}

		// check if undercharged (implement minimum charge limit)
		if (_q0 < 0)
		{
			_I = (q0_old) / dt;
			_P = _I*V;
			_q0 = 0;
		}

		// update SOC, DOD
		_SOC = (_q0 / _qmax) * 100;
		_DOD = 100 - _SOC;

		check_charge_change();
		
		// update voltage
		voltage_model->updateVoltage(this, dt);
	// }
}
void capacity_lithium_ion_t::updateCapacityForThermal(double capacity_percent)
{
	_q0 *= capacity_percent*0.01;
	update_SOC();
}
void capacity_lithium_ion_t::updateCapacityForLifetime(double capacity_percent)
{
	_q0 *= capacity_percent*0.01;
	_qmax = _qmax0 * capacity_percent * 0.01;
	update_SOC();
}
double capacity_lithium_ion_t::q1(){return _q0;}
double capacity_lithium_ion_t::qmaxI(){return _qmax;}
double capacity_lithium_ion_t::q10(){return _qmax;}


/*
Define Voltage Model
*/
voltage_t::voltage_t(int num_cells, double voltage, double cutoff)
{
	_num_cells = num_cells;
	_cell_voltage = voltage;
	_cutoff_voltage = cutoff;
}

double voltage_t::battery_voltage(){ return _num_cells*_cell_voltage; }
double voltage_t::cell_voltage(){ return _cell_voltage; }
double voltage_t::cutoff_voltage(){ return _cutoff_voltage; }


// Dynamic voltage model
voltage_dynamic_t::voltage_dynamic_t(int num_cells, double voltage, double Vfull, double Vexp, double Vnom, double Qfull, double Qexp, double Qnom, double C_rate, double V_cutoff):
voltage_t(num_cells, voltage, V_cutoff)
{
	_Vfull = Vfull;
	_Vexp = Vexp;
	_Vnom = Vnom;
	_Qfull = Qfull;
	_Qexp = Qexp;
	_Qnom = Qnom;
	_C_rate = C_rate;

	// assume fully charged, not the nominal value
	_cell_voltage = _Vfull;

	parameter_compute();
};

void voltage_dynamic_t::parameter_compute()
{
	// Determines parameters according to page 2 of:
	// Tremblay 2009 "A Generic Bettery Model for the Dynamic Simulation of Hybrid Electric Vehicles"
	double eta = 0.995;
	double I = _Qfull*_C_rate; // [A]
	_R = _Vnom*(1. - eta) / (_C_rate*_Qnom); // [Ohm]
	_A = _Vfull - _Vexp; // [V]
	_B = 3. / _Qexp;     // [1/Ah]
	_K = ((_Vfull - _Vnom + _A*(std::exp(-_B*_Qnom) - 1))*(_Qfull - _Qnom)) / (_Qnom); // [V] - polarization voltage
	_E0 = _Vfull + _K + _R*I - _A;
}

void voltage_dynamic_t::updateVoltage(capacity_t * capacity,  double dt)
{

	double Q = capacity->qmaxI();
	double I = capacity->I();
	double q0 = capacity->q0();

	_cell_voltage = voltage_model(Q/_num_cells,I/_num_cells,q0/_num_cells);
//	if (q0/Q > 0.01)
//		_cell_voltage = voltage_model_tremblay_hybrid(Q / _num_cells, fabs(I) / _num_cells, q0 / _num_cells, dt);
}
double voltage_dynamic_t::voltage_model(double Q, double I, double q0)
{
	// Should increase when charge increases, decrease when charge decreases
	// everything in here is on a per-cell basis
	// Unnewehr Universal Model

	double term1 = _E0 - _R*I;
	double term2 = _K*(1 - q0/Q);
	double V = term1 - term2; 
	return V;
}
double voltage_dynamic_t::voltage_model_tremblay_hybrid(double Q, double I, double q0, double dt)
{
	// everything in here is on a per-cell basis
	// Unnewehr Universal Model + Tremblay Dynamic Model
	// dt - should be in hours

	double term1 = _E0 -_R*I; // common to both
	double f = 1 - q0 / Q;
	double term2 = _K*(1. / (1 - f));
	double term3 = _A*exp(-_B*I*dt); // from Tremblay
	double V = term1 - term2 + term3;
	return V;
}

// Basic voltage model
voltage_basic_t::voltage_basic_t(int num_cells, double voltage) :
voltage_t(num_cells, voltage, 0.){}

void voltage_basic_t::updateVoltage(capacity_t * capacity, double dt){}

/*
Define Lifetime Model
*/

lifetime_t::lifetime_t(const util::matrix_t<double> &batt_lifetime_matrix)
{
	_batt_lifetime_matrix = batt_lifetime_matrix;

	for (int i = 0; i < _batt_lifetime_matrix.nrows(); i++)
	{
		_DOD_vect.push_back(batt_lifetime_matrix.at(i,0));
		_cycles_vect.push_back(batt_lifetime_matrix.at(i,1));
		_capacities_vect.push_back(batt_lifetime_matrix.at(i, 2));
	}
	// initialize other member variables
	_nCycles = 0;
	_Dlt = 0;
	_Clt = bilinear(0.,0);
	_jlt = 0;
	_klt = 0; 
	_Xlt = 0;
	_Ylt = 0;
	_Slt = 0;
	_Range = 0;
	_average_range = 0;
	_fortyPercent = 0;
	_hundredPercent = 0;
}

lifetime_t::~lifetime_t(){}

void lifetime_t::rainflow(double DOD)
{
	// initialize return code
	int retCode = LT_GET_DATA;

	// Begin algorithm
	_Peaks.push_back(DOD);
	bool atStepTwo = true;

	// Assign S, which is the starting peak or valley
	if (_jlt == 0)
	{
		_Slt = DOD;
		_klt = _jlt;
	}

	// Loop until break
	while (atStepTwo)
	{
		// Rainflow: Step 2: Form ranges X,Y
		if (_jlt >= 2)
			rainflow_ranges();
		else
		{
			// Get more data (Step 1)
			retCode = LT_GET_DATA;
			break;
		}

		// Rainflow: Step 3: Compare ranges
		retCode = rainflow_compareRanges();

		// We break to get more data, or if we are done with step 5
		if (retCode == LT_GET_DATA)
			break;
	}

	if (retCode == LT_GET_DATA)
		_jlt++;
}

void lifetime_t::rainflow_ranges()
{
	_Ylt = fabs(_Peaks[_jlt - 1] - _Peaks[_jlt - 2]);
	_Xlt = fabs(_Peaks[_jlt] - _Peaks[_jlt - 1]);
}
void lifetime_t::rainflow_ranges_circular(int index)
{
	int end = _Peaks.size() - 1;
	if (index == 0)
	{
		_Xlt = fabs(_Peaks[0] - _Peaks[end]);
		_Ylt = fabs(_Peaks[end] - _Peaks[end - 1]);
	}
	else if (index == 1)
	{
		_Xlt = fabs(_Peaks[1] - _Peaks[0]);
		_Ylt = fabs(_Peaks[0] - _Peaks[end]);
	}
	else
		rainflow_ranges();
}

int lifetime_t::rainflow_compareRanges()
{
	int retCode = LT_SUCCESS;
	bool contained = true;

	if (_Xlt < _Ylt)
		retCode = LT_GET_DATA;
	else if (_Xlt == _Ylt)
	{
		if ((_Slt == _Peaks[_jlt - 1]) || (_Slt == _Peaks[_jlt - 2]))
			retCode = LT_GET_DATA;
		else
			contained = false;
	}
	else if (_Xlt >= _Ylt)
	{
		if (_Xlt > _Ylt)
		{
			if ((_Slt == _Peaks[_jlt - 1]) || (_Slt == _Peaks[_jlt - 2]))
			{
				// Step 4: Move S to next point in vector, then go to step 1
				_klt++;
				_Slt = _Peaks[_klt];
				retCode = LT_GET_DATA;
			}
			else
				contained = false;
		}
		else if (_Xlt == _Ylt)
		{
			if ((_Slt != _Peaks[_jlt - 1]) && (_Slt != _Peaks[_jlt - 2]))
				contained = false;
		}

	}

	// Step 5: Count range Y, discard peak & valley of Y, go to Step 2
	if (!contained)
	{
		
		_Range = _Ylt;
		_average_range = (_average_range*_nCycles + _Range) / (_nCycles + 1);
		_nCycles++;
		_Clt = bilinear(_average_range, _nCycles);

		// check DOD, increment counters
		if (_Range > 40.)
			_fortyPercent++;
		else if (_Range > 98)
			_hundredPercent++;

		// discard peak & valley of Y
		double save = _Peaks[_jlt];
		_Peaks.pop_back(); 
		_Peaks.pop_back();
		_Peaks.pop_back();
		_Peaks.push_back(save);
		_jlt -= 2;
		// stay in while loop
		retCode = LT_RERANGE;
	}

	return retCode;
}

void lifetime_t::rainflow_finish()
{
	// starting indices, must decrement _jlt by 1
	int ii = 0;
	_jlt--;
	double P = 0.;
	int rereadCount = 0;


	while ( rereadCount <= 1 )
	{
		if (ii < _Peaks.size())
			P = _Peaks[ii];
		else
			break;

		// Step 6
		if (P == _Slt)
			rereadCount++;

		bool atStepSeven = true;

		// Step 7: Form ranges X,Y
		while (atStepSeven)
		{
			if (_jlt >= 2)
				rainflow_ranges_circular(ii);
			else
			{
				atStepSeven = false;
				if (_jlt == 1)
				{
					_Peaks.push_back(P);
					_jlt++;
					// move to end point
					ii = _jlt;
					rainflow_ranges_circular(ii);
				}
				// _jlt == 0
				else
				{
					// force out of while
					rereadCount++;
					break;
				}
			}

			// Step 8: compare X,Y
			if (_Xlt < _Ylt)
			{
				atStepSeven = false;
				// move to next point (Step 6)
				ii++;
			}
			else
			{
				_Range = _Ylt;
				_average_range = (_average_range*_nCycles + _Range) / (_nCycles + 1);
				_nCycles++;
				_Clt = bilinear(_average_range, _nCycles);

				// check DOD, increment counters
				if (_Range > 40.)
					_fortyPercent++;
				else if (_Range > 98.)
					_hundredPercent++;

				// Discard peak and vally of Y
				if (ii = 0)
				{
					// Ylt is at end 
					_Peaks.pop_back();
					_Peaks.pop_back();
				}
				else if (ii = 1)
				{	// Ylt is at index 0 and end
					_Peaks.erase(_Peaks.begin());
					_Peaks.pop_back();
					//ii--;
				}
				else
				{
					// Ylt is at index-1, index-2
					_Peaks.erase(_Peaks.begin() + ii - 2, _Peaks.begin() + ii - 1);
					//ii -= 2;
				}
				_jlt -= 2;
			}
		}
	}
}
int lifetime_t::cycles_elapsed(){return _nCycles;}
double lifetime_t::capacity_percent(){ return _Clt; }
int lifetime_t::forty_percent_cycles(){ return _fortyPercent; }
int lifetime_t::hundred_percent_cycles(){ return _hundredPercent; }


double lifetime_t::bilinear(double DOD, int cycle_number)
{
	/*
	Work could be done to make this simpler
	Current idea is to interpolate first along the C = f(n) curves for each DOD to get C_DOD_, C_DOD_+ 
	Then interpolate C_, C+ to get C at the DOD of interest
	*/

	std::vector<double> D_unique_vect;
	std::vector<double> C_n_low_vect;
	std::vector<double> D_high_vect;
	std::vector<double> C_n_high_vect;
	std::vector<int> low_indices;
	std::vector<int> high_indices;
	double D = 0.;
	int n = 0;
	double C = 100;

	// get unique values of D
	D_unique_vect.push_back(_DOD_vect[0]);
	for (int i = 0; i < _DOD_vect.size(); i++){
		bool contained = false;
		for (int j = 0; j < D_unique_vect.size(); j++){
			if (_DOD_vect[i] == D_unique_vect[j]){
				contained = true;
				break;
			}
		}
		if (!contained){
			D_unique_vect.push_back(_DOD_vect[i]);
		}
	}
	n = D_unique_vect.size();

	if (n > 1)
	{
		// get where DOD is bracketed [D_lo, DOD, D_hi]
		double D_lo = 0;
		double D_hi = 100;

		for (int i = 0; i < _DOD_vect.size(); i++)
		{
			D = _DOD_vect[i];
			if (D < DOD && D > D_lo)
				D_lo = D;
			else if (D > DOD && D < D_hi)
				D_hi = D;
		}

		// Seperate table into bins
		double D_min = 100.;
		double D_max = 0.;

		for (int i = 0; i < _DOD_vect.size(); i++)
		{
			D = _DOD_vect[i];
			if (D == D_lo)
				low_indices.push_back(i);
			else if (D == D_hi)
				high_indices.push_back(i);

			if (D < D_min){ D_min = D; }
			else if (D > D_max){ D_max = D; }
		}
		size_t n_rows_lo = low_indices.size();
		size_t n_rows_hi = high_indices.size();
		size_t n_cols = 2;

		// If we aren't bounded, fill in values
		if (n_rows_lo == 0)
		{
			// Assumes 0% DOD
			for (int i = 0; i < n_rows_hi; i++)
			{
				C_n_low_vect.push_back(0. + i * 500); // cycles
				C_n_low_vect.push_back(100.); // 100 % capacity
			}
		}
		else if (n_rows_hi == 0)
		{
			// Assume 100% DOD
			for (int i = 0; i < n_rows_lo; i++)
			{
				C_n_high_vect.push_back(100. + i * 500); // cycles
				C_n_high_vect.push_back(0.); // 100 % capacity
			}
		}

		if (n_rows_lo != 0)
		{
			for (int i = 0; i < n_rows_lo; i++)
			{
				C_n_low_vect.push_back(_cycles_vect[low_indices[i]]);
				C_n_low_vect.push_back(_capacities_vect[low_indices[i]]);
			}
		}
		if (n_rows_hi != 0)
		{
			for (int i = 0; i < n_rows_hi; i++)
			{
				C_n_high_vect.push_back(_cycles_vect[high_indices[i]]);
				C_n_high_vect.push_back(_capacities_vect[high_indices[i]]);
			}
		}
		n_rows_lo = C_n_low_vect.size() / n_cols;
		n_rows_hi = C_n_high_vect.size() / n_cols;

		if (n_rows_lo == 0 || n_rows_hi == 0)
		{
			// need a safeguard here
		}

		util::matrix_t<double> C_n_low(n_rows_lo, n_cols, &C_n_low_vect);
		util::matrix_t<double> C_n_high(n_rows_lo, n_cols, &C_n_high_vect);

		// Compute C(D_lo, n), C(D_hi, n)
		double C_Dlo = util::linterp_col(C_n_low, 0, cycle_number, 1);
		double C_Dhi = util::linterp_col(C_n_high, 0, cycle_number, 1);

		if (C_Dlo < 0.)
			C_Dlo = 0.;
		if (C_Dhi > 100.)
			C_Dhi = 100.;

		// Interpolate to get C(D, n)
		C = util::interpolate(D_lo, C_Dlo, D_hi, C_Dhi, DOD);
	}
	// just have one row, single level interpolation
	else
	{
		C = util::linterp_col(_batt_lifetime_matrix, 1, cycle_number, 2);
	}

	return C;
}


/*
Define Thermal Model
*/
thermal_t::thermal_t(double mass, double length, double width, double height, 
	double Cp,  double h, double T_room, double R,
	const util::matrix_t<double> &c_vs_t )
{
	_cap_vs_temp = c_vs_t;
	_mass = mass;
	_length = length;
	_width = width;
	_height = height;
	_Cp = Cp;
	_h = h;
	_T_room = T_room;
	_R = R;
	_capacity_percent = 100;

	// assume all surfaces are exposed
	_A = 2 * (length*width + length*height + width*height);

	// initialize to room temperature
	_T_battery = T_room;

	// curve fit
	int n = _cap_vs_temp.nrows();
	for (int i = 0; i < n; i++)
	{
		_cap_vs_temp(i,0) += 273.15; // convert C to K
	}
}

void thermal_t::updateTemperature(double I, double dt)
{
	//double T_new = rk4(I, dt*_hours_to_seconds);
	double T_new = trapezoidal(I, dt*_hours_to_seconds);
	_T_battery = T_new;
}
double thermal_t::f(double T_battery, double I)
{
	return (1 / (_mass*_Cp)) * ((_h*(_T_room - T_battery)*_A) + pow(I, 2)*_R);
}
double thermal_t::rk4( double I, double dt)
{
	double k1 = dt*f(_T_battery, I);
	double k2 = dt*f(_T_battery + k1 / 2, I);
	double k3 = dt*f(_T_battery + k2 / 2, I);
	double k4 = dt*f(_T_battery + k3, I);
	return (_T_battery + (1. / 6)*(k1 + k4) + (1. / 3.)*(k2 + k3));
}
double thermal_t::trapezoidal(double I, double dt)
{
	double B = 1 / (_mass*_Cp);
	double C = _h*_A;
	double D = pow(I, 2)*_R;
	double T_prime = f(_T_battery, I);

	return (_T_battery + 0.5*dt*(T_prime + B*(C*_T_room + D))) / (1 + 0.5*dt*B*C);
}
double thermal_t::T_battery(){ return _T_battery; }
double thermal_t::capacity_percent()
{ 
	return util::linterp_col(_cap_vs_temp, 0, _T_battery, 1); 
}
/*
Define Losses
*/
losses_t::losses_t(lifetime_t * lifetime, thermal_t * thermal, capacity_t* capacity)
{
	_lifetime = lifetime;
	_thermal = thermal;
	_capacity = capacity;
	_nCycle = 0;
}
void losses_t::run_losses()
{
	// only update losses if there is power flow from
	if (_capacity->I() > 0)
	{
		// only update capacity for lifetime if cycle number has changed
		if (_lifetime->cycles_elapsed() > _nCycle)
		{
			_nCycle++;
			_capacity->updateCapacityForLifetime(_lifetime->capacity_percent());
		}
		_capacity->updateCapacityForThermal(_thermal->capacity_percent());
	}
}
/* 
Define Battery 
*/
battery_t::battery_t(){};
battery_t::battery_t(double power_conversion_efficiency, double dt)
{
	_power_conversion_efficiency = power_conversion_efficiency;
	_dt = dt;
}
void battery_t::initialize(capacity_t *capacity, voltage_t * voltage, lifetime_t * lifetime, thermal_t * thermal, losses_t * losses)
{
	_capacity = capacity;
	_lifetime = lifetime;
	_voltage = voltage;
	_thermal = thermal;
	_losses = losses;
	_firstStep = true;
}

void battery_t::run(double P)
{
	double lastDOD = _capacity->prev_DOD();

	if (_capacity->chargeChanged() || _firstStep)
	{
		runLifetimeModel(lastDOD);
		_firstStep = false;
	}
	
	// Compute temperature at end of timestep
	runThermalModel(P / _voltage->battery_voltage());
	runCapacityModel(P, _voltage);
	runLossesModel();
	// runVoltageModel();
}

void battery_t::finish()
{
	_lifetime->rainflow_finish();
}
void battery_t::runThermalModel(double I)
{
	_thermal->updateTemperature(I, _dt);
}

void battery_t::runCapacityModel(double P, voltage_t * V)
{
	_capacity->updateCapacity(P, V, _dt,_lifetime->cycles_elapsed() );
	// _capacity->updateCapacityForLifetime( _lifetime->capacity_percent() );
	// _capacity->updateCapacityForThermal(_thermal);
}

void battery_t::runVoltageModel()
{
	_voltage->updateVoltage(_capacity, _dt);
}

void battery_t::runLifetimeModel(double DOD)
{
	_lifetime->rainflow(DOD);
}
void battery_t::runLossesModel()
{
	_losses->run_losses();
}
capacity_t * battery_t::capacity_model()
{
	return _capacity;
}
voltage_t * battery_t::voltage_model()
{
	return _voltage;
}
double battery_t::chargeNeededToFill()
{
	// Leads to minor discrepency, since gets max capacity from the old time step, which is based on the previous current level
	// Since the new time step will have a different power requirement, and a different current level, this leads to charge_needed not truly equaling the charge needed at the new current.
	// I don't know if there is simple way to correct this, or if it is necessary to correct
	// double charge_needed =_capacity->getMaxCapacityAtCurrent() - _capacity->getTotalCapacity();
	double charge_needed = _capacity->qmax() - _capacity->q0();
	if (charge_needed > 0)
		return charge_needed;
	else
		return 0.;
}

double battery_t::getCurrentCharge()
{
	// return available capacity
	return _capacity->q1();
}

double battery_t::cell_voltage()
{
	return _voltage->cell_voltage();
}
double battery_t::battery_voltage()
{
	return _voltage->battery_voltage();
}

/*
Define Battery Bank
*/
battery_bank_t::battery_bank_t(battery_t * battery, int num_batteries_series, int num_batteries_parallel, int battery_chemistry, double power_conversion_efficiency)
{
	_battery = battery;
	_num_batteries_series = num_batteries_series;
	_num_batteries_parallel = num_batteries_parallel;
	_num_batteries = num_batteries_parallel + num_batteries_series;
	_battery_chemistry = battery_chemistry;
	_power_conversion_efficiency = power_conversion_efficiency; // currently unused
}
void battery_bank_t::run(double P)
{
	_battery->run(P / _num_batteries_series);
}
void battery_bank_t::finish()
{
	_battery->finish();
}
double battery_bank_t::bank_charge_needed()
{
	return ( _num_batteries*_battery->chargeNeededToFill() );
}
double battery_bank_t::bank_charge_available()
{
	return ( _num_batteries*_battery->getCurrentCharge() );
}
double battery_bank_t::bank_voltage()
{
	return _num_batteries_series*_battery->battery_voltage();
}
double battery_bank_t::cell_voltage()
{
	return _battery->cell_voltage();
}
int battery_bank_t::num_batteries(){ return _num_batteries; };
battery_t * battery_bank_t::battery(){ return _battery; };

/*
Dispatch base class
*/
dispatch_t::dispatch_t(battery_bank_t * BatteryBank, double dt)
{
	_BatteryBank = BatteryBank;
	_dt = dt;

	// positive quantities describing how much went to load
	_pv_to_load = 0.;
	_battery_to_load = 0.;
	_grid_to_load = 0.;

	// positive or negative quantities describing net power flows
	// note, do not include pv, since we don't modify from pv module
	_e_tofrom_batt = 0.;
	_e_grid = 0.;
}
double dispatch_t::energy_tofrom_battery(){ return _e_tofrom_batt; };
double dispatch_t::energy_tofrom_grid(){ return _e_grid; };
double dispatch_t::pv_to_load(){ return _pv_to_load; };
double dispatch_t::battery_to_load(){ return _battery_to_load; };
double dispatch_t::grid_to_load(){ return _grid_to_load; };

/*
Manual Dispatch
*/
dispatch_manual_t::dispatch_manual_t(battery_bank_t * BatteryBank, double dt, util::matrix_static_t<float, 12, 24> dm_sched, bool * dm_charge, bool *dm_discharge, bool * dm_gridcharge)
	: dispatch_t(BatteryBank, dt)
{
	_sched = dm_sched;
	_charge_array = dm_charge;
	_discharge_array = dm_discharge;
	_gridcharge_array = dm_gridcharge;
}
void dispatch_manual_t::dispatch(size_t hour_of_year, double e_pv, double e_load)
{
	int m, h;
	int iprofile = -1;
	getMonthHour(hour_of_year, &m, &h);
	iprofile = _sched(m - 1, h - 1) - 1;
	//if (iprofile < 0 || iprofile > 3) throw compute_module::exec_error("battery", "invalid battery dispatch schedule profile [0..3] ok");

	_can_charge = _charge_array[iprofile];
	_can_discharge = _discharge_array[iprofile];
	_can_grid_charge = _gridcharge_array[iprofile];


	// current charge state of battery from last time step.  
	double chargeNeededToFill = _BatteryBank->bank_charge_needed();						// [Ah] - qmax - qtotal
	double bank_voltage = _BatteryBank->bank_voltage();									// [V] 
	double cell_voltage = _BatteryBank->cell_voltage();									// [V]
	double energyNeededToFill = (chargeNeededToFill * cell_voltage)*watt_to_kilowatt;	// [kWh]
	
	_e_grid = 0.;																		// [KWh] energy needed from grid to charge battery.  Positive indicates sending to grid.  Negative pulling from grid.
	_e_tofrom_batt = 0.;																// [KWh] energy transferred to/from the battery.     Positive indicates discharging, Negative indicates charging
	_pv_to_load = 0.;
	_battery_to_load = 0.;
	_grid_to_load = 0.;

	// Is there extra energy from array
	if (e_pv > e_load)
	{
		if (_can_charge)
		{
			// use all energy available, it will only use what it can handle
			_e_tofrom_batt = -(e_pv - e_load);

			if ( (e_pv - e_load < energyNeededToFill) && _can_grid_charge)
				_e_tofrom_batt = -energyNeededToFill;
		}
		// if we want to charge from grid without charging from array
		else if (_can_grid_charge)
			_e_tofrom_batt = -energyNeededToFill;
	}
	// Or, is the demand greater than or equal to what the array provides
	else if (e_load >= e_pv)
	{
		// try to discharge full amount.  Will only use what battery can provide
		if (_can_discharge)
			_e_tofrom_batt = e_load - e_pv;
		// if we want to charge from grid
		// this scenario doesn't really make sense
		else if (_can_grid_charge)
			_e_tofrom_batt = -energyNeededToFill;
	}

	// Run Battery Model to update charge based on charge/discharge
	_BatteryBank->run(kilowatt_to_watt*_e_tofrom_batt / _dt);

	// Update how much power was actually used to/from battery
	double current = _BatteryBank->battery()->capacity_model()->I();
	_e_tofrom_batt = current * bank_voltage * _dt / 1000;// [kWh]

	
	// Update net grid energy
	// e_tofrom_batt > 0 -> more energy available to send to grid or meet load (discharge)
	// e_grid > 0 (sending to grid) e_grid < 0 (pulling from grid)
	_e_grid = e_pv + _e_tofrom_batt - e_load;


	// Next, get how much of each component will meet the load.  
	// PV always meets load before battery
	if (e_pv > e_load)
		_pv_to_load = e_load;
	else
	{
		_pv_to_load = e_pv;
		
		if (_e_tofrom_batt > 0)
			_battery_to_load = _e_tofrom_batt;

		_grid_to_load = e_load - (_pv_to_load + _battery_to_load);
	}
}


/*
Non-class functions
*/
void getMonthHour(int hourOfYear, int * out_month, int * out_hour)
{
	int tmpSum = 0;
	int hour = 0;
	int month;

	for ( month = 1; month <= 12; month++)
	{
		int hoursInMonth = util::hours_in_month(month);
		tmpSum += hoursInMonth;

		// found the month
		if (hourOfYear + 1 <= tmpSum)
		{
			// get the day of the month
			int tmp = floor((float)(hourOfYear) / 24);
			hour = (hourOfYear + 1) - (tmp * 24);
			break;
		}
	}

	*out_month = month;
	*out_hour = hour;

}
bool compare(int i, int j)
{
	return i == j;
}