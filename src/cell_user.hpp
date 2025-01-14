/*
 * Cell_user.hpp
 *
 * A child classes of Cell which the user can use to set his/her own parameters
 *
 * Copyright (c) 2019, The Chancellor, Masters and Scholars of the University
 * of Oxford, VITO nv, and the 'Slide' Developers.
 * See the licence file LICENCE.txt for more information.
 */

#pragma once

#include "cell_LGChemNMC.hpp"
#include "constants.hpp"

namespace slide
{

	class Cell_user : public Cell
	{
	public:
		// constructors
		Cell_user(const struct slide::Model &, int verbosei);
		Cell_user(const slide::Model &M, const DEG_ID &, int verbosei);
	};

} // namespace slide
