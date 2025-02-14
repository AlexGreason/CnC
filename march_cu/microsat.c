/*********************************************************************[microsat.c]***

  The MIT License

  Copyright (c) 2014-2018 Marijn Heule

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*************************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#define END        -9
#define MARK        2
#define UNSAT       0
#define SAT         1
#define UNKNOWN	    2
#define IMPLIED     6
// #define STANDALONE

#ifndef STANDALONE
  #include "microsat.h"
#else
struct solver { // The variables in the struct are described in the allocate procedure
  int  *DB, nVars, nClauses, mem_used, mem_fixed, mem_max, maxLemmas, nLemmas, *buffer,
       *assumptions, *assumeHead, nConflicts, *model, *reason, *falseStack,
       *false, *first, *forced, *processed, *assigned, *next, *prev, head, res, set, not; };
#endif

inline int abs (int a) { return (a > 0)?(a):(-a); }                // Compute the absolute value of literal a

int getModel (struct solver* S, int var) { return S->model[var]; } // Return the polarity of var in the current model

void printClause (int *clause) {                                   // Print the clause
  while (*clause) printf ("%i ", *(clause++)); printf ("0\n"); }   // Can be used to emit a RUP proof

void unassign (struct solver* S, int lit) { S->false[lit] = 0; }   // Unassign the literal

void restart (struct solver* S) {                                  // Perform a restart (i.e., unassign all variables)
  while (S->assigned > S->forced) unassign (S, *(--S->assigned));  // Remove all unforced false lits from falseStack
  S->processed = S->forced; }                                      // Reset the processed pointer

void assign (struct solver* S, int* reason, int forced) {          // Make the first literal of the reason true
  int lit = reason[0];                                             // Let lit be the first ltieral in the reason
  S->false[-lit] = forced ? IMPLIED : 1;                           // Mark lit as true and IMPLIED if forced
  *(S->assigned++) = -lit;                                         // Push it on the assignment stack
  if (S->model[abs (lit)] == (lit > 0)) S->not++; S->set++;        // Update the restart statistics
  S->reason[abs (lit)] = 1 + (int) ((reason)-S->DB);               // Set the reason clause of lit
  S->model [abs (lit)] = (lit > 0); }                              // Mark the literal as true in the model

void addWatch (struct solver* S, int lit, int mem) {               // Add a watch pointer to a clause containing lit
  S->DB[mem] = S->first[lit]; S->first[lit] = mem; }               // By updating the database and the pointers

void resetAssumptions (struct solver *S) {                         // Reset the array of assumptions
  S->assumeHead = S->assumptions; }                                // By pointing the head to the start of the array

void assume (struct solver* S, int lit) {                          // Add the assumption lit
  if (!S->false[lit]) S->model[abs (lit)] = (lit > 0);             // Update the model using the assumption
  *S->assumeHead = lit; S->assumeHead++; }                         // To the head of the assumption stack

int* offsetPointer(int* p, u_long offset){
    u_long initial = (ulong)p;
    int* res = (int*)(initial+offset);
    return res;
}

void offsetSolver(struct solver* S, u_long offset){
    S->assumptions = offsetPointer(S->assumptions, offset);
    S->model = offsetPointer(S->model, offset);
    S->next = offsetPointer(S->next, offset);
    S->prev = offsetPointer(S->prev, offset);
    S->buffer = offsetPointer(S->buffer, offset);
    S->reason = offsetPointer(S->reason, offset);
    S->falseStack = offsetPointer(S->falseStack, offset);
    S->forced = offsetPointer(S->forced, offset);
    S->processed = offsetPointer(S->processed, offset);
    S->assigned = offsetPointer(S->assigned, offset);
    S->false = offsetPointer(S->false, offset);
    S->first= offsetPointer(S->first, offset);
}

int* getMemory (struct solver* S, int mem_size) {                  // Allocate memory of size mem_size
  if (S->mem_used + mem_size > S->mem_max) {                       // In case the code is used within a code base
    S->mem_max = 3 * (S->mem_used + mem_size) / 2;                 // Increase the maximum allowed memory by ~50%
    printf ("c reallocating memory to %i\n", S->mem_max);
    u_long olddb = (u_long)(S->DB);
    S->DB = realloc (S->DB, sizeof(int) * S->mem_max);    // And allocated the database appropriately
    u_long newdb = (u_long)(S->DB);
    u_long change = newdb-olddb;
    offsetSolver(S, change);
  }
  int *store = (S->DB + S->mem_used);                              // Compute a pointer to the new memory location
  S->mem_used += mem_size;                                         // Update the size of the used memory
  return store; }                                                  // Return the pointer

int* addClause (struct solver* S, int* in, int size, int irr) {    // Adds a clause stored in *in of size size
  int i, used = S->mem_used;                                       // Store a pointer to the beginning of the clause
  int* clause = getMemory (S, size + 3) + 2;                       // Allocate memory for the clause in the database
  if (size >  1) { addWatch (S, in[0], used  );                    // If the clause is not unit, then add
                   addWatch (S, in[1], used+1); }                  // Two watch pointers to the datastructure
  for (i = 0; i < size; i++) clause[i] = in[i]; clause[i] = 0;     // Copy the clause from the buffer to the database
  if (irr) S->mem_fixed = S->mem_used; else S->nLemmas++;          // Update the statistics
  return clause; }                                                 // Return the pointer to the clause is the database

void reduceDB (struct solver* S, int k) {                     // Removes "less useful" lemmas from DB
  while (S->nLemmas > S->maxLemmas) S->maxLemmas += 300;      // Allow more lemmas in the future
  S->nLemmas = 0;                                             // Reset the number of lemmas

  int i; for (i = -S->nVars; i <= S->nVars; i++) {            // Loop over the variables
    if (i == 0) continue; int* watch = &S->first[i];          // Get the pointer to the first watched clause
    while (*watch != END)                                     // As long as there are watched clauses
      if (*watch < S->mem_fixed) watch = (S->DB + *watch);    // Remove the watch if it points to a lemma
      else                      *watch =  S->DB[  *watch]; }  // Otherwise (meaning an input clause) go to next watch

  int old_used = S->mem_used; S->mem_used = S->mem_fixed;     // Virtually remove all lemmas
  for (i = S->mem_fixed + 2; i < old_used; i += 3) {          // While the old memory contains lemmas
    int count = 0, head = i;                                  // Get the lemma to which the head is pointing
    while (S->DB[i]) { int lit = S->DB[i++];                  // Count the number of literals
      if ((lit > 0) == S->model[abs (lit)]) count++; }        // That are satisfied by the current model
    if (count < k) addClause (S, S->DB+head, i-head, 0); } }  // If the latter is smaller than k, add it back

void bump (struct solver* S, int lit) {                       // Move the variable to the front of the decision list
  if (S->false[lit] != IMPLIED) { S->false[lit] = MARK;       // MARK the literal as involved if not a top-level unit
    int var = abs (lit); if (var != S->head) {                // In case var is not already the head of the list
      S->prev[S->next[var]] = S->prev[var];                   // Update the prev link, and
      S->next[S->prev[var]] = S->next[var];                   // Update the next link, and
      S->next[S->head] = var;                                 // Add a next link to the head, and
      S->prev[var] = S->head; S->head = var; } } }            // Make var the new head

int implied (struct solver* S, int lit) {                  // Check if lit(eral) is implied by MARK literals
  if (S->false[lit] > MARK) return (S->false[lit] & MARK); // If checked before return old result
  if (!S->reason[abs (lit)]) return 0;                     // In case lit is a decision, it is not implied
  int* p = (S->DB + S->reason[abs (lit)] - 1);             // Get the reason of lit(eral)
  while (*(++p))                                           // While there are literals in the reason
    if ((S->false[*p] ^ MARK) && !implied (S, *p)) {       // Recursively check if non-MARK literals are implied
      S->false[lit] = IMPLIED - 1; return 0; }             // Mark and return not implied (denoted by IMPLIED - 1)
  S->false[lit] = IMPLIED; return 1; }                     // Mark and return that the literal is implied

int* analyze (struct solver* S, int* clause) {         // Compute a resolvent from falsified clause
  S->res++; S->nConflicts++;                           // Bump restarts and update the statistic
  while (*clause) bump (S, *(clause++));               // MARK all literals in the falsified clause
  while (S->reason[abs (*(--S->assigned))]) {          // Loop on variables on falseStack until the last decision
    if (S->false[*S->assigned] == MARK) {              // If the tail of the stack is MARK
      int *check = S->assigned;                        // Pointer to check if first-UIP is reached
      while (S->false[*(--check)] != MARK)             // Check for a MARK literal before decision
        if (!S->reason[abs(*check)]) goto build;       // Otherwise it is the first-UIP so break
      clause = S->DB + S->reason[abs (*S->assigned)];  // Get the reason and ignore first literal
      while (*clause) bump (S, *(clause++)); }         // MARK all literals in reason
    unassign (S, *S->assigned); }                      // Unassign the tail of the stack

  build:; int size = 0;                             // Build conflict clause; Empty the clause buffer
  int* p = S->assigned;                             // Loop from tail to front
  while (p >= S->forced) {                          // Only literals on the stack can be MARKed
    if ((S->false[*p] == MARK) && !implied (S, *p)) // If MARK and not implied by other MARKed literals
      S->buffer[size++] = *p;                       // Add literal to conflict clause buffer
    if ((size == 1) && !S->reason[abs (*p)])        // If this is the first literal in the buffer
      S->processed = p;                             // Then set the backjump point (in the search)
    S->false[*(p--)] = 1; }                         // Reset the MARK flag for all variables on the stack

  while (S->assigned > S->processed)                // Loop over all unprocessed literals
    unassign (S, *(S->assigned--));                 // Unassign all lits between tail & head
  unassign (S, *S->assigned);                       // Assigned now equal to processed
  S->buffer[size] = 0; // printClause (S->buffer);     // Terminate the buffer (and potentially print clause)
  return addClause (S, S->buffer, size, 0); }       // Add new conflict clause to redundant DB

void analyzeFinal (struct solver* S, int lit) {     // Compute and print the used assumptions
  int size = 0; S->buffer[size++] = -lit;           // Place the negation of the assumption in the buffer
  if (S->false[lit] < MARK) S->false[lit] = MARK;   // MARK assumption if not a top level unit
  while (S->assigned > S->forced) {                 // Unassign all unforced literals
    lit = *(--S->assigned);                         // Let lit be an assigned literal
    if (S->false[lit] == MARK) {                    // If the liteal was MARKed as involved
      if (S->reason[abs(lit)]) {                    // And if it was not a decision
        int *clause = S->DB + S->reason[abs (lit)]; // Then loop over the literals in its reason
        while (*clause) bump (S, *(clause++)); }    // And MARK all its literals
      else S->buffer[size++] = lit; }               // Add complement of assumption to buffer
    unassign (S, lit); }                            // Unassign the literal
  S->processed = S->forced;                         // Set the processed pointer back to forced
  int *final = addClause (S, S->buffer, size, 0);   // Add a clause blocking the conflicting assumptions
   printClause (final);  }                            // Print the clause blocking the involved assumptions

int propagate (struct solver* S) {                  // Performs unit propagation
  int forced = S->reason[abs (*S->processed)];      // Initialize forced flag
  while (S->processed < S->assigned) {              // While unprocessed false literals
    int lit = *(S->processed++);                    // Get first unprocessed literal
    int* watch = &S->first[lit];                    // Obtain the first watch pointer
    while (*watch != END) {                         // While there are watched clauses (watched by lit)
      int i, unit = 1;                              // Let's assume that the clause is unit
      int* clause = (S->DB + *watch + 1);	    // Get the clause from DB
      if (clause[-2] ==   0) clause++;              // Set the pointer to the first literal in the clause
      if (clause[ 0] == lit) clause[0] = clause[1]; // Ensure that the other watched literal is in front
      for (i = 2; unit && clause[i]; i++)           // Scan the non-watched literals
        if (!S->false[clause[i]]) {                 // When clause[i] is not false, it is either true or unset
          clause[1] = clause[i]; clause[i] = lit;   // Swap literals
          int store = *watch; unit = 0;             // Store the old watch
          *watch = S->DB[*watch];                   // Remove the watch from the list of lit
          addWatch (S, clause[1], store); }         // Add the watch to the list of clause[1]
      if (unit) {                                   // If the clause is indeed unit
        clause[1] = lit; watch = (S->DB + *watch);  // Place lit at clause[1] and update next watch
        if ( S->false[-clause[0]]) continue;        // If the other watched literal is satisfied continue
        if (!S->false[ clause[0]]) {                // If the other watched literal is falsified,
          assign (S, clause, forced); }             // A unit clause is found, and the reason is set
        else { if (forced) return UNSAT;            // Found a root level conflict -> UNSAT
          int* lemma = analyze (S, clause);	    // Analyze the conflict return a conflict clause
          if (!lemma[1]) forced = 1;                // In case a unit clause is found, set forced flag
          assign (S, lemma, forced); break; } } } } // Assign the conflict clause as a unit
  if (forced) S->forced = S->processed;	            // Set S->forced if applicable
  return SAT; }	                                    // Finally, no conflict was found

int solve (struct solver* S, int limit) {                           // Determine satisfiability using at most LIMIT conflicts
  int decision = S->head; S->res = S->set = S->not = 0;             // Initialize the solver
  for (;;) {                                                        // Main solve loop
    int old_nLemmas = S->nLemmas;                                   // Store nLemmas to see whether propagate adds lemmas
    if (propagate (S) == UNSAT) return UNSAT;                       // Propagation returns UNSAT for a root level conflict

    limit -= S->nLemmas - old_nLemmas; if (limit < 0) {             // If the conflict limit is reached,
      restart (S); reduceDB (S, 2); return UNKNOWN; }               // Restart and return the result UNKNOWN

    if (S->nLemmas > old_nLemmas) {                                 // If the last decision caused a conflict
      decision = S->head;                                           // Reset the decision heuristic to head
      float base = (float) S->set / (float) S->not;                 // Compute the restart strategy heuristic
      int i; for (i = 0; i < 4; i++) base *= base;                  // Based on how often the model was updated
      if (S->res > ((int) base) || S->nLemmas > S->maxLemmas) {     // Perform a heuristical or clause deletion restart?
        S->res = S->set = S->not = 0; restart (S); } }              // Reset the restart heuristics
    if (S->nLemmas > S->maxLemmas) reduceDB (S, 6);                 // Reduce the DB when it contains too many lemmas

    int *cube = S->assumptions;                                     // Let cube be a pointer to the assumptions
    while (cube < S->assumeHead) {                                  // Loop over the assumptions (incremental SAT only)
      decision = S->head; int lit = *(cube++);                      // Reset the variable selection heuristic
      if ( S->false[ lit]) { analyzeFinal (S, lit); return UNSAT; } // Compute the set of assumptions that results in false
      if (!S->false[-lit]) { decision = abs (lit); break; } }       // If literal is unassigned, then make it the decision

    while (S->false[decision] || S->false[-decision]) {             // As long as the temporay decision is assigned
      decision = S->prev[decision]; }                               // Replace it with the next variable in the decision list
    if (decision == 0) return SAT;                                  // If the end of the list is reached, then a solution is found
    decision = S->model[decision] ? decision : -decision;           // Otherwise, assign the decision variable based on the model
    S->false[-decision] = 1;                                        // Assign the decision literal to true (change to IMPLIED-1?)
    *(S->assigned++) = -decision;                                   // And push it on the assigned stack
    decision = abs(decision); S->reason[decision] = 0; } }          // Decisions have no reason clauses

void initCDCL (struct solver* S, int n, int m) {
  if (n < 1)   n = 1;                  // The code assumes that there is at least one variable
  S->nVars       = n;                  // Set the number of variables
  S->nClauses    = m;                  // Set the number of clauases
  S->mem_max     = 10000000;           // Set the initial maximum memory
  S->mem_used    = 0;                  // The number of integers allocated in the DB
  S->nLemmas     = 0;                  // The number of learned clauses -- redundant means learned
  S->nConflicts  = 0;                  // Under of conflicts which is used to updates scores
  S->maxLemmas   = 20000;               // Initial maximum number of learnt clauses

  S->DB = (int *) malloc (sizeof (int) * S->mem_max); // Allocate the initial database
  S->assumptions = getMemory (S, n+1); // List of assumptions (for incremental SAT)
  S->model       = getMemory (S, n+1); // Full assignment of the (Boolean) variables (initially set to false)
  S->next        = getMemory (S, n+1); // Next variable in the heuristic order
  S->prev        = getMemory (S, n+1); // Previous variable in the heuristic order
  S->buffer      = getMemory (S, n  ); // A buffer to store a temporary clause
  S->reason      = getMemory (S, n+1); // Array of clauses
  S->falseStack  = getMemory (S, n+1); // Stack of falsified literals -- this pointer is never changed
  S->forced      = S->falseStack;      // Points inside *falseStack at first decision (unforced literal)
  S->processed   = S->falseStack;      // Points inside *falseStack at first unprocessed literal
  S->assigned    = S->falseStack;      // Points inside *falseStack at last unprocessed literal
  S->false       = getMemory (S, 2*n+1); S->false += n; // Labels for variables, non-zero means false
  S->first       = getMemory (S, 2*n+1); S->first += n; // Offset of the first watched clause
  S->DB[S->mem_used++] = 0;            // Make sure there is a 0 before the clauses are loaded.

  int i; for (i = 1; i <= n; i++) {                        // Initialize the main datastructes:
    S->prev [i] = i - 1; S->next[i-1] = i;                 // the double-linked list for variable-move-to-front,
    S->model[i] = S->false[-i] = S->false[i] = 0;          // the model (phase-saving), the false array,
    S->first[i] = S->first[-i] = END; }                    // and first (watch pointers).
  S->head = n;                                             // Initialize the head of the double-linked list
  resetAssumptions (S); }                                  // Reset the assumption array

int parse (struct solver* S, char* filename) {                            // Parse the formula and initialize
  int tmp; FILE* input = fopen (filename, "r");                           // Read the CNF file
  do { tmp = fscanf (input, " p cnf %i %i \n", &S->nVars, &S->nClauses);  // Find the first non-comment line
    if (tmp > 0 && tmp != EOF) break; tmp = fscanf (input, "%*s\n"); }    // In case a commment line was found
  while (tmp != 2 && tmp != EOF);                                         // Skip it and read next line

  initCDCL (S, S->nVars, S->nClauses);                     // Allocate the main datastructures
  int nZeros = S->nClauses, size = 0;                      // Initialize the number of clauses to read
  while (nZeros > 0) {                                     // While there are clauses in the file
    int lit = 0; tmp = fscanf (input, " %i ", &lit);       // Read a literal.
    if (!lit) {                                            // If reaching the end of the clause
      int* clause = addClause (S, S->buffer, size, 1);     // Then add the clause to data_base
      if (!size || ((size == 1) && S->false[clause[0]]))   // Check for empty clause or conflicting unit
        return UNSAT;                                      // If either is found return UNSAT
      if ((size == 1) && !S->false[-clause[0]]) {          // Check for a new unit
        assign (S, clause, 1); }                           // Directly assign new units (forced = 1)
      size = 0; --nZeros; }                                // Reset buffer
    else S->buffer[size++] = lit; }                        // Add literal to buffer
  fclose (input);                                          // Close the formula file
  return SAT; }                                            // Return that no conflict was observed

#ifdef STANDALONE
int main (int argc, char** argv) {			                // The main procedure for a STANDALONE solver
  struct solver S;	                                                // Create the solver datastructure
  if       (parse (&S, argv[1]) == UNSAT) printf("s UNSATISFIABLE\n");  // Parse the DIMACS file in argv[1]
  else if  (solve (&S, 1 << 30) == UNSAT) printf("s UNSATISFIABLE\n");  // Solve without limit (number of conflicts)
  else                                    printf("s SATISFIABLE\n")  ;  // And print whether the formula has a solution
  printf ("c statistics of %s: mem: %i conflicts: %i max_lemmas: %i\n", argv[1], S.mem_used, S.nConflicts, S.maxLemmas); }
#endif
