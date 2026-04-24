
/********************************************************************************************
 * main.cpp -- Copyright (c) 2014, Jan Burchard
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
#include <sys/resource.h>
#include <sys/time.h>  
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include <csignal>
#include <string>
#include <vector>
// gnu bignum package - requires libgmp
#include <gmpxx.h>

// Include antom related headers.
#include "countAntom.hpp"

// Function headers.
uint_fast32_t loadCNF (const std::string& file, countAntom::CountAntom& solver, uint_fast32_t& variables, uint_fast32_t& clauses);
void SIGSEGVhandler(int sig);
long long getTimeStamp(void);

// An example demonstrating how to use antom.
int main (int argc, char** argv)
{ 
  // Define signal handling functions.
  signal(SIGSEGV,SIGSEGVhandler);
  
  // Initialization.
  std::cout.precision(4);
  std::cout.setf(std::ios::unitbuf);
  std::cout.setf(std::ios::fixed);
  
  // Output.
  std::cout 
  << "c ======================================================" << std::endl
  << "c countAntom, Jan Burchard, University of Freiburg, 2015" << std::endl
  << "c   based on the antom SAT-solver, by Tobias Schubert   " << std::endl
  << "c ======================================================" << std::endl;
  
  // No command line parameters?
  if (argc < 2)
  {
    // Ouput. 
    std::cout << "c usage:"                                                                                                                      << std::endl
    << "c   ./countAntom [options] <cnf file>"                                                                                                << std::endl
    << "c"                                                                                                                             << std::endl
    << "c options:"                                                                                                                    << std::endl 
    << "c   --noThreads=<value>                   --> the number of threads used for the computation (default: 4)" << std::endl
    << "c   --memSize=<value>                     --> the total amount of memory (in MB) used (default: 4000)" << std::endl
    << "c   --vsadsFactor=<value>                 --> the weighting factor for vsads (range: [-1:1], default: 0.5)" << std::endl
    << "c === preprocessing: ===" << std::endl
    << "c   --doSelfSubsumption=<yes,no>          --> should the solver do self subsumption during preprocessing (default: yes)" << std::endl
    << "c   --doTseitin=<yes,no>                  --> should the solver do Tseitin encoding detection during preprocessing (default: yes)" << std::endl
    << "c   --doUpla=<yes,no>                     --> should the solver do UPLA during preprocessing (default: yes)" << std::endl
    << "c   --doVariableElimination=<yes,no>      --> should the solver do variable elimination during preprocessing (default: yes)" << std::endl
    << "c   --simplifications=<value>             --> the number of simplification rounds during preprocessing (default: 4)"              << std::endl
    << "c   --preprocessingOnly=<yes,no>          --> runs only the preprocessor. Output: preprocessingOnlyFileName (default: no)"              << std::endl
    << "c   --preprocessingFileName=<value>       --> fileName of the preprocessed file (default: simple.cnf)"              << std::endl
    << "c === debug: ===" << std::endl
    << "c   --addDependencies=<yes,no>            --> set to no to not add laissez-faire dependencies (default: yes)"              << std::endl
    << "c   --enableIBCP=<yes,no>                 --> perform sharpSAT style implicit bcp (default: no)"              << std::endl
    << "c   --attemptSplit=<yes,no>               --> should the solver attempt sub-formula decomposition? (default: yes)"              << std::endl
    << "s UNKNOWN"                                                                                                                     << std::endl;
    
    // Return UNKNOWN.
    return ANTOM_UNKNOWN;
  }
  
  // Initialization.
  bool attemptSplit(true);
  uint_fast32_t simplifications(4);    
  bool preprocessingOnly(false);
  std::string preprocessingOnlyFileName("simple.cnf");
  bool doTseitin(true);
  bool doUpla(true);
  bool doSelfSubsumption(true);
  bool doVariableElimination(true);
  uint_fast32_t noThreads(4);
  uint32_t memSize(4000);
  double vsadsFactor(0.5);
  bool addDependencies(true);
  bool enableIBCP(false);
  
  // Get the additional command line parameters.
  for (uint_fast32_t c = 1; c < ((uint_fast32_t) argc - 1); ++c)
  {
    // Initialization.
    std::string argument(argv[c]); 
    bool matched(false);      
    
    
    // What about splitting?
    if (argument == "--attemptSplit=yes")
    { attemptSplit = true; matched = true; }
    if (argument == "--attemptSplit=no")
    { attemptSplit = false; matched = true; }
    
    // What about preprocessing?
    if (argument == "--preprocessingOnly=yes")
    { preprocessingOnly = true; matched = true; }
    if (argument == "--preprocessingOnly=no")
    { preprocessingOnly = false; matched = true; }
    
    
    // preprocessing file name
    if (argument.compare(0, 24, "--preprocessingFileName=") == 0)
    { std::stringstream ss(argument.substr(24, argument.length())); ss >> preprocessingOnlyFileName; matched = true; }
    
    // What about tseitin?
    if (argument == "--doTseitin=yes")
    { doTseitin = true; matched = true; }
    if (argument == "--doTseitin=no")
    { doTseitin = false; matched = true; }
    
    // What about subsumption?
    if (argument == "--doUpla=yes")
    { doUpla = true; matched = true; }
    if (argument == "--doUpla=no")
    { doUpla = false; matched = true; }
    
    // What about self subsumption?
    if (argument == "--doSelfSubsumption=yes")
    { doSelfSubsumption = true; matched = true; }
    if (argument == "--doSelfSubsumption=no")
    { doSelfSubsumption = false; matched = true; }
    
    // What about variable elimination?
    if (argument == "--doVariableElimination=yes")
    { doVariableElimination = true; matched = true; }
    if (argument == "--doVariableElimination=no")
    { doVariableElimination = false; matched = true; }
    
    // number of simplification rounds
    if (argument.compare(0, 18, "--simplifications=") == 0)
    { std::stringstream ss(argument.substr(18, argument.length())); ss >> simplifications; matched = true; }
    
    // number of threads
    if (argument.compare(0, 12, "--noThreads=") == 0)
    { std::stringstream ss(argument.substr(12, argument.length())); ss >> noThreads; matched = true; }
    
    // memory size
    if (argument.compare(0, 10, "--memSize=") == 0)
    { std::stringstream ss(argument.substr(10, argument.length())); ss >> memSize; matched = true; }
    
    // vsads factor
    if (argument.compare(0, 14, "--vsadsFactor=") == 0)
    {       
      std::string value = argument.substr(14, argument.length());
      
      vsadsFactor = std::stod(value.c_str());
      
      matched = true;      
    }
    

    // What about dependencies elimination?
    if (argument == "--addDependencies=yes")
    { addDependencies = true; matched = true; }
    if (argument == "--addDependencies=no")
    { addDependencies = false; matched = true; }
    
    
    // What about ibcp?
    if (argument == "--enableIBCP=yes")
    { enableIBCP = true; matched = true; }
    if (argument == "--enableIBCP=no")
    { enableIBCP = false; matched = true; }

   
    
    // Unknown option?
    if (!matched)
    {
      // Output. 
      std::cout << "c unknown option: " << argv[c] << std::endl
      << "s UNKNOWN" << std::endl; 
      
      // Return UNKNOWN.
      return ANTOM_UNKNOWN;
    }
  }
  
  // Get a first time stamp.
  long long wcTime(getTimeStamp());
  
  // Initialize an antom object.
  countAntom::CountAntom myCountAntom;
  
  
  
  // Initialization.
  uint_fast32_t variables(0);
  uint_fast32_t clauses(0);
  
  
  // Load the CNF file specified by the user.
  if (loadCNF(argv[(uint_fast32_t) argc - 1], myCountAntom, variables, clauses)>0)
  {      	
	// write a nice unsat formula into simple.cnf
	if (preprocessingOnly) {
	  std::ofstream simplifiedCNF;
	  simplifiedCNF.open (preprocessingOnlyFileName);
	  simplifiedCNF << "p cnf 2 4" << std::endl;	
	  simplifiedCNF << "1 2" << std::endl;
	  simplifiedCNF << "-1 -2" << std::endl;
	  simplifiedCNF << "1 -2" << std::endl;
	  simplifiedCNF << "-1 2" << std::endl;
	  simplifiedCNF.close();
	}
    
    // Return UNSAT.
    std::cout << "s UNSATISFIABLE" << std::endl; 
    return ANTOM_UNSAT;
  }
  
  // Output.
  std::cout << "c cnf file...............: " << argv[(uint_fast32_t) argc - 1] 	<< std::endl
  << "c #variables.............: " << variables                     	<< std::endl
  << "c #clauses...............: " << clauses                       	<< std::endl
  << "c ======================= Settings ============================"      << std::endl
  << "c number of threads......: " << noThreads<< std::endl
  << "c memory size............: " << memSize << std::endl
  << "c vsads factor...........: " << vsadsFactor << std::endl
  << "c enable IBCP............: " << (enableIBCP?"yes":"no") << std::endl
  << "c split into components..: " << (attemptSplit?"yes":"no")     	<< std::endl
  << "c only preprocessing.....: " << (preprocessingOnly?"yes":"no")     	<< std::endl
  << "c Tseitin detection......: " << (doTseitin?"yes":"no")     	<< std::endl
  << "c UPLA...................: " << (doUpla?"yes":"no")     	<< std::endl
  << "c self-subsumption.......: " << (doSelfSubsumption?"yes":"no")     	<< std::endl
  << "c variable elimination...: " << (doVariableElimination?"yes":"no")     	<< std::endl
  << "c simplfication rounds...: " << simplifications         << std::endl
  << "c add dependencies.......: " << (addDependencies?"yes":"no") << std::endl;
  
  if (!addDependencies) {
    std::cout << "c WARNING: without laissez-faire dependencies the solver might compute an incorrect result!" << std::endl;
  }
  
  // Solve the CNF file specified by the user.
  uint_fast32_t rst(myCountAntom.solve(vsadsFactor, attemptSplit, simplifications, preprocessingOnly, preprocessingOnlyFileName, doTseitin, doUpla, doSelfSubsumption, doVariableElimination, noThreads, memSize, addDependencies, enableIBCP));
  
  // unknown can only happen when in preprocessingOnly mode
  if (preprocessingOnly &&  rst == ANTOM_UNKNOWN) {
    std::cout << "s UNKNOWN" << std::endl; 
    return ANTOM_UNKNOWN;
  }
  
  assert(rst != ANTOM_UNKNOWN); 
  
  // Get a second time stamp.
  wcTime = getTimeStamp() - wcTime; 
  
  // Time measurement.
  struct rusage resourcesCPU;
  getrusage(RUSAGE_SELF, &resourcesCPU); 
  double timeS((double) resourcesCPU.ru_utime.tv_sec + 1.e-6 * (double) resourcesCPU.ru_utime.tv_usec);
  timeS += (double) resourcesCPU.ru_stime.tv_sec + 1.e-6 * (double) resourcesCPU.ru_stime.tv_usec;
  
  // Initialization.
  uint_fast32_t decisions(myCountAntom.decisions()); 
  uint_fast32_t bcps(myCountAntom.bcps());
  //uint_fast32_t conflicts(myCountAntom.conflicts()); 
  
  // Output.
  std::cout << "c #decisions.............: " << decisions                      << std::endl
  << "c #bcp operations........: " << bcps                           << std::endl
  // << "c #conflicts.............: " << conflicts                      << std::endl
  //<< "c #cache hits............: " << myCountAntom.cacheHits()		<< std::endl
  //<< "c #cache misses..........: " << myCountAntom.cacheMisses()	<< std::endl	   
  << "c split ratio............: " << myCountAntom.getSplitRatio()	<< std::endl	    
  << "c cpu time...............: " << timeS << " s"                   << std::endl
  << "c wall clock time........: " << ((double) wcTime / 1000000.00) << " s" << std::endl
  << "c ========================================================="   << std::endl
  // << "c #decisions/second......: " << ((double) decisions / timeS)   << std::endl
  // << "c #bcps/second...........: " << ((double) bcps / timeS)        << std::endl
  // << "c #conflicts/second......: " << ((double) conflicts / timeS)   << std::endl
  // << "c ========================================================="   << std::endl
  // << "c ========================================================="   << std::endl
  << "c model count............: ";
  std::cout << myCountAntom.modelCount();
  std::cout << std::endl;
  // << "c ========================================================="   << std::endl
  // << "c ========================================================="   << std::endl;
  
  // Satisfiable CNF formula?
  if (rst == ANTOM_SAT) 
  {
    // Output.
    std::cout << "s SATISFIABLE" << std::endl;    
    
    // Return SAT.
    return ANTOM_SAT; 
  }
  // write a nice unsat formula into preprocessingOnlyFileName
  if (preprocessingOnly) {
    std::ofstream simplifiedCNF;
    simplifiedCNF.open (preprocessingOnlyFileName);
    simplifiedCNF << "p cnf 2 4" << std::endl;	
    simplifiedCNF << "1 2" << std::endl;
    simplifiedCNF << "-1 -2" << std::endl;
    simplifiedCNF << "1 -2" << std::endl;
    simplifiedCNF << "-1 2" << std::endl;
    simplifiedCNF.close();
  }
  
  // Output.
  std::cout << "s UNSATISFIABLE" << std::endl; 
  
  
  
  // Return UNSAT.
  return ANTOM_UNSAT; 
}
uint_fast32_t loadCNF (const std::string& file, countAntom::CountAntom& solver, uint_fast32_t& variables, uint_fast32_t& clauses)
{      
  // Open the file.
  std::ifstream source;
  source.open(file.c_str());
  
  // Any problems while opening the file?
  if (!source) 
  { return 2; } 
  
  // Variables.
  std::vector<uint_fast32_t> clause;
  
  // storeage for the clauses to be added and the maximum variable index
  std::vector<std::vector<uint_fast32_t> > clausesToAdd;
  uint_fast32_t maxVariable(0);
  
  uint_fast32_t literal, sign; 
  char c; 
  
  // differentiate between files with a corrent header (p #vars #clauses)
  bool pMode = false;
  
  // Process the CNF file.
  while (source.good())
  {
    // Get the next character.
    c = source.get();
    
    // No more clauses?
    if (!source.good())
    { break; }
    
    
    // Statistics?
    if (c == 'p')
    {
      
      pMode = true;
      
      // Get the next char. 
      c = source.get(); 
      
      // Remove whitespaces.
      while (c == ' ') 
      { c = source.get(); }
      
      // The next three characters have to be "c", "n", and "f".
      assert(c = 'c');
      c = source.get();
      assert(c = 'n');
      c = source.get(); 
      assert(c = 'f');
      c = source.get(); 
      
      // Remove whitespaces.
      while (c == ' ') 
      { c = source.get(); }
      
      // Let's get the number of variables within the current CNF.
      while (c != ' ') 
      { variables = (variables * 10) + (uint_fast32_t) c - '0'; c = source.get(); }
      
      // Remove whitespaces.
      while (c == ' ') 
      { c = source.get(); }
      
      // Let's get the number of clauses within the current CNF.
      while (c != ' ' && c != '\n') 
      { clauses = (clauses * 10) + (uint_fast32_t) c - '0'; c = source.get(); }
      
      // Set the maximum number of variables the SAT solver has to deal with to "variables".
      solver.setMaxIndex(variables); 
    }
    // Clause? 
    if (c != 'c' && c != 'p' && c != '%')
    {
      // Reset "clause".
      clause.clear();
      
      // Get the next clause.
      do 
      {
	// Initialization.
	literal = 0;
	sign    = 0; 
	
	// Remove whitespaces.
	while (c == ' ') 
	{ c = source.get(); }
	
	// Let's get the next literal.
	while (c != ' ' && c != '\n') 
	{
	  if (c == '-')
	  { sign = 1; }
	  else
	  { literal = (literal * 10) + (uint_fast32_t) c - '0'; }
	  c = source.get();
	}
	
	// Add "literal" to "clause".
	if (literal != 0) 
	{ clause.push_back((literal << 1) + sign); 
	  if (literal > maxVariable) {
	    maxVariable = literal;
	  }	  
	}
      }
      while (literal != 0); 
      
      
      if (!pMode) {
	if (!clause.empty()) {
	  clausesToAdd.push_back(clause);
	}
      }
      else {	
	// Add "clause" to the clause database of "solver".	  
	if (!clause.empty() && !solver.addClause(clause))
	{ 		    
	  // At this point the "addClause" routine has detected
	  // that the given CNF formula is already unsatisfiable.
	  source.close();
	  return 1; 
	}
      }
    }
    
    // Go to the next line of the file. 
    while (c != '\n') 
    { c = source.get(); } 	  
  }
  
  // Close the file.
  source.close();
  
  if (!pMode) {
    // lets resize the data structures
    solver.setMaxIndex(maxVariable);
    
    // and add all the clauses
    for (auto clause : clausesToAdd) {
      if (!solver.addClause(clause)) {
	// At this point the "addClause" routine has detected
	// that the given CNF formula is already unsatisfiable.
	return 1;       
      }
    }
    variables = maxVariable;
    clauses = clausesToAdd.size();
  }  
  
  // Everything went fine.
  return 0;
}


// Terminates antom in case of a segmentation fault.
void SIGSEGVhandler(int sig) 
{
  // Output.
  std::cout << "c segmentation fault (signal " << sig << ")" << std::endl
  << "s UNKNOWN" << std::endl; 
  
  // Exit with termination code UNKNOWN.
  exit(ANTOM_UNKNOWN); 
} 

// Returns a "time stamp".          
long long getTimeStamp(void)
{
  timeval time;
  gettimeofday(&time, nullptr);
  long long wtime  = time.tv_sec;
  wtime            = wtime * 1000000l;
  wtime           += time.tv_usec;
  return wtime;
}
