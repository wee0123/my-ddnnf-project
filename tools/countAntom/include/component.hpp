
#ifndef COMPONENT_HPP
#define COMPONENT_HPP

/********************************************************************************************
 * component.hpp -- Copyright (c) 2014, 2015, Jan Burchard
 * 
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
#include <cassert>
#include <vector>
#include <sstream>
#include <math.h>
#include <ctime>
// gnu bignum package - requires libgmp
#include <gmpxx.h>

#include <atomic>


// Include antom related headers.
#include "countAntom.hpp"
#include "clause.hpp"

// boost threading
#include <boost/thread.hpp>
#include <boost/lockfree/stack.hpp>
#include <boost/lockfree/queue.hpp>

namespace countAntom
{
  //#define DEBUG_OUTPUT
  
  // debug mode: additional assertions to ensure the code is working as intended
  #define DEBUG_MODE
  
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  // forward declerations of classes and structs
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  class CacheController;
  class NodeManager;
  class ComponentTreeNode;
  struct NodeDependency;
  struct CompareComponentTreeNodes; // comperator for std::multiset


  
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  // forward declerations of class bodies
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  
  
  
  // a struct to store the dependency between two nodes
  // (i.e. a node depends on the cached result of the other node)
  // m_first depends on the value of m_second
  struct NodeDependency {
    ComponentTreeNode* m_first;
    ComponentTreeNode* m_second;
    boost::mutex m_mutex;
    
    // constructor
    NodeDependency(ComponentTreeNode* first, ComponentTreeNode* second) :
    m_first(first),
    m_second(second),
    m_mutex(){}
    
    // comparison operator for sorting (used to eliminate doubles)
    bool operator < (const NodeDependency& other) const;
    
    // comparison operator (used by unique to remove doubles)
    bool operator ==(const NodeDependency& other) const ;
    
  private:
    
    // copy constructor
    NodeDependency (const NodeDependency&);
    
    // Assignment operator.
    NodeDependency& operator = (const NodeDependency&);
  };
  
  
  // comperator for component tree nodes
  struct CompareComponentTreeNodes  {
    // use the comperator from ComponentTreeNode
    bool operator()(const ComponentTreeNode* a, const ComponentTreeNode* b) const;  
  };
  
  
  class ComponentTreeNode {
    // the cache controller can access the private variables
    friend class CacheController;
  public:    
    
    // constructor:
    ComponentTreeNode (ComponentTreeNode* parent, uint_fast32_t decisionLevel, bool neg, std::vector<uint_least32_t>* variables, std::vector<uint_least32_t>* clauses, 
                       CacheController* cacheController, NodeManager* nodeManager
    ) :
    m_dependencies(),
    m_stateMutex(),
    m_state(0),
    m_decisionLevel(decisionLevel),
    m_decisionVariable(0),
    m_parentNeg(neg),
    m_parent(parent),
    m_variables(variables),
    m_clauses(clauses),
    m_compressedVarClauses(),
    m_extraImplications(),
    m_hash(0),
    m_modelCount(0),
    m_cacheController(cacheController),
    m_nodeManager(nodeManager)
    {      
      
      // compress the variables and clauses
      
      // the previous variable read
      uint_least32_t lastVar=0;
      // stores if variables have been ommited
      bool compressed = false;
      for (auto var: *variables) {
        
        if (var == lastVar + 1 && lastVar != 0) {
          // compress runs of variables
          compressed = true;
        }
        else {          
          // runs of variables are marked by "-"
          if (compressed) {
            m_compressedVarClauses.push_back(std::numeric_limits<uint_least32_t>::max());
            m_compressedVarClauses.push_back(lastVar);
            compressed = false;
          }
          // add the current variable
          m_compressedVarClauses.push_back(var);
        }      
        
        // store the last variable
        lastVar = var;
      }
      
      // add the last variable if it was compressed and a marker between variables and clauses
      if (compressed) {
        m_compressedVarClauses.push_back(std::numeric_limits<uint_least32_t>::max());
        m_compressedVarClauses.push_back(lastVar);
      }
      m_compressedVarClauses.push_back(std::numeric_limits<uint_least32_t>::max()-1);
      
      
      // clauses
      
      // the previous variable read
      uint_least32_t lastClause=0;
      // stores if clause have been ommited
      compressed = false;
      for (auto clause: *clauses) {
        
        if (clause == lastClause + 1 && lastClause != 0) {
          // compress runs of clause
          compressed = true;
        }
        else {          
          // runs of clause are marked by "-"
          if (compressed) {
            m_compressedVarClauses.push_back(std::numeric_limits<uint_least32_t>::max());
            m_compressedVarClauses.push_back(lastClause);
            compressed = false;
          }
          m_compressedVarClauses.push_back(clause);
          // add the current clause
        }      
        
        // store the last clause
        lastClause = clause;
      }
      
      // add the last clause if it was compressed
      if (compressed) {
        m_compressedVarClauses.push_back(std::numeric_limits<uint_least32_t>::max());
        m_compressedVarClauses.push_back(lastClause);
      }
      
      //uint_fast32_t normalSize = (sizeof(*variables) + sizeof(uint_least32_t) * variables -> size() + sizeof(*clauses)+ sizeof(uint_least32_t) * clauses -> size());
      //uint_fast32_t compressedSize = sizeof(m_compressedVarClauses) + sizeof(uint_least32_t) * m_compressedVarClauses.size();
            
      
      //std::cout << "c normal size = " << normalSize << ", compressed : " << compressedSize<<  std::endl;
      
      
      /*
      std::cout << "c variables: ";
      for (auto var: *variables) {
        std::cout << var << " ";
      }
      std::cout << std::endl;
      std::cout << "c clauses: ";
      for (auto cl: *clauses) {
        std::cout << cl << " ";
      }
      
      std::cout << std::endl << "c compressed: ";
      for (auto v: m_compressedVarClauses) {
        std::cout << v << " ";
      }
      std::cout << std::endl;
      
      delete m_variables;
      delete m_clauses;
      m_variables = nullptr;
      m_clauses = nullptr;
      
      decompressVarClauses();
      std::cout << "c after decompress: " << std::endl;
      std::cout << "c variables: ";
      for (auto var: *m_variables) {
        std::cout << var << " ";
      }
      std::cout << std::endl;
      
      std::cout <<"c clauses: ";
      for (auto cl: *m_clauses) {
        std::cout << cl << " ";
      }
      std::cout << std::endl;
      std::cout << std::endl;
      */
      
      // compute the cache representation and the hash
      computeHash();
    }
    
    // destructor (is defined below)
    ~ComponentTreeNode();
    
    // comparison operator required for hashing
    bool operator ==(const ComponentTreeNode &b) const;
    
    // comparison operator used for std::set
    bool operator < (const ComponentTreeNode &b) const;
    
    // --------------------------------------------------------------------------------------------------------------------------
    // Getters and setters
    // --------------------------------------------------------------------------------------------------------------------------
    
    // returns a reference to the parent node
    ComponentTreeNode* parent();
    std::vector<ComponentTreeNode*>* getPosChildren();
    std::vector<ComponentTreeNode*>* getNegChildren();
    
    // check if the nodes cached result is used by another node
    bool isNodeUsedByOtherNodes();
    
    // returns the decision level of the node
    uint_fast32_t decisionLevel();
    
    // set the decision variable
    void decisionVariable(uint_fast32_t var);
    
    // return the decision variable
    uint_fast32_t decisionVariable();
    
    // returns if the node is on the negated path from its parent
    bool neg();
    
    // returns the variables of this node
    std::vector<uint_least32_t>* variables();
    
    // returns the clauses of this node
    std::vector<uint_least32_t>* clauses();
        
    // returns the state of the component node
    uint_fast32_t state();
    
    // returns the mutex to lock the node
    boost::mutex* getMutex();
    
    // informs the node that its neg branch is completely split
    void splitDone();
    
    // adds a child to the positive or negative children list
    bool addPosChildren(std::vector<ComponentTreeNode*>& children);
    bool addNegChildren(std::vector<ComponentTreeNode*>& children);
    
    // informs the node that a child was removed by a cleanup procedure
    void childRemoved(ComponentTreeNode* child);
    
    // returns the model count
    mpz_class modelCount();
    
    void addExtraImplication(uint_fast32_t imp);
    
    std::vector<uint_fast32_t>* getExtraImplications();
    
    // debug helper: print the state of every child node
    void printChildStates(uint_fast32_t indent, bool neg = false);
    
    
    // --------------------------------------------------------------------------------------------------------------------------
    // other functions
    // --------------------------------------------------------------------------------------------------------------------------
    
    // removes the node from the search tree by moving all its dependencies to its parent
    uint_fast32_t removeFromTree();
    
    // tells the node that one of its child nodes had an incorrect result
    void hasIncorrectChild();    
    
    // checks if there is a cached result for the current node available
    bool cacheLookup();
    
    // store that a node is using the cached result of the current node
    void addDependency(NodeDependency* dep);
    
    // informs the node that its pos/neg branch is satisfied
    void sat(bool neg, std::vector<uint_fast8_t>* assignment);
    
    // informs the node that it is completely satisfied
    void sat(std::vector<uint_fast8_t>* assignment);
    
    // informs the node that one if its branches is UNSAT
    void unsat(bool neg);
    
    // informs the node that it is completely unsat
    void unsat();
    
    // informs the node that the cache value it used is incorrect
    void cacheValueIncorrect();
    
    // aborts the calculation for the current component and all its children
    // if the result for the component is in the cache, it is removed
    void abort();
    
    // informs the node that a child is done with its computation
    // this triggers a check if all children are done
    void childDone();
    
    // informs the node that a child is not satisfiable
    // this means that all sibling nodes don't have to be computed
    void childUnsat(ComponentTreeNode* child);
    
    // attempts to split the current node into multiple sub nodes
    // note: this assumes that the decision variable has already been assigned
    std::vector<ComponentTreeNode*> split(bool attemptSplit, std::vector< Clause* >* database, std::vector< uint_fast8_t >* assignment);
    
    // computes the hash of the current node
    void computeHash();
    
    // returns the pre-computed hash value
    uint64_t getHash();
    
    // returns the size of all dynamic data structures
    uint_fast32_t getSize();
    
    // custom implementation of uniqueClean for dependencies
    // source: http://en.cppreference.com/w/cpp/algorithm/unique
    template<class ForwardIt, class BinaryPredicate>
    ForwardIt uniqueClean(ForwardIt first, ForwardIt last, BinaryPredicate p, ComponentTreeNode* originator);
    
    // removes multiple occurences of the same dependency, must be run by a single thread!
    // NOTE: no locking for performance enhancments
    void fixDependencies();
    
  private:
    
    // Copy constructor.
    ComponentTreeNode (const ComponentTreeNode&);
    
    // Assignment operator.
    ComponentTreeNode& operator = (const ComponentTreeNode&);
    
    // informs all the nodes that depend on the cached value of this node that its no longer valid
    void informDependenciesWrongValue();
    
    // checks if the component is done and computes the final model count
    bool checkDoneComputeModelCount();
    
    // finalizes the computation of the node and informs the parent that it is done
    void finalize();
    
    // restores the clause and variable vector
    void decompressVarClauses();
    
    // all information needed for the positive and negative branch
    struct Branch {
      bool m_split;                               // true when the branch has been split into sub components
      std::vector<ComponentTreeNode*> m_children; // child components
      uint_fast32_t m_freeVariables;               // number of free variables in this branch                 
      bool m_triviallyDone;                       // true iff the component is trivially done (sat / unsat / cache hit)
      mpz_class m_modelCount;                     // the model count if the component is trivially done
      Branch() : m_split(false), m_children(), m_freeVariables(0), m_triviallyDone(false),m_modelCount(0) {}
    };
    
    // the positive & negative decision branch
    Branch m_posBranch = {};
    Branch m_negBranch = {};
    
    // list of dependencies
    std::vector<NodeDependency*> m_dependencies;
    
    // mutex for component access
    boost::mutex m_stateMutex;    
    
    // the current state of the component
    // 0  = newly created
    // 10 = waiting for child results
    // 20 = done
    // 25 = done but child list is incomplete -> on recomputation the node has to be split again
    // 30 = cache hit -> this component was never computed
    // 40 = aborted   -> a sibling or parent component was UNSAT, therefore the calculation was aborted (state = 40 is only set after it is ensured that all computations on the node are done)
    // 50 = invalid   -> set during a cleanup to mark nodes that have been removed from the computation tree and will be deleted in the next step
    uint_fast32_t m_state;
    
    // the decision level this node is on
    uint_fast32_t m_decisionLevel;
    
    // the decision variable
    uint_fast32_t m_decisionVariable;
    
    // stores whether the component is one the path of the positive or negative decision of the parent
    bool m_parentNeg;
    
    // the parent ComponentTreeNode
    ComponentTreeNode* m_parent;
    
    // pointers to clause and variable list
    // these lists are created by split() in the parent component but have to be deleted locally
    std::vector<uint_least32_t>* m_variables;
    std::vector<uint_least32_t>* m_clauses;
    
    // compression is used to minimize the size of the component in the memory
    std::vector<uint_least32_t> m_compressedVarClauses;
    
    // extra implications which were added by ibcp
    std::vector<uint_fast32_t> m_extraImplications;    
        
    // the hash value of the component
    uint64_t m_hash;
    
    // the model count of the current node
    // only valid if state >= 20
    mpz_class m_modelCount;
    
    // reference to the cacheController
    CacheController* m_cacheController;
    // reference to the node manager
    NodeManager* m_nodeManager;
  };
  
  
  // the node manager handles all the nodes that still need to be handled
  class NodeManager {
  public:
    
    NodeManager() :
    m_nodeMutex(),
    m_nodesRecomputeMutex(),
    m_nodesToDeleteMutex(),
    m_nodesChildDone(0),
    m_nodes(),
    m_nodesSize(0),
    m_nodesChildIncorrect(),
    m_nodesChildIncorrectInProgress(),
    m_nodesRecomputeWrongCache(),
    m_nodesRecomputeWrongCacheInProgress(),
    m_nodesToDelete(0),
    m_globalNodeListAccesses(0)
    {
    }
    
    ~NodeManager() {      
      //std::cout << "c NodeManager: had " << m_globalNodeListAccesses << " global node list accesses" << std::endl;
    }
    
    // returns the access mutex
    boost::mutex* getNodeMutex();
    
    // checks that all data structures are ready for cleanup
    void assertReadyForCleanup();
    
    // adds a node which can be deleted at the next cleanup operation
    void addNodeToDelete(ComponentTreeNode* node);
    
    // deletes all nodes that have been previously marked for deletion
    void deleteNodes();
    
    // returns the number of nodes that will be deleted in the next step
    uint_fast32_t getNoNodesToDelete();
    
    // clears the list of nodes to delete
    void clearNodesToDelete();
    
    // add a node who has a finished child. If unsatChild != nullptr, the child is unsat
    void nodeChildDone(ComponentTreeNode* node, ComponentTreeNode* unsatChild);
    
    // returns a node with a finished child
    std::pair <ComponentTreeNode*, ComponentTreeNode*>* getNodeUnsatChild();
    
    // the computation on an incorrect cache value node is done
    void nodeToRecomputeWrongCacheDone(ComponentTreeNode* node);
    
    // the computation on an incorrect child is done
    void nodeHasIncorrectChildDone(ComponentTreeNode* node);
    
    // the given node has a child which had an incorrect model count
    void nodeHasIncorrectChild(ComponentTreeNode* node);
    
    // returns a node with an incorrect child
    ComponentTreeNode* getNodeWithIncorrectChild();
    
    // add a node which has to be recomputed because it was based on a wrong cache result or dependency
    void addNodeToRecomputeWrongCache(ComponentTreeNode* node);
    
    // returns a node which has to be recomputed because it was based on a wrong cache result
    ComponentTreeNode* getNodeToRecomputeWrongCache();
    
    // add a nodes
    // if removeFromInProgress is set to true, the node will be removed from the in progress lists
    void addNode(ComponentTreeNode* node, bool removeFromInProgress = false);
    
    // returns a node which needs to be computed
    ComponentTreeNode* getNode();
    
    // returns the number of nodes the nodemanager currently has waiting for computation
    uint_fast32_t getNoNodes();
    
    // returns true if the node manager has almost no nodes stored
    bool requiresNodes();
    
    // check the list m_nodes for aborted nodes and removes them from it before these nodes are deleted
    void evaluateNodes();
    
  private:
    
    // mutex for vector access
    boost::mutex m_nodeMutex;
    boost::mutex m_nodesRecomputeMutex;
    boost::mutex m_nodesToDeleteMutex;
    
    // pairs of node and unsat child for computation of childDone / childUnsat
    boost::lockfree::stack <std::pair<ComponentTreeNode*, ComponentTreeNode*>*> m_nodesChildDone;
    
    // vector of nodes that need to be computed
    std::multiset <ComponentTreeNode*,CompareComponentTreeNodes> m_nodes;
    //boost::lockfree::stack<ComponentTreeNode*> m_nodes;
    
    // number of elements on the node stack
    std::atomic_uint_fast32_t m_nodesSize;
    
    // vector of nodes that have an incorrect child
    std::vector<ComponentTreeNode*> m_nodesChildIncorrect;
    // "in progress" vector for the above vector
    std::vector<ComponentTreeNode*> m_nodesChildIncorrectInProgress;
    
    // vector of nodes that were based on an incorrect cache result and therefore have to be recomputed
    std::vector<ComponentTreeNode*> m_nodesRecomputeWrongCache;
    // "in progress" vector for the above vector
    std::vector<ComponentTreeNode*> m_nodesRecomputeWrongCacheInProgress;
    
    // a vector of nodes that need to be deleted during cleanup (e.g., aborted nodes)
    boost::lockfree::stack<ComponentTreeNode*> m_nodesToDelete;
    
    uint_fast32_t m_globalNodeListAccesses;
  };
  
  
  // the cache controller manages access to the cache
  // it stores the results for the different nodes and ensures only valid cache records are returned
  class CacheController {
  public:
    
    
    // constructor
    CacheController(uint_fast32_t noVariables, uint_fast32_t noClauses, uint_fast32_t memSize, uint_fast32_t noThreads, bool addDependencies) :
    m_noElements(0),
    m_maxNoElements(0), // overwritten in constructor
    m_tries(0),
    m_hits(0),
    m_removedComponents(0),
    m_hashConflicts(0),
    m_hashTable(),
    m_noBuckets(0), // overwritten in constructor
    m_maxSize(((uint64_t)memSize)*1000000), 
    m_nodesToKeep(),
    m_varHashes(nullptr),
    m_clauseHashes(nullptr),
    m_accessMutexes(100),
    m_nodesToKeepMutex(),
    m_accessMutexCleanup(noThreads),
    m_countMutex(),
    m_addDependencies(addDependencies)
    {      
      // create the lookup tables for the variables and clauses (for hashing)
      m_varHashes = new std::vector<uint64_t>(noVariables+1);
      for (uint64_t i = 0; i < noVariables+1; i++) {
        
        m_varHashes->at(i) = (computeHashForNumber(i));
      }
      
      m_clauseHashes = new std::vector<uint64_t>(noClauses);
      for (uint_fast32_t i = 0; i < noClauses; i++) {
        m_clauseHashes->at(i) = (computeHashForNumber(i+noVariables+2));
      }
      
      
      // compute the maximum number of elements in the cache before a cleanup is triggered
      // each element requires (#var + #clauses)*4+const bytes (assumption: uint_fast32_t used for variables and clauses, const is approx. 136)
      m_maxNoElements = m_maxSize; // (in byte)
            
      // create the hash table itself
      // set the number of buckets to 4*m_maxNoElements
      // number of buckets = 2^m_noBuckets
      //m_noBuckets = ceil(log2(m_maxNoElements / (0.5* noVariables * noClauses))) + 4;
      
      m_noBuckets = 20;
      
      if (memSize > 8000) {
        m_noBuckets = 26;
      }
      if (memSize > 20000) {
        m_noBuckets = 30;
      }

      //std::cout << "c Cache Control: determined maximum number of elements before cleanup to be " << m_maxNoElements << " Hash table has " << pow(2, m_noBuckets) << " buckets" << std::endl;
                 
      // create the vector
      m_hashTable.resize(pow(2, m_noBuckets), nullptr);   
    }
        
    // destructor
    ~CacheController();
    
    // prints the statistics
    void printStatistics();
    
    // getters for statistics
    uint_fast32_t tries();    
    uint_fast32_t hits();    
    uint_fast32_t forbiddenHits();
    
    // check if a cleanup of the cache is required
    bool isCleanupRequired();
    
    // performs a (cache) cleanup operation
    void cleanup(uint_fast32_t pos, uint_fast32_t noThreads);
    
    // removes all the elements marked as invalid from the cache
    void removeInvalid(uint_fast32_t pos, uint_fast32_t noThreads);
    
    // re-adds nodes that were removed during cleanup but should actually still be in the cache
    void reAddNodes();
        
    // add the node to the cache
    void add(ComponentTreeNode* node);
    
    // remove the stored result for the given node
    void invalidate(ComponentTreeNode* node);
    
    // checks if the cache contains a value for the given node
    // returns true on a cache hit and writes the result into the given variable modelCount
    bool lookup(ComponentTreeNode* node, mpz_class& modelCount);
    
    // adds the node the the count of nodes stored in the cache
    void countNode(ComponentTreeNode* node);
    
    // returns the precomputed hash values from the var / clause table
    uint64_t getHashForVar(uint_fast32_t var);    
    uint64_t getHashForClause(uint_fast32_t clause);
  private:
    
    // Copy constructor.
    CacheController (const CacheController&);
    
    // Assignment operator.
    CacheController& operator = (const CacheController&);
    
    // returns the access mutex responsible for the part of the cache in which bucket lies
    uint_fast32_t bucketToAccessMutex(uint_fast32_t bucket);
    
    // data structure to hold a cache element
    struct CacheElement {
      ComponentTreeNode* m_node;
      CacheElement* m_sibling;
      CacheElement* m_nextConflict;  
    };
    
    // invalidates the given CacheElement. 
    // reqires the current and previous elements of all iterators
    void invalidate (uint_fast32_t bucket, CacheElement* element, CacheElement* lastElement, CacheElement* currentSibling, CacheElement* lastSibling);
    
    // compute a hash for a given 64 bit input
    // based on 
    // http://stackoverflow.com/questions/5085915/what-is-the-best-hash-function-for-uint64-t-keys-ranging-from-0-to-its-max-value
    uint64_t computeHashForNumber(uint64_t u);    
    
    // the current number of elements stored in the cache
    std::atomic_uint_fast32_t m_noElements;
    
    // the maximum number of elements before a cleanup of the cache is triggered
    uint64_t m_maxNoElements;
    
    // the number of cache tries
    std::atomic_uint_fast32_t m_tries;
    
    // the number of cache hits
    std::atomic_uint_fast32_t m_hits;
        
    // stores how many components were removed from the cache again
    uint_fast32_t m_removedComponents;
    
    // the number of conflicts in the hash table
    uint_fast32_t m_hashConflicts;
    
    // vector to store the cache
    std::vector<CacheElement*> m_hashTable;
    
    // the number of buckets in the hash table
    uint64_t m_noBuckets;
    
    // the maximum cache size (in byte)
    uint64_t m_maxSize;
    
    // a list of nodes to re-add to the cache after cleanup is done
    std::vector<ComponentTreeNode*> m_nodesToKeep;
    
    // precomputed tables containing hash values for all variables and clauses
    std::vector<uint64_t>* m_varHashes;
    std::vector<uint64_t>* m_clauseHashes;
    
    std::vector<boost::mutex> m_accessMutexes;
    boost::mutex m_nodesToKeepMutex;
    std::vector<boost::mutex> m_accessMutexCleanup;
    boost::mutex m_countMutex;
    
    // stores whether to add laissez-faire dependencies or not
    bool m_addDependencies;
  };
  
  
  
  bool CompareComponentTreeNodes::operator()(const ComponentTreeNode* a, const ComponentTreeNode* b) const { 
    return *a < *b; 
  }

  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  // member functions NodeDependency
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  
  //
  bool NodeDependency::operator < (const NodeDependency& other) const
  {
    if (m_first == other.m_first) {
      return m_second < other.m_second;
    }
    return (m_first < other.m_first);
  }
  
  
  // comparison operator (used by unique to remove doubles)
  bool NodeDependency::operator ==(const NodeDependency& other) const {
    return (m_first == other.m_first && m_second == other.m_second);
  }
  
  
  
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  // member functions ComponentTreeNode
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  
  // -------------------------------------------------------------------------------
  ComponentTreeNode::~ComponentTreeNode () {
    
    m_state = 666;
    
    // remove any remaining dependencies
    for (auto dep: m_dependencies) {
      if (dep -> m_first == this) {
        if (dep -> m_second == nullptr) {
          delete dep;
        }
        else {
          dep -> m_first = nullptr;
        }
      }
      else {
        if (dep -> m_first == nullptr) {
          delete dep;
        }
        else {
          dep -> m_second = nullptr;
        }        
      }
    }
    m_dependencies.clear();
    
    // call the destructor of the children
    for (ComponentTreeNode* node : m_posBranch.m_children) {
      delete node;
    }
    for (ComponentTreeNode* node : m_negBranch.m_children) {
      delete node;
    }
    
    
    // delete the variable and clause tables if they're still present
    if (m_variables != nullptr) {
      delete m_variables;
      m_variables = nullptr;
    }
    if (m_clauses != nullptr) {
      delete m_clauses;
      m_clauses = nullptr;
    }    
  }
  
  // -------------------------------------------------------------------------------
  bool ComponentTreeNode::operator == (const ComponentTreeNode &b) const {
    
    // compare hashs first!
    if (m_hash != b.m_hash) {
      return false;
    }    
    
    #ifdef DEBUG_MODE
    //assert(m_variables != nullptr);
    //assert(m_clauses != nullptr);
    #endif
    
    //return (*m_variables == *b.m_variables && *m_clauses == *b.m_clauses);  
    return m_compressedVarClauses == b.m_compressedVarClauses;
  }
  
  // -------------------------------------------------------------------------------
  bool ComponentTreeNode::operator < (const ComponentTreeNode &b) const {
    
    // dirty hack to get the set to be ordered in descending order
    return m_decisionLevel > b.m_decisionLevel;
  }
  
  // -------------------------------------------------------------------------------
  ComponentTreeNode* ComponentTreeNode::parent ()  {
    return m_parent;
  }
  
  // -------------------------------------------------------------------------------
  std::vector<ComponentTreeNode*>* ComponentTreeNode::getPosChildren () {
    return &m_posBranch.m_children;
  }
  
  // -------------------------------------------------------------------------------
  std::vector<ComponentTreeNode*>* ComponentTreeNode::getNegChildren () {
    return &m_negBranch.m_children;
  }
  
  // -------------------------------------------------------------------------------
  bool ComponentTreeNode::isNodeUsedByOtherNodes () {
    
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif      
    
    return m_dependencies.size() > 0;
  }
  
  // -------------------------------------------------------------------------------
  uint_fast32_t ComponentTreeNode::decisionLevel () {
    return m_decisionLevel;
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::decisionVariable (uint_fast32_t var) {
    
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    // make sure no decision variable was previously set
    if (m_decisionVariable != 0) {
      std::cerr << "c ERROR: decision variable of state " << this << " is " << m_decisionVariable << std::endl;
    }
    assert(m_decisionVariable == 0);
    #endif
    m_decisionVariable = var;
  }
  
  // -------------------------------------------------------------------------------
  uint_fast32_t ComponentTreeNode::decisionVariable () {
    
    #ifdef DEBUG_MODE
    // make sure there is a decision variable set
    if (m_decisionVariable == 0) {
      std::cerr << "c ERROR: decision variable of state " << this << " is " << m_decisionVariable << std::endl;
    }
    assert(m_decisionVariable != 0);
    #endif
    return m_decisionVariable;
  }
  
  // -------------------------------------------------------------------------------
  bool ComponentTreeNode::neg () {
    
    return m_parentNeg;
  }
  
  // -------------------------------------------------------------------------------
  std::vector<uint_least32_t>* ComponentTreeNode::variables () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    assert(m_variables != nullptr);
    #endif
    return m_variables;
  }
  
  // -------------------------------------------------------------------------------
  std::vector<uint_least32_t>* ComponentTreeNode::clauses () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    assert(m_clauses != nullptr);
    #endif
    return m_clauses;
  }
  
  // -------------------------------------------------------------------------------
  uint_fast32_t ComponentTreeNode::state () {
    // DEBUG: make sure the node is locked before any operation
    // NOTE: this is violated when the cache is being cleaned!
    //#ifdef DEBUG_MODE
    //assert(!m_stateMutex.try_lock());
    //#endif
    
    return m_state;
  }
  
  // -------------------------------------------------------------------------------
  boost::mutex* ComponentTreeNode::getMutex () {
    return &m_stateMutex;
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::splitDone () {      
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    assert(!m_negBranch.m_split);
    #endif
    m_negBranch.m_split = true;
    #ifdef DEBUG_MODE
    assert(!m_posBranch.m_split);
    #endif
    m_posBranch.m_split = true;
    
    if (m_state < 10) {
      m_state = 10;
    }  
    
    checkDoneComputeModelCount();
  }
  
  // -------------------------------------------------------------------------------
  bool ComponentTreeNode::addPosChildren (std::vector<ComponentTreeNode*>& children) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    // if the node was aborted, don't add any children
    if (m_state >= 40) {
      return false;
    }
    
    for (ComponentTreeNode* child: children) {
      m_posBranch.m_children.push_back(child);
    }
    
    return true;
  }
  
  // -------------------------------------------------------------------------------
  bool ComponentTreeNode::addNegChildren (std::vector<ComponentTreeNode*>& children) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    // if the node was aborted, don't add any children
    if (m_state >= 40) {
      return false;
    }
    
    for (ComponentTreeNode* child: children) {
      m_negBranch.m_children.push_back(child);
    }
    
    return true;
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::childRemoved (ComponentTreeNode* child) {
    
    #ifdef DEBUG_MODE
    assert(m_state == 20 || m_state == 25);
    #endif
    
    // remove the child from the child lists
    m_posBranch.m_children.erase(std::remove(m_posBranch.m_children.begin(), m_posBranch.m_children.end(), child), m_posBranch.m_children.end());
    m_negBranch.m_children.erase(std::remove(m_negBranch.m_children.begin(), m_negBranch.m_children.end(), child), m_negBranch.m_children.end());
    
    // set the state to incomplete
    m_state = 25;      
  }
  
  // -------------------------------------------------------------------------------
  mpz_class ComponentTreeNode::modelCount () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    //assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    
    if (m_state < 20) {
      std::cerr << "c ERROR, " << this << " state is : " << m_state << std::endl;
      std::cerr << "c node " << this << " m_posBranch.children.size = " << m_posBranch.m_children.size() << std::endl;
      std::cerr << "c node " << this << " m_negBranch.children.size = " << m_negBranch.m_children.size() << std::endl;
      
      std::cerr << std::endl << std::endl;
      printChildStates(0);
    }
    
    
    // the model count is only valid when the node is done
    assert(m_state >= 20);
    #endif
    
    return m_modelCount;
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::addExtraImplication (uint_fast32_t imp) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    m_extraImplications.push_back(imp);
  }
  
  // -------------------------------------------------------------------------------
  std::vector<uint_fast32_t>* ComponentTreeNode::getExtraImplications () {
    
    return &m_extraImplications;
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::printChildStates (uint_fast32_t indent, bool neg) {
    
    if (m_state >= 20) {
      return;
    }
    
    std::cerr << "c ";
    for (uint_fast32_t i = 0; i < indent; i++) {
      std::cerr << " ";
    }
    std::cerr << (neg?"-":"+") << this << ": state = " << m_state << " mc = " << m_modelCount <<  " dl = " << m_decisionLevel << "(" << m_decisionVariable << ")"<< std::endl;
    
    for (uint_fast32_t i = 0; i < m_posBranch.m_children.size(); i++) {
      m_posBranch.m_children[i] -> printChildStates(indent+1,false);
    }
    
    for (uint_fast32_t i = 0; i < m_negBranch.m_children.size(); i++) {
      m_negBranch.m_children[i] -> printChildStates(indent+1,true);
    }
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::addDependency (NodeDependency* dep) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_OUTPUT
    assert(dep -> m_first != NULL && dep -> m_second != NULL);
    #endif
    
    m_dependencies.push_back(dep);      
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::sat (bool neg, std::vector<uint_fast8_t>* assignment) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    #ifdef DEBUG_MODE
    assert(m_variables != nullptr);
    assert(m_clauses != nullptr);
    #endif
    
    
    #ifdef DEBUG_OUTPUT
    std::cout << "c node " << this << " was informed that its " << (neg?"negative":"positive") << " branch is SAT" << std::endl;
    #endif
    
    // check for every variable of the component if its free
    uint_fast32_t freeVars(0);
    
    for (uint_fast32_t var : *m_variables) {
      if (!assignment -> at(var<<1) && !assignment -> at((var<<1)^1)) {
        freeVars++;
      }
    }
    
    mpz_class modelCount = pow(2, freeVars);
    
    if (!neg) {
      m_posBranch.m_modelCount = modelCount;
      m_posBranch.m_triviallyDone = true;
    }
    else {
      m_negBranch.m_modelCount = modelCount;
      m_negBranch.m_triviallyDone = true;
    }
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::sat (std::vector<uint_fast8_t>* assignment) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    #ifdef DEBUG_OUTPUT
    std::cout << "c node " << this << " was informed that it's SAT" << std::endl;
    #endif
    
    // check for every variable of the component if its free
    uint_fast32_t freeVars(0);
    
    for (uint_fast32_t var : *m_variables) {
      if (!assignment -> at(var<<1) && !assignment -> at((var<<1)^1)) {
        freeVars++;
      }
    }
    
    mpz_class modelCount;
    modelCount = pow(2, freeVars);
    
    // simply assign the model count to the pos branch
    
    m_posBranch.m_modelCount = modelCount;
    m_posBranch.m_triviallyDone = true;
    
    m_negBranch.m_modelCount = 0;
    m_negBranch.m_triviallyDone = true;
    
    #ifdef DEBUG_MODE
    assert(m_posBranch.m_children.empty());
    assert(m_negBranch.m_children.empty());
    #endif  
    
    assert(checkDoneComputeModelCount());
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::unsat (bool neg) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_OUTPUT
    std::cout << "c node " << this << " was informed that its " << (neg?"negative":"positive") << " branch is UNSAT" << std::endl;
    #endif
    
    // set the model count
    if (!neg) {
      m_posBranch.m_modelCount = 0;
      m_posBranch.m_triviallyDone = true;
      #ifdef DEBUG_MODE
      assert(m_posBranch.m_children.empty());
      #endif
    }
    else {
      m_negBranch.m_modelCount = 0;
      m_negBranch.m_triviallyDone = true;
      #ifdef DEBUG_MODE
      assert(m_negBranch.m_children.empty());
      #endif
    }
    
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::unsat () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    assert(m_variables != nullptr);
    assert(m_clauses != nullptr);
    #endif
    
    #ifdef DEBUG_OUTPUT
    std::cout << "c node " << this << " was informed that it's UNSAT" << std::endl;
    #endif
    
    
    // abort all children
    for (ComponentTreeNode* node : m_posBranch.m_children) {
      node -> abort();
      
      // store that the node can be deleted
      m_nodeManager -> addNodeToDelete(node);
    }
    for (ComponentTreeNode* node : m_negBranch.m_children) {
      node -> abort();
      
      // store that the node can be deleted
      m_nodeManager -> addNodeToDelete(node);
    }
    
    m_posBranch.m_children.clear();
    m_negBranch.m_children.clear();    
    
    // store that both the positive and the negative branch are unsat
    unsat(false);
    unsat(true);
    
    assert(checkDoneComputeModelCount());
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::childDone () {
    #ifdef DEBUG_OUTPUT
    std::cout << "c node " << this << " was informed that a child is done" << std::endl;
    #endif
    
    boost::mutex::scoped_lock l(m_stateMutex);
    
    // check if there are still children left
    checkDoneComputeModelCount();      
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::childUnsat (ComponentTreeNode* child) {      
    
    boost::mutex::scoped_lock l(m_stateMutex);
    // tell all the siblings to abort
    // lock each node before aborting to provide thread safety
    if (!child -> neg()) {
      for (ComponentTreeNode* node : m_posBranch.m_children) {
        // don't abort the unsat child
        if (node != child) {
          node -> abort();
        }
      }
    }
    else {
      for (ComponentTreeNode* node : m_negBranch.m_children) {
        // don't abort the unsat child
        if (node != child) {
          node -> abort();
        }
      }
    }
    
    // all done, compute the count for this component!
    checkDoneComputeModelCount();
  }
  
  // -------------------------------------------------------------------------------
  std::vector<ComponentTreeNode*> ComponentTreeNode::split (bool attemptSplit, std::vector< Clause* >* database, std::vector<uint_fast8_t>* assignment) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    assert(m_variables != nullptr);
    assert(m_clauses != nullptr);
    #endif
    
    #ifdef DEBUG_MODE
    // there should be a decision variable
    assert(m_decisionVariable != 0);
    
    // the decision variable should be assigned
    assert(assignment->at(m_decisionVariable << 1) || assignment -> at((m_decisionVariable << 1)^1));      
    #endif
    
    // determine if this is the positive or negative branch
    bool posBranch = assignment -> at(m_decisionVariable << 1);
    
    #ifdef DEBUG_MODE
    if (posBranch)  assert(!assignment -> at((m_decisionVariable << 1)^1));
    if (!posBranch) assert(assignment -> at((m_decisionVariable << 1)^1));
    #endif
    
    // the vector that will be returned
    std::vector<ComponentTreeNode*> foundComponents;
    
    if (m_variables -> size() < 4) { // BLACK MAGIC NUMBER
      attemptSplit = false;
    }
    
    // if there is no splitting required, just remove satisfied variables and clauses
    if (!attemptSplit) {
            
      std::vector<uint_least32_t>* nVariableTable = new std::vector<uint_least32_t>();
      std::vector<uint_least32_t>* nClauseTable = new std::vector<uint_least32_t>();      
      
      for (uint_least32_t var : *m_variables) {          
        if (!assignment -> at(var << 1) && !assignment -> at((var << 1)^1)) {
          nVariableTable -> push_back(var);
        }          
      }
      
      for (uint_least32_t clause : *m_clauses) {          
        // get the literals of the clause
        uint_fast32_t* lits = database -> at(clause) -> lits();
        
        // check if the clause is satisfied
        bool sat (false);
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          if (assignment -> at(lits[l])) {
            sat = true;
            break;
          }
        }
        // ignore satisfied clauses
        if (sat) {
          continue;
        }
        
        nClauseTable -> push_back(clause);
      }
      
      // if there is no clause left, the formula is satisfied
      if (nClauseTable -> empty()) {
        
        // clean up
        delete nVariableTable;
        delete nClauseTable;
        
        // add a nullptr component
        foundComponents.push_back(nullptr);
        return foundComponents;
      }
      
      ComponentTreeNode* newNode = new ComponentTreeNode(this, m_decisionLevel+1, posBranch^1, nVariableTable, nClauseTable, m_cacheController, m_nodeManager);
      
      // store the component in the return list
      foundComponents.push_back(newNode);
      
      return foundComponents;
    }
    
    
    // a table which contains the current component for each variable (0 = no component)
    std::vector<uint_fast32_t> inComponent((assignment -> size() >> 1) + 1, 0);
    
    // the current component number
    uint_fast32_t componentCount (0);      
    
    // look at each clause of the component
    for (uint_least32_t clause : *m_clauses) {
      
      // get the literals of the clause
      uint_fast32_t* lits = database -> at(clause) -> lits();
      
      // (1) check if the clause is satisfied, also store the first non-zero component
      bool sat (false);
      uint_fast32_t component1(0);
      for (uint_fast32_t l = 0; lits[l] != 0; l++) {
        if (assignment -> at(lits[l])) {
          sat = true;
          break;
        }
        
        if (component1 == 0 && inComponent[lits[l] >> 1] != 0 && !assignment -> at(lits[l]^1)) {
          component1 = inComponent[lits[l] >> 1];
        }
      }
      // ignore satisfied clauses
      if (sat) {
        continue;
      }
      
      
      // (2.1) if all the variables are in no component, create a new one
      if (component1 == 0) {
        // new component 
        componentCount++;   
        
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          // check if the literal is still part of the component
          if (!assignment -> at(lits[l]^1)) {
            inComponent[lits[l] >> 1] = componentCount;
          }
        }          
      }        
      // (2.2) ensure that all the literals are in component1
      else {
        for (uint_fast32_t l = 0; lits[l] != 0; l++) {
          // check if the literal is still part of the component
          if (!assignment -> at(lits[l]^1)) {
            
            uint_fast32_t var = lits[l] >> 1;
            
            // variable is not part of a component yet
            if (inComponent[var] == 0) {
              inComponent[var] = component1;
            }
            // variable is part of a different component -> join component
            else if (inComponent[var] != component1) {
              
              uint_fast32_t component2 = inComponent[var];
              
              // run through the whole component table and replace all occurences of component2 by component1
              for (uint_least32_t v : *m_variables) {
                if (inComponent[v] == component2) {
                  inComponent[v] = component1;
                }
              }   
            }
          }            
        }
      }
    }
    
    // when no component was created all the clauses are satisfied
    if (componentCount == 0) {
      
      // add a nullptr component
      foundComponents.push_back(nullptr);
      return foundComponents;
    }
    
    
    // at this point, inComponent contains the splitting into components
    // however, not all values between 1 and componentCount still represent components (many are empty due to joining)
    
    // the new tables
    std::vector < std::vector <uint_least32_t>* > newVariableTables (componentCount + 1, nullptr);
    std::vector < std::vector <uint_least32_t>* > newClauseTables (componentCount + 1, nullptr);
    
    // look at each variable of the original component
    // if the variable is not part of any component (and not assigned) it is free
    
    for (uint_least32_t var : *m_variables) {
      
      // ignore variables that are assigned
      if (assignment -> at(var << 1) || assignment -> at((var << 1) ^1)) {
        #ifdef DEBUG_MODE
        // assigned variables should not be part of any component
        assert(inComponent[var] == 0);
        #endif
        continue;
      }
      
      // not in component? -> free
      if (inComponent[var] == 0) {
        // check if this is the positive or negative branch
        if (posBranch) {
          // positive branch
          m_posBranch.m_freeVariables++;
        }
        else {
          // negative branch
          m_negBranch.m_freeVariables++;
        }
        continue;
      }
      
      // add to the corresponding component
      // check if the tables have to be created
      if (newVariableTables.at(inComponent[var]) == nullptr) {
        newVariableTables.at(inComponent[var]) = new std::vector<uint_least32_t>();
        // also create the clause table, it will be required in the next step anyway
        newClauseTables.at(inComponent[var]) = new std::vector<uint_least32_t>();
      }
      
      // add to the corresponding table
      newVariableTables.at(inComponent[var]) -> push_back(var); 
    }
    
    // look at each clause of the component
    for (auto clause : *m_clauses) {
      
      // get the literals of the clause
      uint_fast32_t* lits = database -> at(clause) -> lits();
      
      
      // (1) check if the clause is satisfied, also store the first non-zero component
      bool sat (false);
      uint_fast32_t component1(0);
      for (uint_fast32_t l = 0; lits[l] != 0; l++) {
        
        if (assignment -> at(lits[l])) {
          
          // clause is satisfied
          sat = true;
          break;
        }
        
        // get the first non-zero component
        if (component1 == 0 && inComponent[lits[l] >> 1] != 0 && !assignment -> at(lits[l]^1)) {
          component1 = inComponent[lits[l] >> 1];
        }
      }
      // ignore satisfied clauses
      if (sat) {
        continue;
      }
      
      #ifdef DEBUG_MODE
      assert(component1 != 0);        
      
      assert(newVariableTables.at(component1) != nullptr);  
      // (2) add the clause to the corrent component
      assert(newClauseTables.at(component1) != nullptr);
      #endif
      
      newClauseTables.at(component1) -> push_back(clause);  
    }
    
    
    
    // look at each component class
    for (uint_fast32_t cc = 1; cc <= componentCount; cc++) {
      
      // ignore components that are gone
      if (newVariableTables[cc] == nullptr) {
        continue;
      }
      
      ComponentTreeNode* newNode = new ComponentTreeNode(this, m_decisionLevel+1, posBranch^1, newVariableTables[cc], newClauseTables[cc], m_cacheController, m_nodeManager);
      
      // store the component in the return list
      foundComponents.push_back(newNode);    
    }
    
    return foundComponents;            
  }
  
  // -------------------------------------------------------------------------------
  uint64_t ComponentTreeNode::getHash () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    #ifdef DEBUG_MODE
    assert(m_hash != 0);
    #endif
    return m_hash;
  }
  
  // -------------------------------------------------------------------------------
  uint_fast32_t ComponentTreeNode::getSize () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    // dynamic size: variables + clauses + dependencies + branch children
    //uint_fast32_t size = m_variables -> size() + m_clauses -> size() + m_dependencies.size() + m_posBranch.m_children.size() + m_negBranch.m_children.size();
    
    
    // fast heuristic: assume 2 children per branch + 2 dependencies
    if (m_variables != nullptr && m_clauses != nullptr) {
      // multiply size by 8 (64 bit pointers) to get size in byte
      return (m_variables -> size() + m_clauses -> size() + 2+2+2) << 3;
    }
    else {
      return (m_compressedVarClauses.size() +2+2+2) << 3;      
    }
    
  }
  
  // -------------------------------------------------------------------------------
  template<class ForwardIt, class BinaryPredicate>
  ForwardIt ComponentTreeNode::uniqueClean (ForwardIt first, ForwardIt last, BinaryPredicate p, ComponentTreeNode* originator) {
    if (first == last)
      return last;
    
    ForwardIt result = first;
    while (++first != last) {
      if (!p(*result, *first)) {
        *(++result) = *first;
      }
      else {
        
        // tell first it will be deleted, or delete the dependencie if it is already invalidated by the other node
        if ((*first) -> m_first == originator) {
          
          if ((*first) -> m_second == nullptr) {
            delete *first;
          }
          else {         
            (*first) -> m_first = nullptr;
          }
        }
        else {
          
          if ((*first) -> m_first == nullptr) {
            delete *first;
          }
          else {
            (*first) -> m_second = nullptr;
          }
        }
        
      }
    }
    return ++result;
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::fixDependencies () {
        
    // (1) sort
    std::sort( m_dependencies.begin(), m_dependencies.end(), [](NodeDependency* x, NodeDependency* y) {return *x < *y;} );
    // (2) remove doublicates
    m_dependencies.erase( uniqueClean( m_dependencies.begin(), m_dependencies.end(), [](NodeDependency*x, NodeDependency* y) {return *x == *y;} , this), m_dependencies.end());
    // (3) remove all invalid dependencies
    m_dependencies.erase( std::remove_if(std::begin(m_dependencies), std::end(m_dependencies), [&](NodeDependency* x) {
      // possibilities:
      // (this, other)       <-- keep
      // (this, nullptr)     <-- first if
      // (nullptr, this)     <-- second if
      // (nullptr, nullptr)  <-- should not occur
      if (x -> m_first == this) {
        if (x -> m_second == nullptr) {
          delete x;
          return true;
        }
      }
      else if (x -> m_first == nullptr) {
        delete x;
        return true;
      }
      return false;
      
    }), m_dependencies.end());
  }
  
  // -------------------------------------------------------------------------------
  bool ComponentTreeNode::checkDoneComputeModelCount () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    if (m_state >= 20) {
      return true;
    }
    
    
    if (!m_posBranch.m_split && !m_posBranch.m_triviallyDone) {
      #ifdef DEBUG_OUTPUT
      std::cout << "c node " << this << " is not done because (pos branch) split = " << m_posBranch.m_split << " && tDone = " << m_posBranch.m_triviallyDone << std::endl;
      #endif
      return false;
    }
    if (!m_negBranch.m_split && !m_negBranch.m_triviallyDone) {
      #ifdef DEBUG_OUTPUT
      std::cout << "c node " << this << " is not done because (neg branch) split = " << m_negBranch.m_split << " && tDone = " << m_negBranch.m_triviallyDone << std::endl;
      #endif
      return false;
    }
    
    
    #ifdef DEBUG_OUTPUT
    std::cout << "c node " << this << " is done" << std::endl;
    #endif
    
    
    // compute the final model count while checking if the component is done
    // pos:
    if (!m_posBranch.m_children.empty()) {
      {  
        boost::mutex::scoped_lock l(*m_posBranch.m_children[0] -> getMutex());
        
        if (m_posBranch.m_children[0] -> state() < 20)  {return false;}
        // first element as initialization
        m_posBranch.m_modelCount = m_posBranch.m_children[0] -> modelCount();
      }
      
      
      // all other elements
      for (uint_fast32_t i = 1; i < m_posBranch.m_children.size(); i++) {
        {  
          boost::mutex::scoped_lock l(*m_posBranch.m_children[i] -> getMutex());
          
          if (m_posBranch.m_children[i] -> state() < 20)  {return false;}
          m_posBranch.m_modelCount *= m_posBranch.m_children[i] -> modelCount();
        }
      }
      
      
      // free variables
      m_posBranch.m_modelCount *= pow(2, m_posBranch.m_freeVariables);
    }
    
    // neg:
    if (!m_negBranch.m_children.empty()) {
      {  
        boost::mutex::scoped_lock l(*m_negBranch.m_children[0] -> getMutex());
        
        if (m_negBranch.m_children[0] -> state() < 20)  {return false;}
        // first element as initialization
        m_negBranch.m_modelCount = m_negBranch.m_children[0] -> modelCount();
      }
      
      
      // all other elements
      for (uint_fast32_t i = 1; i < m_negBranch.m_children.size(); i++) {
        {  
          boost::mutex::scoped_lock l(*m_negBranch.m_children[i] -> getMutex());
          
          if (m_negBranch.m_children[i] -> state() < 20)  {return false;}
          m_negBranch.m_modelCount *= m_negBranch.m_children[i] -> modelCount();
        }
      }
      
      // free variables
      m_negBranch.m_modelCount *= pow(2,  m_negBranch.m_freeVariables);
    }
    
    assert(m_state < 20);
    
    #ifdef DEBUG_OUTPUT
    if (m_modelCount != 0) {
      std::cout << "c node " << this << " resetting model count from " << m_modelCount << " to " << m_posBranch.m_modelCount + m_negBranch.m_modelCount << std::endl;
    }
    #endif
    
    // set the state as done and compute its final model count
    m_state = 20;
    m_modelCount = m_posBranch.m_modelCount + m_negBranch.m_modelCount;
    
    // add the node to the cache
    
//     if (m_variables -> size() > 15) {
    if (m_modelCount != 0) {
      
      m_cacheController->add(this);
    }
//     }

#ifdef DEBUG_MODE
assert(m_variables != nullptr);
assert(m_clauses != nullptr);
#endif  
    finalize();
    
    return true;
  }
  
  
  // -------------------------------------------------------------------------------
  uint_fast32_t ComponentTreeNode::removeFromTree () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    // if the state is already removed from the tree there is nothing to do
    if (m_state == 50) {
      return 0;
    }
    
    // counter to store the number of removed nodes (initialized with 1 for the current node
    uint_fast32_t removedNodes(1);
       
    for (auto c : m_posBranch.m_children) {
      boost::mutex::scoped_lock l(*c -> getMutex());
      removedNodes += c -> removeFromTree();
    }
    
    for (auto c : m_negBranch.m_children) {
      boost::mutex::scoped_lock l(*c -> getMutex());
      removedNodes += c -> removeFromTree();
    }
   
    m_posBranch.m_children.clear();
    m_negBranch.m_children.clear();
    
    
    // now this node is a leaf in the (incomplete) search tree
    // move all dependencies to the parent node
    // NOTE: parent is assumed to be locked!
    
    if (m_parent != nullptr) {
      for (NodeDependency* dep: m_dependencies) {
        // lock
        boost::mutex::scoped_lock l(dep -> m_mutex);
        
        // check if the dependency is still valid, otherwise delete it
        if (dep -> m_first == nullptr || dep -> m_second == nullptr) {
          l.unlock();
          delete dep;
          continue;
        }
        
        // update the dependency
        if (dep -> m_first == this) {
          dep -> m_first = m_parent;
        }
        else {
          #ifdef DEBUG_MODE
          assert(dep -> m_second == this);
          #endif
          dep -> m_second = m_parent;
        }
        
        // if the dependency became a dependency of the parent on itself, invalidate it
        // the parent will delete it on the next access
        if (dep -> m_first == dep -> m_second) {
          dep -> m_second = nullptr;
          continue;
        }
        
        // and add to parent        
        m_parent -> addDependency(dep);
      }
      
      m_dependencies.clear();
    }
    
    // mark the node as invalid
    m_state = 50;
    
    // and add it to the "to delete" list
    m_nodeManager -> addNodeToDelete(this);
    
    return removedNodes;
  }
  
  // -------------------------------------------------------------------------------
  bool ComponentTreeNode::cacheLookup () {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    mpz_class res(0);
    
    if (m_cacheController->lookup(this, res)) {
      
      #ifdef DEBUG_MODE
      // the branches should not be split prior to a cache hit
      assert(m_posBranch.m_children.empty());
      assert(m_negBranch.m_children.empty());
      assert(m_state == 0);
      #endif
      
      // set the state to "cache hit"
      m_state = 30;
      
      // set the model count
      m_modelCount = res;
      
      #ifdef DEBUG_MODE
      assert(m_variables != nullptr);
      assert(m_clauses != nullptr);
      #endif  
      finalize();
            
      // the node is in the cache
      return true;
    }
    
    return false;
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::computeHash () {
    
    uint64_t res = 0;
    
    // compute the result as the XOR of all hashes for the variables and clauses
    
    for (uint_least32_t var: *m_variables) {
      res^= m_cacheController -> getHashForVar(var);
    }
    for (uint_least32_t clause: *m_clauses) {
      res^= m_cacheController -> getHashForClause(clause);
    }
    
    m_hash = res;
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::abort () {
    
    {
      // lock the state
      boost::mutex::scoped_lock l(m_stateMutex);
      
      // if the state is already aborted, ignore it
      if (m_state == 40) {
        return;
      }
      
      
      // if the state is done, remove it from the cache
      if (m_state == 20 || m_state == 25) {
        if (m_modelCount != 0) {
          m_cacheController -> invalidate(this);
        }
        // now inform all the nodes that used the node's cache result that they have to be re-computed
        
        informDependenciesWrongValue();
      }
      if (m_state == 30) {
        // the node was a cache hit
        // inform all dependencies that the node is no longer dependent
        for (NodeDependency* dep: m_dependencies) {
          
          // lock
          boost::mutex::scoped_lock ld(dep -> m_mutex);
          
          // check if the dependency should be deleted
          if (dep -> m_first == nullptr || dep -> m_second == nullptr) {
            ld.unlock();
            delete dep;
            continue;
          }
          
          if (dep -> m_first == this) {
            dep -> m_first = nullptr;
          }
          else {
            #ifdef DEBUG_MODE
            assert(dep -> m_second == this);
            #endif
            dep -> m_second = nullptr;
          }
        }
        
        // clear the list of dependencies
        m_dependencies.clear();
      }
      
      // store that the state was aborted
      m_state = 40;
      
      // tell the sub-components that they have to abort the computation or invalidate their cache results
      for (ComponentTreeNode* node : m_posBranch.m_children) {
        node -> abort();
        
        // store that the node can be deleted
        m_nodeManager -> addNodeToDelete(node);
      }
      for (ComponentTreeNode* node : m_negBranch.m_children) {
        node -> abort();
        
        // store that the node can be deleted
        m_nodeManager -> addNodeToDelete(node);
      }
      
      // empty the lists
      m_posBranch.m_children.clear();
      m_negBranch.m_children.clear();
      
      
      // the tables are no longer required
      delete m_variables;
      delete m_clauses;
      m_variables = nullptr;
      m_clauses = nullptr;
            
    }
  }  
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::decompressVarClauses() {
    
    //std::cout << "c " << this << ": decompressVarClauses" << std::endl;
    
    #ifdef DEBUG_MODE
    // variables and clauses should have  been erased
    assert(m_variables == nullptr);
    assert(m_clauses== nullptr);
    #endif
    
    m_variables = new std::vector<uint_least32_t>();
    m_clauses = new std::vector<uint_least32_t>();
    
    // stores whether clauses are received
    bool clauses(false);
    // stores whether there are compressed variables / clauses to add
    bool compressed(false);
    // stores the last none compressed value
    uint_least32_t lastNonCompressedVal(0);
    
    // iterate through the compressed vector
    for (auto c: m_compressedVarClauses) {
      
      // switch from variables to clauses
      if (c == std::numeric_limits<uint_least32_t>::max() - 1) {
        clauses = true;
      }
      // compression marker
      else if (c == std::numeric_limits<uint_least32_t>::max()) {
        compressed = true;
      }
      // value
      else {
        // check if the value was compressed 
        if (compressed) {
          // compressed -> add the whole range
          for (uint_least32_t val = lastNonCompressedVal + 1; val < c; val++) {
            if (clauses) {
              m_clauses -> push_back(val);
            }
            else {
              m_variables -> push_back(val);
            }
          }
          compressed = false;
        }
        
        // add the current value
        if (clauses) {
          m_clauses -> push_back(c);
        }
        else {
          m_variables -> push_back(c);
        }
        
        // and store the last handled value
        lastNonCompressedVal = c;
      }      
    }    
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::finalize () {
    
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    assert(m_variables != nullptr);
    assert(m_clauses != nullptr);
    #endif    
    
    // inform the parent that a child is done
    if (m_parent != nullptr) {
      // if the model count is 0, this component is unsat
      if (m_modelCount == 0) {
        m_nodeManager -> nodeChildDone(m_parent, this);
      }
      else {
        #ifdef DEBUG_MODE
        assert(m_state != 40);
        #endif
        
        m_nodeManager -> nodeChildDone(m_parent, nullptr);
      }
    }
    
    // compress the database
    delete m_variables;
    delete m_clauses;
    m_variables = nullptr;
    m_clauses = nullptr;
  }

  // -------------------------------------------------------------------------------
  void ComponentTreeNode::cacheValueIncorrect (){      
    
    // lock the node which has to be reset 
    boost::mutex::scoped_lock l(m_stateMutex);
    
    // if the node was aborted it does not require any recomputation
    if (m_state >= 40) {
      m_nodeManager -> nodeToRecomputeWrongCacheDone(this);
      return;
    }
    // if the node has been reset anyway it does not require any additional recomputation
    if (m_state == 0) {
      m_nodeManager -> nodeToRecomputeWrongCacheDone(this);
      return;
    }
    
    // if the node depends on a wrong cache result or is currently waiting for other children, inform it that a child was wrong
    if (m_state == 10 || m_state == 25) {
      
      l.unlock();
      hasIncorrectChild();
      
      // since the node was stored as one with an incorrect cache result, it has to be removed from this list as well
      m_nodeManager -> nodeToRecomputeWrongCacheDone(this);
      
      return;
    }
    
    #ifdef DEBUG_MODE
    // the node must have been a cache hit
    if (m_state != 30) {
      std::cerr << "c ERROR: cacheValueIncorrect state of " << this << " is " << m_state << std::endl;
    }
    assert(m_state == 30);
    #endif
    
    // set the clause and variable table
    //m_variables = variables;
    //m_clauses = clauses;
    
    // restore the variable and clause vectors
    decompressVarClauses();
    
    // set the state to 0
    m_state = 0;    
    
    // inform all dependencies that this nodes result is incorrect
    informDependenciesWrongValue();
    
    // re-add the node
    m_nodeManager -> addNode(this, true);
    
    // tell the node manager that the parent state has a child with invalid result
    m_nodeManager -> nodeHasIncorrectChild(m_parent);      
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::hasIncorrectChild () {
    // lock the node
    boost::mutex::scoped_lock l(m_stateMutex);
    
    // if the node is not finished yet, no update needs to happen
    if (m_state < 20) {
      // inform the node manager
      m_nodeManager -> nodeHasIncorrectChildDone(this);
      return;
    }
    
    // if the node is done, remove its value from the cache, reset its state and inform its parent
    // also inform all the nodes who used the cached result
    if (m_state == 20) {
      
      // store the old model count
      mpz_class modelCountOld = m_modelCount;
      
      // extract the database
      decompressVarClauses();
      
      // reset the state
      m_state = 10;
      
      // remove from cache
      m_cacheController -> invalidate(this);
      
      // if the child was already recomputed and has the same mc the parents don't need to be changed (and the value in the cache can still be used)
      //bool mcStillValid(false);
      
      // check if the child is done already again (this is important, otherwise the solver might get stuck!)
      // checkDoneComputeModelCount resets the state to 20 and re-adds to the node
      if (checkDoneComputeModelCount()) {
        if (m_modelCount == modelCountOld) {
          //mcStillValid = true;
          // done here :)
          // inform the node manager
          m_nodeManager -> nodeHasIncorrectChildDone(this);
          return;
        }          
      }
      
      
      // if the model count is not valid anymore, inform the parent and all nodes using the cached result
      //if (!mcStillValid) {
      
      // inform the dependencies that the node had an incorrect result
      informDependenciesWrongValue();
      
      // inform the parent
      m_nodeManager -> nodeHasIncorrectChild(m_parent);
      
      // NOTE: the nodes is now again in the state "waiting for children" which does not require any more interaction with the solver
      // therefore it MUST NOT be re-added to the node manager
      // simply tell the node manager, that it is no longer part of the "in progress" list
      m_nodeManager -> nodeHasIncorrectChildDone(this);
      // and recompute
      //m_nodeManager -> addNode(this, true);
      //}
    }
    // when the node needs to be recomputed, set its state to 0 and reset the decision var
    else if(m_state == 25) {
      
      
      // restore the variable and clause vectors
      decompressVarClauses();
      
      // reset the state
      m_state = 0;
      
      // the decision variable
      m_decisionVariable = 0;
      
      // and the branches      
      // abort first
      for (ComponentTreeNode* n : m_posBranch.m_children) {
        n -> abort();
        
        // store that the node can be deleted
        m_nodeManager -> addNodeToDelete(n);
      }
      for (ComponentTreeNode* n : m_negBranch.m_children) {
        n -> abort();
        
        // store that the node can be deleted
        m_nodeManager -> addNodeToDelete(n);
      }
      
      m_posBranch.m_children.clear();
      m_posBranch.m_split = false;
      m_posBranch.m_freeVariables = 0;
      m_posBranch.m_triviallyDone = false;
      
      m_negBranch.m_children.clear();
      m_negBranch.m_split = false;
      m_negBranch.m_freeVariables = 0;
      m_negBranch.m_triviallyDone = false;
      
      // remove from cache
      m_cacheController -> invalidate(this);
      
      
      
      // inform the dependencies that the node had an incorrect result
      informDependenciesWrongValue();
      
      // inform the parent
      m_nodeManager -> nodeHasIncorrectChild(m_parent);
      
      // and recompute (which tells the node manager that the node is done)
      m_nodeManager -> addNode(this, true);
    }
    else {
      // node was aborted or was not done yet anyway
      
      #ifdef DEBUG_MODE
      assert(m_state == 0 || m_state == 10 || m_state == 40);
      #endif
      // inform the node manager
      m_nodeManager -> nodeHasIncorrectChildDone(this);
    }
    
  }
  
  // -------------------------------------------------------------------------------
  void ComponentTreeNode::informDependenciesWrongValue () {
    
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!m_stateMutex.try_lock());
    #endif
    
    // inform all nodes that used this nodes cached value
    for (NodeDependency* dep: m_dependencies) {
      // lock
      boost::mutex::scoped_lock l(dep -> m_mutex);
      
      // check if the depedency is invalid
      if (dep -> m_first == nullptr || dep -> m_second == nullptr) {
        
        l.unlock();
        delete dep;
        
        continue;
      }
      
      // notify all nodes tht depend on this node -> dep -> second = this        
      if (dep -> m_second == this) {
        
        m_nodeManager -> addNodeToRecomputeWrongCache(dep -> m_first);
        
        // invalidate the depedency
        dep -> m_second = nullptr;              
      }
      else {
        #ifdef DEBUG_MODE
        assert(dep -> m_first == this);
        #endif          
        // this is a dependency the current node depended on -> simply invalidate
        dep -> m_first = nullptr;
      }
      // at this point the dependency is invalid, but not deleted. It will be deleted by the other node on the next access            
    }
    
    // clear the list of dependencies
    m_dependencies.clear();
  }
  
  
  
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  // member functions NodeManager
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  
  // -------------------------------------------------------------------------------
  boost::mutex* NodeManager::getNodeMutex () {
    return &m_nodeMutex;
  }
  
  // -------------------------------------------------------------------------------
  uint_fast32_t NodeManager::getNoNodes() {
    return m_nodesSize;
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::assertReadyForCleanup() {
    {      
      std::pair<ComponentTreeNode*, ComponentTreeNode*>* node;
      assert(!m_nodesChildDone.pop(node));
    }
    
    {
      boost::mutex::scoped_lock l(m_nodesRecomputeMutex);
      
      assert(m_nodesChildIncorrect.empty());
      assert(m_nodesChildIncorrectInProgress.empty());
      assert(m_nodesRecomputeWrongCache.empty());
      assert(m_nodesRecomputeWrongCacheInProgress.empty());
    }
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::addNodeToDelete (ComponentTreeNode* node) {
    
    // lock
    //boost::mutex::scoped_lock l(m_nodesToDeleteMutex);
    
    m_nodesToDelete.push(node);
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::deleteNodes () {
    
    ComponentTreeNode* node;
    
    // delete nodes from the lockfree stack
    while(m_nodesToDelete.pop(node)) {
      delete node;
    }
    
    /*
    // lock
    //boost::mutex::scoped_lock l(*m_nodesToDeleteMutex);
    
    double fac = 1 / (double) maxPos;
    
    uint_fast32_t start = fac*pos*m_nodesToDelete.size();
    uint_fast32_t end = fac*(pos+1)*m_nodesToDelete.size();
    
    //std::cout << "c node manager: delete nodes" << start << " to " << end << std::endl;
    
    
    // delete
    for (uint_fast32_t i = start; i < end; i++) {
      delete m_nodesToDelete[i];
    }
    */
  }
  
  // -------------------------------------------------------------------------------
  uint_fast32_t NodeManager::getNoNodesToDelete() {
    //return m_nodesToDelete.size();
    return 0;
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::clearNodesToDelete () {
    //m_nodesToDelete.clear();
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::nodeChildDone (ComponentTreeNode* node, ComponentTreeNode* unsatChild) {
    {      
      m_nodesChildDone.push(new std::pair <ComponentTreeNode*, ComponentTreeNode*>(node, unsatChild));
    }
  }
  
  // -------------------------------------------------------------------------------
  std::pair <ComponentTreeNode*, ComponentTreeNode*>* NodeManager::getNodeUnsatChild() {
          
      // return the last node / unsat child pair in the vector
      std::pair <ComponentTreeNode*, ComponentTreeNode*>* ret;
      if (!m_nodesChildDone.pop(ret)) {
                
        return nullptr;
      }
      return ret;
    
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::nodeToRecomputeWrongCacheDone (ComponentTreeNode* node) {
    
    // lock
    boost::mutex::scoped_lock l(m_nodesRecomputeMutex);
    
    m_nodesRecomputeWrongCacheInProgress.erase(std::remove(std::begin(m_nodesRecomputeWrongCacheInProgress), std::end(m_nodesRecomputeWrongCacheInProgress), node), std::end(m_nodesRecomputeWrongCacheInProgress));
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::nodeHasIncorrectChildDone (ComponentTreeNode* node) {
    
    // lock
    boost::mutex::scoped_lock l(m_nodesRecomputeMutex);
    
    m_nodesChildIncorrectInProgress.erase(std::remove(std::begin(m_nodesChildIncorrectInProgress), std::end(m_nodesChildIncorrectInProgress), node), std::end(m_nodesChildIncorrectInProgress));
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::nodeHasIncorrectChild (ComponentTreeNode* node) {
    
    // lock
    boost::mutex::scoped_lock l(m_nodesRecomputeMutex);
    
    // check if the node is in any of the recomputation lists
    for (auto nodeInProgress: m_nodesRecomputeWrongCacheInProgress) {
      if (nodeInProgress == node) {         
        //std::cout << "c nodeHasIncorrectChild: node currently in progress w/ wrong cache (" << node << ")!" << std::endl;          
        return;
      }
    }
    for (auto nodeInProgress: m_nodesChildIncorrectInProgress) {
      if (nodeInProgress == node) {         
        //std::cout << "c nodeHasIncorrectChild: node currently in progress w/ wrong child (" << node << ")!" << std::endl;          
        return;
      }
    }
    
    // check if the node already is in the cache incorrect list
    for (auto nodeInProgress: m_nodesRecomputeWrongCache) {
      if (nodeInProgress == node) {         
        //std::cout << "c nodeHasIncorrectChild: node already in list (" << node << ")!" << std::endl;          
        return;
      }
    }
    for (auto nodeInProgress: m_nodesChildIncorrect) {
      if (nodeInProgress == node) {         
        //std::cout << "c nodeHasIncorrectChild: node already in list (" << node << ")!" << std::endl;          
        return;
      }
    }
    
    m_nodesChildIncorrect.push_back(node);
  }
  
  // -------------------------------------------------------------------------------
  ComponentTreeNode* NodeManager::getNodeWithIncorrectChild () {
    
    // if another node is currently blocking the computation, do not wait for it, simply return
    if (!(m_nodesRecomputeMutex).try_lock()) {
      return nullptr;
    }
    
    // check if there is a node available
    if (m_nodesChildIncorrect.empty()) {
      
      // no nodes available -> unlock and return
      (m_nodesRecomputeMutex).unlock();      
      return nullptr;
    }
    
    // get the last node
    ComponentTreeNode* node = m_nodesChildIncorrect.back();
    m_nodesChildIncorrect.pop_back();
    
    // add the node to the in progress list
    m_nodesChildIncorrectInProgress.push_back(node);
    
    (m_nodesRecomputeMutex).unlock();      
    return node;      
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::addNodeToRecomputeWrongCache (ComponentTreeNode* node) {
    
    boost::mutex::scoped_lock l(m_nodesRecomputeMutex);
    
    // make sure the node is not already in the recomputation list
    for (auto nodeToAdd: m_nodesRecomputeWrongCache) {
      if(nodeToAdd == node) {      
        return;          
      }
    }
    
    for (auto nodeInProgress: m_nodesRecomputeWrongCacheInProgress) {
      if (nodeInProgress == node) {                 
        return;
      }
    }
    
    // remove the node from the wrong child list
    m_nodesChildIncorrect.erase(std::remove(std::begin(m_nodesChildIncorrect), std::end(m_nodesChildIncorrect), node), std::end(m_nodesChildIncorrect));   
    
    /*
    // what do we do when the node is in the "wrong child in progress" list???
    // simply add, cache value incorrect can handle that now
    assert(std::find(std::begin(m_nodesChildIncorrectInProgress), std::end(m_nodesChildIncorrectInProgress), node) == std::end(m_nodesChildIncorrectInProgress));
    */
    
    m_nodesRecomputeWrongCache.push_back(node);
  }
  
  // -------------------------------------------------------------------------------
  ComponentTreeNode* NodeManager::getNodeToRecomputeWrongCache () {
    if (!(m_nodesRecomputeMutex).try_lock()) {
      return nullptr;
    }
    //boost::mutex::scoped_lock l(m_nodesRecomputeMutex);
    
    // check if there is a node available
    if (m_nodesRecomputeWrongCache.empty()) {
      (m_nodesRecomputeMutex).unlock();     
      return nullptr;
    }
    
    // get the last node
    ComponentTreeNode* node = m_nodesRecomputeWrongCache.back();
    m_nodesRecomputeWrongCache.pop_back();
    
    // store that the node is currently beeing worked upon
    m_nodesRecomputeWrongCacheInProgress.push_back(node);            
    
    (m_nodesRecomputeMutex).unlock();     
    return node;      
  }
  
  // -------------------------------------------------------------------------------
  void NodeManager::addNode (ComponentTreeNode* node, bool removeFromInProgress) {
    
    if (removeFromInProgress) {
      
      boost::mutex::scoped_lock ll(m_nodesRecomputeMutex);
      m_nodesRecomputeWrongCacheInProgress.erase( std::remove( std::begin(m_nodesRecomputeWrongCacheInProgress), std::end(m_nodesRecomputeWrongCacheInProgress), node), std::end(m_nodesRecomputeWrongCacheInProgress) ); 
      m_nodesChildIncorrectInProgress.erase(std::remove(std::begin(m_nodesChildIncorrectInProgress), std::end(m_nodesChildIncorrectInProgress), node), std::end(m_nodesChildIncorrectInProgress));        
    }
    
    
    boost::mutex::scoped_lock l(m_nodeMutex);
    //m_nodes.push(node);      
    m_nodes.insert(node);      
    
    //m_nodesSize++;
  }
  
  // -------------------------------------------------------------------------------
  ComponentTreeNode* NodeManager::getNode () {
    
    
    // NOTE: this assumes that m_nodes is already externally locked!
    //#ifdef DEBUG_MODE
    //assert(!*m_nodeMutex.try_lock());
    //#endif
    
    // check if there is a node available
    if (m_nodes.empty()) {
      return nullptr;
    }
    
    m_globalNodeListAccesses++;
    
    // get the first node
    ComponentTreeNode* node = *m_nodes.begin();
    m_nodes.erase(m_nodes.begin());
    
    /*
    ComponentTreeNode* node;
    
    if (!m_nodes.pop(node)) {
      
      return nullptr;
    }
    
    m_nodesSize--;
    */
    
    return node;
  }
  
  // -------------------------------------------------------------------------------
  bool NodeManager::requiresNodes () {

    // BLACK MAGIC NUMBER
    return m_nodes.size() < 4;
    //return m_nodesSize < 4; 
  }

  // -------------------------------------------------------------------------------
  void NodeManager::evaluateNodes() {
          
    auto it = m_nodes.begin();
    
    while (it != m_nodes.end()) {
      if ((*it) -> state() >= 40) {
        //std::cout << "c removing node " << (*it) << " with state " << ((*it) -> state()) << std::endl;
        
        // store the current iterator
        auto itOld = it;
        
        // increment before since erase will make "it" invalid
        it++;
        
        m_nodes.erase(itOld);
      }
      else {
        it++;
      }
    }
  }
  
  
  
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  // member functions CacheController
  ///////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////
  
  
  // -------------------------------------------------------------------------------
  CacheController::~CacheController () {
    
    uint_fast32_t noElements(0);
    
    // lock!
    
    // TODO TODO TODO
    //boost::mutex::scoped_lock l(m_accessMutex);
    
    // clean up the precomputed hash tables
    delete m_varHashes;
    delete m_clauseHashes;
    
    // now clean up the whole hash table
    
    // helpers to store all the references
    CacheElement* currentElement(nullptr);
    CacheElement* nextElement(nullptr);
    
    CacheElement* currentSibling(nullptr);
    CacheElement* nextSibling(nullptr);
    
    for (uint64_t e = 0; e < pow(2, m_noBuckets); e++) {
      
      currentElement = m_hashTable[e];
      
      // iterate through the conflicts
      while (currentElement != nullptr) {
        // iterate through the siblings
        currentSibling = currentElement -> m_sibling;
        
        while (currentSibling != nullptr) {
          
          noElements++;
          
          // store where to go next
          nextSibling = currentSibling -> m_sibling;
          // delete the current one
          delete currentSibling;
          // go to the next
          currentSibling = nextSibling;            
        }
        
        noElements++;
        
        // all sibling done
        // store where to go next
        nextElement = currentElement -> m_nextConflict;
        // delete the current one
        delete currentElement;
        // go to the next
        currentElement = nextElement;             
      }
      
      // now set the entry to nullptr
      m_hashTable[e] = nullptr;
    }
    
    //std::cout << "c Cache Control: had a total of " << noElements << " elements stored" << std::endl;
  }
  
  // -------------------------------------------------------------------------------
  void CacheController::printStatistics () {
    
    std::cout << "c ===================Cache Statistics=========================="      << std::endl;
    std::cout << "c tries..................: " << m_tries << std::endl;
    std::cout << "c hits...................: " << m_hits << std::endl;
    std::cout << "c hit rate...............: " << (double) m_hits / m_tries*100. << " % "<< std::endl;
    std::cout << "c removed items..........: " << m_removedComponents << std::endl;
    //std::cout << "c hash conflicts.........: " << m_hashConflicts<< std::endl;      
  }
  
  // -------------------------------------------------------------------------------
  uint_fast32_t CacheController::bucketToAccessMutex(uint_fast32_t bucket) {
    uint_fast32_t res = (uint_fast32_t)floor((double)bucket / (double) pow(2, m_noBuckets) * (double)m_accessMutexes.size());
    
#ifdef DEBUG_MODE
    assert(res < m_accessMutexes.size());
#endif
        
    return res;    
  }
  
  
  // -------------------------------------------------------------------------------
  uint_fast32_t CacheController::tries () {
    return m_tries;
  }
  
  // -------------------------------------------------------------------------------
  uint_fast32_t CacheController::hits () {
    return m_hits;
  }
  
  
  // -------------------------------------------------------------------------------
  bool CacheController::isCleanupRequired () {
    
    // does this need to be thread safe? -- don't think so
    // thread safe counter checking
    //boost::mutex::scoped_lock l(m_accessMutex);
    
    return m_noElements >= m_maxNoElements;
  }
  
  // -------------------------------------------------------------------------------
  void CacheController::cleanup (uint_fast32_t pos, uint_fast32_t noThreads) {
    
    double frac = 1./(double)noThreads;
    uint64_t noElements = pow(2, m_noBuckets);
    
    // compute the range this thread has to clean
    uint64_t startPos = pos * frac * noElements;
    uint64_t endPos =   (pos+1) * frac * noElements;
    
    //std::cout << "c Cache: cleanup started (thread " << pos << " of " << noThreads << " has to clean positions " << startPos << " to " << (endPos - 1) << std::endl;
    
    uint64_t elementsRemovedFromTree(0);
    uint64_t elementsNotRemovedFromTree(0);
        
    // clean the cache
    CacheElement* currentElement;
    CacheElement* lastElement;
    
    for (uint64_t e = startPos; e < endPos; e++) {
      
      // thread safe cache access
      boost::mutex::scoped_lock l(m_accessMutexCleanup[pos]);
      currentElement = m_hashTable[e];
      lastElement    = nullptr;
      
      while (currentElement != nullptr) {
        
        // jump to the next element already since the current element might be deleted next
        lastElement = currentElement;
        currentElement = currentElement -> m_nextConflict;
        
        // get the node
        ComponentTreeNode* node  = lastElement -> m_node;
        
        // lock the corresponding node
        boost::mutex::scoped_lock nl(*(node -> getMutex()));
        
        // get the parent
        ComponentTreeNode* parent = node -> parent();
        
        if (parent == nullptr) {
          continue;
        }        
        
        // unlock node before the next steps
        nl.unlock();
        
        // check if the parent is done, too
        {
          boost::mutex::scoped_lock ll(*parent -> getMutex());
          
          if (parent -> state() >= 20 && parent -> state() <= 40) {
            
            // inform the parent that a child will be removed
            parent -> childRemoved(node);

            // lock the child node again before removal
            boost::mutex::scoped_lock nl(*(node -> getMutex()));
            
            elementsRemovedFromTree += node -> removeFromTree();
          }
          else {
            elementsNotRemovedFromTree++;
          }
        }
      }
    }
    
    //std::cout << "c had to leave " << elementsNotRemovedFromTree << " in tree because their parent wasn't finished!" << std::endl;
    
    //std::cout << "c Cache: cleanup finished. Elements before: " << elementsBeforeCleanUp << ", after: " << m_noElements << ", removed from tree: " << elementsRemovedFromTree << std::endl;
  }
  
  // -------------------------------------------------------------------------------
  void CacheController::removeInvalid (uint_fast32_t pos, uint_fast32_t noThreads) {
    
    //std::cout << "c cache: remove invalidated elements" << std::endl;
    
    double frac = 1./(double)noThreads;
    uint64_t noElements = pow(2, m_noBuckets);
    
    // compute the range this thread has to clean
    uint64_t startPos = pos * frac * noElements;
    uint64_t endPos =   (pos+1) * frac * noElements;
    
    // a vector to store the nodes that should be re-inserted into the cache since they're not invalid
    std::vector<ComponentTreeNode*> nodesToKeep;
    
    for (uint64_t e = startPos; e < endPos; e++) {
      
      CacheElement* nextElement = m_hashTable[e];
      CacheElement* currentElement = nullptr;
      
      // iterate through the conflict list
      while (nextElement != nullptr) {          
        
        // go to the next element, storing the current one
        currentElement = nextElement;
        nextElement = nextElement -> m_nextConflict;
        
        // no look through the sibling nodes
        CacheElement* nextSibling = currentElement;
        CacheElement* currentSibling = nullptr;
        
        while (nextSibling != nullptr) {            
          
          // go to the next sibling
          currentSibling = nextSibling;
          nextSibling = nextSibling -> m_sibling;
          
          // store the node before it is deleted
          ComponentTreeNode* node = currentSibling -> m_node;
          
          boost::mutex::scoped_lock l (*node -> getMutex());
            // remove from cache
            delete currentSibling;
          if (node -> state() != 50) {             
            boost::mutex::scoped_lock ll (m_nodesToKeepMutex);
            
            m_nodesToKeep.push_back(node);
          }
        }
      }
      
      // clear the hash table entry
      m_hashTable[e] = nullptr;
    }
  }
  
  // -------------------------------------------------------------------------------
  void CacheController::reAddNodes () {
    
    // store that the cache is empty right now
    m_noElements = 0;
    
    // add each node to the cache and fix its dependencies
    for(ComponentTreeNode* node: m_nodesToKeep) {
      boost::mutex::scoped_lock l (*node -> getMutex());
      
      node -> fixDependencies();
      
      add(node);
      
      // non thread-safe counting, since only one thread is active
      m_noElements += node -> getSize() + sizeof(*node);      
    }   
    
    // empty the list of kept nodes
    m_nodesToKeep.clear();
  }
  
  
  // -------------------------------------------------------------------------------
  void CacheController::add (ComponentTreeNode* node) {      
    
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!node -> getMutex() -> try_lock());
    #endif
    
    #ifdef DEBUG_MODE
    if(node -> state() != 20 && node -> state() != 25) {
      std::cerr << "c ERROR: state of " << node << " is " << node -> state() << std::endl;
    }
    // the node should be done
    assert((node -> state()) == 20 || (node -> state()) == 25);
    #endif
    
    
    // determine the correct bucket
    uint64_t bucket = (node -> getHash() >> (64-m_noBuckets));
    
    #ifdef DEBUG_MODE
    // the bucket should be in the valid range
    assert(bucket < pow(2, m_noBuckets));
    #endif
    
    // thread safe cache access
    boost::mutex::scoped_lock l(m_accessMutexes[bucketToAccessMutex(bucket)]);
    
    // check if there is a sibling state
    CacheElement* currentElement = m_hashTable[bucket];
    
    // iterate through the conflict list
    while (currentElement != nullptr) {
      
      // the current element has the same value as the node to add
      // -> add as a sibling node
      // NOTE: this compares the nodes using operator==, NOT the pointers!
      if (*(currentElement -> m_node) == *node) {
        
        // create a new cache element (m_node = node, m_sibling = currentElement -> m_sibling, m_nextConflict = nullptr)
        CacheElement* newElement = new CacheElement {node, currentElement -> m_sibling, nullptr};
        
        // and modify the current element
        currentElement -> m_sibling = newElement;
        
        // done :)
        return;
      }
      
      // jump to the next conflict clause
      currentElement = currentElement -> m_nextConflict;
    }
    
    // no sibling found (or there simply is none) -> add regularly
    
    // create the new cache element (m_node = node, m_sibling = nullptr, m_nextConflict = m_hashTable[bucket])
    CacheElement* newElement = new CacheElement {node, nullptr, m_hashTable[bucket]};
    
    // count conflicts
    if (m_hashTable[bucket] != nullptr) {
      m_hashConflicts++;
    }
    
    // and add it to the table 
    m_hashTable[bucket] = newElement;
  }
  
  // -------------------------------------------------------------------------------
  void CacheController::invalidate (ComponentTreeNode* node) {
    
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!node -> getMutex() -> try_lock());
    #endif
    
    #ifdef DEBUG_OUTPUT
    std::cout << "c cache invalidate node " << node << std::endl;
    #endif
    
    // determine the correct bucket
    uint64_t bucket = (node -> getHash() >> (64-m_noBuckets));
    
    // thread safe cache access
    boost::mutex::scoped_lock l(m_accessMutexes[bucketToAccessMutex(bucket)]);      
    
    // count
    m_removedComponents++;
    
    CacheElement* currentElement = m_hashTable[bucket];
    CacheElement* lastElement = nullptr;
    
    // find the correct entry by iterating through the conflict list
    while (currentElement != nullptr) {
      
      // found the right one in the conflict list
      // NOTE: this compares the nodes using operator==, NOT the pointers!
      if (currentElement -> m_node != nullptr && *(currentElement -> m_node) == *node) {
        // no look through the sibling nodes to find the exact match
        CacheElement* currentSibling = currentElement;
        CacheElement* lastSibling = nullptr;
        
        while (currentSibling != nullptr) {
          
          // NOTE: pointer comparison!
          if (currentSibling -> m_node == node) {
            
            invalidate(bucket, currentElement, lastElement, currentSibling, lastSibling);
            
            // found! and done :)
            return;
          }
          
          // go to the next sibling
          lastSibling = currentSibling;
          currentSibling = currentSibling -> m_sibling;
        }
        
        // not found.. this should not happen?
        
        std::cerr << "c ERROR: invalidate on node " << node << " with state: " << node -> state() << ": not in cache!" << std::endl;
        
        return;
        //assert(false);
      }
      
      // go to the next element, storing the current one
      lastElement = currentElement;
      currentElement = currentElement -> m_nextConflict;
    }
  }
  
  
  // -------------------------------------------------------------------------------
  void CacheController::countNode(ComponentTreeNode* node) {
    
    // lock
    
    {
      // thread safe counting of the node
      // boost::mutex::scoped_lock l(m_countMutex);
      // count        
      if (node -> m_variables != nullptr) {
        m_noElements += (node -> m_variables -> size() << 3);
      }
      if (node -> m_clauses != nullptr) {
        m_noElements += (node -> m_clauses -> size() << 3);
      }
      m_noElements += 160;
    }
  }
  
  // ------------------------------------------------------------------------------
  bool CacheController::lookup (ComponentTreeNode* node, mpz_class& modelCount) {
    // DEBUG: make sure the node is locked before any operation
    #ifdef DEBUG_MODE
    assert(!node -> getMutex() -> try_lock());
    #endif
    
    
    {
      // thread safe counting of the cache try
      //boost::mutex::scoped_lock l(m_countMutex);
      //m_countMutex.lock();
      
      // cache try
      m_tries++;
      
      // count        
      m_noElements += node -> getSize() + sizeof(*node);
      
      
      //m_countMutex.unlock();
    }
    
    
    // determine the correct bucket
    uint64_t bucket = (node -> getHash() >> (64-m_noBuckets));
    
    CacheElement* currentElement(nullptr);
    ComponentTreeNode* resNode (nullptr);
    
    // thread safe cache access
    {
      boost::mutex::scoped_lock l(m_accessMutexes[bucketToAccessMutex(bucket)]);
      
      
      currentElement = m_hashTable[bucket];
      
      while (currentElement != nullptr) {
        
        // compare the nodes
        // NOTE: this compares the nodes using operator==, NOT the pointers!
        if ((*currentElement -> m_node) == *node) {
          // break the loop execution
          break;
        }
        
        currentElement = currentElement -> m_nextConflict;
      }
      
      if (currentElement != nullptr) {
        resNode = currentElement -> m_node;
      }
    }
    
    // check if there was a cache hit (which is stored in resNode)
    if (resNode != nullptr) {
      {
        boost::mutex::scoped_lock l(*resNode -> getMutex());
        
        // if the node was aborted in the meantime (after the cache hit), do not use it!
        if (resNode -> state() != 20 && resNode -> state() != 25) {
          return false;
        }
        
        // write the result back    
        modelCount = resNode -> modelCount();
        
	// add dependency if required
	if (m_addDependencies) {
	  // now create a new depedency
	  NodeDependency* dep = new NodeDependency(node, resNode);
	  
	  // add the dependency to node and to resNode
	  node -> addDependency(dep);
	  resNode -> addDependency(dep);
	}
      }  
      
      // cache hit!
      //boost::mutex::scoped_lock l(m_countMutex);
      m_hits++;
      
      return true;
    }
    
    
    // not found
    return false;   
  }
  
  // -------------------------------------------------------------------------------
  uint64_t CacheController::getHashForVar (uint_fast32_t var) {
    return m_varHashes->at(var);
  }
  
  // -------------------------------------------------------------------------------
  uint64_t CacheController::getHashForClause (uint_fast32_t clause) {
    return m_clauseHashes->at(clause);
  }
  
  // -------------------------------------------------------------------------------
  void CacheController::invalidate (uint_fast32_t bucket, CacheElement* element, CacheElement* lastElement, CacheElement* currentSibling, CacheElement* lastSibling) {
    
    // if the last sibling is nullptr, the found node is the first
    // therefore, we also need to update the conflict list
    if (lastSibling == nullptr) {
      
      // determine what the the changed element in the conflict list should point to
      CacheElement* nextElement;
      
      // if there is a sibling available, chose it as the new element
      if (currentSibling -> m_sibling != nullptr) {
        
        nextElement = currentSibling -> m_sibling;
        
        #ifdef DEBUG_MODE
        // since the node was a sibling before it should not have any conflict link yet
        assert(nextElement -> m_nextConflict == nullptr);
        #endif
        
        // set the conflict pointer to the one of the current element
        nextElement -> m_nextConflict = element -> m_nextConflict;                  
      }
      else {
        // no sibling available -> chose next conflict
        nextElement = element -> m_nextConflict;
      }
      // DONE tetermining the next element, lets change the pointers to it
      
      
      // if the last element is nullptr, we're at the first element in the conflict list -> simply replace the hashTable entry by nextElement
      if (lastElement == nullptr) {
        
        #ifdef DEBUG_MODE
        assert(m_hashTable[bucket] = element);
        #endif
        
        // set the cache to the next element
        m_hashTable[bucket] = nextElement;
      }
      else {
        
        // not the first element -> let lastElementPoint at nextElement
        lastElement -> m_nextConflict = nextElement;
      }
      
      // remove the current element
      delete element;
    }
    else {
      // the element only needs to be removed from the sibling list
      
      // let last sibling point to our next sibling
      lastSibling -> m_sibling = currentSibling -> m_sibling;
      
      // delete
      delete currentSibling;
    }
  }
  
  
  // -------------------------------------------------------------------------------
  uint64_t CacheController::computeHashForNumber (uint64_t u) {
    uint64_t v = u * 3935559000370003845 + 2691343689449507681;
    
    v ^= v >> 21;
    v ^= v << 37;
    v ^= v >>  4;
    
    v *= 4768777513237032717;
    
    v ^= v << 20;
    v ^= v >> 41;
    v ^= v <<  5;
    
    return v;
  }
  
}
#endif // COMPONENT_HPP
