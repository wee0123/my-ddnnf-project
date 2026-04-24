 
/********************************************************************************************
countAntom.cpp -- Copyright (c) 2014, 2015 Jan Burchard

partially based on the antom SAT solver, Copyright (c) 2014, Tobias Schubert

Permission is hereby granted, free of charge, to any person obtaining a copy of this 
software and associated documentation files (the "Software"), to deal in the Software 
without restriction, including without limitation the rights to use, copy, modify, merge, 
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons 
to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
********************************************************************************************/

// Include standard headers.
#include <vector>
// gnu bignum package - requires libgmp
#include <gmpxx.h>

// Include antom related headers.
#include "core.hpp"
#include "countAntom.hpp"

namespace countAntom
{
  // Constructor.
  CountAntom::CountAntom (void) : m_core(nullptr) { m_core = new Core; }
 
  // Destructor.
  CountAntom::~CountAntom (void) { delete m_core; }

  // Returns the number of decisions made so far.
  uint_fast32_t CountAntom::decisions (void) const { return m_core->decisions(); }

  // Returns the number of BCP operations performed so far.
  uint_fast32_t CountAntom::bcps (void) const { return m_core->bcps(); }

  // Returns the number of conflicts encountered so far.
  uint_fast32_t CountAntom::conflicts (void) const { return m_core->conflicts(); }

  // returns the number of cache hits
  uint_fast32_t CountAntom::cacheHits (void) const {return m_core -> cacheHits(); }
  
  // returns the number of cache misses
  uint_fast32_t CountAntom::cacheMisses (void) const {return m_core -> cacheMisses(); }
  
  double CountAntom::getSplitRatio(void) const {return m_core -> getSplitRatio(); }
  
  // returns the number of satisfying assignments
  mpz_class CountAntom::modelCount (void) const {return m_core -> modelCount(); }

  // Sets the maximum variable index to "max". 
  void CountAntom::setMaxIndex (uint_fast32_t max) { m_core->setMaxIndex(max); }
  
  
 
  // Adds a clause to the clause database. Returns FALSE if the CNF formula is unsatisfiable,
  // otherwise TRUE will be returned. Assumes that the solver is on decision level 0 and that 
  // "clause" is not empty. Furthermore, all literals have to be encoded as follows, having 
  // variable indices greater 0:
  //  x3 <--> (3 << 1)     = 6
  // -x3 <--> (3 << 1) + 1 = 7
  // All clauses inserted into the clause database using "addClause()" are assumed to belong to 
  // the original CNF formula. Note, that "clause" gets modified. USE "setMaxIndex()" BEFOREHAND 
  // TO ENSURE THAT THE SOLVER IS ABLE TO HANDLE THE VARIABLES WITHIN "clause". 
  bool CountAntom::addClause (std::vector<uint_fast32_t>& clause) { return m_core->addClause(clause); }

  // Solves the current CNF formula, taking the specified assumptions into account. 
  // The return values are UNKNOWN/SAT/UNSAT. 
  // The number of satisfying assignments can be retrieved via CountAntom::modelCount()
  uint_fast32_t CountAntom::solve (double vsadsFactor, bool attemptSplit, uint_fast32_t simplifications, bool preprocessingOnly, std::string preprocessingOnlyFileName, bool doTseitin, bool doUpla, bool doSelfSubsumption, bool doVariableElimination, uint_fast32_t noThreads, uint32_t memSize, bool addDependencies, bool enableIBCP) 
  { //std::vector<uint_fast32_t> noAssumptions; 
    return m_core->solve(vsadsFactor, attemptSplit, simplifications, preprocessingOnly, preprocessingOnlyFileName, doTseitin, doUpla, doSelfSubsumption, doVariableElimination, noThreads, memSize, addDependencies, enableIBCP); }
}
