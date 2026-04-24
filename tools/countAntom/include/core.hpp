
#ifndef CORE_HPP
#define CORE_HPP

/********************************************************************************************
 * core.hpp -- Copyright (c) 2014, 2015, Jan Burchard
 * 
 * partially based on the antom SAT solver, Copyright (c) 2014, Tobias Schubert
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this 
 * software and associated documentation files (the "Software"), to deal in the Software 
 * without restriction, including without limitation the rights to use, copy, modify, merge, 
 * publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons 
 * to whom the Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING 
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 ********************************************************************************************/

// Include standard headers.
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <cmath>
// gnu bignum package - requires libgmp
#include <gmpxx.h>


// resident set size to determine memory usage
#include "rss.hpp"

// boost threading
#include <boost/thread.hpp>
#include <boost/lockfree/stack.hpp>


// Include countAntom related headers.
#include "countAntom.hpp"
#include "clause.hpp"
#include "component.hpp"

/*
 * NOTE:
 * 
 * tautological clauses in the initial database might cause a wrong count (e.g., the model count for the formula (a, b, -a) would be 1, not 4)
 * 
 * */



namespace countAntom
{
  //#define DEBUG_OUTPUT
  
  // comments during preprocessing
  #define PREPROCESS_OUTPUT
  
  // if defined, all counts are secured by a mutex to ensure thread safety
  #define TS_COUNTS
  
  // A helper function to compare two clauses wrt. their "Literals Blocks Distance".
  bool compareLBD (Clause* c1, Clause* c2) { return c1->lbd() > c2->lbd(); }
  
  // The "Core" class.
  class Core
  {
    
  public:
    
    // Constructor.
    Core () :   
    m_noThreads(1),
    m_nodesAvailable(),
    m_waitingThreads(0),
    m_coutMutex(),
    m_cacheCleaningMutex(),
    m_cleanCacheBarrier(nullptr),
    m_doCacheCleaning(false),
    m_threadsReadyForCacheCleaning(0),
    m_topNode(nullptr),
    m_solverDatas(),
    m_conflictClauses(),
    m_emptyClause(false), 
    m_enableBinaryClauses(false),
    m_variables(0),
    m_clauses(0),
    m_originalClauses(0),
    m_timeComputationStart(0),
    m_decisions(0),
    m_bcps(0),
    m_modelCount(0),
    m_additionalFreeVariables(0),
    m_solverData(),
    m_requestForNodes(0),
    m_incVarActivity(1),
    m_decayFactor(1.05),
    m_activity(nullptr),
    m_maxActivity(0),
    m_litToClause(),
    m_assumptions(),
    m_units(),
    m_cacheController(nullptr),
    m_nodeManager(nullptr)
    { 
    }
    
    
    
    
    
    // Destructor.
    ~Core (void)
    {
      // remove the cache controller
      if (m_cacheController != nullptr) {
        delete m_cacheController;
      }
      if (m_nodeManager != nullptr) {
        delete m_nodeManager;
      }
      
      // delete pointer type variables
      if (m_cleanCacheBarrier != nullptr) {
        delete m_cleanCacheBarrier;
      }
      
      // delete the activity vector
      if (m_activity != nullptr) {
        delete m_activity;
      }
    }
    
    // struct to hold the solver data for the different threads
    struct SolverData {
      
      // the number of the thread
      uint_fast32_t m_threadNumber;
      
      // The clause database.
      std::vector<Clause*> m_database; 
      
      // The (partial) variable assignment.
      std::vector<uint_fast8_t> m_assignment;
      
      // The decision level on which a particular variable has been assigned.
      std::vector<uint_fast32_t> m_level; 
      
      // For each variable we store whether it is an implication or a decision.
      // solverData -> m_forcing == nullptr --> decision.
      // solverData -> m_forcing != nullptr --> implication.
      std::vector<uint_fast32_t*> m_forcing;        
      
      // the variable assignment stack
      std::vector<uint_fast32_t> m_assignmentStack;
      
      // returns the position of the decision variable of a decision level in the assignment stack
      std::vector<uint_fast32_t> m_decisionLevelToAssignmentStack;
      
      // the current decisionLevel
      uint_fast32_t m_decisionLevel;
      
      // last position in the assignment stack
      uint_fast32_t m_assignmentStackEndPosition;
      
      // the current read position in the assignment stack
      uint_fast32_t m_assignmentStackCurrentPosition;  
      
      // Helper structure to maintain binary and ternary forcing clauses. 
      std::vector<uint_fast32_t> m_dynamicForcingClauses; 
      
      // For each literal we keep track in which binary clauses it occurs.
      std::vector<std::vector<uint_fast32_t> > m_binaries;
      
      // For each literal we store a vector to represent in which non-binary clauses it currently serves as a watched literal.
      std::vector<std::vector<std::pair<uint_fast32_t, uint_fast32_t*> > > m_watches;
      
      // a lockfree stack of new conflict clauses to add
      boost::lockfree::stack<std::vector<uint_fast32_t>* > m_newConflictClauses;
      
      // Conflict analysis related variables.
      // The number of conflicts encountered so far.
      uint_fast32_t m_conflicts; 
      std::vector<uint_fast32_t> m_seenVar;
      std::vector<uint_fast32_t> m_touchedLevel; 
      
      // for backtracking: store the current node for each decision level
      ComponentTreeNode* m_currentNode;
      
      // local list of nodes to work from
      std::vector<ComponentTreeNode*> m_nodes;
      
      // the factor for VSADS
      double m_vsadsFactor;
      
      // statistics
      uint_fast32_t m_decisions;
      uint_fast32_t m_bcps;
      uint_fast32_t m_abortedComponents;
      // how often the thread is idle because the stack is empty
      uint_fast32_t m_blocks;
      // the number of backtracking operations
      uint_fast32_t m_backtracks;
      // the total distance (in decision levels) backtracked (this includes backtracking and re-assigning)
      uint_fast32_t m_backtrackDistance;
      // the number of nodes that need to be recomputed
      uint_fast32_t m_recomputations;
      // the number of split operations
      uint_fast32_t m_splits;
      // the total number of components creates by split operations
      uint_fast32_t m_splitNoComponents;
      // number of nodes found to be sat / unsat
      uint_fast32_t m_noFinishedNodes;
      
      uint_fast32_t m_nodeFinishedDL;
      
      // constructor
      SolverData() : 
      m_threadNumber(0),
      m_database(),
      m_assignment(),    
      m_level(),
      m_forcing(),    
      m_assignmentStack(),
      m_decisionLevelToAssignmentStack(),
      m_decisionLevel(0),
      m_assignmentStackEndPosition(1),
      m_assignmentStackCurrentPosition(1),
      m_dynamicForcingClauses(), 
      m_binaries(), 
      m_watches(), 
      m_newConflictClauses(0),
      m_conflicts(0),
      m_seenVar(),
      m_touchedLevel(),
      m_currentNode(nullptr),
      m_nodes(),
      m_vsadsFactor(0),
      m_decisions(0),
      m_bcps(0),
      m_abortedComponents(0),
      m_blocks(0),
      m_backtracks(0),
      m_backtrackDistance(0),
      m_recomputations(0),
      m_splits(0),
      m_splitNoComponents(0),
      m_noFinishedNodes(0),
      m_nodeFinishedDL(0)
      {}
      
      // Destructor
      ~SolverData() {
        // Delete the entire clause database. 
        for (uint_fast32_t c = 0; c < m_database.size(); ++c)
        { delete m_database[c]; }
      }
      
      // copy constructor
      
      SolverData (const SolverData& other) : 
      m_threadNumber(0),
      m_database(),
      m_assignment(other.m_assignment),    
      m_level(other.m_level),
      m_forcing(other.m_forcing),    
      m_assignmentStack(other.m_assignmentStack),
      m_decisionLevelToAssignmentStack(other.m_decisionLevelToAssignmentStack),
      m_decisionLevel(other.m_decisionLevel),
      m_assignmentStackEndPosition(other.m_assignmentStackEndPosition),
      m_assignmentStackCurrentPosition(other.m_assignmentStackCurrentPosition),
      m_dynamicForcingClauses(other.m_dynamicForcingClauses), 
      m_binaries(other.m_binaries), 
      m_watches(), 
      m_newConflictClauses(0),
      m_conflicts(0),
      m_seenVar(other.m_seenVar),
      m_touchedLevel(other.m_touchedLevel),
      m_currentNode(other.m_currentNode),
      m_nodes(), // empty node list
      m_vsadsFactor(other.m_vsadsFactor), 
      m_decisions(0),
      m_bcps(0),
      m_abortedComponents(0),
      m_blocks(0),
      m_backtracks(0),
      m_backtrackDistance(0),
      m_recomputations(0),
      m_splits(0),
      m_splitNoComponents(0),
      m_noFinishedNodes(0),
      m_nodeFinishedDL(0)
      {        
        // set the correct size for m_watches
        m_watches.resize(other.m_watches.size());
        
        // copy over the database
        for (uint_fast32_t cl = 0; cl < other.m_database.size(); cl++) {
          
          std::vector<uint_fast32_t> newCl;
          
          
          // get the literals from the clause
          uint_fast32_t* lits = other.m_database[cl] -> lits();
          for (uint_fast32_t l = 0; lits[l] != 0; l++) {
            newCl.push_back(lits[l]);
          }
          
          // now add the clause to the new database          
          Clause* newClause(new Clause(newCl, other.m_database[cl] -> lbd())); 
          m_database.push_back(newClause);          
          
          // and add the watches
          uint_fast32_t* newLits = m_database[cl] -> lits();
          
          m_watches[newLits[0]].push_back(std::make_pair(newLits[1], newLits));
          m_watches[newLits[1]].push_back(std::make_pair(newLits[0], newLits)); 
        }
      }
    private:
      // Assignment operator.
      SolverData& operator = (const SolverData&);
    };
    
    // Returns the number of decisions made so far.
    uint_fast32_t decisions (void) const { return m_decisions; }
    
    // Returns the number of BCP operations performed so far.
    uint_fast32_t bcps (void) const { return m_bcps; }
    
    // Returns the number of conflicts encountered so far.
    uint_fast32_t conflicts (void) const { return 0;  } // TODO
    
    // returns the model count of the top component
    mpz_class modelCount (void) const {return m_modelCount; }
    
    // Sets the maximum variable index to "max". 
    void setMaxIndex (uint_fast32_t max) { assert(max > 0); updateDataStructures(max); }
    
    // returns the number of cache hits
    uint_fast32_t cacheHits (void) {if (m_cacheController != nullptr) {return m_cacheController->hits();} return 0;}
    
    // returns the number of cache misses
    uint_fast32_t cacheMisses (void) {if (m_cacheController != nullptr) {return m_cacheController->tries() - m_cacheController->hits();} return 0;}
    
    // returns the ratio of components and attempted splits (1.0 if no component split successfuly)
    //double getSplitRatio (void) const {return (double)m_splitNoComponents / (double) m_splits; }
    double getSplitRatio () const {return 0;}
    
    
    
    // Adds a clause to the clause database. Currently one to one copy from antom
    // Returns FALSE if the CNF formula is unsatisfiable,
    // otherwise TRUE will be returned. Assumes that the solver is on decision level 0 and that 
    // "clause" is not empty. Furthermore, all literals have to be encoded as follows, having 
    // variable indices greater 0:
    //  x3 <--> (3 << 1)     = 6
    // -x3 <--> (3 << 1) + 1 = 7
    // All clauses inserted into the clause database using "addClause()" are assumed to belong to 
    // the original CNF formula. Note, that "clause" gets modified. USE "setMaxIndex()" BEFOREHAND 
    // TO ENSURE THAT THE SOLVER IS ABLE TO HANDLE THE VARIABLES WITHIN "clause". 
    bool addClause (std::vector<uint_fast32_t>& clause)
    {
      // What about the empty clause?
      if (m_emptyClause)
      {  std::cout << "c " << boost::this_thread::get_id() << ": m_emptyClause" << std::endl;
        return false; }
        
        // Are we really on decision level 0?
        assert(m_solverData.m_decisionLevel == 0); 
        
        // If "clause" is empty, we might have a problem.
        assert(!clause.empty()); 
        
        // Sort "clause" (by increasing values) to speedup the checks below.
        std::sort(clause.begin(), clause.end());
        
        // Consistency check.
        assert((clause.front() >> 1) != 0);
        assert((clause.back() >> 1) <= m_variables); 
        
        // Initialization.
        uint_fast32_t stop(clause.size());
        uint_fast32_t lit(0);
        uint_fast32_t size(0); 
        
        // Check whether "clause" is already satisfied or represents a tautological clause.  
        // By the way, search for multiple copies of the same literal and literals evaluating to FALSE. 
        for (uint_fast32_t c = 0; c < stop; ++c)
        {
          // Get the next literal.
          uint_fast32_t l(clause[c]);
          
          // "clause" satisfied by "l"? Do we have a tautological clause?
          // this might not not fine for model counting..
          if (m_solverData.m_assignment[l] || (l ^ 1) == lit)
          {     
            return true; 
          }
          
          // Do we have to take the current literal into account?
          if (!m_solverData.m_assignment[l ^ 1] && l != lit)
          { clause[size++] = l; lit = l; }
        }
        
        // Do we have an empty clause? CNF formula unsatisfiable?
        if (size == 0) { 
          m_emptyClause = true;
          std::cout << "c " << boost::this_thread::get_id() << ": size==0" << std::endl; 
          
          return false;           
        }
        
        // Do we have a unit clause?
        if (size == 1)
        {
          // Push the unit literal as an implication onto the decision stack.
          // this is also fine for model counting
          addImplication(clause[0], nullptr, &m_solverData);
          
          // What about the effects of this implication?
          if (deduce(&m_solverData) != nullptr)
          { m_emptyClause = true; 
            std::cout << "deduce() != nullptr" << std::endl;return false; 
          }
          // Everything went fine.
          return true; 
        }
        
        // Resize "clause".
        clause.resize(size); 
        
        // Update "m_activity".
        for (uint_fast32_t l = 0; l < size; ++l)
        { increaseActivity(clause[l] >> 1); }
        
        // Generate a new clause and add it to the clause database. 
        Clause* cl(new Clause(clause, 1)); 
        m_solverData.m_database.push_back(cl); 
        
        // Update "m_solverData.m_watches".
        m_solverData.m_watches[clause[0]].push_back(std::make_pair(clause[1], cl->lits()));
        m_solverData.m_watches[clause[1]].push_back(std::make_pair(clause[0], cl->lits())); 
        
        
        // Everything went fine.
        return true;
    }
    
    
    
    
    // set the given literal as an assumptions
    // returns false if this causes a conflict
    bool setAssumption(uint_fast32_t lit) {
      
      // NOTE: assumptions not implemented at the moment
      
      std::cerr << "c " << boost::this_thread::get_id() << ": Assumptions are not fully implemented at the moment" << std::endl;
      assert(false);
      
      
      std::cout << "set assumption " << lit << "(assumptions: " << m_assumptions.size() << ")" << std::endl;
      // if the literal is already assigned, return true
      if (m_solverData.m_assignment[lit]) {
        return true;
      }
      
      // if the literal is assigned with the other polarity, return false
      if (m_solverData.m_assignment[lit^1]){
        
        std::cout << "literal is already assigned with the other polarity on dl " << m_solverData.m_level[lit >> 1] << std::endl;
        return false;
      }
      
      // store the assumption
      m_assumptions.push_back(lit);
      
      // return success (NOTE: this does not mean that there could be no conflict during deduce...)
      return true;
    }
    
    // removes all previous assumptions
    void clearAssumptions() {
      
      // NOTE: assumptions not implemented at the moment
      
      std::cerr << "c " << boost::this_thread::get_id() << ": Assumptions are not fully implemented at the moment" << std::endl;
      assert(false);
      m_assumptions.clear();
    }
    
    
    
    uint_fast32_t solve (double vsads_factor, bool attemptSplit, uint_fast32_t simplifications, bool preprocessingOnly, std::string preprocessingOnlyFileName, bool doTseitin, bool doUpla, bool doSelfSubsumption, bool doVariableElimination, uint_fast32_t noThreads, uint_fast32_t memSize, bool addDependencies, bool enableIBCP)
    {
      
      /* boost library check       
       *      std::cout << "c using Boost "     
       *          << BOOST_VERSION / 100000     << "."  // major version
       *          << BOOST_VERSION / 100 % 1000 << "."  // minior version
       *          << BOOST_VERSION % 100                // patch level
       *          << std::endl;
       */
      
      // store the number of threads
      m_noThreads = noThreads;
      
      m_timeComputationStart = getTimeStamp();
      
      
      // create a barrier for the cache cleanup operation
      m_cleanCacheBarrier = new boost::barrier(m_noThreads);
      
      // initialize and simplify the formula and the top component
      // store the number of clauses
      m_clauses = m_solverData.m_database.size();
      
      
      bool doPreprocess(true);
      
      if (doPreprocess) {
        if (!simplify(simplifications, doTseitin, doUpla, doSelfSubsumption, doVariableElimination)) {
          return ANTOM_UNSAT;
        } 
      }
      else {
        antomSimplify();
      }
      
      #ifdef DEBUG_OUTPUT                
      std::cout << "c " << " simplify done, m_originalClauses = " << m_originalClauses << ", m_clauses = " << m_clauses << std::endl;
      #endif
      //std::cout << "c " << boost::this_thread::get_id() << ": additional free variables: " << m_additionalFreeVariables << std::endl;
      
      
      
      // run clustering
      //clusteringVarsToRemove();
      
      
      std::cout << "c ================Preprocessing Finished======================="      << std::endl;
      std::cout << "c clauses: " << m_solverData.m_database.size() << ", variables: " << m_variables << std::endl;
      
      if (preprocessingOnly) {      
        // write the the simplified CNF to a text file
        std::ofstream simplifiedCNF;
        simplifiedCNF.open (preprocessingOnlyFileName);
        simplifiedCNF << "p cnf " << m_variables << " "<<  (m_solverData.m_database.size()) << std::endl;
        for (uint_fast32_t clause= 0; clause < m_solverData.m_database.size(); clause++) {
          if (m_solverData.m_database[clause] -> deactivated()) {
            continue;
          }
          uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
          
          for (uint_fast32_t l = 0; lits[l] != 0; l++) {
            simplifiedCNF << ((lits[l]&0x01)>0?"-":" ") << (lits[l]>>1) << " ";
          }
          simplifiedCNF << "0" << std::endl;
        }
        simplifiedCNF.close();
        
        return ANTOM_UNKNOWN;
      }
      
      
      // empty lit to clause
      m_litToClause.clear();
      m_litToClause.resize((m_variables << 1) + 2);
      // compute m_litToClause and the initial variable count
      for (uint_fast32_t c = 0; c < m_clauses; c++) {
        
        uint_fast32_t* lits(m_solverData.m_database[c]->lits()); 
        
        for(uint_fast32_t l = 0; lits[l] != 0 ; l++) {
          
          // add to m_litToClause
          m_litToClause[lits[l]].push_back(c);
        }
      }
      
      
      // compute vectors for the components
      std::vector<uint_least32_t>* activeVars = new std::vector<uint_least32_t>;
      for (uint_least32_t var = 1; var <= m_variables; var++) {
        activeVars -> push_back(var);     
      }
      std::vector<uint_least32_t>* activeCl = new std::vector<uint_least32_t>;
      for (uint_least32_t cl = 0; cl < m_clauses; cl++) {
        activeCl -> push_back(cl);   
      }
      
      // set up the cache controller
      m_cacheController = new CacheController(m_variables, m_clauses, memSize, m_noThreads, addDependencies);
      
      // and the node manager
      m_nodeManager = new NodeManager();
      
      // generate the top component w/ all the clauses and variables
      // note: decision path can be set arbitrarily here
      m_topNode = new ComponentTreeNode(nullptr, 1, false, activeVars, activeCl, m_cacheController, m_nodeManager);
      
      m_nodeManager -> addNode(m_topNode);
      
      
      // add the assumptions on DL 1 
      m_solverData.m_decisionLevel = 1;  
      for (uint_fast32_t assumption: m_assumptions) {
        
        // if the literal is already assigned (i.e. during deduce, ...) just jump over it
        if (m_solverData.m_assignment[assumption]) {
          continue;
        }
        
        // add the assignment as an implication
        addImplication(assumption, nullptr, &m_solverData);
      }
      
      // check if the formula is unsat after assumptions
      if (deduce(&m_solverData) != nullptr) {
        m_modelCount = 0;
        backtrack(nullptr, &m_solverData);
        return ANTOM_UNSAT;
      }
      
      
      // If there are no variables, the CNF formula is satisfiable by definition.
      if (m_variables == 0) 
      {
        // the formula has exactly 1 * 2^(free variables) satisfying assignments
        m_modelCount = 1;
        for (uint_fast32_t i = 0; i < m_additionalFreeVariables; i++) {
          m_modelCount *= 2;
        }
        
        return ANTOM_SAT; 
      }
      
      // What about the empty clause?
      if (m_emptyClause)
      { 
        return ANTOM_UNSAT;   
      }
      
      
      std::vector<boost::thread*> solverThreads;
      
      for (uint_fast32_t i = 0; i < m_noThreads; i++) {
        SolverData* newSolverData = new SolverData(m_solverData); // call copy constructor to create solver Data struct for each thread
        m_solverDatas.push_back(newSolverData);
      }
      
      
      // create the initial vsads factors
      for (uint_fast32_t i = 0; i < m_noThreads; i++) {
        
        m_solverDatas[i] -> m_vsadsFactor = vsads_factor;
        
      }
      
      
      
      // start the solver threads      
      for (uint_fast32_t i = 0; i < m_noThreads; i++) {
        
        
        boost::thread* workerThread = new boost::thread(&Core::solveLoop, this, i, m_solverDatas[i], attemptSplit, enableIBCP);
        solverThreads.push_back(workerThread);
      }
      
      // wait for all the threads to finish
      for (uint_fast32_t i = 0; i < m_noThreads; i++) {
        solverThreads[i] -> join();
      }
      
      // DEBUG information
      
      
      std::cout << std::endl << std::endl << "c =================Computation Finished========================"      << std::endl;
      // sum over all threads
      uint_fast32_t backtracks(0);
      uint_fast32_t backtrackDistance(0);
      uint_fast32_t recomputations(0);
      for (uint_fast32_t i = 0; i < m_noThreads; i++) {
        std::cout << "c =======================Thread "<<i<<"=============================="      << std::endl;
        std::cout << "c decisions         : " << m_solverDatas[i] -> m_decisions << std::endl;
        std::cout << "c bcps              : " << m_solverDatas[i] -> m_bcps<< std::endl;
        std::cout << "c conflicts         : " << m_solverDatas[i] -> m_conflicts<< std::endl;
        std::cout << "c aborted components: " << m_solverDatas[i] -> m_abortedComponents<< std::endl;
        std::cout << "c blocks            : " << (m_solverDatas[i] -> m_blocks) << std::endl; 
        std::cout << "c backtracks        : " << m_solverDatas[i] -> m_backtracks<< std::endl;
        std::cout << "c backtrack dist    : " << ((double)m_solverDatas[i] -> m_backtrackDistance / m_solverDatas[i] -> m_backtracks) << std::endl;
        std::cout << "c recomputations    : " << m_solverDatas[i] -> m_recomputations<< std::endl;
        std::cout << "c splits            : " << m_solverDatas[i] -> m_splits << std::endl;
        std::cout << "c split ratio       : " << (double)m_solverDatas[i] -> m_splitNoComponents / (double)m_solverDatas[i] -> m_splits << std::endl;
        //std::cout << "c finished nodes    : " << (double)m_solverDatas[i] -> m_noFinishedNodes << std::endl;
        
        
        backtracks +=  m_solverDatas[i] -> m_backtracks;
        backtrackDistance +=  m_solverDatas[i] -> m_backtrackDistance;
        
        m_decisions += m_solverDatas[i] -> m_decisions;
        m_bcps += m_solverDatas[i] -> m_bcps;        
        recomputations += m_solverDatas[i] -> m_recomputations;
      }
      
      // print the cache data
      m_cacheController -> printStatistics();
      
      std::cout << "c ===================Overall Statistics========================"      << std::endl;
      std::cout << "c TOTAL backtracks.......: " << backtracks<< std::endl;
      std::cout << "c TOTAL backtrack dist...: " << ((double)backtrackDistance / backtracks) << std::endl;
      std::cout << "c TOTAL recomputations...: " << recomputations<< std::endl;
      
      
      // clean up
      for (uint_fast32_t i = 0; i < m_noThreads; i++) {
        delete m_solverDatas[i];
        delete solverThreads[i];
      }      
      
      #ifdef DEBUG_OUTPUT
      {boost::mutex::scoped_lock l(m_coutMutex);
        std::cout << "c " << boost::this_thread::get_id() << ": main loop completed!" << std::endl;
      }
      #endif
      
      // the final model count is stored in the top node
      {
        boost::mutex::scoped_lock l(*m_topNode -> getMutex());
        m_modelCount = m_topNode -> modelCount();
      }
      
      // memory cleanup
      delete m_topNode;
      
      // ... continued
      m_nodeManager -> deleteNodes();
      
      // add the removed free variables
      for (uint_fast32_t i = 0; i < m_additionalFreeVariables; i++) {
        m_modelCount *= 2;
      }
      
      if (m_modelCount == 0) {
        return ANTOM_UNSAT;
      }
      
      return ANTOM_SAT;
    }
    
    
    // adds a new conflict clause to each threads list of conflict clauses
    // implementation is lockfree
    void addNewConflictClause(std::vector<uint_fast32_t> cl, uint_fast32_t lbd) {
      
      // lbd is stored as the last element of the clause
      cl.push_back(lbd);
      
      // create a clause copy for each thread and add it to its list of conflict clauses
      for (uint_fast8_t thread = 0; thread < m_noThreads; thread++) {
        
        std::vector<uint_fast32_t>* newClause = new std::vector<uint_fast32_t>(cl);
        
        m_solverDatas[thread] -> m_newConflictClauses.push(newClause);        
      }      
    }
    
