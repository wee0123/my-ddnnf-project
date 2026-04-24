
#ifndef VARHEAP_HPP
#define VARHEAP_HPP

/********************************************************************************************
varheap.hpp -- Copyright (c) 2013, Tobias Schubert

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
  // The "VarHeap" class.
  class VarHeap
  {

  public:

    // Constructor.
    VarHeap (const std::vector<double>& act) : 
      m_heap(),
      m_position(),
      m_size(0),
      m_variables(0),
      m_activity(act)
    { } 

    // Updates all data structures to be able to handle "var" variables.
    void resize (unsigned int var) 
    {
      // Resize "m_position".
      m_position.resize(var + 1, -1);

      // Resize "m_heap".
      m_heap.resize(var, 0); 

      // Update "m_variables".
      m_variables = var;
    }

    // Returns whether the heap is empty or not.
    bool empty (void) const { return (m_size == 0); }

    // Returns whether "var" is an element of "m_heap".
    bool inHeap (unsigned int var) const 
    {
      // "var" has to be less or equal "m_variables".
      assert(var <= m_variables);

      // Return whether "var" is part of the heap.
      return (m_position[var] > -1); 
    }

    // Updates the position of "var" within the heap.
    void update (unsigned int var)
    {
      // "var" has to be less or equal "m_variables".
      assert(var <= m_variables);

      // "var" has to be an element of "m_heap".
      assert(m_position[var] > -1);

      // Ensure that the heap property holds. 
      shiftUpwards(m_position[var]); 
      shiftDownwards(m_position[var]); 
    }

    // Inserts variable "var" into "m_heap". 
    void insert (unsigned int var) 
    {
      // "var" has to be less or equal "m_variables".
      assert(var <= m_variables);

      // If "var" is an element of "m_heap", we've got a problem.
      assert(m_position[var] == -1); 

      // Update "m_position".
      m_position[var] = m_size;  
      
      // Add "var" to "m_heap".
      m_heap[m_size] = var;
      
      // Increment "m_size".
      ++m_size; 
	
      // Ensure that the heap property holds.
      shiftUpwards(m_position[var]);
    }

    // Returns the topmost variable that is part of variable table
    unsigned int top (std::vector<bool>* variableTable)
    {
      // If "m_heap" is empty, we've got a problem.
      assert(m_size > 0); 
      
      // search for a variable that is part of the component
      unsigned int position = 0;
      while (position <= m_size) {
	if(variableTable -> at(m_heap[position])) {
	  break;
	}
	position++;
      }
      
      // make sure a variable is found
      assert(position <= m_size);
      
      // Get the root variable.
      unsigned int var = m_heap[position]; 

      // Decrement "m_size".
      --m_size;
      
      // Overwrite "m_heap[position]" with the last element of "m_heap".
      m_heap[position] = m_heap[m_size];

      // Update "m_position".
      m_position[var] = -1;
   
      // If we removed the last element from the heap, we can skip the following operations.
      if (m_size > 0)
	{
	  // Update "m_position".
	  m_position[m_heap[position]] = 0; 
	  
	  // Ensure that the heap property holds. 
	  shiftDownwards(0);
	}
      
      // Return "var".
      return var;
    }

    // Removes "var" from the heap.
    void remove (unsigned int var)
    {
      // If "m_heap" is empty, we've got a problem.
      assert(m_size > 0); 
      
      // Initialization.
      int pos(m_position[var]); 

      // If "var" is not part of the heap, we've got a problem.
      assert(pos > -1); 

      // Decrement "m_size".
      --m_size;
      
      // Overwrite "m_heap[pos]" with the last element of "m_heap".
      m_heap[pos] = m_heap[m_size];

      // Update "m_position".
      m_position[var] = -1;
   
      // If we removed the right-most element from the heap, we can skip the following operations.
      if ((unsigned int) pos != m_size)
	{
	  // Update "m_position".
	  m_position[m_heap[pos]] = pos; 
	  
	  // Ensure that the heap property holds. 
	  shiftDownwards(pos);
	}

      // Consistency check.
      assert(m_position[var] == -1);
    }

  private:

    // Returns the position of the "father" of the element stored on position "pos".
    unsigned int father (unsigned int pos) const { return ((pos - 1) >> 1); }

    // Returns the position of the left "son" of the element stored on position "pos".
    unsigned int left (unsigned int pos) const { return ((pos << 1) + 1); }

    // Returns the position of the right "son" of the element stored on position "pos".
    unsigned int right (unsigned int pos) const { return ((pos + 1) << 1); }

    // Ensures the heap property by shifting the element on position "pos" of "m_heap" upwards.
    void shiftUpwards (unsigned int pos)
    {
      // Get the variable stored on position "pos".
      unsigned int var = m_heap[pos]; 
      
      // Determine the correct position of "var" within "m_heap".
      while (pos > 0 && m_activity[var] > m_activity[m_heap[father(pos)]])
	{
	  m_heap[pos]             = m_heap[father(pos)];
	  m_position[m_heap[pos]] = pos;
	  pos                     = father(pos);
	}
      
      // Store "var" at position "pos".
      m_heap[pos]     = var;
      m_position[var] = pos;
    }

    // Ensures the heap property by shifting the element on position "pos" of "m_heap" downwards.
    void shiftDownwards (unsigned int pos)
    {      
      // Get the variable stored on position "pos".
      unsigned int var = m_heap[pos]; 

      // Determine the correct position of "var" within "m_heap".
      while (left(pos) < m_size)
	{
	  unsigned int r = right(pos);
	  unsigned int child = r < m_size && m_activity[m_heap[r]] > m_activity[m_heap[left(pos)]] ? r : left(pos);
	  	
	  if (m_activity[m_heap[child]] <= m_activity[var])
	    { break; }
	  
	  m_heap[pos]             = m_heap[child];
	  m_position[m_heap[pos]] = pos;
	  pos                     = child; 
	}
      
      // Store "var" at position "pos".
      m_heap[pos]     = var;
      m_position[var] = pos;
    }

    // The heap.
    std::vector<unsigned int> m_heap;

    // The position of a particular variable within the heap. 
    std::vector<int> m_position;

    // The current size of the heap.
    unsigned int m_size; 

    // The maximum number of variables for which memory has been reserved.
    unsigned int m_variables;

    // The variables' activities.
    const std::vector<double>& m_activity;

  };
}

#endif
