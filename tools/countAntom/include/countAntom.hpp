
#ifndef COUNTANTOM_HPP
#define COUNTANTOM_HPP

/********************************************************************************************
countAntom.hpp -- Copyright (c) 2014,2015 Jan Burchard

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
#include <cstdint>
#include <vector>
// gnu bignum package - requires libgmp
#include <gmpxx.h>

// Some definitions.
#define ANTOM_UNKNOWN  0
#define ANTOM_SAT     10
#define ANTOM_UNSAT   20

namespace countAntom
{
  // Some forward declarations.
  class Core;

  // The "countAntom" class.
  class CountAntom
  {

  public:

    // Constructor.
    CountAntom (void);
   
    // Destructor.
    ~CountAntom (void); 
    
    // Returns the number of decisions made so far.
    uint_fast32_t decisions (void) const;

    // Returns the number of BCP operations performed so far.
    uint_fast32_t bcps (void) const;

    // Returns the number of conflicts encountered so far.
    uint_fast32_t conflicts (void) const;
    
    // returns the number of cache misses
    uint_fast32_t cacheHits (void) const;
    
    // returns the number of cache hits
    uint_fast32_t cacheMisses (void) const;
    
    double getSplitRatio(void) const;
    
    mpz_class modelCount (void) const;

  
    // Sets the maximum variable index to "max". 
    void setMaxIndex (uint_fast32_t max);

    // Adds a clause to the clause database. Returns FALSE if the CNF formula is unsatisfiable,
    // otherwise TRUE will be returned. Assumes that the solver is on decision level 0 and that 
    // "clause" is not empty. Furthermore, all literals have to be encoded as follows, having 
    // variable indices greater 0:
    //  x3 <--> (3 << 1)     = 6
    // -x3 <--> (3 << 1) + 1 = 7
    // All clauses inserted into the clause database using "addClause()" are assumed to belong to 
    // the original CNF formula. Note, that "clause" gets modified. USE "setMaxIndex()" BEFOREHAND 
    // TO ENSURE THAT THE SOLVER IS ABLE TO HANDLE THE VARIABLES WITHIN "clause". 
    bool addClause (std::vector<uint_fast32_t>& clause);

    // Solves the current CNF formula,
    // The return values are UNKNOWN/SAT/UNSAT. 
    uint_fast32_t solve (double vsadsFactor ,bool attemptSplit, uint_fast32_t simplifications, bool preprocessingOnly, std::string preprocessingOnlyFileName, bool doTseitin, bool doUpla, bool doSelfSubsumption, bool doVariableElimination, uint_fast32_t noThreads, uint32_t memSize, bool addDependencies, bool enableIBCP);
  
  private:

    // Copy constructor.
    CountAntom (const CountAntom&);

    // Assignment operator.
    CountAntom& operator = (const CountAntom&);

    // The SAT solving core.
    Core* m_core;
  };
}

#endif // COUNTANTOM_HPP