    // adds new conflict clauses to ensure equal levels of knowledge
    void addNewConflictClauses (SolverData* solverData) {
      
      
      
      // pointer to store the new conflict clause should there be one
      std::vector<uint_fast32_t>* conflictClause;
      
      // check if there is a new conflict clause available
      while (solverData -> m_newConflictClauses.pop(conflictClause)) {
        
        
        // lbd is the last element of the conflict clause
        uint_fast32_t newLbd = conflictClause -> back();
        conflictClause -> pop_back();
        
        
        // check if the new conflict "clause" is unit
        if (conflictClause -> size() == 1) {
          
          // backtrack to the first decision level
          backtrack(nullptr, solverData);
          
          // if the variable is not yet assigned, add it
          if (!solverData -> m_assignment[conflictClause -> at(0)]) {
            
            
            if (solverData -> m_assignment[conflictClause -> at(0)^1]) {
              std::cerr << "c ERROR: learned a new unit implication which is assigned negatively!" << std::endl;
            }
            else {
              
              // add the implication and ensure it is not removed again by moving the assignment stack end position              
              addImplication(conflictClause -> at(0), nullptr, solverData);
              
              deduce(solverData);
              
              solverData -> m_decisionLevelToAssignmentStack[2] = solverData -> m_assignmentStackEndPosition;
              
            }
          }
          
          // done with this "clause"
          continue;
        }
        
        
        // determine a the highest decision level on which two of the literals in the conflict clause are not assigned negativly
        uint_fast32_t highestDlNoAssignment = 1;
        uint_fast32_t secondHighestDlNoAssignment = 1;
        
        
        for (uint_fast32_t lit: *conflictClause) {
          
          // if the literal is currently not assigned at all or assigned positivly, simply use the current decision level
          if (!solverData -> m_assignment[lit^1]) {
            
            // old first -> second
            secondHighestDlNoAssignment = highestDlNoAssignment;
            
            // new first
            highestDlNoAssignment = solverData -> m_decisionLevel;
            
            continue;
          }
          
          // otherwise compare its level to the second highest dl
          if (solverData -> m_level[lit >> 1] > secondHighestDlNoAssignment) {
            
            // new second highest dl
            secondHighestDlNoAssignment = solverData -> m_level[lit >> 1];
            
            // check if switch is necessary
            if (secondHighestDlNoAssignment > highestDlNoAssignment) {
              uint_fast32_t tmp = highestDlNoAssignment;
              
              highestDlNoAssignment = secondHighestDlNoAssignment;
              
              secondHighestDlNoAssignment = tmp;              
            }
          }
        }
        
        
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": highestDlNoAssignment " << highestDlNoAssignment << " , secondHighestDlNoAssignment = " << secondHighestDlNoAssignment << std::endl;
        }
        #endif
        
        uint_fast32_t targetDL = secondHighestDlNoAssignment - 1;
        
        if (targetDL > 0) {        
          // determine the correct node
          ComponentTreeNode* targetNode = solverData -> m_currentNode;
          
          while (targetNode -> decisionLevel() > secondHighestDlNoAssignment - 1) {
            targetNode = targetNode -> parent();
          }
          
          backtrack(targetNode, solverData);
        }
        else {
          backtrack(nullptr, solverData);
        }
        
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": after bracktrack, now on DL  " << solverData -> m_decisionLevel << std::endl;
          std::cout << "c " << boost::this_thread::get_id() << ": clause:  ";
          for (auto lit: *conflictClause) {
            std::cout << lit << "(" << solverData -> m_assignment[lit] << ", " << solverData -> m_assignment[lit^1] <<", l:" << solverData -> m_level[lit >> 1] << ")  ";
          }
          std::cout << std::endl;
        }
        #endif
        
        // now re-order the clause such that the first and second literal are not assigned to false
        bool firstSet (false);
        for (uint_fast32_t i = 0; i < conflictClause -> size(); i++) {
          
          // check if the literal is free
          if (!solverData -> m_assignment[(*conflictClause)[i]^1]) {
            
            uint_fast32_t tmp;
            
            // check which position to move the unassigned literal to
            if (!firstSet) {
              
              // switch positions 0 and i
              tmp = (*conflictClause)[0];
              (*conflictClause)[0] = (*conflictClause)[i];
              (*conflictClause)[i] = tmp;   
              
              // store that the first lit has been set
              firstSet = true;
            }
            else {
              
              // switch positions 1 and i
              tmp = (*conflictClause)[1];
              (*conflictClause)[1] = (*conflictClause)[i];
              (*conflictClause)[i] = tmp;     
              
              // and done
              break;
            }            
          }
        }
        
        
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": after order on DL  " << solverData -> m_decisionLevel << std::endl;
          std::cout << "c " << boost::this_thread::get_id() << ": clause:  ";
          for (auto lit: *conflictClause) {
            std::cout << lit << "(" << solverData -> m_assignment[lit] << ", " << solverData -> m_assignment[lit^1] <<", l:" << solverData -> m_level[lit >> 1] << ")  ";
          }
          std::cout << std::endl;
        }
        #endif
        
        // if the newly learned conflict clause does not contain two unassigned literals, it might imply a unit
        if (solverData -> m_assignment[(*conflictClause)[0]^1] || solverData -> m_assignment[(*conflictClause)[1]^1]) {
          
          // the solver should be on dl 1 here
          assert(solverData -> m_decisionLevel == 1);
          
          // count the number as positively, negatively and unassigned literals in the clause
          uint_fast32_t noNeg(0);
          uint_fast32_t noPos(0);
          uint_fast32_t noNot(0);
          uint_fast32_t lastNot(0);
          for (auto lit: *conflictClause) {
            if (solverData -> m_assignment[lit]) {
              noPos++;
            }
            else if (solverData -> m_assignment[lit^1]) {
              noNeg++;
            }
            else {
              noNot++;
              // store the last not assigned literal for later use
              lastNot = lit;
            }
          }
          
          // check if the clause is a new implication
          if (noPos == 0 && noNot == 1){            
            
            // add the implication and ensure it is not removed again by moving the assignment stack end position
            addImplication(lastNot, nullptr, solverData);
            
            deduce(solverData);
            
            solverData -> m_decisionLevelToAssignmentStack[2] = solverData -> m_assignmentStackEndPosition;
          }
          
          continue;
        }
        
        // the first two literals should now be unassigned
        assert(!solverData -> m_assignment[(*conflictClause)[0]^1]);
        assert(!solverData -> m_assignment[(*conflictClause)[1]^1]);
        
        // create the new clause
        Clause*  cl = new Clause(*conflictClause, newLbd); 
        
        solverData -> m_database.push_back(cl);     
        // Update "solverData -> m_watches".
        solverData -> m_watches[(*conflictClause)[0]].push_back(std::make_pair((*conflictClause)[1], cl->lits()));
        solverData -> m_watches[(*conflictClause)[1]].push_back(std::make_pair((*conflictClause)[0], cl->lits())); 
        
