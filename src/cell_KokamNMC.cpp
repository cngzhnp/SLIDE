/*
 * Cell_KokamNMC.cpp
 *
 * One of the child classes that implements a real cell.
 * The cycling parameters are for a high power 18650 NMC cell manufactured by Kokam.
 * The degradation parameters are set such that each mechanism clearly affects the battery life (which is not the case in reality).
 *
 * Copyright (c) 2019, The Chancellor, Masters and Scholars of the University
 * of Oxford, VITO nv, and the 'Slide' Developers.
 * See the licence file LICENCE.txt for more information.
 */

#include <cmath>
#include <iostream>
#include <array>

#include "cell_KokamNMC.hpp"
#include "read_CSVfiles.h"
#include "interpolation.h"
#include "state.hpp"
#include "param/param_default.hpp"

Cell_KokamNMC::Cell_KokamNMC(const slide::Model &MM, int verbosei)
	: Cell(settings::path::Kokam::namepos, 49, settings::path::Kokam::nameneg, 63, settings::path::Kokam::nameentropicC, 11, settings::path::Kokam::nameentropicCell, 11)
{
	/*
	 * Standard constructor to initialise the battery parameters
	 *
	 * IN
	 * M 			structure of the type Model, with the matrices of spatial discretisation for solid diffusion
	 * verbosei		integer indicating how verbose the simulation has to be.
	 * 				The higher the number, the more output there is.
	 * 				Recommended value is 1, only use higher values for debugging
	 * 				From 4 (and above) there will be too much info printed to follow what is going on, but this might be useful for debugging to find where the error is and why it is happening
	 * 					0 	almost no messages are printed, only in case of critical errors related to illegal parameters
	 * 					1 	error messages are printed in case of critical errors which might crash the simulation
	 * 					2 	all error messages are printed, whether the simulation can recover from the errors or not
	 * 					3 	on top of the output from 2, a message is printed every time a function in the Cycler and BasicCycler is started and terminated
	 * 					4 	on top of the output from 3, the high-level flow of the program in the Cycler is printed (e.g. 'we are going to discharge the cell')
	 * 					5 	on top of the output from 4, the low-level flow of the program in the BasicCycler is printed (e.g. 'in time step 101, the voltage is 3.65V')
	 * 					6 	on top of the output from 5, we also print details of the nonlinear search for the current needed to do a CV phase
	 * 					7 	on top of the output from 6, a message is printed every time a function in the Cell is started and terminated
	 *
	 * THROWS
	 * 110 	the matrices for the solid diffusion discretisation, produced by Matlab, are wrong
	 * 111 	the OCV curves are too long
	 */

	verbose = verbosei;

	// maximum concentrations
	Cmaxpos = 51385; // value for NMC
	Cmaxneg = 30555; // value for C
	C_elec = 1000;	 // standard concentration of 1 molar

	// constants
	n = 1;

	// Cell parameters
	nomCapacity = 2.7;
	Vmax = 4.2;	  // value for an NMC/C cell
	Vmin = 2.7;	  // value for an NMC/C cell
	Icell = 0;	  // initial cell current is 0A
	dIcell = 1.0; // ramp at 1A
	dt_I = 1e-2;  // ramp at 10ms so changing the current goes fast
				  // now changing the current takes 0.01 second per A

	// thermal parameters
	T_ref = PhyConst::Kelvin + 25;
	T_env = PhyConst::Kelvin + 25;
	Qch = 90; // 90 gives very good cooling, as if there is a fan pointed at the cell. values of 30-60 are representative for a cell on a shelf without forced cooling
	rho = 1626;
	Cp = 750;

	// geometry
	L = 1.6850e-4;
	Rp = 8.5e-6;  // do NOT CHANGE this value, if you do change it, you have to recalculate the spatial discretisation with the supplied Matlab scripts. See the word document '2 overview of the code', section 'Matlab setup before running the C++ code'
	Rn = 1.25e-5; // do NOT CHANGE this value, if you do change it, you have to recalculate the spatial discretisation with the supplied Matlab scripts. See the word document '2 overview of the code', section 'Matlab setup before running the C++ code'
	SAV = 252.9915;
	elec_surf = 0.0982;

	// Stress parameters
	sparam = settings::def_param::StressParam_Kokam;

	// main Li reaction
	kp = 5e-11; // fitting parameter
	kp_T = 58000;
	kn = 1.7640e-11; // fitting parameter
	kn_T = 20000;
	// The diffusion coefficients at reference temperature are part of 'State'.
	// The values are set in the block of code below ('Initialise state variables')
	Dp_T = 29000;
	Dn_T = 35000;

	// spatial discretisation of the solid diffusion PDE
	M = MM;
	checkModelparam(); // check if the inputs to the Matlab code are the same as the ones here in the C++ code

	// Initialise state variables
	slide::z_type up, un;
	double fp, fn, T, delta, LLI, thickp, thickn, ep, en, ap, an, CS, Dp, Dn, R, delta_pl;
	double Rdc = 0.0102;																			  // DC resistance of the total cell in Ohm
	fp = 0.689332;																					  // lithium fraction in the cathode at 50% soc [-]
	fn = 0.479283;																					  // lithium fraction in the anode at 50% soc [-]
	T = PhyConst::Kelvin + 25;																		  // cell temperature
	delta = 1e-9;																					  // SEI thickness. Start with a fresh cell, which has undergone some formation cycles so it has an initial SEI layer.
																									  // never start with a value of 0, because some equations have a term 1/delta, which would give nan or inf
																									  // so this will give errors in the code
	LLI = 0;																						  // lost lithium. Start with 0 so we can keep track of how much li we lose while cycling the cell
	thickp = 70e-6;																					  // thickness of the positive electrode
	thickn = 73.5e-6;																				  // thickness of the negative electrode
	ep = 0.5;																						  // volume fraction of active material in the cathode
	en = 0.5;																						  // volume fraction of active material in the anode
	ap = 3 * ep / Rp;																				  // effective surface area of the cathode, the 'real' surface area is the product of the effective surface area (a) with the electrode volume (elec_surf * thick)
	an = 3 * en / Rn;																				  // effective surface area of the anode
	CS = 0.01 * an * elec_surf * thickn;															  // initial crack surface. Start with 1% of the real surface area
	Dp = 8e-14;																						  // diffusion constant of the cathode at reference temperature
	Dn = 7e-14;																						  // diffusion constant of the anode at reference temperature
	R = Rdc * ((thickp * ap * elec_surf + thickn * an * elec_surf) / 2);							  // specific resistance of the combined electrodes, see State::iniStates
	delta_pl = 0;																					  // thickness of the plated lithium layer. You can start with 0 here
	s_ini.initialise(up, un, T, delta, LLI, thickp, thickn, ep, en, ap, an, CS, Dp, Dn, R, delta_pl); // set the states, with a random value for the concentration
	s = s_ini;																						  // set the states, with a random value for the concentration

	// Check if this was a valid state
	try
	{
		validState();
	}
	catch (int e)
	{
		std::cout << "Error in State::initialise, one of the states has an illegal value, throwing an error\n";
		throw 12;
	}
	setC(fp, fn); // set the lithium concentration

	// SEI parameters
	nsei = 1;
	alphasei = 1;
	OCVsei = 0.4;
	rhosei = 100e3;
	Rsei = 2037.4;
	Vmain = 13.0;
	Vsei = 64.39;
	c_elec0 = 4.541 / 1000;
	// fitting parameters of the models
	seiparam = settings::def_param::SEIparam_Kokam;

	// surface cracking
	// fitting parameters of the models
	csparam.CS1alpha = 4.25e-5;
	csparam.CS2alpha = 6.3e-7;
	csparam.CS3alpha = 2.31e-16;
	csparam.CS4alpha = 4.3306e-8;
	csparam.CS4Amax = 5 * getAnodeSurface(); // assume the maximum crack surface is 5 times the initial anode surface
	csparam.CS5k = 1e-18;
	csparam.CS5k_T = -127040;
	csparam.CS_diffusion = 2;

	// LAM
	OCVnmc = 4.1;
	// fitting parameters
	lamparam = settings::def_param::LAMparam_Kokam;

	// li-plating parameters
	npl = 1;
	alphapl = 1;
	OCVpl = 0;
	rhopl = 10000e3;
	// fitting parameters
	plparam.pl1k = 4.5e-10;
	plparam.pl1k_T = -2.014008e5;

	// degradation identifiers: no degradation
	deg_id.SEI_id[0] = 0;	 // no SEI growth
	deg_id.SEI_n = 1;		 // there is 1 SEI model (namely '0')
	deg_id.SEI_porosity = 0; // don't decrease the volume fraction
	deg_id.CS_id[0] = 0;	 // no surface cracks
	deg_id.CS_n = 1;		 // there is 1 model (that there are no cracks)
	deg_id.CS_diffusion = 0; // don't decrease the diffusion coefficient
	deg_id.LAM_id[0] = 0;	 // no LAM
	deg_id.LAM_n = 1;		 // there is 1 LAM model
	deg_id.pl_id = 0;		 // no lithium plating

	// Check if we will have to calculate the stress according to Dai's stress model
	for (int i = 0; i < deg_id.CS_n; i++)
		sparam.s_dai = sparam.s_dai || deg_id.CS_id[i] == 2;
	for (int i = 0; i < deg_id.LAM_n; i++)
		sparam.s_dai = sparam.s_dai || deg_id.LAM_id[i] == 1;

	// check if we need to calculate the stress according to Laresgoiti's stress model
	for (int i = 0; i < deg_id.CS_n; i++)
		sparam.s_lares = sparam.s_lares || deg_id.CS_id[i] == 1;
}

Cell_KokamNMC::Cell_KokamNMC(const slide::Model &M, const DEG_ID &degid, int verbosei) : Cell_KokamNMC(M, verbosei)
{
	/*
	 * constructor to initialise the degradation parameters
	 *
	 * IN
	 * M 		structure of the type Model, with the matrices of spatial discretisation for solid diffusion
	 * degid 	structure of the type DEG_ID, with the identifications of which degradation model(s) to use
	 */

	deg_id = degid;

	// Check if we will have to calculate the stress according to Dai's stress model
	for (int i = 0; i < deg_id.CS_n; i++)
		sparam.s_dai = sparam.s_dai || deg_id.CS_id[i] == 2;
	for (int i = 0; i < deg_id.LAM_n; i++)
		sparam.s_dai = sparam.s_dai || deg_id.LAM_id[i] == 1;

	// check if we need to calculate the stress according to Laresgoiti's stress model
	for (int i = 0; i < deg_id.CS_n; i++)
		sparam.s_lares = sparam.s_lares || deg_id.CS_id[i] == 1;
}