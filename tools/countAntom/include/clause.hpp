
#ifndef CLAUSE_HPP
#define CLAUSE_HPP

/********************************************************************************************
clause.hpp -- Copyright (c) 2014, Tobias Schubert

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
#include <cassert>
#include <vector>

namespace countAntom
{
  // The "Clause" class.
  class Clause
  {

  public:

    // Constructor.
    Clause (std::vector<uint_fast32_t>& clause, uint_fast32_t lbd) : m_literals(NULL), m_lbd(lbd)
    {
      // Consistency checks.
      assert(!clause.empty());  
      assert(m_lbd != 0);

      // Initialization.
      uint_fast32_t l(0);

      // Generate a new array of literals.
      uint_fast32_t* lits(new uint_fast32_t[clause.size() + 1]); 
      
      // Copy the literals of "clause" to "lits".
      for (l = 0; l < clause.size(); ++l)
	{ lits[l] = clause[l]; }
      
      // Add the clause stopper. 
      lits[l] = 0; 
      
      // Update "m_literals".
      m_literals = lits; 
    }
       
    // Destructor.
    ~Clause (void) { delete [] m_literals; }
 
    // Returns a pointer to the literals.
    uint_fast32_t* lits (void) const { return m_literals; }

    // Returns the "Literals Blocks Distance".
    uint_fast32_t lbd (void) const { return m_lbd; }

    // Returns whether the clause has been marked as "to be deleted".
    bool deactivated (void) const { return m_lbd == 0; }

    // Marks the clause as "to be deleted".
    void deactivate (void) { m_lbd = 0; }

  private:

    // Copy constructor.
    Clause (const Clause&);

    // Assignment operator.
    Clause& operator = (const Clause&);

    // A pointer to the clause's literals.
    uint_fast32_t* m_literals;

    // The clause's "Literals Blocks Distance", including some special cases:
    // m_lbd = 0 --> clause marked as "to be deleted".
    // m_lbd = 1 --> clause belongs to the original CNF formula.
    // m_lbd > 1 --> conflict clause. 
    uint_fast32_t m_lbd;

  };
}

#endif