        // cleanup
        delete conflictClause;
      }
    }
    
    
    // performs a cache cleanup operation starting at the given position
    void cleanCache (uint_fast32_t pos) {
      
      // make sure no threads are waiting around
      do {
        m_nodesAvailable.notify_all();
        
        boost::mutex::scoped_lock nodeLock(*m_nodeManager -> getNodeMutex());
        if (m_waitingThreads == 0) {
          break;
        }
        
      } while(true);
      
      
      // wait until all threads are ready
      
      m_cleanCacheBarrier -> wait();
      
      // the first thread stores the start time
      std::time_t tStart(0);
      if (pos == 1) {
        tStart = std::time(nullptr);
        
        std::cout << "c" <<std::endl << "c ====================Memory Cleanup==========================="      << std::endl;
        computeWaitingNodes();
      }
      
      m_cleanCacheBarrier -> wait();
      
      // let the cache controller perform the cleanup
      m_cacheController -> cleanup(pos - 1, m_noThreads);
      
      // wait until all threads are done with the first cleaning step
      m_cleanCacheBarrier -> wait();
      
      m_cacheController -> removeInvalid(pos - 1, m_noThreads);
      
      // first thread also quickly removes any aborted nodes before they're deleted in the next step
      if (pos == 1) {
        m_nodeManager -> evaluateNodes();
      }
      
      // wait until all threads are done with the second cleaning step
      m_cleanCacheBarrier -> wait();
      
      
      // single threaded cleaning:
      // thread 1: re-add nodes to the cache
      // thread 2: remove nodes that were marked for deletion in the node manager
      
      uint_fast32_t noNodesToDelete = 0;
      
      if (pos == 1) {
        m_cacheController -> reAddNodes();
        
        noNodesToDelete = m_nodeManager -> getNoNodesToDelete();
        
        
        // special case: only one thread
        if (m_noThreads == 1) {
          m_nodeManager -> deleteNodes();          
        }        
      }
      else {
        m_nodeManager -> deleteNodes();
      }
      
      // wait until the first thread is done with the last cleaning step
      m_cleanCacheBarrier -> wait();
      
      
      if (pos == 1) {
        
        m_nodeManager -> clearNodesToDelete();
        
        std::time_t now = std::time(nullptr);
        
        std::cout << "c ... finshed. Time since start: " <<  (getTimeStamp() - m_timeComputationStart) << " ms" << std::endl;
        std::cout << "c rss = " << (getCurrentRSS()>>20) <<" MB, peak = " << (getPeakRSS()>>20) << " MB, ";
        std::cout << "deleted " << noNodesToDelete << " nodes, ";
        std::cout << "time: " << now - tStart << " seconds" << std::endl; 
        //, there are currently " << m_nodeManager -> getNoNodes();
        //std::cout << " nodes waiting for computation.  " << std::endl;
        
        m_cacheController -> printStatistics();
        
        m_doCacheCleaning = false;
        
        // no threads are waiting for cleaning
        m_threadsReadyForCacheCleaning = 0;
      }
      
      
      m_cleanCacheBarrier -> wait();
    }
    
    
    
    
    
    void computeWaitingNodes() {
      ComponentTreeNode* node;
      // a node which has a finished / unsat child
      std::pair<ComponentTreeNode*, ComponentTreeNode*>* nodeWithUnsatChild;
      
      
      // see if there are any nodes which need to be recomputed
      while((node = m_nodeManager -> getNodeToRecomputeWrongCache()) != nullptr) {
        
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": node " << node << " needs to be recomputed" << std::endl;
        }
        #endif
        
        //solverData -> m_recomputations++;
        
        node -> cacheValueIncorrect();
      }
      
      
      // see if there are any nodes with incorrect children that need attention
      while((node = m_nodeManager -> getNodeWithIncorrectChild()) != nullptr) {
        
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": node " << node << " has an incorrect child" << std::endl;
        }
        #endif
        
        node -> hasIncorrectChild();
      }
      
      // see if there are any nodes with finished children that need attention
      while ((nodeWithUnsatChild = m_nodeManager -> getNodeUnsatChild()) != nullptr) {
        
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": new child done pair: <" << nodeWithUnsatChild -> first << ", " << nodeWithUnsatChild -> second << ">" << std::endl;
        }
        #endif
        
        if (nodeWithUnsatChild->second != nullptr) {
          nodeWithUnsatChild->first -> childUnsat(nodeWithUnsatChild->second);
        }
        else {
          nodeWithUnsatChild->first -> childDone();
        }
        
        // memory cleanup
        delete nodeWithUnsatChild;
      }
    }
    
    // the solve loop
    // takes nodes from the node stack and performs an operation on them
    void solveLoop (uint_fast32_t threadNo, SolverData* solverData, bool attemptSplit, bool enableIBCP) {
      
      #ifdef DEBUG_OUTPUT
      {boost::mutex::scoped_lock l(m_coutMutex);
        std::cout << "c " << boost::this_thread::get_id() << ": start "<< std::endl;
      }
      #endif
      
      solverData -> m_threadNumber = threadNo;
      
      
      // temporary data structure to hold any occuring conflict clauses
      uint_fast32_t* conflictingClause(nullptr); 
      
      
      
      // the current node beeing processed
      ComponentTreeNode* node;
      
      
      // main solver loop
      while(true) {
        
        try{
          // check if a cache cleanup is required, TODO: make only one thread do this
          
          uint_fast32_t pos(0);
          {
            boost::mutex::scoped_lock lcm(m_cacheCleaningMutex);
            
            if (!m_doCacheCleaning) {
              if (m_cacheController -> isCleanupRequired()) {
                
                m_doCacheCleaning = true;
              }
            }
            
            if (m_doCacheCleaning) {
              // count this thread as waiting
              m_threadsReadyForCacheCleaning++;
              
              // store the position of this thread in the queue
              pos = m_threadsReadyForCacheCleaning;
              
              // release the lock
              lcm.unlock();
              
              // wake any thread that might be waiting for new nodes
              m_nodesAvailable.notify_all();
              
              // backtrack to decision level 0 to ensure the decision stack is correct
              backtrack(nullptr, solverData);
              
              // put the local nodes into the node manager
              for (ComponentTreeNode* node: solverData -> m_nodes) {
                m_nodeManager -> addNode(node);
              }
              // and empty the local node list
              solverData -> m_nodes.clear();
              
              // clean the cache      
              cleanCache(pos);
            }
          }
          
          
          computeWaitingNodes();
          
          
          // check if there is a local node available
          if (! solverData -> m_nodes.empty()) {
            
            // take the last node
            node = solverData -> m_nodes.back();
            // and remove it
            solverData -> m_nodes.pop_back();
          }
          else {
            // take the top node from the global stack
            {
              boost::mutex::scoped_lock nodeLock(*m_nodeManager -> getNodeMutex());
              if ((node = m_nodeManager -> getNode()) == nullptr) {
                
                
                //boost::mutex::scoped_lock nodeLock(*m_nodeManager -> getNodeMutex());
                
                // within the lock make sure all nodes are really computed
                //computeWaitingNodes();
                //if ((node = m_nodeManager -> getNode()) == nullptr) {
                
                // the thread is blocked because the stack is empty
                solverData -> m_blocks++;
                
                // count the node as waiting
                m_waitingThreads++;
                
                #ifdef DEBUG_OUTPUT
                {boost::mutex::scoped_lock l(m_coutMutex);
                  std::cout << "c " << boost::this_thread::get_id() << ": waiting, total number of waiting threads: " << m_waitingThreads << std::endl;
                }
                #endif
                
                if (m_waitingThreads == m_noThreads) {                                      
                  
                  // all threads are waiting -> notify them all and return
                  m_nodesAvailable.notify_all();
                  return;
                }
                
                // wait until a node is available
                // this will unlock nodeLock until a node is available, then re-aquire nodeLock
                m_nodesAvailable.wait(nodeLock);   
                
                if (m_waitingThreads == m_noThreads) {
                  
                  // all threads are waiting -> notify them all and return
                  m_nodesAvailable.notify_all();
                  return;
                }
                
                // count the node as not waiting
                m_waitingThreads--;
                
                #ifdef DEBUG_OUTPUT
                {boost::mutex::scoped_lock l(m_coutMutex);
                  std::cout << "c " << boost::this_thread::get_id() << ": no longer waiting" << std::endl;
                }
                #endif
                
                // if the nodes are still empty, another thread "stole" our node -> try again
                if ((node = m_nodeManager -> getNode()) == nullptr) {
                  #ifdef DEBUG_OUTPUT
                  {boost::mutex::scoped_lock l(m_coutMutex);
                    std::cout << "c " << boost::this_thread::get_id() << ": nodes are still empty :( " << std::endl;
                  }
                  #endif
                  continue;
                }
                // }
              }
            }
          }
          
          
          #ifdef DEBUG_OUTPUT          
          {boost::mutex::scoped_lock l(m_coutMutex);
            std::cout << "c " << boost::this_thread::get_id() << ": took node " << node << " from stack" << std::endl;
          }
          #endif
          
          
          // see if there are any new conflict clauses to learn
          // NOTE: this is done after getting a new node, but before backtracking
          // this ensures that all the conflict clauses known during the node creationg are definately learned
          addNewConflictClauses(solverData);
          
          
          deduce(solverData);
          
          // possibilities:
          // aborted -> ignore
          // cache hit -> inform node -> ignore
          // not split -> decide -> assign pos -> split both sides
          
          
          // lock the node
          {
            boost::mutex::scoped_lock l(*(node -> getMutex()));
            
            // check if the node was aborted or is done
            if (node -> state() >= 20) {         
              #ifdef DEBUG_OUTPUT          
              {boost::mutex::scoped_lock l(m_coutMutex); 
                std::cout << "c " << boost::this_thread::get_id() << ": node " << node << " was ignored because its state is "<< node -> state() << std::endl;
              }        
              #endif    
              
              // count aborted components
              if (node -> state() == 40) {
                solverData -> m_abortedComponents++;
              }
              
              // count node
              m_cacheController -> countNode(node);
              
              // ignore node
              continue;
            }      
            
            //if (node -> variables() -> size() > 15) {
            // check if the node is already cached
            if (node -> cacheLookup()) {
              
              #ifdef DEBUG_OUTPUT          
              {boost::mutex::scoped_lock l(m_coutMutex);
                std::cout << "c " << boost::this_thread::get_id() << ": cache hit for node " << node << std::endl;
              }
              #endif
              continue;
            }   
            //}
            
            
            bool abort(false);
            // vector to store the result of the split
            std::vector<ComponentTreeNode*> splitResultPos;
            std::vector<ComponentTreeNode*> splitResultNeg;
            
            for (uint_fast32_t r = 0; r < 2; r++) {
              
              // backtrack to the correct decision level
              // backtracking has to occur before the decision to ensure all variables are correctly (un)assigned              
              if(!backtrack(node, solverData)) {
                
                #ifdef DEBUG_OUTPUT
                {boost::mutex::scoped_lock l(m_coutMutex);
                  std::cout << "c " << boost::this_thread::get_id() << ": node " << node << " is UNSAT during backtracking -> aborted" << std::endl;
                }
                #endif
                
                // conflict during backtrack -> abort the node calculation
                node -> unsat();
                abort = true;
                
                // count the node as finished
                solverData -> m_noFinishedNodes+=1;
                solverData -> m_nodeFinishedDL += solverData -> m_decisionLevel;
                
                break;
              }
              
              // DEBUG: check the assignment stack
              
              deduce(solverData);
              
              
              // store whether we're currently computing the positive or negative branch
              bool neg = (r == 1);
              
              // chose a decision variable
              if (!neg) {
                
                // do ibcp before choosing a decision variable
                if (enableIBCP) {
                  doIBCP(node, solverData);
                  
                  // check for a conflict
                  if ((conflictingClause = deduce(solverData)) != nullptr) {
                    
                    //analyze(conflictingClause, false, solverData);      
                    // and implement all the conflict clauses
                    //addNewConflictClauses(solverData);
                    // conflict because of ibcp -> abort the node calculation
                    node -> unsat();
                    abort = true;
                    
                    // count the node as finished
                    solverData -> m_noFinishedNodes+=1;
                    solverData -> m_nodeFinishedDL += solverData -> m_decisionLevel;
                    break;
                  }
                }
                
                uint_fast32_t decisionVar = decide(node, solverData);
                
                if (decisionVar == 0) {
                  // the whole node is SAT
                  
                  #ifdef DEBUG_OUTPUT         
                  {boost::mutex::scoped_lock l(m_coutMutex);
                    std::cout << "c " << boost::this_thread::get_id() << ": node " << node << " is SAT (has " << (node -> variables()->size()) << " vars and " << (node-> clauses()->size()) << " clauses" << std::endl;
                  }
                  #endif
                  
                  node -> sat(& (solverData -> m_assignment));
                  
                  abort = true;
                  
                  // count the node as finished
                  solverData -> m_noFinishedNodes+=1;
                  solverData -> m_nodeFinishedDL += solverData -> m_decisionLevel;
                  break;
                }
                // set the decision variable
                node -> decisionVariable(decisionVar);
              }
              
              // split node for both pos and neg side
              uint_fast32_t decision = node->decisionVariable()<<1;
              
              if (neg) {
                // negation
                decision ^= 1;
              }   
              
              // the decision var should obviously not be assigned negatively
              assert(!solverData -> m_assignment[decision^1]);
              
              if (!solverData -> m_assignment[decision]) {
                addDecision(decision, solverData);
              }
              else {
                // add empty decision level
                // update the decion level
                solverData -> m_decisionLevel++;
                
                // since this is a decision, also store the position of the assignment in the assignment stack
                solverData -> m_decisionLevelToAssignmentStack[solverData -> m_decisionLevel] = solverData -> m_assignmentStackEndPosition;
              }
              
              // check for a conflict
              if ((conflictingClause = deduce(solverData)) != nullptr) {
                
                analyze(conflictingClause, false, solverData);      
                // and implement all the conflict clauses
                addNewConflictClauses(solverData);
                
                #ifdef DEBUG_OUTPUT            
                {boost::mutex::scoped_lock l(m_coutMutex);
                  std::cout << "c " << boost::this_thread::get_id() << ": conflict -> branch unsat" << std::endl;
                }
                #endif
                
                // tell the node that the current branch is unsat
                node -> unsat(neg);
                
                // count the branch as finished
                solverData -> m_noFinishedNodes++;
                solverData -> m_nodeFinishedDL += solverData -> m_decisionLevel;
              }
              else {
                
                #ifdef DEBUG_OUTPUT          
                {boost::mutex::scoped_lock l(m_coutMutex);
                  std::cout << "c " << boost::this_thread::get_id() << ": will split node " << node << " (" << (neg?"negative":"positive") << " branch)" <<  std::endl;
                }
                #endif
                
                
                if (neg) {
                  splitResultNeg = node -> split(attemptSplit, &(solverData -> m_database), & (solverData -> m_assignment));
                  
                  // split result empty? -> SAT
                  if (splitResultNeg[0] == nullptr) {                  
                    node -> sat(neg, & (solverData -> m_assignment)); 
                    
                    // count the branch as finished
                    solverData -> m_noFinishedNodes++;
                    solverData -> m_nodeFinishedDL += solverData -> m_decisionLevel;
                  }
                  else {
                    // count the split
                    {
                      solverData -> m_splits++;            
                    }
                  }
                  
                  if (!splitResultNeg.empty() && splitResultNeg[0] != nullptr) {   
                    node -> addNegChildren(splitResultNeg);   
                  }
                }              
                else {
                  splitResultPos = node -> split(attemptSplit, &(solverData -> m_database), & (solverData -> m_assignment));
                  
                  // split result empty? -> SAT
                  if (splitResultPos[0] == nullptr) {          
                    
                    node -> sat(neg, & (solverData -> m_assignment)); 
                    
                    // count the branch as finished
                    solverData -> m_noFinishedNodes++;
                    solverData -> m_nodeFinishedDL += solverData -> m_decisionLevel;
                  }
                  else {
                    // count the split
                    {
                      solverData -> m_splits++;            
                    }
                  }
                  
                  if (!splitResultPos.empty() && splitResultPos[0] != nullptr) {   
                    node -> addPosChildren(splitResultPos);   
                  }               
                }              
              }              
            } // end of for loop
            
            if (!abort) {
              
              // tell the node that it has been split
              node -> splitDone();
              
              if (node -> state() < 40) {
                
                if (!splitResultPos.empty() && splitResultPos[0] != nullptr) {   
                  // add the first result to the current node
                  solverData -> m_nodes.push_back(splitResultPos[0]);                  
                  
                  
                  // add the rest as global nodes if the nodes manager requires nodes
                  if (m_nodeManager -> requiresNodes()) {
                    for (uint_fast32_t n = 1; n < splitResultPos.size(); n++) {
                      
                      // add to computation stack
                      m_nodeManager -> addNode(splitResultPos[n]);
                      
                      // notify a waiting thread that there is a new node available
                      m_nodesAvailable.notify_one();
                      
                      // increase activity of variable involved in split
                      increaseActivity(node->decisionVariable());
                    }                  
                  }
                  else {
                    for (uint_fast32_t n = 1; n < splitResultPos.size(); n++) {
                      solverData -> m_nodes.push_back(splitResultPos[n]);         
                      
                      // test: increase activity of variable involved in split
                      increaseActivity(node->decisionVariable());
                    }
                  }                  
                }
                
                // and the neg ones
                if (!splitResultNeg.empty() && splitResultNeg[0] != nullptr) {   
                  //solverData -> m_nodes.push_back(splitResultNeg[0]);      
                  
                  if (m_nodeManager -> requiresNodes()) {
                    for (uint_fast32_t n = 0; n < splitResultNeg.size(); n++) {
                      
                      m_nodeManager -> addNode(splitResultNeg[n]);
                      
                      // notify a waiting thread that there is a new node available
                      m_nodesAvailable.notify_one();
                      
                      // test: increase activity of variable involved in split
                      increaseActivity(node->decisionVariable());
                    }     
                  }
                  else {                    
                    for (uint_fast32_t n = 0; n < splitResultNeg.size(); n++) {
                      solverData -> m_nodes.push_back(splitResultNeg[n]);       
                      
                      // test: increase activity of variable involved in split
                      increaseActivity(node->decisionVariable());                  
                    }
                  }
                }
                solverData -> m_splitNoComponents += splitResultPos.size() + splitResultNeg.size();
              }
            }
          }
          
          
          boost::this_thread::interruption_point();
        }
        catch(const boost::thread_interrupted&)
        {
          // Thread interruption request received, break the loop
          std::cout << "c " << boost::this_thread::get_id() << ": thread interrupt!" << std::endl;
          break;
        }        
      }
      
      std::cout << "c " << boost::this_thread::get_id() << ": " << boost::this_thread::get_id() << ": done" << std::endl;
    }
    
    
    
    // backtracks to the given backtrack level, reverting all variable assignments done in the process
    void backtrackOneLevel (SolverData* solverData) {
      #ifdef DEBUG_OUTPUT
      {boost::mutex::scoped_lock l(m_coutMutex);
        std::cout << "c " << boost::this_thread::get_id() << ": backtracking one level from " << solverData -> m_decisionLevel << " to " << (solverData -> m_decisionLevel-1) << std::endl;
      }
      #endif
      
      assert(0 < solverData -> m_decisionLevel);
      
      // get the position in the assignment stack to backtrack to
      uint_fast32_t newEndPosition = solverData -> m_decisionLevelToAssignmentStack[solverData -> m_decisionLevel];
      
      // reset all the assignments
      for (uint_fast32_t i = newEndPosition; i < solverData -> m_assignmentStackEndPosition; i++) {
        solverData -> m_assignment[solverData -> m_assignmentStack[i]] = 0;
      }
      
      // set the new assignment stack end position
      solverData -> m_assignmentStackCurrentPosition = newEndPosition;
      solverData -> m_assignmentStackEndPosition = newEndPosition;
      
      // set the new decision level
      solverData -> m_decisionLevel = solverData -> m_decisionLevel - 1;          
    }
    
    
  private:
    // Copy constructor.
    Core (const Core&);
    
    // Assignment operator.
    Core& operator = (const Core&);
    
    // Backtracks to the given component tree node
    // returns false when a conflict occured (this can happen through learning)
    bool backtrack (ComponentTreeNode* node, SolverData* solverData)
    {      
      // count the backtrack
      solverData -> m_backtracks++;
      
      uint_fast32_t bL = 0;
      
      // decision on the way from the common ancestor to node. Note: order is reversed
      std::vector<uint_fast32_t> decisions;
      
      // backtrack to dl 1
      if (node == nullptr) {
        
        // in the initial case the solver has not visited any nodes yet -> nothing to do
        if (solverData -> m_currentNode == nullptr) {
          return true;
        }
        
        bL = 1;
        
        // determine the new current node if it is set
        ComponentTreeNode* a = solverData -> m_currentNode;
        
        while (a != nullptr && a -> decisionLevel() != 1) {
          a = a -> parent();
        }
        
        node = a;
        
        assert(node -> getExtraImplications() -> empty());
      }
      // initial case (no node has been visited yet)
      else if (solverData -> m_currentNode == nullptr) {
        // go to the highest possible level
        bL = 1;
        
        // and assign all the values along the path there
        ComponentTreeNode* a = node;
        while (a -> parent() != nullptr) {
          // get the decision variable
          uint_fast32_t dec = (a -> parent() -> decisionVariable()) << 1;
          // and the sign
          if (a -> neg()) {
            dec ^= 1;
          }
          
          // get the extra implications
          for (auto imp: *(a -> getExtraImplications())) {
            
            // implications are marked by shifting them
            decisions.push_back(imp + 4*m_variables);
          }
          
          // now store the decision
          decisions.push_back(dec);
          
          a = a -> parent();
        }        
      }
      else { // (node != nullptr && m_currentNode != nullptr)
        // determine the level to backtrack to and the assignments to re-assign
        
        // helpers variables for the common ancestor search
        ComponentTreeNode* a = node;
        ComponentTreeNode* b = solverData -> m_currentNode;
        
        // when a == b, then this is the common ancestor
        while (a != b) {
          
          // make sure that the parents always existed
          assert(a != nullptr);
          assert(b != nullptr);
          
          // always go to the parent of the node with the higher dl
          
          uint_fast32_t dlA = a -> decisionLevel();
          uint_fast32_t dlB = b -> decisionLevel();
          
          if (dlA >= dlB) {
            // when backtracking from a, we need to store the decisions along the way
            if (a -> parent() != nullptr) {
              // get the decision variable
              uint_fast32_t dec = (a -> parent() -> decisionVariable()) << 1;
              // and the sign
              if (a -> neg()) {
                dec ^= 1;
              }  
              
              // get the extra implications
              for (auto imp: *(a -> getExtraImplications())) {
                
                // implications are marked by shifting them
                decisions.push_back(imp+4*m_variables);
                //extraImplications.push_back(imp);
              }   
              
              // now store the decision
              decisions.push_back(dec);    
            }
            
            a = a -> parent();
          }
          if (dlA <= dlB) {
            b = b -> parent();
          }
        }
        
        // now the common ancestor should have been found
        assert(a == b);
        
        
        // also, a should have a valid decision
        assert(a -> decisionVariable() != 0);
        
        // get the decision level
        bL = a -> decisionLevel();
        
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": determined common ancestor to be " << a << ", current node = " << solverData -> m_currentNode << std::endl;
        }
        #endif
      }
      
      #ifdef DEBUG_OUTPUT
      {boost::mutex::scoped_lock l(m_coutMutex);
        std::cout << "c " << boost::this_thread::get_id() << ": backtrack to dl " << bL << ", then go to decision level " << node -> decisionLevel() << std::endl;
      }
      #endif
      
      // statistics: 
      // count the number of decision levels backtracked
      solverData -> m_backtrackDistance += (solverData -> m_decisionLevel - bL);
      // add the number of re-assignments to the backtrack distance
      solverData -> m_backtrackDistance += decisions.size();
      
      // now call the backtrack operation itself
      backtrack(bL, node, solverData);
      
      #ifdef DEBUG_OUTPUT
      {boost::mutex::scoped_lock l(m_coutMutex);
        std::cout << "c " << boost::this_thread::get_id() << ": ...done" << std::endl;
      }
      #endif
      
      
      // re-assignment of variables to reach node from the current level
      
      //for (int i = decisions.size() - 1; i >= 0; i--) {
      while (!decisions.empty()) {
        uint_fast32_t dec = decisions.back();
        decisions.pop_back();
        
        // store whether the decision is actually an implication
        bool implication(false);
        
        // check if the decision is actually an implication
        if (dec > 4*m_variables) {
          dec = dec - 4*m_variables;
          
          implication = true;
        }
        
        // if the decision variable is assigned with the other polarity, the backtracking failed (this can occur due to learning)
        if (solverData -> m_assignment[dec^1]) {
          
          #ifdef DEBUG_OUTPUT
          {boost::mutex::scoped_lock l(m_coutMutex);
            std::cout << "c " << boost::this_thread::get_id() << ": node " << node << " decision conflict during backtrack" << std::endl;
          }
          #endif
          
          // when there was a conflict during the assignment, determine the last "valid" node
          // this is the node in m_decisionLevel - 1
          ComponentTreeNode* btNode = node;
          while (btNode != nullptr && btNode -> decisionLevel() != (solverData -> m_decisionLevel - 1)) {
            btNode = btNode -> parent();
          }
          
          // backtrack to the parent node
          assert(backtrack(btNode, solverData));           
          
          return false;
        }
        
        // check if the decision variable is already assigned
        if (!solverData -> m_assignment[dec]) {   
          if (implication) {
            
            addImplication(dec, nullptr, solverData);
          }
          else {
            addDecision(dec, solverData);
          }
        }
        else if (!implication){
          // decision variable already assigned -> add an "empty" decision level
          
          // update the decion level
          solverData -> m_decisionLevel++;      
          
          // since this is a decision, also store the position of the assignment in the assignment stack
          solverData -> m_decisionLevelToAssignmentStack[solverData -> m_decisionLevel] = solverData -> m_assignmentStackEndPosition;
          
          
          #ifdef DEBUG_OUTPUT
          {boost::mutex::scoped_lock l(m_coutMutex);
            std::cout << "c " << boost::this_thread::get_id() << ": node " << node << " added empty decision level (" << solverData -> m_decisionLevel << ") during backtrack, since " << dec << " is already assigned" << std::endl;
          }
          #endif
        }
        
        // if there is a conflict during deduce, the backtracking failes (this can occur due to learning)
        if (deduce(solverData) != nullptr) {
          #ifdef DEBUG_OUTPUT
          {boost::mutex::scoped_lock l(m_coutMutex);
            std::cout << "c " << boost::this_thread::get_id() << ": node " << node << " assignment conflict during backtrack" << std::endl;
          }
          #endif
          
          // when there was a conflict during the assignment, determine the last "valid" node
          // this is the node in m_decisionLevel - 2
          ComponentTreeNode* btNode = node;
          while (btNode != nullptr && btNode -> decisionLevel() != (solverData -> m_decisionLevel - 2)) {
            btNode = btNode -> parent();
          }
          
          // backtrack to the parent node
          assert(backtrack(btNode, solverData));
          
          return false;
        }
      }
      
      #ifdef DEBUG_OUTPUT
      {boost::mutex::scoped_lock l(m_coutMutex);
        std::cout << "c " << boost::this_thread::get_id() << ": backtrack done, current assignment stack: ";
        
        for (uint_fast32_t i = 0; i < solverData -> m_assignmentStackEndPosition; i++) {
          std::cout << solverData -> m_assignmentStack[i] << " ";
        }
        std::cout << std::endl;
      }
      #endif
      
      return true;
    }
    
    
    void backtrack (uint_fast32_t bL, ComponentTreeNode* node,  SolverData* solverData) {
      
      // backtrack
      // Initialization.
      uint_fast32_t stopper(solverData -> m_decisionLevelToAssignmentStack[bL + 1]);
      
      
      if (stopper > solverData -> m_assignmentStackEndPosition) {
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": stopper = " << stopper << ",solverData -> m_assignmentStackEndPosition =  " << solverData -> m_assignmentStackEndPosition << std::endl;
        }
      }      
      assert(stopper <= solverData -> m_assignmentStackEndPosition);
      
      // Undo all variable assignments on decision levels higher than "bL".
      // This loop assumes that we have a dummy assignment at the first 
      // position of the decision stack, which has been assigned on decision level 0. 
      while (stopper != solverData -> m_assignmentStackEndPosition)
      {
        // Decrement "solverData -> m_assignmentStackEndPosition".
        --solverData -> m_assignmentStackEndPosition;
        
        // Get the current assignment.
        uint_fast32_t lit(solverData -> m_assignmentStack[solverData -> m_assignmentStackEndPosition]); 
        
        // Undo the current assignment.
        solverData -> m_assignment[lit] = 0;        
        
        
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": backtrack removed assignment of " << lit << " which was on position " << solverData -> m_assignmentStackEndPosition << std::endl;
        }
        #endif
      }
      
      assert(solverData -> m_decisionLevelToAssignmentStack[bL + 1] == solverData -> m_assignmentStackEndPosition);
      
      // Update "solverData -> m_decisionLevel".
      solverData -> m_decisionLevel = bL;
      
      // Update "m_dsImplIndex".
      solverData -> m_assignmentStackCurrentPosition = solverData -> m_assignmentStackEndPosition;
      
      // store the node as the current node
      solverData -> m_currentNode = node;
      
    }
    
    // chooses the next variable to branch on
    // do not choose variables on the ignore list
    uint_fast32_t decide (ComponentTreeNode* node, SolverData* solverData) {
      
      if (solverData -> m_threadNumber == 0 && m_maxActivity > 1e100) { 
        decreaseActivities();
      }
      
      // count the decision
      solverData -> m_decisions ++;
      
      double maxAct(0);
      uint_fast32_t maxVar(0);
      bool firstAssignment(true);
      
      
      // t1
      
      std::vector<uint_fast32_t> occurences(m_variables+1, 0);
      uint_fast32_t maxOccurences=0;
      
      if (solverData -> m_vsadsFactor < 0) {
        
        for (auto clause: *(node -> clauses())) {
          
          uint_fast32_t* lits = solverData -> m_database[clause] -> lits();
          
          
          for (uint_fast32_t l = 0; lits[l] != 0; l++) {
            occurences[lits[l] >> 1]++;    
          }
        }
      }
      else {
        // TODO: this is much slower now than it was before...
        for (uint_fast32_t var = 0; var < m_variables + 1; var++) {
          occurences[var] = m_litToClause[var<<1].size() + m_litToClause[(var<<1)^1].size();
        }        
      }
      
      for (uint_fast32_t var = 0; var < m_variables + 1; var++) {
        if (occurences[var] > maxOccurences) {
          maxOccurences = occurences[var];
        }
      }
      
      
      
      for (auto var: *(node -> variables())) {
        if (!solverData -> m_assignment[var << 1] && !solverData -> m_assignment[(var << 1)^1]) {
          
          double score;
          double activityNormalized =  (double)m_activity[var] / (double)m_maxActivity;
          double occurencesNormalized = (double)occurences[var] / (double)maxOccurences;
          
          if (activityNormalized > 0 || occurencesNormalized > 0) {
            //std::cout << "c var: " << var << " an: " << activityNormalized << ", on: " << occurencesNormalized << std::endl;
          }
          
          if (solverData -> m_vsadsFactor < 0) {            
            score = (-solverData -> m_vsadsFactor)*activityNormalized + occurencesNormalized; 
          }
          else {
            score = (solverData -> m_vsadsFactor)*activityNormalized + occurencesNormalized;            
          }          
          
          if (score > maxAct || firstAssignment) {
            maxVar = var;
            maxAct = score;
          }
          
          firstAssignment = false;
        }
      }
      
      if (maxVar != 0) {
        #ifdef DEBUG_OUTPUT
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << ": choosing " << maxVar << " as decision variable" << std::endl;
        }
        #endif
        return maxVar;
      }
      
      return 0;      
    }
    
    
    // Performs "Boolean Constraint Propagation". In case of a conflict, a pointer 
    // to the conflicting clause will be returned, otherwise the return value is nullptr.
    uint_fast32_t* deduce (SolverData* solverData) 
    {
      // Increment "m_bcps".
      solverData -> m_bcps++;
      
      // Process all assignments not checked so far.
      while (solverData->m_assignmentStackCurrentPosition !=  solverData->m_assignmentStackEndPosition)
      {
        // Get the next assignment.
        uint_fast32_t wlit(solverData -> m_assignmentStack[solverData -> m_assignmentStackCurrentPosition++] ^ 1); // ^ 1: look at clauses in which the literal is false
        
        // "wlit" has to be assigned, since it's part of the decision stack.
        assert(solverData -> m_assignment[wlit] || solverData -> m_assignment[wlit ^ 1]); 
        
        // Initialization.
        std::vector<uint_fast32_t>& bWatches(solverData -> m_binaries[wlit]); 
        uint_fast32_t bSize(bWatches.size()); 
        
        
        // Process all binary clauses in which "wlit" occurs.
        for (uint_fast32_t w = 0; w < bSize; ++w)
        {  
          
          // no binary clauses atm
          assert(false);
          
          // "Second" literal not satisfying the current binary clause?
          if (!solverData -> m_assignment[bWatches[w]])
          {
            
            // At this point we either have a conflict or an implication.
            
            // Conflict?
            if (solverData -> m_assignment[bWatches[w] ^ 1])
            {
              // Update "m_dynamicForcingClauses".
              solverData -> m_dynamicForcingClauses[0] = bWatches[w]; 
              solverData -> m_dynamicForcingClauses[1] = wlit;
              solverData -> m_dynamicForcingClauses[2] = 0; 
              
              // Return a pointer to the conflicting clause.
              return &solverData -> m_dynamicForcingClauses[0]; 
            }
            
            // Initialization.
            uint_fast32_t ptr((bWatches[w] >> 1) << 2); 
            
            // Update "m_dynamicForcingClauses".
            solverData -> m_dynamicForcingClauses[ptr    ] = bWatches[w]; 
            solverData -> m_dynamicForcingClauses[ptr + 1] = wlit;
            solverData -> m_dynamicForcingClauses[ptr + 2] = 0; 
            
            // Push the second literal as an implication forced
            // by a binary clause onto the decision stack.
            addImplication(bWatches[w], &solverData -> m_dynamicForcingClauses[ptr], solverData); 
            
          }
        }
        
        // Initialization.
        std::vector<std::pair<uint_fast32_t, uint_fast32_t*> >& watches(solverData -> m_watches[wlit]);
        uint_fast32_t size(watches.size()); 
        
        
        // Process all clauses in which "wlit" currently serves as a watched literal.
        for (uint_fast32_t w = 0; w < size; ++w)
        {       
          // Clause not satisfied by the blocking literal?
          if (!solverData -> m_assignment[watches[w].first])
          {
            // Get the literals of the current clause. 
            uint_fast32_t* lits(watches[w].second); 
            
            // Get the "second" watched literal (the one not equal to "wlit").
            // note: this is equivalent to  "if (lits[0] == wlit) sWL = lits[1]; else sWL = lits[0];"
            uint_fast32_t sWL(lits[0]); 
            sWL = sWL ^ lits[1];
            sWL = sWL ^ wlit;
            
            // Current clause satisfied by "sWL"?
            if (solverData -> m_assignment[sWL])
            {
              // Update the blocking literal.
              watches[w].first = sWL;
            }
            else
            {
              // Initialization ("pos" is set to 2, since there is no need 
              // to check the current watched literals again).
              uint_fast32_t pos(2);
              
              // Try to find a new watched literal within the remainder of "lits".
              // This loop assumes that "m_assignment[0]" and "m_assignment[1]" 
              // are both set to FALSE.
              while (solverData -> m_assignment[lits[pos] ^ 1]) 
              {
                ++pos; }
                
                // Have we found a new watched literal?
                if (lits[pos] != 0)
                {
                  // Get the literal checked last.
                  uint_fast32_t nWL(lits[pos]); 
                  
                  // Update "lits":
                  lits[0]   = sWL;
                  lits[1]   = nWL;
                  lits[pos] = wlit;
                  
                  // Update "m_watches" for both "wlit" and "nWL".
                  solverData -> m_watches[nWL].push_back(watches[w]); 
                  --size;
                  watches[w] = watches[size];
                  --w;
                  watches.pop_back();
                }
                else
                {
                  // At this point, we either have an implication or a conflict.
                  
                  // Conflict?
                  if (solverData -> m_assignment[sWL ^ 1])
                  { return watches[w].second; }
                  
                  // Update "lits":
                  lits[0] = sWL;
                  lits[1] = wlit;
                  
                  // Push "sWL" as an implication onto the decision stack.
                  addImplication(sWL, watches[w].second, solverData); 
                }
            }
          }
        }
      }
      
      // Everything went fine.
      return nullptr; 
    }
    
    
    
    // Performs conflict analysis according to the 1UIP scheme and backtracks afterwards.
    void analyze (uint_fast32_t* lits, bool doBacktrack, SolverData* solverData)
    {      
      
      // Increment "m_conflicts".
      solverData -> m_conflicts++;
      
      // Consistency check. 
      assert(lits != nullptr);
      
      // Update "m_incVarActivity".
      /*{
       *        m_incVarActivity *= m_decayFactor;
    } 
    */
      
      std::vector<uint_fast32_t> m_conflictClause; 
      
      // Add a dummy element (will be the UIP later on) to "m_conflictClause".
      m_conflictClause.push_back(0); 
      
      // Initialization.
      uint_fast32_t elements(0);
      uint_fast32_t pos(solverData -> m_assignmentStackEndPosition - 1); 
      uint_fast32_t uip(0); 
      uint_fast32_t btLV(0); 
      uint_fast32_t lbd(1); 
      
      // Perform conflict analysis according to the 1UIP scheme.
      do
      {
        // Analyze "lits".
        do
        {          
          // Get the index of the current literal of "lits".
          uint_fast32_t index((*lits) >> 1); 
          
          // Current literal not checked so far and not assigned on decision level 0? 
          // Checking the decision level on which a particular variable has been 
          // assigned, requires that assumptions are stored on decision levels 
          // greater 0. Otherwise we run into problems in the incremental mode. 
          if (solverData -> m_seenVar[index] != solverData -> m_conflicts && solverData -> m_level[index] > 0)
          {
            // Update "seen".
            solverData -> m_seenVar[index] = solverData -> m_conflicts;
            
            // Increase the activity of the variable corresponding to the current literal.
            increaseActivity(index);
            
            // Current literal assigned on "solverData -> m_decisionLevel"?
            if (solverData -> m_level[index] == solverData -> m_decisionLevel)
            { ++elements; }
            else
            {
              // Add the current literal to "m_conflictClause".
              m_conflictClause.push_back(*lits); 
              
              // Decision level on which the current literal has been assigned on not seen so far?
              if (solverData -> m_touchedLevel[solverData -> m_level[index]] != solverData -> m_conflicts)
              { 
                // Update "solverData -> m_touchedLevel". 
                solverData -> m_touchedLevel[solverData -> m_level[index]] = solverData -> m_conflicts; 
                
                // Update "lbd".
                ++lbd; 
                
                // Do we have to update the backtrack level "btLV"?       
                if (solverData -> m_level[index] > btLV)
                { btLV = solverData -> m_level[index]; }
              }
            }
          }
          
          // Increment "lits" to process the next literal of the current clause under consideration.
          ++lits; 
        }
        while ((*lits) != 0);
        
        // Determine the next clause to be processed. 
        while (solverData -> m_seenVar[solverData -> m_assignmentStack[pos] >> 1] != solverData -> m_conflicts) { 
          --pos; 
        }
        
        uip = solverData -> m_assignmentStack[pos];
        
        // Update the status variables.
        lits = solverData -> m_forcing[uip >> 1];
        assert(elements == 1 || lits != nullptr);
        if (lits != nullptr)
        { ++lits; }
        --pos;
        --elements;
      }
      while (elements > 0);
      
      // Flip the sign of the UIP.
      uip = uip ^ 1;
      
      // Initialization.
      uint_fast32_t size(m_conflictClause.size());
      
      // Do we have an unit clause?
      if (size == 1)
      {        
        // add the new unit as a conflict clause
        
        
        std::vector<uint_fast32_t> newUnit;
        
        newUnit.push_back(uip);
        
        //boost::mutex::scoped_lock l(*m_conflictClauseWriteMutex);
        
        //m_conflictClauses.push_back(std::make_pair(newUnit, 0));
        
        addNewConflictClause(newUnit, 0);
        
        return;
      }
      
      // Perform a simple conflict clause minimization step. See also "Towards Understanding 
      // and Harnessing the Potential of Clause Learning" by Beame, Kautz, and Sabharwal.
      for (uint_fast32_t l = 1; l < size; ++l)
      {
        // Get the next literal of "m_conflictClause" and its forcing clause.
        uint_fast32_t literal(m_conflictClause[l]); 
        uint_fast32_t* reason(solverData -> m_forcing[literal >> 1]); 
        
        // Do we talk about an implication?
        if (reason != nullptr)
        {
          // Check whether all literals stored by "reason" have been processed during conflict analysis. 
          // In this particular case we are allowed to remove "literal" from the conflict clause.
          do
          {
            // Do we have to keep "literal"?
            if (solverData -> m_seenVar[(*reason) >> 1] != solverData -> m_conflicts && solverData -> m_level[(*reason) >> 1] > 0)
            { break; }
            
            // Increment "reason".
            ++reason; 
          }
          while (*reason != 0);
          
          // Is it safe to remove "literal"?
          if (*reason == 0)
          { --size; m_conflictClause[l] = m_conflictClause[size]; --l; }
        }
      }
      
      // If we now removed all literals, we've got a problem.
      assert(size > 1);
      
      // Resize "m_conflictClause".
      m_conflictClause.resize(size); 
      
      // Store the UIP at the first position of "m_conflictClause". 
      m_conflictClause[0] = uip; 
      
      // Update the second watched literal.
      uint_fast32_t maxPos(1); 
      uint_fast32_t maxLevel(solverData -> m_level[m_conflictClause[1] >> 1]); 
      for (uint_fast32_t l = 2; l < size; ++l)
      {
        if (solverData -> m_level[m_conflictClause[l] >> 1] > maxLevel)
        { maxPos = l; maxLevel = solverData -> m_level[m_conflictClause[l] >> 1]; }
      }
      uint_fast32_t tmp(m_conflictClause[1]); 
      m_conflictClause[1] = m_conflictClause[maxPos]; 
      m_conflictClause[maxPos] = tmp; 
      
      // Backtrack to decision level "btLV".      
      //std::cout << "c " << boost::this_thread::get_id() << ": call backtrack to level " << btLV << ", uip = " << uip << std::endl; 
      
      if (doBacktrack) {
        //backtrack(btLV);
      }
      
      // Do we have a binary clause to be handled separately?
      if (size == 2 && m_enableBinaryClauses) 
      {
        // Consistency check.
        assert(m_conflictClause.size() == 2); 
        
        // Update "solverData -> m_binaries".
        solverData -> m_binaries[m_conflictClause[0]].push_back(m_conflictClause[1]); 
        solverData -> m_binaries[m_conflictClause[1]].push_back(m_conflictClause[0]); 
        
        // Initialization.
        uint_fast32_t ptr((uip >> 1) << 2); 
        
        // Update "solverData -> m_dynamicForcingClauses".
        solverData -> m_dynamicForcingClauses[ptr    ] = uip; 
        solverData -> m_dynamicForcingClauses[ptr + 1] = m_conflictClause[1];
        solverData -> m_dynamicForcingClauses[ptr + 2] = 0; 
        
        // Add the UIP (equal to "m_conflictClause[0]") as an implication to the decision stack.
        //addImplication(uip, &solverData -> m_dynamicForcingClauses[ptr]); 
        
        return; 
      }
      
      // Generate a new clause and add it to the clause database. 
      
      // lock the database
      {
        //boost::mutex::scoped_lock l(*m_conflictClauseWriteMutex);
        
        assert(!m_conflictClause.empty());
        
        /*
         *        std::cout << "c new conflict clause: ";
         *        for (auto lit: m_conflictClause) {
         *          std::cout << ((lit&0x01) > 0?"-":"") << (lit >> 1) << " ";
      }
      std::cout << "0" << std::endl;
      */
        
        // add the new clause
        //m_conflictClauses.push_back(std::make_pair(m_conflictClause, lbd)); 
        
        
        addNewConflictClause(m_conflictClause, lbd);
      }
      
      // Add the UIP as an implication to the decision stack.
      //addImplication(uip, cl->lits());
    }
    
    
    /*
     *    // compute clusters in accordance with Fan et al: Clustering and Partition Based Divide and Conquer for SAT Solving
     *    class Cluster {
     *    public: 
     *      // creates a new cluster with a single clause
     *      Cluster(uint_fast32_t clause, std::vector<Clause*>* database) {
     *        
     *        // store the clause
     *        m_cl.push_back(clause);
     *        
     *        // get the variables from the clause
     *        uint_fast32_t* lits = database -> at(clause) -> lits();
     *        
     *        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
     *          m_var.push_back(lits[l] >> 1);
  }
  
  // variables are always sorted, this simplifies the merge operation
  std::sort(m_var.begin(), m_var.end());
  }
  
  // creates a new cluster by merging two other clusters
  Cluster(Cluster& c1, Cluster& c2) {
  
  // merge the clauses
  uint_fast32_t pos1(0);
  uint_fast32_t pos2(0);
  
  while (pos1 != c1.m_cl.size() && pos2 != c2.m_cl.size()) {
    
    if (c1.m_cl[pos1] <= c2.m_cl[pos2]) {
      m_cl.push_back(c1.m_cl[pos1]);
      pos1++;
      
      // if the clauses are the same, increment pos2, too
      if (c1.m_cl[pos1-1] == c2.m_cl[pos2]) {
        pos2++;
  }
  }
  else {
    m_cl.push_back(c2.m_cl[pos2]);
    pos2++;            
  }
  }
  
  // add remaining clauses
  while (pos1 != c1.m_cl.size()) {
    
    m_cl.push_back(c1.m_cl[pos1]);
    pos1++;
  }
  while (pos2 != c2.m_cl.size()) {
    
    m_cl.push_back(c2.m_cl[pos2]);
    pos2++;
  }
  
  
  // merge the variables
  pos1 = 0;
  pos2 = 0;
  
  while (pos1 != c1.m_var.size() && pos2 != c2.m_var.size()) {
    
    if (c1.m_var[pos1] <= c2.m_var[pos2]) {
      m_var.push_back(c1.m_var[pos1]);
      pos1++;
      
      // if the clauses are the same, increment pos2, too
      if (c1.m_var[pos1-1] == c2.m_var[pos2]) {
        pos2++;
  }
  }
  else {
    m_var.push_back(c2.m_var[pos2]);
    pos2++;            
  }
  }
  
  // add remaining clauses
  while (pos1 != c1.m_var.size()) {
    
    m_var.push_back(c1.m_var[pos1]);
    pos1++;
  }
  while (pos2 != c2.m_var.size()) {
    
    m_var.push_back(c2.m_var[pos2]);
    pos2++;
  }
  }
  
  double similarityWith(Cluster* b) {
  
  // count the variables that occur in both clusters
  double sharedVariables = 0;
  
  uint_fast32_t pos1(0);
  uint_fast32_t pos2(0);
  
  while (pos1 != m_var.size() && pos2 != b -> m_var.size()) {
    
    if (m_var[pos1] <= b -> m_var[pos2]) {
      pos1++;
      
      // if the clauses are the same, increment pos2, too
      if (m_var[pos1] == b -> m_var[pos2]) {
        
        // and count as a shared variable
        sharedVariables++;
        pos2++;
  }
  }
  else {
    pos2++;            
  }          
  }
  
  double minElements = m_var.size();
  if (b -> m_var.size() < minElements) {
    minElements = b -> m_var.size();
  }
  
  // 
  double factor = 1;
  if (b -> m_var.size() < 10 || m_var.size() < 10) {
    factor = 1.1;
  }
  
  return factor * sharedVariables / minElements;
  }
  
  std::vector<uint_fast32_t> m_var;
  std::vector<uint_fast32_t> m_cl;
  };
  std::vector<Cluster*> clustering() {
  std::vector<Cluster*> C;
  std::vector<Cluster*> Cmin;
  
  // BLACK MAGIC NUMBERS
  uint_fast32_t minClusterNumber = 1;
  double threshold = 0.5;
  uint_fast32_t maxClusterNumber = 50;
  
  // create cluster for each clause, add to c
  for (uint_fast32_t cl = 0; cl < m_clauses; cl++) {
    C.push_back(new Cluster(cl, &m_solverData.m_database));
  }
  
  bool merged = true;
  uint_fast32_t loopUpperBound = C.size();
  
  while(merged) {
    
    merged = false;
    
    if (C.size() > minClusterNumber) {
      Cmin = C;
  }
  else {
    break;
  }
  
  for(uint_fast32_t i = 0; i < loopUpperBound; i++) {
    double maxSim = 0; 
    uint_fast32_t maxIndex = i;
    
    for(uint_fast32_t j = 0; j < loopUpperBound; j++) {
      if (i != j) {
        double sim = C[i] -> similarityWith(C[j]);
        
        if (sim > maxSim) {
          maxSim = sim;
          maxIndex = j;
  }
  }
  } // end for j
  
  if (maxSim > threshold) {
    // merge C[i] and C[maxIndex]
    
    Cluster* del2 = C[i];
    C[i] = new Cluster(*C[i], *C[maxIndex]);
    
    //delete del2;
    
    // remove the other original from C
    Cluster* del1 = C[maxIndex];
    
    uint_fast32_t sizeb = C.size();
    
    C.erase(std::remove(C.begin(), C.end(), del1), C.end());
    assert(sizeb - C.size()== 1);
    
    
    //delete del1;
    
    merged = true;
    
    // one less element
    loopUpperBound--;
  }
  }
  
  // decrease threshold if there are to many clusters left
  if (!merged && C.size() > maxClusterNumber) {
    threshold *= 0.5;
    merged = true;
  }
  }
  
  if (C.size() > minClusterNumber) {
    
    return C;
  }
  
  std::cout << "c returning Cmin!" << std::endl;
  return Cmin;
  }
  
  // computes the variables to remove to split the formula into clusters
  void clusteringVarsToRemove() {
  
  std::vector<Cluster*> C = clustering();
  
  std::cout << "c clustering found " << C.size() << " clusters" << std::endl;
  
  // for each variable store in which clusters it occurs
  std::vector<std::vector<uint_fast32_t> > varOccursIn(m_variables+1);
  
  
  for (uint_fast32_t cl = 0; cl < C.size(); cl ++) {
    std::cout << "c Cluster " << cl << ": ";
    for (uint_fast32_t v: C[cl] -> m_var) {
      std::cout << v << " ";
      varOccursIn[v].push_back(cl);
  }
  std::cout << std::endl;
  }
  
  // count the number of variables in more than one cluster
  uint_fast32_t inCut(0);
  for (uint_fast32_t v = 0; v <= m_variables; v++) {
    if (varOccursIn[v].size() > 1) {
      inCut++;
  }
  }
  
  std::cout << "c would have to remove " << inCut << " variables to split the clusters" << std::endl;
  
  
  // min cut 
  uint_fast32_t noNodes = C.size();
  
  // the min cut of each round
  std::vector<std::vector<uint_fast32_t> > roundMinCut(noNodes);
  
  
  for (uint_fast32_t i = 0; i < C.size() - 1; i++) {
    std::vector<uint_fast32_t> Si;
    Si.push_back((noNodes-1)/2);
    
    uint_fast32_t lastScore = 0;
    
    // now compute the strongest connected node
    for (uint_fast32_t rnd = 0; rnd < noNodes-1; rnd++) {
      
      //std::cout << "c round " << rnd << std::endl;
      
      int32_t maxScore = -1;
      int32_t maxNode = -1;
      
      for(uint_fast32_t node = 0; node < noNodes; node++) {
        
        //std::cout << "c node: " << node << std::endl;
        int32_t score = 0;
        
        // check if the node is in Si
        bool found = false;
        for (auto si: Si) {
          if (si == node) {
            found = true;
            break;
  }
  }
  if (found) {
    //std::cout << "c node " << node << " is already in Si" << std::endl;
    continue;
  }
  
  for (uint_fast32_t v = 0; v <= m_variables; v++) {
    if (varOccursIn[v].size() > 1) {
      bool cOccurs = false;
      bool SiOccurs = false;
      for (auto c: varOccursIn[v]) {
        if (c == node) {
          cOccurs = true;
  }
  else if (!SiOccurs) {
    for (auto si: Si) {
      if (si == node) {
        SiOccurs = true;
  }
  }
  }
  }
  if (cOccurs && SiOccurs) {
    // node and sI share this variable
    score++;
  }                
  }            
  }
  
  if(score > maxScore) {
    maxScore = score;
    maxNode = node;
  }
  }
  
  assert(maxNode != -1);
  
  // add the maxNode to Si
  Si.push_back(maxNode);
  
  std::cout << "c added " << maxNode << " to Si" << std::endl;
  }
  
  // compute the cut between the last node and all other nodes        
  for (uint_fast32_t v = 0; v <= m_variables; v++) {
    if (varOccursIn[v].size() > 1) {
      
      bool inLast = false;
      bool inOther = false;
      for (auto c: varOccursIn[v]) {
        if (c == Si[noNodes - 1]) {
          
          inLast = true;
  }
  else {
    inOther = true;
  }
  }
  
  if (inLast && inOther) {
    
    // add the var to the round min cut
    roundMinCut[i].push_back(v);
  }
  }
  }
  
  // now merge the last two nodes -> replace all occureces of Si[noNodes-2] by Si[noNodes-1]
  std::cout << "c merge group " << Si[noNodes-2] << " into " << Si[noNodes-1] << std::endl;
  for (uint_fast32_t v = 0; v <= m_variables; v++) {
    
    bool occurs(false);
    bool replaced(false);
    for(uint_fast32_t c = 0; c < varOccursIn[v].size(); c++) {
      if(varOccursIn[v][c] == Si[noNodes-2]) {
        varOccursIn[v][c] = Si[noNodes-1];
        replaced = true;
  }
  else if(varOccursIn[v][c] == Si[noNodes-1]) {
    occurs = true;
  }
  }
  
  // check if we added a double
  if (occurs && replaced) {
    
  }
  }
  
  // move every node > Si[noNodes - 2] to one position earlier
  for (uint_fast32_t v = 0; v <= m_variables; v++) {
    for(uint_fast32_t c = 0; c < varOccursIn[v].size(); c++) {
      if(varOccursIn[v][c] > Si[noNodes-2]) {
        varOccursIn[v][c]--;
  }
  }
  }
  
  
  noNodes--;
  }
  
  uint_fast32_t minCut = roundMinCut[0].size();
  uint_fast32_t minCutRound = 0;
  for (uint_fast32_t i = 0; i < C.size() - 1; i++) {
    std::cout << "c round " << i << " minCut: " << roundMinCut[i].size() << std::endl;
    
    if (roundMinCut[i].size() < minCut) {
      minCut = roundMinCut[i].size();
      minCutRound = i;
  }
  }
  
  std::cout << "c min cut in round " << minCutRound << ", " << minCut << " cuts" << std::endl << std::endl;
  
  for (auto var: roundMinCut[minCutRound]) {
    //increaseActivity(var);
  }
  }
  */
    
    
    // simplifies the formula with various preprocessing techniques for the given number of rounds
    bool simplify (uint_fast32_t simplifications, bool doTseitin, bool doUpla, bool doSelfSubsumption, bool doVariableElimination) {
      
      
      std::cout << "c ==================Start Preprocessing========================"      << std::endl;
      
      // multi step simplification
      // first: run only deduce, upla, subsumption, self-subsumption, tseitin detection on clauses, store resulting clauses as originalClauses
      // second: run all possible simplification, resulting in a set of clauses newClauses
      // last: combine originalClauses and newClauses
      
      // (1)
      //  1.1) run bcp
      if (deduce(&m_solverData) != nullptr) {
        return false;
      }     
      
      #ifdef PREPROCESS_OUTPUT
      uint_fast32_t assignmentStackPosAfterDeduce = m_solverData.m_assignmentStackEndPosition;
      std::cout << "c " << "deduce added \t\t\t\t" << (assignmentStackPosAfterDeduce - 1) << " new implications" << std::endl;
      #endif
      
      // 1.2) run upla
      
      antomSimplify();
      
      if (doUpla) {
        if (!upla(true)) {
          return false;
        }
        
        if (deduce(&m_solverData) != nullptr) {
          return false;
        }
        
        // remove satisfied clauses. 
        antomSimplify();      
      }
      
      
      // initially, all variables and clauses are present
      std::vector<bool>* activeClauses = new std::vector<bool>(m_clauses, true);
      std::vector<bool>* activeVariables = new std::vector<bool>(m_variables + 1, true);
      activeVariables -> at(0) = false;
      
      std::vector<uint_fast32_t> replacedVar(m_variables + 1, 0);
      
      // 1.3) compute litToClause
      m_litToClause.clear();
      m_litToClause.resize((m_variables << 1) + 2);
      for (uint_fast32_t c = 0; c < m_clauses; c++) {
        
        // add the clause to m_litToClause (for each literal in the clause)
        uint_fast32_t* lits(m_solverData.m_database[c]->lits()); 
        
        for(uint_fast32_t l = 0; lits[l] != 0 ; l++) {
          // and to m_litToClause
          m_litToClause[lits[l]].push_back(c);
        }
      }
      
      
      if (doTseitin) {
        TseitinDetection(&replacedVar);
        activeClauses -> resize(m_clauses + 1, true);
      }
      
      // 2.3) subsumption
      subsumption();    
      
      if (deduce(&m_solverData) != nullptr) {
        return false;
      }
      antomSimplify();
      
      // 2.4 self subsumption
      if (doSelfSubsumption) {
        selfSubsumption();      
        
        
        if (deduce(&m_solverData) != nullptr) {
          return false;
        }           
        antomSimplify();        
      }
      
      // 1.4) compute which variables are already assigned (by deduce / upla) and which clauses are therefore satisfied
      // this invalidates m_litToClause
      // remove variables that have been assigned already, and clauses that are sat
      for (uint_fast32_t a = 1; a < m_solverData.m_assignmentStackEndPosition; a++) {
        activeVariables -> at((m_solverData.m_assignmentStack[a]) >> 1) = false;
        // remove all clauses which contain the literal (in a positive way -> clause is sat)
        for (auto clause : m_litToClause[m_solverData.m_assignmentStack[a]]) {
          assert(false);
          activeClauses -> at(clause) = false; 
        }
      }  
      
      // make sure litToClause is not used anymore since it is incorrect
      m_litToClause.clear();
      
      // 1.4) compress the database (part 1: this does not re-add the clauses yet, it only stores them in a vector<vector<uint_fast32_t>>       
      std::vector<std::vector<uint_fast32_t> > originalClauses = compressDatabase(activeVariables, activeClauses, &replacedVar);
      
      // add the original clauses
      for (auto clause: originalClauses) {
        
        // empty vector = removed clause -> just ignore
        if (clause.empty()) {
          continue;
        }       
        
        // add the clause
        if (!addClause(clause)) {
          return false;
        }
      }
      
      // store the new size of the database
      m_clauses = m_solverData.m_database.size();
      
      #ifdef DEBUG_OUTPUT
      std::cout << "c " << boost::this_thread::get_id() << ": database size after first simplification: clauses: " << m_solverData.m_database.size() << "   variables: " << m_variables<< std::endl;
      #endif
      
      // when there is no deep simplification loop, return here
      if (simplifications == 0) {
        m_originalClauses = m_clauses;
        return true;
      }
      
      uint_fast32_t assignmentStackPre = m_solverData.m_assignmentStackEndPosition;
      
      // (2)      
      // vector to store the result of the upcoming simplifications
      std::vector<std::vector<uint_fast32_t> > newClauses;
      
      // simplification loop
      for (uint_fast32_t i = 0; i < simplifications; i++) {
        #ifdef PREPROCESS_OUTPUT
        std::cout <<  "c" << std::endl << "c simplify: round " << i << std::endl;      
        #endif 
        
        #ifdef PREPROCESS_OUTPUT
        uint_fast32_t assignmentStackPosBeforeDeduce = m_solverData.m_assignmentStackEndPosition;
        #endif 
        
        //  2.1) run bcp
        if (deduce(&m_solverData) != nullptr) {
          return false;
        }
        
        #ifdef PREPROCESS_OUTPUT
        uint_fast32_t assignmentStackPosAfterDeduce = m_solverData.m_assignmentStackEndPosition;
        
        std::cout << "c " << "deduce added \t\t\t\t" << (assignmentStackPosAfterDeduce - assignmentStackPosBeforeDeduce) << " new implications" << std::endl; 
        #endif
        
        // 2.2) compute litToClause
        m_litToClause.clear();
        m_litToClause.resize((m_variables << 1) + 2);
        for (uint_fast32_t c = 0; c < m_clauses; c++) {
          
          // ignore clauses that are no longer part of the component
          if (m_solverData.m_database[c] -> deactivated()) {
            continue;
          }
          
          // add the clause to m_litToClause (for each literal in the clause)
          uint_fast32_t* lits(m_solverData.m_database[c]->lits()); 
          
          for(uint_fast32_t l = 0; lits[l] != 0 ; l++) {
            // and to m_litToClause
            m_litToClause[lits[l]].push_back(c);
          }
        }
        if (doVariableElimination) {
          // 2.5) try to eliminate variables
          variableElimination();
          
          antomSimplify();        
        }
        
        // 2.4) self subsumption
        if (doSelfSubsumption) {
          selfSubsumption();    
          
          #ifdef PREPROCESS_OUTPUT
          uint_fast32_t bDeduce = m_solverData.m_assignmentStackCurrentPosition;
          #endif
          if (deduce(&m_solverData) != nullptr) {
            return false;
          }         
          #ifdef PREPROCESS_OUTPUT
          std::cout << "c " << "deduce added\t\t\t\t" << (m_solverData.m_assignmentStackEndPosition - bDeduce) << " new implications" << std::endl;
          #endif
          antomSimplify();
        }
        
        subsumption();  
        
        antomSimplify();
        if (deduce(&m_solverData) != nullptr) {
          return false;
        }
      } // end simplification loop
      
      #ifdef DEBUG_OUTPUT
      std::cout << "c " << boost::this_thread::get_id() << ": simplification loop done" << std::endl;
      #endif
      
      newClauses.clear();
      for (uint_fast32_t c = 0; c < m_clauses; c++) {
        
        // ignore clauses that are no longer part of the component
        if (!m_solverData.m_database[c] -> deactivated()) {
          
          // the new clauses
          std::vector<uint_fast32_t> newClause;
          
          uint_fast32_t* lits(m_solverData.m_database[c]->lits()); 
          
          for(uint_fast32_t l = 0; lits[l] != 0 ; l++) {
            newClause.push_back(lits[l]);         
          }
          
          // add the clause to new clauses
          newClauses.push_back(newClause);
        }
        
        // delete the clause (memory cleanup)
        delete m_solverData.m_database[c];
      }
      
      // clear the current database
      m_solverData.m_database.clear();
      
      // remove any assignments made during the optimization
      // TODO: is that required?
      while(m_solverData.m_assignmentStackEndPosition > assignmentStackPre) {
        m_solverData.m_assignmentStackEndPosition--;
        
        #ifdef DEBUG_OUTPUT
        std::cout << "c " << boost::this_thread::get_id() << ": removing assignment of " << m_solverData.m_assignmentStack[m_solverData.m_assignmentStackEndPosition] << std::endl;
#endif
        
        m_solverData.m_assignment[m_solverData.m_assignmentStack[m_solverData.m_assignmentStackEndPosition]] = 0;       
      }
      
      m_solverData.m_assignmentStackCurrentPosition = m_solverData.m_assignmentStackEndPosition;
      
      // make sure there are no more empty clauses
      m_emptyClause = false;
      
      
      #ifdef DEBUG_OUTPUT
      std::cout << "c " << boost::this_thread::get_id() << ": adding original clauses" << std::endl;
      #endif
      
      // add the original clauses
      for (auto clause: originalClauses) {
        
        // empty vector = removed clause -> just ignore
        if (clause.empty()) {
          continue;
        }
        
        // add the clause
        if (!addClause(clause)) {
          
          // conflict during preprocessing
          return false;
        }
      }
      
      // store the new size of the database as the number of original clauses
      m_originalClauses = m_solverData.m_database.size();
      
      // create a representation of the original clauses by literal. This is used to check if a new clause is already part of the original clauses
      std::vector<std::vector<uint_fast32_t> > litToOriginalClause;
      litToOriginalClause.resize((m_variables << 1) + 2);
      
      for (uint_fast32_t c = 0; c < originalClauses.size(); c++) {
        
        for (auto lit: originalClauses[c]) {
          litToOriginalClause[lit].push_back(c);
        }
      }
      
      #ifdef DEBUG_OUTPUT
      std::cout << "c " << boost::this_thread::get_id() << ": adding new clauses" << std::endl;
      #endif
      // add the new clauses (but only if they're not part of originalClauses) 
      uint_fast32_t addedClauses(0);
      for (auto clause: newClauses) {
        
        // empty vector = removed clause -> just ignore
        if (clause.empty()) {
          continue;
        }
        
        // check if the clause is already part of the database
        bool found = false;
        for (uint_fast32_t c: litToOriginalClause[clause[0]]) {
          
          if (originalClauses[c] == clause) {
            found = true; 
            break;
          }       
        }
        
        if (found) {
          continue;
        }
        
        // add the clause       
        if (!addClause(clause)) {
          std::cout << "c " << boost::this_thread::get_id() << ": add new clause: false" << std::endl;
          for (auto l : clause) {
            std::cout << l << " ";
          }
          
          std::cout << std::endl;
          return false;
        }  
        
        // count the clause as added
        addedClauses++;
      }
      
      #ifdef DEBUG_OUTPUT
      std::cout << "c " << "simplify done, database size: " << m_solverData.m_database.size() << " of which " << addedClauses << " clauses were added by advanced simplification" << std::endl;
      #endif
      
      
      // store the new size of the database
      m_clauses = m_solverData.m_database.size();
      
      
      //compute litToClause
      m_litToClause.clear();
      m_litToClause.resize((m_variables << 1) + 2);
      for (uint_fast32_t c = 0; c < m_clauses; c++) {
        
        // add the clause to m_litToClause (for each literal in the clause)
        uint_fast32_t* lits(m_solverData.m_database[c]->lits()); 
        
        for(uint_fast32_t l = 0; lits[l] != 0 ; l++) {
          // and to m_litToClause
          m_litToClause[lits[l]].push_back(c);
        }
      }
      
      subsumption();    
      
      antomSimplify();
      if (deduce(&m_solverData) != nullptr) {
        return false;
      }
      
      m_clauses = m_solverData.m_database.size();
      
      if (m_originalClauses > m_clauses) {
        m_originalClauses = m_clauses;
      }
      
      return true;
    }
    
    // simplification of antom sat solver
    void antomSimplify() {
      // count the number of variables removed by antom simplify
      uint_fast32_t removedVar(0);
      
      // clear & resize lit to clause
      m_litToClause.clear();
      m_litToClause.resize((m_variables << 1) + 2);
      
      
      // Update "m_solverData.m_binaries".
      for (uint_fast32_t v = 1; v <= m_variables; ++v)
      {
        // Initialization.
        uint_fast32_t pLit(v << 1);
        uint_fast32_t nLit((v << 1) ^ 1); 
        
        // Current variable assigned on decision level 0?
        if (m_solverData.m_assignment[pLit] || m_solverData.m_assignment[nLit])
        {              
          // Clear "m_solverData.m_binaries[pLit/nLit]".
          std::vector<uint_fast32_t>().swap(m_solverData.m_binaries[pLit]); 
          std::vector<uint_fast32_t>().swap(m_solverData.m_binaries[nLit]);             
        }
        else
        {
          // Remove binary clauses that are satisfied from "m_solverData.m_binaries[pLit/nLit]". 
          for (uint_fast32_t r = 0; r < 2; ++r)
          {
            // Flip "pLit".
            pLit ^= 1; 
            
            // Update "m_solverData.m_binaries".
            std::vector<uint_fast32_t>& watches(m_solverData.m_binaries[pLit]);
            uint_fast32_t size(watches.size()); 
            for (uint_fast32_t s = 0; s < size; ++s)
            {
              // Consistency check.
              assert(!m_solverData.m_assignment[watches[s] ^ 1]);
              
              // Literal "watches[s]" set to TRUE?
              if (m_solverData.m_assignment[watches[s]])
              { --size; watches[s] = watches[size]; watches.pop_back(); --s; }
            }
          }
        }
      }
      
      // Reset "m_solverData.m_watches".
      for (uint_fast32_t v = 1; v <= m_variables; ++v)
      {
        // Initialization.
        uint_fast32_t pLit(v << 1);
        uint_fast32_t nLit((v << 1) ^ 1); 
        
        // Clear "m_solverData.m_watches[pLit/nLit]".
        std::vector<std::pair<uint_fast32_t, uint_fast32_t*> >().swap(m_solverData.m_watches[pLit]);
        std::vector<std::pair<uint_fast32_t, uint_fast32_t*> >().swap(m_solverData.m_watches[nLit]);    
      }
      
      // Initialization.
      uint_fast32_t size(m_solverData.m_database.size());
      std::vector<Clause*> candidates;
      
      // Update "candidates".
      for (uint_fast32_t c = 0; c < size; ++c)
      {       
        // Consistency check.
        if(m_solverData.m_database[c]->deactivated()) {
          continue;
        }
        
        // Get the literals of the current clause.  
        uint_fast32_t* lits(m_solverData.m_database[c]->lits()); 
        
        // Initialization.
        uint_fast32_t p(0);
        uint_fast32_t q(0);
        
        // Check whether the clause is satisfied by an assignment made on decision level 0 or 
        // if it contains literals evaluating to false due to an assignment made on decision level 0.
        while (lits[p] != 0)
        {
          // Clause satisfied by the current literal?
          if (m_solverData.m_assignment[lits[p]])
          { break; }
          
          // Do we have to keep the current literal?
          if (!m_solverData.m_assignment[lits[p] ^ 1])
          { lits[q++] = lits[p]; }
          else {
            // the literal was removed
            removedVar++;
          }
          
          // Increment "p".
          ++p;
        }
        
        // Have we reached the clause stopper?
        if (lits[p] == 0)
        {
          // Add a new clause stopper.
          lits[q] = 0;  
          
          // Do we have a binary clause to be handled separately?
          if (q == 2 && m_enableBinaryClauses)
          {
            // Mark the current clause as "to be deleted".
            m_solverData.m_database[c]->deactivate();
            
            // Let's check whether we already have that particular binary clause.
            std::vector<uint_fast32_t>& watches(m_solverData.m_binaries[lits[0]]);
            uint_fast32_t size(watches.size()); 
            uint_fast32_t s(0); 
            for (s = 0; s < size; ++s)
            {
              if (watches[s] == lits[1])
              { break; }
            }
            
            // Update "m_solverData.m_binaries" if necessary.
            if (s == size)
            {
              m_solverData.m_binaries[lits[0]].push_back(lits[1]);
              m_solverData.m_binaries[lits[1]].push_back(lits[0]);
            }
          }
        }
        else
        {
          // Current clause satisfied by an assignment made on decision level 0, 
          // so let's mark it as "to be deleted".
          m_solverData.m_database[c]->deactivate();
        }
        
        // Current clause not belonging to the original CNF formula, and not deactivated? 
        if (m_solverData.m_database[c]->lbd() > 1 && !m_solverData.m_database[c]->deactivated())
        { candidates.push_back(m_solverData.m_database[c]); }             
      }
      
      // What about "candidates"?
      if (!candidates.empty())
      {
        // Sort "candidates" by decreasing LBD values. 
        std::sort(candidates.begin(), candidates.end(), compareLBD); 
        
        // Resize "candidates".
        candidates.resize(candidates.size() >> 1); 
        
        // Mark all candidates as "to be deleted" by setting 
        // the corresponding "Literals Blocks Distance" values to 0.
        while (!candidates.empty())
        { candidates.back()->deactivate(); candidates.pop_back(); }
      }
      
      // Remove all clauses marked as "to be deleted" from the clause database.
      uint_fast32_t newDBsize(0); 
      for (uint_fast32_t c = 0; c < size; ++c)
      {
        // Current clause marked as "to be deleted"?
        if (m_solverData.m_database[c]->deactivated())
        { delete m_solverData.m_database[c]; }
        else 
        { 
          // Add the current clause to the clause database. 
          m_solverData.m_database[newDBsize++] = m_solverData.m_database[c]; 
          
          // Initialization.
          uint_fast32_t* lits(m_solverData.m_database[c]->lits()); 
          
          // Update "m_solverData.m_watches".
          assert(lits[1] != 0); 
          m_solverData.m_watches[lits[0]].push_back(std::make_pair(lits[1], lits));
          m_solverData.m_watches[lits[1]].push_back(std::make_pair(lits[0], lits));          
          
          // add to lit to clause
          for (uint_fast32_t l = 0; lits[l] != 0; l++) {
            m_litToClause[lits[l]].push_back(newDBsize - 1);
          }
        }
      } 
      
      #ifdef PREPROCESS_OUTPUT
      //std::cout << "c " << "antom simplify removed an additional " << (m_clauses - newDBsize) << " clauses and " << removedVar << " literals from clauses" << std::endl;
      #endif
      
      
      // Resize "m_database".
      m_solverData.m_database.resize(newDBsize); 
      
      // store the new size of the db
      m_clauses = newDBsize;
    }
    
    
    
    // compresses the database by removing variables that are assigned and clauses that are satisfied
    // returns an std::vector of new clauses (NOTE: the variables in the clauses change -> recompute all helper data structures!)
    // deletes activeClauses and activeClauses
    std::vector<std::vector<uint_fast32_t> > compressDatabase (std::vector<bool>* activeVariables, std::vector<bool>* activeClauses, std::vector<uint_fast32_t>* replacedVar, std::vector<uint_fast32_t>* replacement = NULL) {
      
      uint_fast32_t removedVar(0);
      
      // the new clauses to be returned
      std::vector<std::vector<uint_fast32_t> > newClauses;
      
      // for each variable check if it still occurs in a clause
      std::vector<uint_fast32_t> occurs(m_variables + 1, 0);
      for (uint_fast32_t c = 0; c < m_clauses; c++) {
        
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // get the literals
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          
          occurs[lits[l] >> 1] = true;
        }
      }
      
      
      uint_fast32_t removedLiteralsNotOccuring(0);
      
      // compute the variable replacement
      uint_fast32_t currentVar = 1;
      std::vector<int32_t> oldToNewVar(m_variables+1, 0);
      for (uint_fast32_t oldVar = 1; oldVar < m_variables + 1; oldVar++) {
        
        if (occurs[oldVar] && activeVariables -> at(oldVar)) {
          
          // if the variable will be replaced, don't increase current var
          if (replacement != NULL && replacement -> at(oldVar<<1) != (oldVar<<1)) {
            
            // instead just store its repacement            
            assert(oldToNewVar[replacement -> at(oldVar << 1)>>1] != 0);
            
            oldToNewVar[oldVar] = oldToNewVar[replacement -> at(oldVar<<1)>>1];
            
            // check if this is a replacement of type "a by -b"
            if ((replacement -> at(oldVar<<1) & 0x01) > 0) {
              
              // negate old to new var
              oldToNewVar[oldVar] *= -1;
            }
            
            // count the variable as removed
            removedVar++;
            
            continue;
          }
          
          // store replacement in the local oldToNew structure
          oldToNewVar[oldVar] = currentVar;
          currentVar++;   
        }
        else {
          
          if (!m_solverData.m_assignment[oldVar << 1] && !m_solverData.m_assignment[(oldVar << 1)^1]) {
            
            if (!replacedVar -> at(oldVar)) {
              
              if (replacement == NULL || replacement -> at(oldVar<<1) == (oldVar << 1)) {
                removedLiteralsNotOccuring++;
                #ifdef DEBUG_OUTPUT
                std::cout << "c " << boost::this_thread::get_id() << ": var " << oldVar << " has no forced assignment and should therefore still be present" << std::endl;
                #endif                
              }                
            }
          }
          
          removedVar++;
        }
      }
      
      
      // store the number of additional free variables
      m_additionalFreeVariables += removedLiteralsNotOccuring;
      #ifdef DEBUG_OUTPUT
      std::cout << "c " << boost::this_thread::get_id() << ": compression removed " << removedLiteralsNotOccuring << " not occuring variables that are now free " << std::endl;
      #endif
      
      // resize the data structures with the new variable number
      updateDataStructures(m_variables - removedVar);
      
      
      // clear the assignments of all variables
      for (uint_fast32_t a = 1; a < m_solverData.m_assignmentStackEndPosition; a++) {
        m_solverData.m_assignment[m_solverData.m_assignmentStack[a]] = 0;
      }
      
      // and reset any values
      m_solverData.m_assignmentStackEndPosition = 1;
      m_solverData.m_assignmentStackCurrentPosition = 1;
      
      
      uint_fast32_t removedClauses(0);
      
      // add the clauses from the original database to the new database if they still exist
      for (uint_fast32_t clause = 0; clause < m_solverData.m_database.size(); clause++) {
        if (activeClauses -> at(clause) && ! m_solverData.m_database[clause] -> deactivated()) {
          // create a new clause with only the literals that are still available
          std::vector<uint_fast32_t> newClause;
          
          uint_fast32_t* lits(m_solverData.m_database[clause] -> lits()); 
          
          for(uint_fast32_t l = 0; lits[l] != 0 ; l++) {
            uint_fast32_t var(lits[l] >> 1);
            if (!activeVariables -> at(var)) {
              continue;
            }
            
            uint_fast32_t newLit;
            if (oldToNewVar[var] > 0) {
              newLit = (oldToNewVar[var] << 1);
            }
            else {
              newLit = ((-oldToNewVar[var]) << 1)^1;              
            }
            // there should be a valid entry in the conversation table
            assert(newLit != 0);
            
            // add the sign of the literal (- or +)
            if ((lits[l] & 0x01) > 0) {
              newLit ^= 1;
            }
            
            // add new literal to the new clause
            newClause.push_back(newLit);
          }
          // there should be at least 1 new variable in the clause
          assert(newClause.size() >= 1);
          
          // sort the clause
          std::sort(newClause.begin(), newClause.end());
          
          // iterate through the clause to find doubles
          uint_fast32_t writePos = 0;
          bool isSat (false);
          for (uint_fast32_t i = 1; i < newClause.size(); i++) {
            
            // check for sat
            if (newClause[writePos] == (newClause[i]^1)) {
              //std::cout << "c clause is sat" << std::endl;
              isSat = true;
              break;
            }
            
            if (newClause[writePos] != newClause[i]) {
              writePos++;
              newClause[writePos] = newClause[i];
            }
          }
          
          
          if (!isSat) {
            
            // remove last elements
            if (writePos != newClause.size() - 1) {
              //std::cout<< "erase" << std::endl;
              newClause.erase(newClause.begin() + writePos+1, newClause.end());
            }
            
            newClauses.push_back(newClause);
          }
        }
        else {
          removedClauses++;
        }
        
        // delete the clause from the database
        delete m_solverData.m_database[clause];
      }
      
      // memory cleanup
      delete activeVariables;
      delete activeClauses;         
      m_solverData.m_database.clear();
      
      
      
      #ifdef DEBUG_OUTPUT
      std::cout << "c " << boost::this_thread::get_id() << ": compression removed " << (removedVar) << " variables and " << removedClauses << " clauses (database size: " << newClauses.size() << ")" << std::endl;
      #endif
      
      return newClauses;
    }
    
    
    // updates all the data structures to work with a maximum of noVariables many variables
    void updateDataStructures(uint_fast32_t noVariables) {
      // note: 0 is not a valid variable -> we need one more space for the highest variable..
      
      // Initialization.
      uint_fast32_t max((noVariables << 1) + 2);
      
      // size of assignment:  2* size of # variables
      m_solverData.m_assignment.clear();
      m_solverData.m_assignment.resize(max, false);
      
      m_solverData.m_level.clear();
      m_solverData.m_level.resize(noVariables + 1, 0);
      
      // Update "m_activity".
      if (m_activity != nullptr) {
        delete m_activity;
      }
      m_activity = new std::atomic_uint_fast64_t[noVariables+1];
      for (uint_fast32_t i = 0; i < noVariables + 1; i++) {
        m_activity[i] = 0;
      }
      
      // Update "m_solverData.m_forcing".
      m_solverData.m_forcing.clear();
      m_solverData.m_forcing.resize(noVariables + 1, nullptr); 
      
      // Update "m_solverData.m_seenVar".
      m_solverData.m_seenVar.clear();
      m_solverData.m_seenVar.resize(noVariables + 1, 0);
      
      // Update "m_solverData.m_touchedLevel".
      m_solverData.m_touchedLevel.clear();
      m_solverData.m_touchedLevel.resize(noVariables + 1, 0); 
      
      m_litToClause.clear();
      m_litToClause.resize(max, std::vector<uint_fast32_t>());
      
      m_solverData.m_assignmentStack.clear();
      m_solverData.m_assignmentStack.resize(noVariables + 1, 0);
      
      m_solverData.m_decisionLevelToAssignmentStack.clear();
      m_solverData.m_decisionLevelToAssignmentStack.resize(noVariables + 1, 1);      
      
      // Update "m_solverData.m_dynamicForcingClauses".
      m_solverData.m_dynamicForcingClauses.clear();
      m_solverData.m_dynamicForcingClauses.resize((noVariables + 1) << 2, 0); 
      
      // Update "m_solverData.m_binaries".
      m_solverData.m_binaries.clear();
      m_solverData.m_binaries.resize(max, std::vector<uint_fast32_t>()); 
      
      // Update "m_solverData.m_watches".
      m_solverData.m_watches.clear();
      m_solverData.m_watches.resize(max);      
      
      // Update "m_variables".
      m_variables = noVariables; 
    }
    
    
    // decrase activities of all variables    
    void decreaseActivities() {
      
      //std::cout << std::endl << "c DECREASE ACTIVITIES! ##########################################################################################" << std::endl << std::endl;
      
      for (uint_fast32_t v = 1; v <= m_variables; ++v)
      { 
        m_activity[v] = 0.5 * m_activity[v]; 
      }
      
      m_maxActivity = 0.5 * m_maxActivity;
    }
    
    
    // Increases the activity of variable "var".
    void increaseActivity (uint_fast32_t var)
    {      
      
      // "var" has to be less or equal "m_variables".
      assert(var <= m_variables); 
      
      // t1
      //m_incVarActivity = 1;
      
      // Increase "var's" activity.
      m_activity[var]++; 
      
      // update the maximum activity
      if (m_activity[var] > m_maxActivity) {
        m_maxActivity = (uint_fast64_t)m_activity[var];
      }
      
      /*
       *      // Do we have to "normalize" the variables' activities?
       *      if (m_activity[var] > 1e100)
       *      {
       *        // Divide all activities by 1e100.
       *        for (uint_fast32_t v = 1; v <= m_variables; ++v)
       *        { m_activity[v] *= 1e-100; }
       *        
       *        // update the maximum activity
       *        m_maxActivity *= 1e-100;
       *        
       *        // Update "m_incVarActivity" and ensure it has a valid minimum value
       *        m_incVarActivity *= 1e-100;
       *        if (m_incVarActivity < 1) {
       *          m_incVarActivity = 1;
    }
    }
    */
    }
    
    // adds a decision to the assignment stack
    // updates the decion level
    void addDecision (uint_fast32_t lit, SolverData* solverData) {
      
      // update the decion level
      solverData -> m_decisionLevel++;   
      
      #ifdef DEBUG_OUTPUT
      {boost::mutex::scoped_lock l(m_coutMutex);
        std::cout << "c " << boost::this_thread::get_id() << ": add decision " << lit << " @ " << solverData -> m_decisionLevel << " (pos = " << solverData -> m_assignmentStackEndPosition << ")" << std::endl;
      }
      #endif
      
      // since this is a decision, also store the position of the assignment in the assignment stack
      solverData -> m_decisionLevelToAssignmentStack[solverData -> m_decisionLevel] = solverData -> m_assignmentStackEndPosition;
      
      // assign the value (if it is not already assigned)
      if (!solverData -> m_assignment[lit]) {
        assignValue(lit, solverData);      
        
        // add the assignment to the assignment stack
        solverData -> m_assignmentStack[solverData -> m_assignmentStackEndPosition] = lit;
        
        // update the assignment stack end position
        solverData -> m_assignmentStackEndPosition++;
        
        // Update "solverData -> m_level".
        solverData -> m_level[lit >> 1] = solverData -> m_decisionLevel;
        
        // Update "solverData -> m_forcing".
        solverData -> m_forcing[lit >> 1] = nullptr;    
      }
      else {
        std::cout << "c " << boost::this_thread::get_id() << ": ERROR: lit "<< lit<<" is already assigned!" << std::endl;
        assert(false);
      }
    }
    
    
    // adds an implication to the assignment stack
    // does not update the decion level
    void addImplication (uint_fast32_t lit, uint_fast32_t* reason, SolverData* solverData) {
      #ifdef DEBUG_OUTPUT
      {boost::mutex::scoped_lock l(m_coutMutex);
        std::cout << "c " << boost::this_thread::get_id() << ": add implication " << lit << " @ " << solverData -> m_decisionLevel << " (pos = " << solverData -> m_assignmentStackEndPosition << ")" << std::endl;
      }
      #endif
      
      // assign the value
      assignValue(lit, solverData);
      
      // Update "solverData -> m_level".
      solverData -> m_level[lit >> 1] = solverData -> m_decisionLevel;
      
      // store the reason for the implication in solverData -> m_forcing
      solverData -> m_forcing[lit >> 1] = reason;
      
      // add the assignment to the assignment stack
      solverData -> m_assignmentStack[solverData -> m_assignmentStackEndPosition] = lit;
      
      // update the assignment stack end position
      solverData -> m_assignmentStackEndPosition++;
    }
    
    
    // assigns the given literal
    void assignValue (uint_fast32_t lit, SolverData* solverData) {
      
      
      #ifdef DEBUG_OUTPUT
      if (solverData -> m_assignment[lit]) {
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << " ERROR, lit " << lit << "is positivly assigned" << std::endl;
        }
      }
      if (solverData -> m_assignment[lit^1]) {
        {boost::mutex::scoped_lock l(m_coutMutex);
          std::cout << "c " << boost::this_thread::get_id() << " ERROR, lit " << lit << "is negativly assigned" << std::endl;
        }
      }
      #endif
      
      // the variable should not be assigned yet
      assert(! solverData -> m_assignment[lit]); 
      assert(! solverData -> m_assignment[lit^1]);
      
      // assign the variable
      solverData -> m_assignment[lit] = 1;
    }
    
    
    
    // performs implicit BCP by assigning up to k variables (chosen by the decision heuristic) to 
    // both polarities to see if one leads to a conflict
    void doIBCP (ComponentTreeNode* node, SolverData* solverData) {
      
      std::vector<uint_fast32_t> candidates;
      
      // candidate selection
      for (auto c: *(node -> clauses())) {
        
        uint_fast32_t* lits = solverData -> m_database[c] -> lits();
        
        uint_fast32_t unassignedLits(0);
        
        uint_fast32_t lastLevel(0);
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          
          // sat
          if (solverData -> m_assignment[lits[l]]) {
            unassignedLits = 0;
            break;
          }
          else if (!solverData -> m_assignment[lits[l]^1]) {
            unassignedLits++;
          }     
          else {
            // negatively assigned, get last level
            if (solverData -> m_level[lits[l]^1] > lastLevel) {
              lastLevel = solverData -> m_level[lits[l]^1];
            }
          }
        }
        
        if (unassignedLits == 2 && lastLevel > solverData -> m_decisionLevel - 2) {
          // add as cadidates
          for (uint_fast32_t l = 0; lits[l] != 0; l++) {
            if (!solverData -> m_assignment[lits[l]^1]) {
              candidates.push_back(lits[l] >> 1);
            }
          }
        }       
      }
      
      
      //std::cout << "c IBCP found " << candidates.size() << " candidates" << std::endl;
      
      
      // temporary data structure to hold any occuring conflict clauses
      uint_fast32_t* conflictingClause(nullptr); 
      
      // assign each candidate to true and false and check for a conflict
      for (auto candidate: candidates) {
        
        uint_fast32_t lit = candidate << 1;
        
        if (solverData -> m_assignment[lit] || solverData -> m_assignment[lit^1]) {
          continue;
        }
        
        addDecision(lit, solverData);
        
        // check for a conflict
        if ((conflictingClause = deduce(solverData)) != nullptr) {
          
          analyze(conflictingClause, false, solverData);
          
          // backtrack one level to remove the incorrect assignment again
          backtrackOneLevel(solverData);
          
          // add an implication for the negated lit
          addImplication(lit^1, nullptr, solverData);
          
          // store the implication in the node
          node -> addExtraImplication(lit^1);
          
          // conflict -> done with this candidate
          break;
        }
        
        // backtrack one level to remove the previous assignment
        backtrackOneLevel(solverData);
        
        addDecision(lit^1, solverData);
        
        // check for a conflict
        if ((conflictingClause = deduce(solverData)) != nullptr) {
          
          analyze(conflictingClause, false, solverData);
          
          // backtrack one level to remove the incorrect assignment again
          backtrackOneLevel(solverData);
          
          // add an implication for the negated lit
          addImplication(lit, nullptr, solverData);
          
          // store the implication in the node
          node -> addExtraImplication(lit);
          
          // conflict -> done with this candidate
          break;
        }
        
        // backtrack one level to remove the previous assignment
        backtrackOneLevel(solverData);  
      }
    }
    
    
    // perform detection of Tseitin gates
    void TseitinDetection(std::vector<uint_fast32_t>* replacedVar) {
      
      // settings to decide which gates to remove
      // BLACK MAGIC NUMBERS
      bool doAnd (true);
      bool doOr  (true);
      bool doXor (true);
      bool doEquiv(true);
      double ratioThreshold (0.95);
      
      // store which clauses have already been processed
      std::vector<uint_fast32_t> alreadyProcessed(m_clauses, 0);
      
      // counters for the number of found gates
      uint_fast32_t foundAndGates(0);
      uint_fast32_t foundOrGates(0);
      uint_fast32_t foundXorGates(0);
      uint_fast32_t foundEquivGates(0);
      
      // the original number of clauses prior to any changes
      uint_fast32_t originalClauses(m_clauses);
      
      // look at every clause
      for (uint_fast32_t c = 0; c < originalClauses; c++) {
        
        // only check every clause once
        if (alreadyProcessed[c]) {
          continue;
        }
        alreadyProcessed[c] = true;
        
        // check if the clause belongs to a gate
        
        // vector to store the other clauses belonging to the gate
        std::vector<uint_fast32_t>* gate  = nullptr;
        
        if (doOr && (gate = extractOrGate(c)) != nullptr) {
          
          // the result should contain exactly 3 literals
          assert(gate -> size() == 3);
          
          double ratioScore = replaceAndGateRatio(gate -> at(0)^1, gate -> at(1)^1, gate -> at(2)^1);
          
          if (ratioScore <= ratioThreshold) {
            
            // count the gate
            foundOrGates++;
            // gate = a, b, x
            // -> replace all occurences of x by (a or b)
            replaceOrGate(gate -> at(0), gate -> at(1), gate -> at(2), false);
            
            replaceAndGate(gate -> at(0)^1, gate -> at(1)^1, gate -> at(2)^1, false);
            
            // store that x is not a free variable
            replacedVar -> at(gate -> at(2) >> 1) = 1;
          }
          
          // clean up
          delete gate;
        }   
        else if (doAnd && (gate = extractAndGate(c)) != nullptr) {
          
          assert(gate -> size() == 3);
          
          double ratioScore = replaceAndGateRatio(gate -> at(0), gate -> at(1), gate -> at(2));
          
          if (ratioScore <= ratioThreshold) {
            
            // count the gate
            foundAndGates++;
            
            // -> replace all occurences of x by (a and b) [this results in 2 clauses]
            replaceAndGate(gate -> at(0), gate -> at(1), gate -> at(2), false);
            
            // replace negation (this is an or gate again, this time, the defining clauses can be deleted
            replaceOrGate (gate -> at(0)^1, gate -> at(1)^1, gate -> at(2)^1, false);
            
            // store that x is not a free variable
            replacedVar -> at(gate -> at(2) >> 1) = 1;    
          }
          
          // clean up
          delete gate;    
        }
        
        
        else if (doXor && (gate = extractXorGate(c)) != nullptr) {
          
          
          // the result should contain exactly 3 literals
          assert(gate -> size() == 3);
          
          double ratioScore = replaceXorGateRatio(gate -> at(0), gate -> at(1), gate -> at(2)) * replaceEquivGateRatio(gate -> at(0)^1, gate -> at(1)^1, gate -> at(2)^1);
          
          if (ratioScore <= ratioThreshold) {
            
            // count the gate
            foundXorGates++;
            
            replaceXorGate(gate -> at(0), gate -> at(1), gate -> at(2), false);
            replaceEquivGate(gate -> at(0)^1, gate -> at(1)^1, gate -> at(2)^1, false);
            
            // store that x is not a free variable
            replacedVar -> at(gate -> at(2) >> 1) = 1;
          }
          // clean up
          delete gate;
        }
        else if (doEquiv && (gate = extractEquivGate(c)) != nullptr) {
          
          
          // the result should contain exactly 3 literals
          assert(gate -> size() == 3);
          
          double ratioScore = replaceEquivGateRatio(gate -> at(0), gate -> at(1), gate -> at(2)) * replaceXorGateRatio(gate -> at(0)^1, gate -> at(1)^1, gate -> at(2)^1);
          
          if (ratioScore <= ratioThreshold) {
            
            // count the gate
            foundEquivGates++;
            
            replaceEquivGate(gate -> at(0), gate -> at(1), gate -> at(2), false); 
            replaceXorGate(gate -> at(0)^1, gate -> at(1)^1, gate -> at(2)^1, false); 
            
            // store that x is not a free variable
            replacedVar -> at(gate -> at(2) >> 1) = 1;
          }
          
          // clean up
          delete gate;
        }       
      }
      
      std::cout << "c Tseitin detection found " << foundAndGates << " AND gates, " << foundOrGates << " OR gates, " << foundXorGates << " XOR gates, " << foundEquivGates << " EQUIV gates, " << std::endl;
    }
    
    // check if the given clause belongs to a tseitin encoded AND gate
    // (a, -x), (b, -x), (-a, -b, x)
    // only detects gate for the last given clause
    std::vector<uint_fast32_t>* extractAndGate(uint_fast32_t clause) {
      
      // get the literals of the clause
      uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
      
      // the clause should have exactly 3 literals
      if (lits[0] == 0 || lits[1] == 0 || lits[2] == 0 || lits[3] != 0) {
        return nullptr;
      }      
      
      // temp variables for in & outputs
      uint_fast32_t a, b, x;
      
      int clause1 = -1;
      int clause2 = -1;
      
      // step 1: determine which variable could be the gate in / output
      
      // (1): inputs: lits[0] and lits[1]
      if ((lits[0] & 0x01) != 0 && (lits[1] & 0x01) != 0 && (lits[2] & 0x01) == 0) {
        a = lits[0]^1;
        b = lits[1]^1;
        x = lits[2];
      }
      // (2): inputs: lits[0] and lits[2]
      else if ((lits[0] & 0x01) != 0 && (lits[2] & 0x01) != 0 && (lits[1] & 0x01) == 0) {
        a = lits[0]^1;
        b = lits[2]^1;
        x = lits[1];
      }
      // (3): inputs: lits[1] and lits[2]
      else if ((lits[1] & 0x01) != 0 && (lits[2] & 0x01) != 0 && (lits[01] & 0x01) == 0) {
        a = lits[1]^1;
        b = lits[2]^1;
        x = lits[0];
      }
      // sign of variables doesn't fit 
      else {
        return nullptr;
      }
      
      // step 2: find the other clauses of the gate definition
      clause1 = findClause(a, x^1);
      clause2 = findClause(b, x^1);
      
      if (clause1 != -1 && clause2 != -1) {
        std::vector<uint_fast32_t>* result = new std::vector<uint_fast32_t>();
        result -> push_back(a);
        result -> push_back(b);
        result -> push_back(x);
        
        return result;
      }      
      
      // gate definition not found
      return nullptr;
    }
    
    
    // check if the given clause belongs to a tseitin encoded OR gate
    // (-a, x), (-b, x), (a, b, -x)
    // detection is based on the clause a, b, -x
    // returns a vector containing a, b, x
    std::vector<uint_fast32_t>* extractOrGate(uint_fast32_t clause) {
      
      // get the literals of the clause
      uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
      
      // the clause should have exactly 3 literals
      if (lits[1] == 0 || lits[2] == 0 || lits[3] != 0) {
        return nullptr;
      }
      
      // now search for the following clauses:
      // 1 (lits[0]^1, lits[1]^1), (lits[0]^1, lits[2]^1) -> output is lits[0]^1
      // 2 (lits[1]^1, lits[0]^1), (lits[1]^1, lits[2]^1) -> output is lits[1]^1
      // 3 (lits[2]^1, lits[1]^1), (lits[2]^1, lits[0]^1) -> output is lits[2]^1
      
      int clause1 = -1;
      int clause2 = -1;
      uint_fast32_t a, b, x;
      
      // 1
      if ((lits[1] & 0x01) == 0 && (lits[2] & 0x01) == 0 && (lits[0] & 0x01) != 0) {
        clause1 = findClause( lits[0]^1, lits[1]^1);
        if (clause1 != -1) 
          clause2 = findClause( lits[0]^1, lits[2]^1);
      }
      
      if (clause1 != -1 && clause2 != -1) {
        a = lits[1];
        b = lits[2];
        x = lits[0]^1;
      }
      else {
        
        // 2
        if ((lits[0] & 0x01) == 0 && (lits[2] & 0x01) == 0 && (lits[1] & 0x01) != 0) {
          clause1 = findClause( lits[1]^1, lits[0]^1);
          if (clause1 != -1) 
            clause2 = findClause( lits[1]^1, lits[2]^1);
        }
        
        if (clause1 != -1 && clause2 != -1) {
          a = lits[0];
          b = lits[2];
          x = lits[1]^1;
        }
        else {
          
          // 3
          if ((lits[0] & 0x01) == 0 && (lits[1] & 0x01) == 0 && (lits[2] & 0x01) != 0) {
            clause1 = findClause( lits[2]^1, lits[0]^1);
            if (clause1 != -1) 
              clause2 = findClause( lits[2]^1, lits[1]^1);
          }
          
          if (clause1 != -1 && clause2 != -1) {
            a = lits[0];
            b = lits[1];
            x = lits[2]^1;
          }
          else {
            return nullptr;
          }
        }
      }
      
      std::vector<uint_fast32_t>* result = new std::vector<uint_fast32_t>();
      result -> push_back(a);
      result -> push_back(b);
      result -> push_back(x);
      
      return result;
    }
    
    
    // check if the given clause belongs to a tseitin encoded XOR gate
    // (-a, -b, -x), (a, b, -x), (a, -b, x), (-a, b, x)
    // only detects gate for the last given clause
    std::vector<uint_fast32_t>* extractXorGate(uint_fast32_t clause) {
      
      // get the literals of the clause
      uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
      
      // the clause should have exactly 3 literals
      if (lits[1] == 0 || lits[2] == 0 || lits[3] != 0) {
        return nullptr;
      }
      
      
      uint_fast32_t a, b, x;
      
      int clause1 = -1;
      int clause2 = -1;
      int clause3 = -1;
      
      // step 1: determine which variable could be the gate in / output
      // look for (a, b, -x)
      
      // (1): inputs: lits[0] and lits[1]
      if ((lits[0] & 0x01) == 0 && (lits[1] & 0x01) == 0 && (lits[2] & 0x01) != 0) {
        a = lits[0];
        b = lits[1];
        x = lits[2]^1;
      }
      // (2): inputs: lits[0] and lits[2]
      else if ((lits[0] & 0x01) == 0 && (lits[2] & 0x01) == 0 && (lits[1] & 0x01) != 0) {
        a = lits[0];
        b = lits[2];
        x = lits[1]^1;
      }
      // (3): inputs: lits[1] and lits[2]
      else if ((lits[1] & 0x01) == 0 && (lits[2] & 0x01) == 0 && (lits[0] & 0x01) != 0) {
        a = lits[1];
        b = lits[2];
        x = lits[0]^1;
      }
      // sign of variables doesn't fit 
      else {
        return nullptr;
      }
      
      // step 2: find the other clauses of the gate definition
      clause1 = findClause(a^1, b^1, x^1); // (-a, -b, -x)
      clause2 = findClause(a, b^1, x);     // (a,  -b, x )
      clause3 = findClause(a^1, b, x);     // (-a,  b, x )
      
      if (clause1 != -1 && clause2 != -1 && clause3 != -1) {
        std::vector<uint_fast32_t>* result = new std::vector<uint_fast32_t>();
        result -> push_back(a);
        result -> push_back(b);
        result -> push_back(x);
        
        return result;
      }      
      
      // gate definition not found
      return nullptr;
    }
    
    
    // check if the given clause belongs to a tseitin encoded EQUIV gate
    // (a, b, x), (-a, -b, x), (-a, b, -x), (a, -b, -x)
    // only detects gate for the last given clause
    std::vector<uint_fast32_t>* extractEquivGate(uint_fast32_t clause) {
      
      // get the literals of the clause
      uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
      
      // the clause should have exactly 3 literals
      if (lits[1] == 0 || lits[2] == 0 || lits[3] != 0) {
        return nullptr;
      }
      
      
      uint_fast32_t a, b, x;
      
      int clause1 = -1;
      int clause2 = -1;
      int clause3 = -1;
      
      // step 1: determine which variable could be the gate in / output
      // look for (-a, -b, x)
      
      // (1): inputs: lits[0] and lits[1]
      if ((lits[0] & 0x01) != 0 && (lits[1] & 0x01) != 0 && (lits[2] & 0x01) == 0) {
        a = lits[0]^1;
        b = lits[1]^1;
        x = lits[2];
      }
      // (2): inputs: lits[0] and lits[2]
      else if ((lits[0] & 0x01) != 0 && (lits[2] & 0x01) != 0 && (lits[1] & 0x01) == 0) {
        a = lits[0]^1;
        b = lits[2]^1;
        x = lits[1];
      }
      // (3): inputs: lits[1] and lits[2]
      else if ((lits[1] & 0x01) != 0 && (lits[2] & 0x01) != 0 && (lits[0] & 0x01) == 0) {
        a = lits[1]^1;
        b = lits[2]^1;
        x = lits[0];
      }
      // sign of variables doesn't fit 
      else {
        return nullptr;
      }
      
      // step 2: find the other clauses of the gate definition
      clause1 = findClause(a, b, x);    // (a, b, x)
      clause2 = findClause(a^1, b, x^1);// (-a, b, -x)   
      clause3 = findClause(a, b^1, x^1);// (a, -b, -x)     
      
      if (clause1 != -1 && clause2 != -1 && clause3 != -1) {
        std::vector<uint_fast32_t>* result = new std::vector<uint_fast32_t>();
        result -> push_back(a);
        result -> push_back(b);
        result -> push_back(x);
        
        return result;
      }      
      
      // gate definition not found
      return nullptr;
    }
    
    
    // replaces all occurences of x by (a, b) [but keeps the defining clauses of the or gate when keepDefiningClause is set]
    void replaceOrGate(uint_fast32_t a, uint_fast32_t b, uint_fast32_t x, bool keepDefiningClause = true) {
      
      std::vector<uint_fast32_t> xClauses;
      
      // replace all occurences of x by (a or b)
      for (uint_fast32_t c : m_litToClause[x]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        // get the literals of the clause
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        
        // now check if the clause is one of the defining clauses (-a, x) or (-b, x) [(a, b, -x) will be kept anyway]
        if (keepDefiningClause) {
          if (lits[0] != 0 && lits[1] != 0 && lits[2] == 0) {
            if(lits[0] == (a^1) || lits[1] == (a^1) || lits[0] == (b^1) || lits[1] == (b^1)) {
              xClauses.push_back(c);
              continue;
            }
          }
        }
        
        // store the original literals apart from a, b, x
        std::vector<uint_fast32_t> literals;
        
        // store if a tautology was found
        bool tautology(false);
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (lits[l] != x && lits[l] != a && lits[l] != b) {
            literals.push_back(lits[l]);
          }
          
          // the clause will become a tautology after adding (a, b)
          if (lits[l] == (a^1) || lits[l] == (b^1)) {
            m_solverData.m_database[c] -> deactivate();
            tautology = true;       
          }
        }
        
        // if the clause would become a tautology: ignore it
        if (tautology) {
          continue;
        }
        
        //std::cout << "c " << boost::this_thread::get_id() << ": adding " << a << ", " << b << " to clause " << c;
        
        // add a, b
        literals.push_back(a);
        literals.push_back(b);
        
        // get the lbd of the clause
        uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
        
        // delete the clause
        delete m_solverData.m_database[c];
        
        // create a new clause
        m_solverData.m_database[c] = new Clause(literals, lbd);
        
        // add the clause to litToClause of a and b
        m_litToClause[a].push_back(c);
        m_litToClause[b].push_back(c);
      }      
      
      // since x was replaced, remove it from litToClause
      m_litToClause[x].clear();
      
      if (keepDefiningClause) {
        if (xClauses.size() < 2) {
          std::cout << "c " << boost::this_thread::get_id() << ": xClauses size: " << xClauses.size() << std::endl;
        }
        
        assert(xClauses.size() >= 2);
        for (uint_fast32_t c : xClauses) {
          m_litToClause[x].push_back(c);
        }  
      }
    }
    
    // replaces all occurences of x by (a and b) [but keeps the defining clauses of the and gate when keepDefiningClause is set]
    void replaceAndGate(uint_fast32_t a, uint_fast32_t b, uint_fast32_t x, bool keepDefiningClause = true) {
      
      std::vector<uint_fast32_t> xClauses;
      
      // replace all occurences of x by (a and b)
      for (uint_fast32_t c : m_litToClause[x]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // get the literals of the clause
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        
        // now check if the clause is one of the defining clauses (-a, x) or (-b, x) [(a, b, -x) will be kept anyway]
        if (keepDefiningClause) {
          if (lits[0] != 0 && lits[1] != 0 && lits[2] != 0 && lits[3] == 0) {
            bool aFound(false);
            bool bFound(false);
            
            for (uint_fast32_t l = 0; l < 3; l++) {
              if (lits[l] == (a^1)) {
                aFound = true;
              }
              if (lits[l] == (b^1)) {
                bFound = true;
              }
            }
            
            if(aFound && bFound) {
              xClauses.push_back(c);
              continue;
            }
          }
        }
        
        
        // store the original literals apart x
        // also store whether the clause already contains  -a, -b
        bool apFound(false);
        bool bpFound(false);
        std::vector<uint_fast32_t> literals;
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (lits[l] != x) {
            literals.push_back(lits[l]);
          }
          if (lits[l] == (a^1)) {
            apFound = true;
          }
          if (lits[l] == (b^1)) {
            bpFound = true;
          }
          
          if (m_solverData.m_assignment[lits[l]]) {
            std::cout << "c " << boost::this_thread::get_id() << ": clause SAT!" << std::endl;
          }
        }
        
        
        // create 2 new clauses: (literals, a) and (literals, b)
        
        
        // if literals contains both -a and -b, the resulting clauses will both be tautologies
        if (apFound && bpFound) {
          m_solverData.m_database[c] -> deactivate();
          continue;
        }
        
        // if the clause contains -a, only add (literals, b)
        if (apFound) {
          literals.push_back(b);
          
          // get the lbd of the clause
          uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
          
          // delete the clause
          delete m_solverData.m_database[c];
          
          // create a new clause
          m_solverData.m_database[c] = new Clause(literals, lbd);
          
          // add the clause to litToClause of b
          m_litToClause[b].push_back(c);
          
          // done with this clause
          continue;       
        }
        
        // if the clause contains -b, only add (literals, a)
        if (bpFound) {
          literals.push_back(a);
          
          // get the lbd of the clause
          uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
          
          // delete the clause
          delete m_solverData.m_database[c];
          
          // create a new clause
          m_solverData.m_database[c] = new Clause(literals, lbd);
          
          // add the clause to litToClause of b
          m_litToClause[a].push_back(c);
          
          // done with this clause
          continue;       
        }
        
        
        // copy literals
        std::vector<uint_fast32_t> literals2(literals);
        assert(literals2.size() == literals.size());
        
        // add a to literals
        literals.push_back(a);
        // and b to literals 2
        literals2.push_back(b);
        
        // get the lbd of the clause
        uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
        
        // delete the clause
        delete m_solverData.m_database[c];
        
        // create a new clause
        m_solverData.m_database[c] = new Clause(literals, lbd);
        
        // add a new clause for literals 2
        addClause(literals2);
        
        // if literals2 is actually a new clause, add it to litToClause
        if (m_solverData.m_database.size() - m_clauses == 1) {
          uint_fast32_t* lits = m_solverData.m_database[m_clauses] -> lits();
          for (uint_fast32_t l = 0; lits[l] != 0; l++) {
            m_litToClause[lits[l]].push_back(m_clauses);
          }
        }
        assert(m_solverData.m_database.size() - m_clauses <= 1);
        m_clauses = m_solverData.m_database.size();
        
        // add the clauses to litToClause of a and b
        m_litToClause[a].push_back(c);
      }
      
      // since x was replaced, remove it from litToClause
      m_litToClause[x].clear();
      
      if (keepDefiningClause) {
        if (xClauses.size() < 1) {
          std::cout << "c " << boost::this_thread::get_id() << ": xClauses size: " << xClauses.size() << std::endl;
        }
        
        assert(xClauses.size() >= 1);
        for (uint_fast32_t c : xClauses) {
          m_litToClause[x].push_back(c);
        }
      }
    }
    
    // replaces all occurences of x by (a xor b) [but keeps the defining clauses of the and gate when keepDefiningClause is set]
    void replaceXorGate(uint_fast32_t a, uint_fast32_t b, uint_fast32_t x, bool keepDefiningClause = true) {
      
      std::vector<uint_fast32_t> xClauses;
      
      // replace all occurences of x by (a and b)
      for (uint_fast32_t c : m_litToClause[x]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // get the literals of the clause
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        
        // now check if the clause is one of the defining clauses (-a, b, x) or (a, -b, x) [(a, b, -x) and (-a, -b, -x) will be kept anyway]
        if (keepDefiningClause) {
          
          if (lits[0] != 0 && lits[1] != 0 && lits[2] != 0 && lits[3] == 0) {
            bool aFound(false);
            bool bFound(false);
            bool apFound(false);
            bool bpFound(false);
            
            for (uint_fast32_t l = 0; l < 3; l++) {
              if (lits[l] == (a)) {
                aFound = true;
              }
              if (lits[l] == (a^1)) {
                apFound = true;
              }
              if (lits[l] == (b)) {
                bFound = true;
              }
              if (lits[l] == (b^1)) {
                bpFound = true;
              }
            }
            
            if((aFound && bpFound ) || (apFound && bFound)) {
              xClauses.push_back(c);
              continue;
            }
          }
        }
        
        
        // store the original literals apart x
        // also store whether the clause already contains a, b, -a, -b
        bool aFound(false);
        bool bFound(false);
        bool apFound(false);
        bool bpFound(false);
        std::vector<uint_fast32_t> literals;
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (lits[l] != x &&lits[l] != a && lits[l] != b &&lits[l] != (a^1) && lits[l] != (b^1)) {
            literals.push_back(lits[l]);
          }
          if (lits[l] == a) {
            aFound = true;
          }
          if (lits[l] == (a^1)) {
            apFound = true;
          }
          if (lits[l] == b) {
            bFound = true;
          }
          if (lits[l] == (b^1)) {
            bpFound = true;
          }
          
          if (m_solverData.m_assignment[lits[l]]) {
            std::cout << "c " << boost::this_thread::get_id() << ": clause SAT!" << std::endl;
          }
        }
        
        
        // create 2 new clauses: (literals, -a, -b) and (literals, a, b)
        
        
        // if literals contains (-a, b) or (a, -b) the resulting clauses will both be tautologies
        if ((apFound && bFound) || (aFound && bpFound)) {
          m_solverData.m_database[c] -> deactivate();
          continue;
        }
        
        
        // if the clause contains -a or -b only add (literals, -a, -b)
        if (apFound || bpFound) {
          literals.push_back(a^1);
          literals.push_back(b^1);
          
          // get the lbd of the clause
          uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
          
          // delete the clause
          delete m_solverData.m_database[c];
          
          // create a new clause
          m_solverData.m_database[c] = new Clause(literals, lbd);
          
          // add the clause to litToClause of -a and -b
          m_litToClause[a^1].push_back(c);
          m_litToClause[b^1].push_back(c);
          
          // done with this clause
          continue;       
        }
        
        // if the clause contains a or b, only add (literals, a, b)
        if (aFound || bFound) {
          literals.push_back(a);
          literals.push_back(b);
          
          // get the lbd of the clause
          uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
          
          // delete the clause
          delete m_solverData.m_database[c];
          
          // create a new clause
          m_solverData.m_database[c] = new Clause(literals, lbd);
          
          // add the clause to litToClause of a and b
          m_litToClause[a].push_back(c);
          m_litToClause[b].push_back(c);
          
          // done with this clause
          continue;       
        }
        
        
        // copy literals
        std::vector<uint_fast32_t> literals2(literals);
        assert(literals2.size() == literals.size());
        
        // add a, b to literals
        literals.push_back(a);
        literals.push_back(b);
        // and -a, -b to literals 2
        literals2.push_back(a^1);
        literals2.push_back(b^1);
        
        // get the lbd of the clause
        uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
        
        // delete the clause
        delete m_solverData.m_database[c];
        
        // create a new clause
        m_solverData.m_database[c] = new Clause(literals, lbd);
        
        // add a new clause for literals 2
        addClause(literals2);
        
        // if literals2 is actually a new clause, add it to litToClause
        if (m_solverData.m_database.size() - m_clauses == 1) {
          uint_fast32_t* lits = m_solverData.m_database[m_clauses] -> lits();
          for (uint_fast32_t l = 0; lits[l] != 0; l++) {
            m_litToClause[lits[l]].push_back(m_clauses);
          }
        }
        assert(m_solverData.m_database.size() - m_clauses <= 1);
        
        m_clauses = m_solverData.m_database.size();
        
        // add the clauses to litToClause of a and b
        m_litToClause[a].push_back(c);
        m_litToClause[b].push_back(c);
      }
      
      // since x was replaced, remove it from litToClause
      m_litToClause[x].clear();
      
      if (keepDefiningClause) {
        if (xClauses.size() < 2) {
          std::cout << "c " << boost::this_thread::get_id() << ": xClauses size: " << xClauses.size() << std::endl;
        }
        
        assert(xClauses.size() >= 2);
        for (uint_fast32_t c : xClauses) {
          m_litToClause[x].push_back(c);
        }
      }
    }
    
    
    // replaces all occurences of x by (a = b) [but keeps the defining clauses of the and gate when keepDefiningClause is set]
    void replaceEquivGate(uint_fast32_t a, uint_fast32_t b, uint_fast32_t x, bool keepDefiningClause = true) {
      
      std::vector<uint_fast32_t> xClauses;
      
      // replace all occurences of x by (a and b)
      for (uint_fast32_t c : m_litToClause[x]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // get the literals of the clause
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        
        // now check if the clause is one of the defining clauses (a, b, x) or (-a, -b, x) [(-a, b, -x) and (a, -b, -x) will be kept anyway]
        if (keepDefiningClause) {
          
          if (lits[0] != 0 && lits[1] != 0 && lits[2] != 0 && lits[3] == 0) {
            bool aFound(false);
            bool bFound(false);
            bool apFound(false);
            bool bpFound(false);
            
            for (uint_fast32_t l = 0; l < 3; l++) {
              if (lits[l] == (a)) {
                aFound = true;
              }
              if (lits[l] == (a^1)) {
                apFound = true;
              }
              if (lits[l] == (b)) {
                bFound = true;
              }
              if (lits[l] == (b^1)) {
                bpFound = true;
              }
            }
            
            if((aFound && bFound ) || (apFound && bpFound)) {
              xClauses.push_back(c);
              continue;
            }
          }
        }        
        
        // store the original literals apart x
        // also store whether the clause already contains a, b, -a, -b
        bool aFound(false);
        bool bFound(false);
        bool apFound(false);
        bool bpFound(false);
        std::vector<uint_fast32_t> literals;
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (lits[l] != x && lits[l] != a && lits[l] != b && lits[l] != (a^1) && lits[l] != (b^1)) {
            literals.push_back(lits[l]);
          }
          if (lits[l] == a) {
            aFound = true;
          }
          if (lits[l] == (a^1)) {
            apFound = true;
          }
          if (lits[l] == b) {
            bFound = true;
          }
          if (lits[l] == (b^1)) {
            bpFound = true;
          }
          
          if (m_solverData.m_assignment[lits[l]]) {
            std::cout << "c " << boost::this_thread::get_id() << ": clause SAT!" << std::endl;
          }
        }        
        
        // create 2 new clauses: (literals, -a, b) and (literals, a, -b)
        
        // if literals contains (a, b) or (-a, -b) the resulting clauses will both be tautologies
        if ((aFound && bFound) || (apFound && bpFound)) {
          m_solverData.m_database[c] -> deactivate();
          continue;
        }
        
        
        // if the clause contains a or -b only add (literals, a, -b)
        if (aFound || bpFound) {
          literals.push_back(a);
          literals.push_back(b^1);
          
          // get the lbd of the clause
          uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
          
          // delete the clause
          delete m_solverData.m_database[c];
          
          // create a new clause
          m_solverData.m_database[c] = new Clause(literals, lbd);
          
          // add the clause to litToClause of -a and -b
          m_litToClause[a].push_back(c);
          m_litToClause[b^1].push_back(c);
          
          // done with this clause
          continue;       
        }
        
        // if the clause contains -a or b, only add (literals, -a, b)
        if (apFound || bFound) {
          literals.push_back(a^1);
          literals.push_back(b);
          
          // get the lbd of the clause
          uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
          
          // delete the clause
          delete m_solverData.m_database[c];
          
          // create a new clause
          m_solverData.m_database[c] = new Clause(literals, lbd);
          
          // add the clause to litToClause of a and b
          m_litToClause[a^1].push_back(c);
          m_litToClause[b].push_back(c);
          
          // done with this clause
          continue;       
        }
        
        
        // copy literals
        std::vector<uint_fast32_t> literals2(literals);
        assert(literals2.size() == literals.size());
        
        // add -a, b to literals
        literals.push_back(a^1);
        literals.push_back(b);
        // and a, -b to literals 2
        literals2.push_back(a);
        literals2.push_back(b^1);
        
        // get the lbd of the clause
        uint_fast32_t lbd = m_solverData.m_database[c] -> lbd();
        
        // delete the clause
        delete m_solverData.m_database[c];
        
        // create a new clause
        m_solverData.m_database[c] = new Clause(literals, lbd);
        
        // add a new clause for literals 2
        addClause(literals2);
        
        // if literals2 is actually a new clause, add it to litToClause
        if (m_solverData.m_database.size() - m_clauses == 1) {
          uint_fast32_t* lits = m_solverData.m_database[m_clauses] -> lits();
          for (uint_fast32_t l = 0; lits[l] != 0; l++) {
            m_litToClause[lits[l]].push_back(m_clauses);
          }
        }
        
        assert(m_solverData.m_database.size() - m_clauses <= 1);
        m_clauses = m_solverData.m_database.size();
        
        // add the clauses to litToClause of -a and b
        m_litToClause[a^1].push_back(c);
        m_litToClause[b].push_back(c);
      }
      
      // since x was replaced, remove it from litToClause
      m_litToClause[x].clear();
      
      if (keepDefiningClause) {
        if (xClauses.size() < 2) {
          std::cout << "c " << boost::this_thread::get_id() << ": xClauses size: " << xClauses.size() << std::endl;
        }
        
        assert(xClauses.size() >= 2);
        for (uint_fast32_t c : xClauses) {
          m_litToClause[x].push_back(c);
        }
      }
    }
    
    
    // computes the ratio of newClauses / oldClauses when replacing the given xor gate
    double replaceAndGateRatio(uint_fast32_t a, uint_fast32_t b, uint_fast32_t x) {
      
      uint_fast32_t oldClauses (0);
      uint_fast32_t newClauses (0);
      
      for (uint_fast32_t c : m_litToClause[x]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // count the x clause
        oldClauses++;
        
        // get the literals of the clause
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        // store whether the clause already contains a, b, -a, -b
        bool apFound(false);
        bool bpFound(false);
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (lits[l] == (a^1)) {
            apFound = true;
          }
          if (lits[l] == (b^1)) {
            bpFound = true;
          }
        }
        
        
        if (apFound && bpFound) {
          // tautology -> no new clauses
        }
        else if (apFound) {
          newClauses++;
        }
        else if (bpFound) {
          newClauses++;
        }
        else {
          newClauses += 2;
        }       
      }
      
      return ((double)newClauses / (double)oldClauses);      
    }
    
    
    // computes the ratio of newClauses / oldClauses when replacing the given equivalence gate
    double replaceEquivGateRatio(uint_fast32_t a, uint_fast32_t b, uint_fast32_t x) {
      
      uint_fast32_t oldClauses (0);
      uint_fast32_t newClauses (0);
      
      for (uint_fast32_t c : m_litToClause[x]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // count the x clause
        oldClauses++;
        
        // get the literals of the clause
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        // store whether the clause already contains a, b, -a, -b
        bool aFound(false);
        bool bFound(false);
        bool apFound(false);
        bool bpFound(false);
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (lits[l] == a) {
            aFound = true;
          }
          if (lits[l] == (a^1)) {
            apFound = true;
          }
          if (lits[l] == b) {
            bFound = true;
          }
          if (lits[l] == (b^1)) {
            bpFound = true;
          }
        }
        
        
        if ((aFound && bFound) || (apFound && bpFound)) {
          // tautology -> no new clauses
        }
        else if (aFound || bpFound) {
          newClauses++;
        }
        else if (apFound || bFound) {
          newClauses++;
        }
        else {
          newClauses += 2;
        }       
      }
      
      return ((double)newClauses / (double)oldClauses);      
    }
    
    
    // computes the ratio of newClauses / oldClauses when replacing the given xor gate
    double replaceXorGateRatio(uint_fast32_t a, uint_fast32_t b, uint_fast32_t x) {
      
      uint_fast32_t oldClauses (0);
      uint_fast32_t newClauses (0);
      
      for (uint_fast32_t c : m_litToClause[x]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // count the x clause
        oldClauses++;
        
        // get the literals of the clause
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        // store whether the clause already contains a, b, -a, -b
        bool aFound(false);
        bool bFound(false);
        bool apFound(false);
        bool bpFound(false);
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (lits[l] == a) {
            aFound = true;
          }
          if (lits[l] == (a^1)) {
            apFound = true;
          }
          if (lits[l] == b) {
            bFound = true;
          }
          if (lits[l] == (b^1)) {
            bpFound = true;
          }
        }
        
        
        if ((apFound && bFound) || (aFound && bpFound)) {
          // tautology -> no new clauses
        }
        else if (apFound || bpFound) {
          newClauses++;
        }
        else if (aFound || bFound) {
          newClauses++;
        }
        else {
          newClauses += 2;
        }       
      }
      
      return ((double)newClauses / (double)oldClauses);      
    }
    
    
    // searches for the clause consisting of the two given literals
    // returns -1 if the clause doesn't exist
    int findClause(uint_fast32_t lit1, uint_fast32_t lit2) {
      
      // look at all clauses in which lit1 occurs
      
      for (uint_fast32_t c : m_litToClause[lit1]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // get the literals
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        
        // check if the clause also contains lit2 and if it only contains two literals
        if (lits[0] == lit2 && lits[2] == 0) {
          return c;
        }
        if (lits[1] == lit2 && lits[2] == 0) {
          return c;
        }       
      }
      
      // no fitting clause found
      return -1;
    }
    
    // searches for the clause consisting of the three given literals
    // returns -1 if the clause doesn't exist
    int findClause(uint_fast32_t lit1, uint_fast32_t lit2, uint_fast32_t lit3) {
      
      // look at all clauses in which lit1 occurs
      
      for (uint_fast32_t c : m_litToClause[lit1]) {
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        // get the literals
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        
        // only look at clauses with 3 literals
        if (lits[0] != 0 && lits[1] != 0 && lits[2] != 0 && lits[3] == 0) {
          
          bool found2(false);
          bool found3(false);
          for (uint_fast32_t l = 0; l < 3; l ++) {
            if (lits[l] == lit2) {
              found2 = true;
            }
            if (lits[l] == lit3) {
              found3 = true;
            }
          }
          
          if (found2 && found3) {
            return c;
          }
        }       
      }
      
      // no fitting clause found
      return -1;
    }
    
    
    
    // returns true if clause1 subsumes clause2 (clause1 is smaller and consists of only variables of clause2)
    // negatedLit is a literal in clause2 that has been negated
    bool subsumes(uint_fast32_t clause1, uint_fast32_t clause2, uint_fast32_t negatedLit) {
      
      // look at the literals of clause1: they have to be all part of clause2,
      
      // get the literals
      uint_fast32_t* litsCl1 = m_solverData.m_database[clause1] -> lits();
      uint_fast32_t* litsCl2 = m_solverData.m_database[clause2] -> lits();
      
      
      bool foundNegatedLit(false);
      
      // look at each literal of clause1
      for(uint_fast32_t l1 = 0; litsCl1[l1] != 0; l1++) {
        //for(uint_fast32_t lCl1 : clauses -> at(clause1)) {
        uint_fast32_t lCl1 = litsCl1[l1];
        
        // check if this literal is also part of clause2
        bool found(false);
        for(uint_fast32_t l2 = 0; litsCl2[l2] != 0; l2++) {
          //for(uint_fast32_t lCl2 : clauses -> at(clause2)) {
          uint_fast32_t lCl2 = litsCl2[l2];
          if (lCl1 == (negatedLit^1) && lCl2 == negatedLit) {
            foundNegatedLit = true;
            found = true;
            break;          
          }
          else if (lCl1 == lCl2) {
            found = true;
            break;
          } 
        }
        
        // if the literal does not exist in clause2, there is no subsumption
        if (!found) {
          return false;
        }
      }
      
      // found representative for each literal
      return (foundNegatedLit || negatedLit == 0);
    }
    
    // returns all clauses subsumed by clause
    // negatedLit is a literal in the clause that has been negated
    std::vector<uint_fast32_t> subsumedWith(uint_fast32_t clause, uint_fast32_t negatedLit) {  
      
      // the list of clauses that will be returned
      std::vector<uint_fast32_t> foundClauses;
      
      // determine the shortest litToClause list
      uint_fast32_t min = -1;
      uint_fast32_t minLit = 0;
      
      uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
      
      for(uint_fast32_t l = 0; lits[l] != 0; l++) {
        
        uint_fast32_t lit = lits[l];
        
        if (m_litToClause[lit].size() < min) {
          min = m_litToClause[lit].size();
          minLit = lit;
        }
      }
      
      // this should always find a minimum literal
      assert(minLit != 0);
      
      // look at all the possible clauses that could by subsumed by the current one
      for (uint_fast32_t possibleClause : m_litToClause[minLit]) {
        
        // a clause will always subsume itself -> ignore
        if (possibleClause == clause) {
          continue;
        }
        
        if (subsumes(clause, possibleClause, negatedLit)) {
          foundClauses.push_back(possibleClause);
        }
      }
      
      return foundClauses;
    }
    
    // compute self-subsumption (SATElite)
    void selfSubsumption() {      
      
      // counter for the number of removed literals
      uint_fast32_t removedLit(0);
      
      // look at each clause
      for (uint_fast32_t clause = 0; clause < m_clauses; clause++) {
        
        // ignore clauses that have already been removed
        if (m_solverData.m_database[clause] -> deactivated()) {
          continue;
        }
        
        uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
        
        // for each literal, compute the set of clauses subsumed by negating this literal
        for(uint_fast32_t l = 0; lits[l] != 0; l++) {
          uint_fast32_t lit = lits[l];
          
          std::vector<uint_fast32_t> subsumed = subsumedWith(clause, lit^1);
          
          // in each subsumed clause, remove lits[l]^1
          for (uint_fast32_t subsumedClause : subsumed) {
            
            // remove l^1 from the subsumed clause
            // if the literal is not assigned already
            if (!m_solverData.m_assignment[lit^1]) {
              removeLitFromClause(lit^1, subsumedClause);       
              removedLit++;
            }
          }
        }
      }
      
      #ifdef PREPROCESS_OUTPUT
      std::cout << "c " << "selfSubsumption removed \t\t" << removedLit << " literals" << std::endl;      
      #endif
    }
    
    // tries to subsume each clause with each other clause
    void subsumption() {
      
      // counter for the number of found subsumptions
      uint_fast32_t subsumedClauses(0);
      
      // look at each clause
      for (uint_fast32_t clause = 0; clause < m_clauses; clause++) {
        
        // ignore clauses that have already been removed
        if (m_solverData.m_database[clause] -> deactivated()) {
          continue;
        }
        
        // determine the shortest litToClause list
        uint_fast32_t min = -1;
        uint_fast32_t minLit = 0;
        
        uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
        for(uint_fast32_t l = 0; lits[l] != 0; l++) {
          
          uint_fast32_t lit = lits[l];
          
          if (m_litToClause[lit].size() < min) {
            min = m_litToClause[lit].size();
            minLit = lit;
          }
        }
        
        if (minLit == 0) {
          std::cout << "minLit = 0" << std::endl; 
        }       
        assert(minLit != 0);
        
        // look at each clause lit occurs in
        for (uint_fast32_t possibleClause : m_litToClause[minLit]) {
          
          // a clause will always subsume itself -> ignore
          if (possibleClause == clause) {
            continue;
          }
          
          if (subsumes(possibleClause, clause, 0)) {        
            
            subsumedClauses++;
            
            // update litToClause           
            for(uint_fast32_t l = 0; lits[l] != 0; l++) {
              
              uint_fast32_t lit = lits[l];
              m_litToClause[lit].erase(std::remove(m_litToClause[lit].begin(), m_litToClause[lit].end(), clause), m_litToClause[lit].end());
            }
            
            // delete the clause
            m_solverData.m_database[clause] -> deactivate();
            
            break;
          }
        }   
      }
      
      #ifdef PREPROCESS_OUTPUT
      std::cout << "c " << "subsumption removed \t\t\t" << subsumedClauses << " clauses " << std::endl;
      #endif
    }
    
    // attempt variable elimination
    void variableElimination() {
      
      // count how many variables were removed
      uint_fast32_t eliminatedVar(0);
      
      // look at each variable
      for (uint_fast32_t var = 1; var <= m_variables; var++) {
        
        // ignore variables which only occur pos or neg (they will be handled by the next bcp)
        if (m_litToClause[(var<<1)].size() == 0 || m_litToClause[(var<<1)^1].size() == 0) {
          continue;
        }
        
        //std::cout << "trying var " << var << "(pos: " << (m_litToClause[(var<<1)].size()) << ", neg: " << (m_litToClause[(var<<1)^1].size()) << ")" << std::endl << std::endl;
        
        // check the number of occurences (positive / negative)
        // if the number of occurences is big, do not attempt variable elimination (from SATelite)
        if (m_litToClause[(var<<1)].size() > 10 && m_litToClause[(var<<1)^1].size() > 10) {
          continue;
        }
        
        // store the number of clauses that would be removed from the formula if the variable was eliminated
        uint_fast32_t removedClauses = m_litToClause[(var<<1)].size() + m_litToClause[(var<<1)^1].size();
        
        std::vector<std::vector<uint_fast32_t> > newClauses;
        
        // now calculate the new clauses that need to be added due to the elimination
        // combine each clause containing var with each clause containing var^1
        for (uint_fast32_t clausePos : m_litToClause[(var<<1)]) {
          for (uint_fast32_t clauseNeg : m_litToClause[(var<<1)^1]) {
            
            // the clauses should be valid
            if (m_solverData.m_database[clausePos] -> deactivated() || m_solverData.m_database[clauseNeg] -> deactivated()) {
              continue;
            }
            
            // at the resolvent of the two clauses to the new clauses (if it is not already part of it)
            std::vector<uint_fast32_t> resolvent = resolution(clausePos, clauseNeg, var);
            
            // resolvent size == 0 means tautology
            if (resolvent.size() == 0) {
              continue;
            }
            
            // check if the resolvent is already part of newClauses
            bool found(false);
            for (auto clause: newClauses) {
              if (clause == resolvent) {                
                found = true;
                break;
              }
            }
            
            if (!found) {
              newClauses.push_back(resolvent);
            }
          }
        }
        
        
        if (removedClauses > newClauses.size()) {
          
          // remove the old clauses
          for (auto clause: m_litToClause[(var<<1)]) {
            
            m_solverData.m_database[clause] -> deactivate();
          }
          for (auto clause: m_litToClause[(var<<1)^1]) {
            
            m_solverData.m_database[clause] -> deactivate();
          }
          
          eliminatedVar++;
          
          // add the new clauses
          for (auto clause : newClauses) {
            addClause(clause);
            
            m_clauses = m_solverData.m_database.size();
            
            // update m_litToClause
            for (uint_fast32_t lit: clause) {
              m_litToClause[lit].push_back(m_clauses - 1);
            }
          }
        }
      }
      
      #ifdef PREPROCESS_OUTPUT
      std::cout << "c " << "variableElimination eliminated \t" << eliminatedVar << " variables!" << std::endl;
      #endif
    }
    
    
    // returns the resolvent of clause1 and clause2 (wrt to var)
    std::vector<uint_fast32_t> resolution(uint_fast32_t clause1,uint_fast32_t clause2, uint_fast32_t var) {
      
      // vector to store the result
      std::vector<uint_fast32_t> result;
      
      // add each literal from clause 1, apart from var
      uint_fast32_t* lits1 = m_solverData.m_database[clause1] -> lits();
      
      for (uint_fast32_t l = 0; lits1[l] != 0; l++) {
        if (lits1[l] != (var<<1)) {
          // add it to the result
          result.push_back(lits1[l]);
        }
      }
      
      // for each literal in clause2: check if it is not already part of the result, nor negated in clause1
      uint_fast32_t* lits2 = m_solverData.m_database[clause2] -> lits();
      for (uint_fast32_t l = 0; lits2[l] != 0; l++) {
        // if the literal is the negated var, ignore it
        if (lits2[l] == ((var<<1)^1)) {
          continue;
        }
        
        // check if the literal is part of the result
        // if the negated literal is part of the result, we have a tautology
        bool found (false);
        bool tautology(false);
        for (uint_fast32_t rl : result) {
          if (rl == lits2[l]) {
            found = true;
            break;
          }
          else if (rl == (lits2[l]^1)) {
            tautology = true;
            break;
          }       
        }
        // check if the literal is already part of the clause
        if (found) {
          continue;
        }
        // if the negated lit is part of the clause, we have a tautology
        if (tautology) {
          result.clear();
          break;
        }
        
        // otherwise, add it to the result
        result.push_back(lits2[l]);
      }
      
      return result;
    }
    
    
    // removed the given literal lit from the clause
    // updates the watched literals
    void removeLitFromClause(uint_fast32_t lit, uint_fast32_t clause) {
      
      // the clause should still be active
      assert(!m_solverData.m_database[clause] -> deactivated());
      
      // literals of the clause
      uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
      
      // check if lit was a watched literal
      bool replaceWatched(false);
      uint_fast32_t otherLit(0);
      
      for (uint_fast32_t wc = 0; wc < m_solverData.m_watches[lit].size(); wc++) {
        if (m_solverData.m_watches[lit][wc].second == lits) {
          //std::cout << "c " << boost::this_thread::get_id() << ": remove lit " << lit << " from clause " << clause << " also removing watched literal!" << std::endl;
          
          // we need to find a new watched literal
          replaceWatched = true;
          
          // store the other watched literal
          otherLit = m_solverData.m_watches[lit][wc].first;
          
          // clear the watched literal
          m_solverData.m_watches[lit].erase(m_solverData.m_watches[lit].begin() + wc);
          
          
          break;
        }
      }
      
      // write position
      uint_fast32_t q(0);
      
      // copy over the literals apart from lit
      // find a replacement for watched literals
      for (uint_fast32_t p = 0; lits[p] != 0; p++) {
        //std::cout << lits[p] << std::endl;
        if (lits[p] != lit) {
          lits[q++] = lits[p];
          
          if (replaceWatched && lits[p] != otherLit) {
            
            // new watched literal
            m_solverData.m_watches[lits[p]].push_back(std::make_pair(otherLit, lits));
            
            // update the other watched literal     
            for (uint_fast32_t wc = 0; wc < m_solverData.m_watches[otherLit].size(); wc++) {
              if (m_solverData.m_watches[otherLit][wc].second == lits) {
                m_solverData.m_watches[otherLit][wc].first = lits[p];
              }
            }
            
            replaceWatched = false;
          }     
        }
      }
      
      // the clause should have at least one literal left
      assert(q > 0);
      
      // if the literal was part of the clause, q should be one position before the original clause stopper
      assert(lits[q] != 0);
      assert(lits[q+1] == 0);
      
      // add the new clause stopper
      lits[q] = 0;
      
      
      // check if the result is a new unit clause
      if (q == 1) {
        
        //std::cout << "c " << boost::this_thread::get_id() << ": new unit clause \""<< lits[0] <<"\" during removeLitFromClause in clause " << clause << std::endl;
        
        // this clause no longer contains the literal
        m_litToClause[lit].erase(std::remove(m_litToClause[lit].begin(), m_litToClause[lit].end(), clause), m_litToClause[lit].end());     
        
        // lits[0] is a new implication -> doesn't exist in any clause anymore
        m_litToClause[lits[0]].clear();    
        
        // TODO: handling of conflict
        assert(!m_solverData.m_assignment[lits[0]^1]);
        
        // add the implication
        if (!m_solverData.m_assignment[lits[0]]) {
          addImplication(lits[0], nullptr, &m_solverData);
        }
        
        // remove the clause
        m_solverData.m_database[clause] -> deactivate();
        
        return;
      }
      
      // no unit clause? then there should be no more outstanding search for a watch literal
      assert(!replaceWatched);
      
      // update litToClause
      uint_fast32_t sizeBefore = m_litToClause[lit].size();
      m_litToClause[lit].erase(std::remove(m_litToClause[lit].begin(), m_litToClause[lit].end(), clause), m_litToClause[lit].end());     
      
      // the operation should remove exactly one clause from litToClause
      if (sizeBefore - m_litToClause[lit].size() != 1) {
        std::cout << "sizeBefore: " << sizeBefore << ", m_litToClause[lit].size() = " << m_litToClause[lit].size() << std::endl;
      }
      assert(sizeBefore - m_litToClause[lit].size() >= 1);       
    }
    
    // replaces every occurence of lit with by
    // does not update litToClause, nor watches literals
    void replaceLiteralBy(uint_fast32_t lit, uint_fast32_t by) {
      
      uint_fast32_t count(0);
      
      // this would require updating lit to clause...
      for (uint_fast32_t clause = 0; clause < m_clauses; clause++) {
        
        // ignore deactivated clauses
        if (m_solverData.m_database[clause] -> deactivated()) {
          continue;
        }
        
        // look at all the literals of the clause
        uint_fast32_t* lits = m_solverData.m_database[clause] -> lits();
        
        // by already encountered?
        bool byEncountered(false);
        // already replaced
        bool replaced(false);
        // -by encountered
        bool byNegEncountered(false);
        
        // write position
        uint_fast32_t q(0);
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {    
          
          if (lits[l] == (by^1)) {
            byNegEncountered = true;
          }
          
          // check if the clause already contains by
          if (lits[l] == by) {
            
            // only write when 'lit' hasn't been replaced yet
            if (!replaced) {
              lits[q++] = by;
            }
            byEncountered = true;
          }
          
          // replace lit with by
          else if (lits[l] == lit) {
            
            // only write when 'by' hasn't been encounteed yet
            if (!byEncountered)  {
              lits[q++] = by;
            }
            // we've done a replacement
            replaced = true;
            
            count++;
          }
          else {
            lits[q++] = lits[l];
          }
        }
        
        // check if we have a tautology
        if (byNegEncountered && (replaced || byEncountered)) {
          
          //std::cout << "c " << boost::this_thread::get_id() << ": tautology in clause " << clause  << "(replaced: " << replaced << ", byEncountered = " << byEncountered << std::endl;
          
          m_solverData.m_database[clause] -> deactivate();
          continue;       
        }
        
        
        // check if a literal was removed
        if (lits[q] != 0) {
          
          //std::cout << "c " << boost::this_thread::get_id() << ": q is not clause stopper" << std::endl;
          
          // at most one literal should be removed
          assert(lits[q+1] == 0); 
          
          // new clause stopper
          lits[q] = 0;
          
          // check if this is a new implication
          if (q == 1) {
            addImplication(lits[0], nullptr, &m_solverData);
            
            // remove the clause
            m_solverData.m_database[clause] -> deactivate();
            
            std::cout << "c " << boost::this_thread::get_id() << ": new implication due to removel of " << lit << " from clause!" << std::endl;  
            
            // TODO!
            assert(false);
          }
        }
      }
    }
    
    // unit propagation look ahead: 
    // for each variable: check implications of assigning it to true or false
    // if one leads to a conflict, store the other one as an implication
    // additionally, a check for equivalent variables is performed. After upla, the vector replacedVar contains a '1' at each replaced variable
    bool upla (bool doEquivalenceElimination = true) {
      
      // Initialization.
      std::vector<uint_fast32_t> implications; 
      bool uplaSuccess(false);
      
      std::vector<uint_fast32_t> equivalenceClasses((m_variables+2)<<1, 0);
      uint_fast32_t equivalenceClassCounter(0);
      
      
      // store the implications in the first round
      double firstRoundImplications = 0;
      
      // Perform UPLA as long as mandatory implications can be found. 
      do 
      {
        // Reset "uplaSuccess".
        uplaSuccess = false;
        
        
        uint_fast32_t posBefore =  m_solverData.m_assignmentStackEndPosition;
        
        //double noOccurences = 3. * (double) m_clauses / (double)m_variables;
        
        //std::cout << "c UPLA: noOccurences: " << noOccurences << std::endl;
        
        // Check all unassigned variables.
        for (uint_fast32_t v = 1; v <= m_variables; ++v)
        {
          // check if the variable occurs in at least one binrary clause
          /*
           *          uint_fast32_t noBinaries(0);
           *          for (auto cl: m_litToClause[v << 1]) {
           *            uint_fast32_t* lits = m_solverData.m_database[cl] -> lits();
           *            if(lits[1] != 0 && lits[2] == 0) {
           *              noBinaries++;
        }
        }
        for (auto cl: m_litToClause[(v << 1)^1]) {
          uint_fast32_t* lits = m_solverData.m_database[cl] -> lits();
          if(lits[1] != 0 && lits[2] == 0) {
            noBinaries++;
        }
        }
        
        if (noBinaries < 2) { // BLACK MAGIC NUMBER
          continue;
        }
        */
          
          // Initialization.
          uint_fast32_t lit(v << 1);
          uint_fast32_t pos(0);
          
          // Current variable still unassigned?
          if (!m_solverData.m_assignment[lit] && !m_solverData.m_assignment[lit ^ 1])
          {
            // Add "lit" as a decision to the decision stack.
            addDecision(lit, &m_solverData);
            
            // Initialization.
            pos = m_solverData.m_assignmentStackEndPosition;
            
            
            // What about the effects of this decision?           
            if (deduce(&m_solverData) != nullptr)
            {                 
              // Set "uplaSuccess".
              uplaSuccess = true;
              
              // Backtrack.
              backtrackOneLevel(&m_solverData);
              
              // Add "lit ^ 1" as an implication to the decision stack.
              addImplication(lit ^ 1, 0, &m_solverData);
              
              // Unresolvable conflict?
              if (deduce(&m_solverData) != 0)
              { return false; }
            }
            else
            { 
              // Clear "implications".
              implications.clear(); 
              
              // Store all implications forced by decision "lit" within "implications".
              for (uint_fast32_t i = pos; i < m_solverData.m_assignmentStackEndPosition; ++i)
              {
                //std::cout << "c " << boost::this_thread::get_id() << ": store implication " << m_solverData.m_assignmentStack[i] << std::endl;
                implications.push_back(m_solverData.m_assignmentStack[i]); 
              }
              
              // Backtrack.
              backtrackOneLevel(&m_solverData);
              
              // Add "lit" as a decision to the decision stack.
              addDecision(lit ^ 1, &m_solverData);
              
              // Initialization.
              pos = m_solverData.m_assignmentStackEndPosition;
              
              // What about the effects of this decision?                 
              if (deduce(&m_solverData) != nullptr)
              {
                // Set "uplaSuccess".
                uplaSuccess = true;
                
                // Backtrack.
                backtrackOneLevel(&m_solverData);
                
                // Add "lit" as an implication to the decision stack.
                addImplication(lit, 0, &m_solverData);
                
                // Unresolvable conflict?
                if (deduce(&m_solverData) != 0)
                { return false; }
              }
              else
              { 
                if (doEquivalenceElimination) {
                  
                  //std::cout << "c have to compare " << (implications.size() * (m_solverData.m_assignmentStackEndPosition-pos)) << " elements" << std::endl;
                  
                  // check for equivalent variables
                  for (uint_fast32_t j = 0; j < implications.size(); ++j)
                  {
                    uint_fast32_t i(0);
                    for (i = pos; i < m_solverData.m_assignmentStackEndPosition; ++i) {
                      // look for implication a => b, -a => -b (which means a <=> b)
                      // or a => -b , -a => b (which means a <=> -b)
                      if (implications[j] != (m_solverData.m_assignmentStack[i]^1)) {
                        continue;
                      }
                      
                      //std::cout << "implications[j] = " << implications[j] << ", m_solverData.m_assignmentStack[i] = " << m_solverData.m_assignmentStack[i] << std::endl;
                      //std::cout << "lit1 : " << (v << 1) << ", lit2: " << implications[j] << std::endl;
                      
                      
                      // possibilities: 
                      // 1: v and implications[j] are not in any equivalence class
                      if(equivalenceClasses[v << 1] == 0 && equivalenceClasses[implications[j]] == 0) {
                        
                        // create a new equivalence classes
                        
                        // positive:
                        equivalenceClassCounter++;
                        
                        equivalenceClasses[v << 1] = equivalenceClassCounter;
                        equivalenceClasses[implications[j]] = equivalenceClassCounter;
                        
                        // negative:
                        
                        
                        // shouldn't be part of any eq class, yet
                        assert(equivalenceClasses[(v << 1)^1] == 0);
                        assert(equivalenceClasses[(implications[j])^1] == 0);
                        
                        equivalenceClassCounter++;
                        
                        equivalenceClasses[(v << 1)^1] = equivalenceClassCounter;
                        equivalenceClasses[(implications[j])^1] = equivalenceClassCounter;
                      }
                      
                      // 2: v is part, but implications[j] isn't
                      else if(equivalenceClasses[v << 1] != 0 && equivalenceClasses[implications[j]] == 0) {
                        
                        // add implications[j] to the class of v
                        equivalenceClasses[implications[j]] = equivalenceClasses[v << 1];
                        
                        // (v<<1)^1 should also be part of an eq class
                        assert(equivalenceClasses[(v << 1)^1] != 0);
                        // shouldn't be part of any eq class, yet
                        assert(equivalenceClasses[(implications[j])^1] == 0);
                        
                        // and the negative, too
                        equivalenceClasses[(implications[j])^1] = equivalenceClasses[(v << 1)^1];
                      }
                      
                      // 3: v is not part, but implications[j] is
                      else if(equivalenceClasses[v << 1] == 0 && equivalenceClasses[implications[j]] != 0) {
                        
                        // set v to the class of implications[j]
                        equivalenceClasses[v << 1] = equivalenceClasses[implications[j]];
                        
                        // (implications[j])^1 should also be part of an eq class
                        assert(equivalenceClasses[(implications[j])^1] != 0);
                        // shouldn't be part of any eq class, yet
                        assert(equivalenceClasses[(v << 1)^1] == 0);
                        
                        // and the negative, too
                        equivalenceClasses[(v << 1)^1] = equivalenceClasses[(implications[j])^1];
                      }
                      
                      // 4: v and implications[j] are part of the same eq class
                      else if(equivalenceClasses[v << 1] == equivalenceClasses[implications[j]]) {
                        
                        // negations should have the same class, too
                        assert(equivalenceClasses[(v << 1)^1] == equivalenceClasses[(implications[j])^1]);
                      }
                      
                      // 5: v and implications[j] are part of different eq classes
                      else {                        
                        // join the eq classes
                        
                        //std::cout << "c " << boost::this_thread::get_id() << ": joining eq classes" << std::endl;
                        
                        // set the eq class to the one of v
                        for (uint_fast32_t lit = 2; lit < ((m_variables+1) << 1) ; lit++) {
                          if (equivalenceClasses[lit] == equivalenceClasses[(implications[j])]) {
                            equivalenceClasses[lit] = equivalenceClasses[v << 1];
                          }
                          if (equivalenceClasses[lit] == equivalenceClasses[(implications[j])^1]) {
                            equivalenceClasses[lit] = equivalenceClasses[(v << 1)^1];
                          }
                        }                        
                      }
                    }
                  }
                }                
                
                
                // Let's see which implications are forced by both "lit" and "lit ^ 1".
                for (uint_fast32_t j = 0; j < implications.size(); ++j)
                {
                  uint_fast32_t i(0);
                  for (i = pos; i < m_solverData.m_assignmentStackEndPosition; ++i)
                  {
                    if (m_solverData.m_assignmentStack[i] == implications[j])
                    { break; }
                  }
                  if (i == m_solverData.m_assignmentStackEndPosition)
                  { implications.erase(implications.begin() + j); --j; }
                }
                
                // Backtrack.
                backtrackOneLevel(&m_solverData);
                
                // Have we found some mandatory assignments?
                if (!implications.empty())
                {
                  // Set "uplaSuccess".
                  uplaSuccess = true;
                  
                  // Push all elements of "implications" as implications onto the decision stack.
                  uint_fast32_t size(implications.size());
                  for (uint_fast32_t j = 0; j < size; ++j)
                  {
                    uint_fast32_t ilit(implications[j]);
                    if (m_solverData.m_assignment[ilit ^ 1])
                    { return false; }
                    if (!m_solverData.m_assignment[ilit])
                    { 
                      addImplication(ilit, 0, &m_solverData); 
                      if (deduce(&m_solverData) != 0)
                      { return false; }
                    }
                  }
                }
              }
            }
          }
        }
        
        // compute the number of new implications
        uint_fast32_t noNewImplications =  m_solverData.m_assignmentStackEndPosition - posBefore;
        
        if (firstRoundImplications == 0) {
          firstRoundImplications = noNewImplications;
        }
        else if (noNewImplications < 0.1 * firstRoundImplications) {
          // abort UPLA if new implications are few
          uplaSuccess = false;
        }
        
        //std::cout << "c new implications = "  << noNewImplications << std::endl;
        
      }
      while (uplaSuccess);
      
      if (deduce(&m_solverData) != NULL) {
        return false;
      }
      
      antomSimplify();
      
      // create an array which stores a replacement for each literal
      std::vector<uint_fast32_t> replacement((m_variables+1)<<1);
      
      // temp array which stores the representative for each group
      std::vector<uint_fast32_t> representative(equivalenceClassCounter+1, 0);
      // temp array to store if a eq class is satisfied (0 = not sat, 1 = pos sat, 2 = neg sat)
      std::vector<uint_fast32_t> eqClassSat(equivalenceClassCounter+1, 0);
      
      uint_fast32_t replaceCount(0);
      
      for (uint_fast32_t lit = 2; lit < ((m_variables+1)<< 1); lit++) {
        
        // get the equivalence class
        uint_fast32_t eqClass = equivalenceClasses[lit];   
        uint_fast32_t eqClassNeg = equivalenceClasses[lit^1];        
        
        
        if (eqClass != 0) {
          
          // count the replacement
          replaceCount++;
          
          // check if the eq class is satisfied
          if (eqClassSat[eqClass] != 0) {
            
            // check if this is a conflict
            if (m_solverData.m_assignment[lit^1]) {
              // conflict!
              std::cout << "c conflict during UPLA" << std::endl;
              return false;
            }
            
            if (!m_solverData.m_assignment[lit]) {
              addImplication(lit, nullptr, &m_solverData);
            }            
            continue;
          }
          // check if the neg eq class is satisfied
          if (eqClassSat[eqClassNeg] != 0) {
            
            // check if this is a conflict
            if (m_solverData.m_assignment[lit]) {
              // conflict!
              std::cout << "c conflict during UPLA" << std::endl;
              return false;
            }
            
            if (!m_solverData.m_assignment[lit^1]) {
              addImplication(lit^1, nullptr, &m_solverData);
            }            
            continue;
          }
          
          // check if the current literal is sat
          if (m_solverData.m_assignment[lit]) {
            // store as sat
            eqClassSat[eqClass] = 1;
            
            // and tell all variables in the eq class thus far that they're also assigned
            for (uint_fast32_t eqLit = 2; eqLit < lit ; eqLit++) {
              if(equivalenceClasses[eqLit] == eqClass) {
                if (!m_solverData.m_assignment[eqLit]) {
                  addImplication(eqLit, nullptr, &m_solverData);
                }
              }
              if(equivalenceClasses[eqLit] == eqClassNeg) {
                if (!m_solverData.m_assignment[eqLit^1]) {
                  addImplication(eqLit^1, nullptr, &m_solverData);
                }
              }
            }            
            
            continue;
          }
          
          // if there is no representative for the class, make this var it
          if (representative[eqClass] == 0) {
            representative[eqClass] = lit;
            
            replaceCount--;
          }
          
          // replace this lit by the representative
          replacement[lit] = representative[eqClass];
        }
        else {
          
          // if there is no replacement, replace lit by itself
          replacement[lit] = lit;
        }
      }
      
      
      if (deduce(&m_solverData) != NULL) {
        return false;
      }
      
      // now compress the database and combine it with the replacement
      std::vector<bool>* activeVariables = new std::vector<bool>(m_variables+1, 1);
      std::vector<bool>* activeClauses = new std::vector<bool>(m_clauses, 1);
      std::vector<uint_fast32_t> replacedVar_ (m_variables+1, 0);
      
      for (uint_fast32_t a = 1; a < m_solverData.m_assignmentStackEndPosition; a++) {
        activeVariables -> at((m_solverData.m_assignmentStack[a]) >> 1) = false;
      } 
      for (uint_fast32_t c = 0; c < m_solverData.m_database.size(); c++) {
        
        if (m_solverData.m_database[c] -> deactivated()) {
          continue;
        }
        
        uint_fast32_t* lits = m_solverData.m_database[c] -> lits();
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (m_solverData.m_assignment[lits[l]]) {
            m_solverData.m_database[c] -> deactivate();
            break;
          }
        }        
      }      
      
      std::vector<std::vector<uint_fast32_t> > originalClauses = compressDatabase(activeVariables, activeClauses, &replacedVar_, &replacement);
      
      // add the original clauses
      for (auto clause: originalClauses) {
        
        // empty vector = removed clause -> just ignore
        if (clause.empty()) {
          continue;
        } 
        
        // add the clause
        if (!addClause(clause)) {
          return false;
        }
      }
      
      // store the new size of the database
      m_clauses = m_solverData.m_database.size();
      
      std::cout << "c UPLA removed " << (replaceCount>>1) << " equivalent variables" << std::endl;
      
      return true;
    }
    
    
    // Returns a "time stamp".          
    uint64_t getTimeStamp (void)
    {
      timeval time;
      gettimeofday(&time, nullptr);
      uint64_t  wtime  = time.tv_sec;
      wtime            = wtime * 1000000l;
      wtime           += time.tv_usec;
      return wtime;
    }
    
    
    // the number of threads
    uint_fast32_t m_noThreads;    
    
    // condition variable to create efficient producer / consumer system
    boost::condition_variable m_nodesAvailable;
    
    // numer of threads waiting for cleanup
    uint_fast32_t m_waitingThreads;
    
    // mutex to ensure threadsafty of std::cout
    boost::mutex m_coutMutex;
    // #endif    
    
    
    // mutex for cleanup of cache
    boost::mutex m_cacheCleaningMutex;
    
    // the condition variable for the cleanup of the cache
    boost::barrier* m_cleanCacheBarrier;
    
    bool m_doCacheCleaning;
    
    uint_fast32_t m_threadsReadyForCacheCleaning;
    
    // the topmost node in the decision tree
    ComponentTreeNode* m_topNode;
    
    // vector to store the data of each thread
    std::vector<SolverData*> m_solverDatas;
    
    // vector of all the learnt conflict clauses
    // pairs of clauses and lbd
    std::vector<std::pair<std::vector<uint_fast32_t>, uint_fast32_t> > m_conflictClauses;
    
    // A flag indicating whether the solver has been able to deduce the empty clause.
    // In particular important for the incremental mode to distinguish between
    // "unsatisfiable" and "unsatisfiable under assumptions". 
    bool m_emptyClause;
    
    // switch to enable extra processing of binary clauses, currently always true!
    bool m_enableBinaryClauses;
    
    // The current number of variables.
    uint_fast32_t m_variables; 
    
    // the number of clauses (before learning, but after simplify)
    uint_fast32_t m_clauses;
    
    // the number of clauses (before learning and before adding additional clauses from simplify)
    uint_fast32_t m_originalClauses;   
    
    // timestamp for when the computation started
    uint_fast64_t m_timeComputationStart;
    
    // The number of decisions made so far.
    uint_fast32_t m_decisions; 
    
    // The number of BCP operations performed so far. 
    uint_fast32_t m_bcps; 
    
    // the model count
    mpz_class m_modelCount;
    
    // the number of additional free variables
    uint_fast32_t m_additionalFreeVariables;    
    
    SolverData m_solverData = {};
    
    // flag to inform other solver threads that new nodes are required on the global stack
    uint_fast32_t m_requestForNodes;
    
    
    // Variable activity related variables.
    double m_incVarActivity;
    double m_decayFactor;
    
    // For each variable we store its activity.
    std::atomic_uint_fast64_t* m_activity;  
    
    // and the maximum activity
    std::atomic_uint_fast64_t m_maxActivity;
    
    // for each literal store the clauses in which it occurs (clauses represented by their position in the clause db)
    std::vector< std::vector<uint_fast32_t> > m_litToClause;    
    
    // stores the assumptions which will be set on the call to solve()
    std::vector<uint_fast32_t> m_assumptions;
    
    // stores all learned (through conflicts) unit clauses
    std::vector<uint_fast32_t> m_units;
    
    CacheController* m_cacheController;
    
    NodeManager* m_nodeManager;
  };
}

#endif
