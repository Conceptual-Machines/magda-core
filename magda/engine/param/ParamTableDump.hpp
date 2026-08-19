#pragma once

#include <string>

#include "param/ParamTable.hpp"

namespace magda::engine {

/**
 * @brief Render a parameter table as canonical text.
 *
 * The table's testable surface, and the third dump the goldens compare (#2076)
 * beside the plan and its layout. Deterministic and diff-friendly: one line per
 * parameter in table order, fixed columns, no addresses.
 *
 * Shape:
 * @code
 * magda-param-table v1
 * params=3 modifiers=1 links=1
 * [  0] T1:macro0            lin[0,1]        base=0.500  links=0
 * [  1] T1/D7:param0         log[20,20000]   base=0.250  links=1
 *       <- param[  0] amount=0.500 bipolar=0
 * modifiers:
 * [  0] T1:mod0              value=0.000
 * order: 0 1 2
 * @endcode
 *
 * Values are printed to three decimals, which is finer than any of them are
 * edited at and coarser than the last bit of a float: a golden that changed
 * because an unrelated arithmetic order moved a value by an ulp would be a
 * golden nobody could read.
 */
std::string dumpParamTable(const ParamTable& table);

}  // namespace magda::engine
