// Copyright (c) 2013 Primecoin developers
// Distributed under conditional MIT/X11 software license,
// see the accompanying file COPYING

#include "global.h"
#include <bitset>
#include <time.h>
#include <set>
#include <algorithm>

// Prime Table
//std::vector<unsigned int> vPrimes;
//uint32* vPrimes;
uint32* vPrimesTwoInverse;

__declspec( thread ) BN_CTX* pctx = NULL;

// changed to return the ticks since reboot
// doesnt need to be the correct time, just a more or less random input value
uint64 GetTimeMicros() {
   LARGE_INTEGER t;
   QueryPerformanceCounter(&t);
   return (uint64)t.QuadPart;
}

std::vector<unsigned int> vPrimes;
static unsigned int int_invert(unsigned int a, unsigned int nPrime);

void GeneratePrimeTable(unsigned int nSieveSize) {
   const unsigned int nPrimeTableLimit = nSieveSize ;
   vPrimes.clear();
   // Generate prime table using sieve of Eratosthenes
   std::vector<bool> vfComposite (nPrimeTableLimit, false);
   for (unsigned int nFactor = 2; nFactor * nFactor < nPrimeTableLimit; nFactor++) {
	   if (vfComposite[nFactor]) { continue; }
	   for (unsigned int nComposite = nFactor * nFactor; nComposite < nPrimeTableLimit; nComposite += nFactor) { vfComposite[nComposite] = true; }
   }
   for (unsigned int n = 2; n < nPrimeTableLimit; n++) { if (!vfComposite[n]) { vPrimes.push_back(n); } }
   printf("GeneratePrimeTable() : prime table [1, %d] generated with %lu primes\n", nPrimeTableLimit, vPrimes.size());
   vPrimesSize = vPrimes.size();  
}

// Get next prime number of p
bool PrimeTableGetNextPrime(unsigned int& p) {
   for (uint32 i=0; i<vPrimesSize; i++) {
      unsigned int nPrime = vPrimes[i];
      if ( nPrime > p) { p = nPrime;return true; }
   }
   return false;
}

// Get previous prime number of p
bool PrimeTableGetPreviousPrime(unsigned int& p) {
   uint32 nPrevPrime = 0;
   for (uint32 i=0; i<vPrimesSize; i++) { if (vPrimes[i] >= p) { break; } nPrevPrime = vPrimes[i]; }
   if (nPrevPrime) { p = nPrevPrime;return true; }
   return false;
}

// Compute Primorial number p#
void Primorial(unsigned int p, CBigNum& bnPrimorial) {
   bnPrimorial = 1;
   for (uint32 i=0; i<vPrimesSize; i++) { unsigned int nPrime = vPrimes[i];if (nPrime > p) { break; } bnPrimorial *= nPrime; }
}

// Compute Primorial number p#
void Primorial(unsigned int p, mpz_class& mpzPrimorial) {
   mpzPrimorial = 1;
   for (uint32 i=0; i<vPrimesSize; i++) { unsigned int nPrime = vPrimes[i];if (nPrime > p) { break; } mpzPrimorial *= nPrime; }
}

// Compute Primorial number p#
// Fast 32-bit version assuming that p <= 23
unsigned int PrimorialFast(unsigned int p) {
   unsigned int nPrimorial = 1;
   for (uint32 i=0; i<vPrimesSize; i++) { unsigned int nPrime = vPrimes[i];if (nPrime > p) { break; } nPrimorial *= nPrime; }
   return nPrimorial;
}

// Compute first primorial number greater than or equal to pn
void PrimorialAt(mpz_class& bn, mpz_class& mpzPrimorial) {
   mpzPrimorial = 1;
   for (uint32 i=0; i<vPrimesSize; i++) { unsigned int nPrime = vPrimes[i];mpzPrimorial *= nPrime;if (mpzPrimorial >= bn) { return; } }
}

// Check Fermat probable primality test (2-PRP): 2 ** (n-1) = 1 (mod n)
// true: n is probable prime
// false: n is composite; set fractional length in the nLength output
static bool FermatProbablePrimalityTest(const CBigNum& n, unsigned int& nLength) {
   //CBigNum a = 2; // base; Fermat witness
   CBigNum e = n - 1;CBigNum r;
   BN_mod_exp(&r, &bnTwo, &e, &n, pctx);
   if (r == 1) { return true; }
   // Failed Fermat test, calculate fractional length
   unsigned int nFractionalLength = (((n-r) << nFractionalBits) / n).getuint();
   if (nFractionalLength >= (1 << nFractionalBits))
      return error("FermatProbablePrimalityTest() : fractional assert");
   nLength = (nLength & TARGET_LENGTH_MASK) | nFractionalLength;
   return false;
}

// Check Fermat probable primality test (2-PRP): 2 ** (n-1) = 1 (mod n)
// true: n is probable prime
// false: n is composite; set fractional length in the nLength output
static bool FermatProbablePrimalityTest(const mpz_class& n, unsigned int& nLength) {
   // Faster GMP version
   mpz_t mpzN;mpz_t mpzE;mpz_t mpzR;
   mpz_init_set(mpzN, n.get_mpz_t());
   mpz_init(mpzE);
   mpz_sub_ui(mpzE, mpzN, 1);
   mpz_init(mpzR);
   mpz_powm(mpzR, mpzTwo.get_mpz_t(), mpzE, mpzN);
   if (mpz_cmp_ui(mpzR, 1) == 0) { mpz_clear(mpzN);mpz_clear(mpzE);mpz_clear(mpzR);return true; }
   // Failed Fermat test, calculate fractional length
   mpz_sub(mpzE, mpzN, mpzR);
   mpz_mul_2exp(mpzR, mpzE, nFractionalBits);
   mpz_tdiv_q(mpzE, mpzR, mpzN);
   unsigned int nFractionalLength = mpz_get_ui(mpzE);
   mpz_clear(mpzN);mpz_clear(mpzE);mpz_clear(mpzR);
   if (nFractionalLength >= (1 << nFractionalBits)) { return error("FermatProbablePrimalityTest() : fractional assert"); }
   nLength = (nLength & TARGET_LENGTH_MASK) | nFractionalLength;
   return false;
}

// Test probable primality of n = 2p +/- 1 based on Euler, Lagrange and Lifchitz
// fSophieGermain:
//   true:  n = 2p+1, p prime, aka Cunningham Chain of first kind
//   false: n = 2p-1, p prime, aka Cunningham Chain of second kind
// Return values
//   true: n is probable prime
//   false: n is composite; set fractional length in the nLength output
static bool EulerLagrangeLifchitzPrimalityTest(const CBigNum& n, bool fSophieGermain, unsigned int& nLength) {
   //CBigNum a = 2;
   CBigNum e = (n - 1) >> 1;CBigNum r;
   BN_mod_exp(&r, &bnTwo, &e, &n, pctx);
   uint32 nMod8U32 = 0;
   if (n.top > 0) { nMod8U32 = n.d[0]&7; }

   bool fPassedTest = false;
   if (fSophieGermain && (nMod8U32 == 7)) // Euler & Lagrange
      fPassedTest = (r == 1);
   else if (fSophieGermain && (nMod8U32 == 3)) // Lifchitz
      fPassedTest = ((r+1) == n);
   else if ((!fSophieGermain) && (nMod8U32 == 5)) // Lifchitz
      fPassedTest = ((r+1) == n);
   else if ((!fSophieGermain) && (nMod8U32 == 1)) // LifChitz
      fPassedTest = (r == 1);
   else
      return error("EulerLagrangeLifchitzPrimalityTest() : invalid n %% 8 = %d, %s", nMod8U32, (fSophieGermain? "first kind" : "second kind"));

   if (fPassedTest)
      return true;
   // Failed test, calculate fractional length
   r = (r * r) % n; // derive Fermat test remainder
   unsigned int nFractionalLength = (((n-r) << nFractionalBits) / n).getuint();
   if (nFractionalLength >= (1 << nFractionalBits))
      return error("EulerLagrangeLifchitzPrimalityTest() : fractional assert");
   nLength = (nLength & TARGET_LENGTH_MASK) | nFractionalLength;
   return false;
}


class CPrimalityTestParams {
public:
   // GMP variables
   mpz_t mpzE;mpz_t mpzR;mpz_t mpzRplusOne;

   // GMP C++ variables
   mpz_class mpzOriginMinusOne;mpz_class mpzOriginPlusOne;mpz_class N;

   // Values specific to a round
   unsigned int nBits;
   unsigned int nPrimorialSeq;
   unsigned int nCandidateType;
   unsigned int nTargetLength;
   unsigned int nHalfTargetLength;

   // Results
   unsigned int nChainLength;

   CPrimalityTestParams(unsigned int nBits, unsigned int nPrimorialSeq) {
      this->nBits = nBits;
      this->nTargetLength = TargetGetLength(nBits);
      this->nHalfTargetLength = nTargetLength / 2;
      this->nPrimorialSeq = nPrimorialSeq;
      nChainLength = 0;
      mpz_init(mpzE);mpz_init(mpzR);mpz_init(mpzRplusOne);
   }

   ~CPrimalityTestParams() { mpz_clear(mpzE);mpz_clear(mpzR);mpz_clear(mpzRplusOne); }
};


// Check Fermat probable primality test (2-PRP): 2 ** (n-1) = 1 (mod n)
// true: n is probable prime
// false: n is composite; set fractional length in the nLength output
static bool FermatProbablePrimalityTestFast(const mpz_class& n, unsigned int& nLength, CPrimalityTestParams& testParams, bool fFastFail = false) {
   // Faster GMP version
   mpz_t& mpzE = testParams.mpzE;
   mpz_t& mpzR = testParams.mpzR;

   mpz_sub_ui(mpzE, n.get_mpz_t(), 1);
   mpz_powm(mpzR, mpzTwo.get_mpz_t(), mpzE, n.get_mpz_t());
   if (mpz_cmp_ui(mpzR, 1) == 0)
      return true;
   if (fFastFail)
      return false;
   // Failed Fermat test, calculate fractional length
   mpz_sub(mpzE, n.get_mpz_t(), mpzR);
   mpz_mul_2exp(mpzR, mpzE, nFractionalBits);
   mpz_tdiv_q(mpzE, mpzR, n.get_mpz_t());
   unsigned int nFractionalLength = mpz_get_ui(mpzE);
   if (nFractionalLength >= (1 << nFractionalBits))
      return error("FermatProbablePrimalityTest() : fractional assert");
   nLength = (nLength & TARGET_LENGTH_MASK) | nFractionalLength;
   return false;
}



// Test probable primality of n = 2p +/- 1 based on Euler, Lagrange and Lifchitz
// fSophieGermain:
//   true:  n = 2p+1, p prime, aka Cunningham Chain of first kind
//   false: n = 2p-1, p prime, aka Cunningham Chain of second kind
// Return values
//   true: n is probable prime
//   false: n is composite; set fractional length in the nLength output
static bool EulerLagrangeLifchitzPrimalityTestFast(const mpz_class& n, bool fSophieGermain, unsigned int& nLength, CPrimalityTestParams& testParams/*, bool fFastDiv = false*/) {
   // Faster GMP version
   mpz_t& mpzE = testParams.mpzE;
   mpz_t& mpzR = testParams.mpzR;
   mpz_t& mpzRplusOne = testParams.mpzRplusOne;

   mpz_sub_ui(mpzE, n.get_mpz_t(), 1);
   mpz_tdiv_q_2exp(mpzE, mpzE, 1);
   mpz_powm(mpzR, mpzTwo.get_mpz_t(), mpzE, n.get_mpz_t());
   unsigned int nMod8 = mpz_get_ui(n.get_mpz_t()) % 8;
   bool fPassedTest = false;
   if (fSophieGermain && (nMod8 == 7)) // Euler & Lagrange
      fPassedTest = !mpz_cmp_ui(mpzR, 1);
   else if (fSophieGermain && (nMod8 == 3)) // Lifchitz
   {
      mpz_add_ui(mpzRplusOne, mpzR, 1);
      fPassedTest = !mpz_cmp(mpzRplusOne, n.get_mpz_t());
   }
   else if ((!fSophieGermain) && (nMod8 == 5)) // Lifchitz
   {
      mpz_add_ui(mpzRplusOne, mpzR, 1);
      fPassedTest = !mpz_cmp(mpzRplusOne, n.get_mpz_t());
   }
   else if ((!fSophieGermain) && (nMod8 == 1)) // LifChitz
      fPassedTest = !mpz_cmp_ui(mpzR, 1);
   else
      return error("EulerLagrangeLifchitzPrimalityTest() : invalid n %% 8 = %d, %s", nMod8, (fSophieGermain? "first kind" : "second kind"));

   if (fPassedTest) { return true; }

   // Failed test, calculate fractional length
   mpz_mul(mpzE, mpzR, mpzR);
   mpz_tdiv_r(mpzR, mpzE, n.get_mpz_t()); // derive Fermat test remainder

   mpz_sub(mpzE, n.get_mpz_t(), mpzR);
   mpz_mul_2exp(mpzR, mpzE, nFractionalBits);
   mpz_tdiv_q(mpzE, mpzR, n.get_mpz_t());
   unsigned int nFractionalLength = mpz_get_ui(mpzE);

   if (nFractionalLength >= (1 << nFractionalBits))
      return error("EulerLagrangeLifchitzPrimalityTest() : fractional assert");
   nLength = (nLength & TARGET_LENGTH_MASK) | nFractionalLength;
   return false;
}


// Test probable primality of n = 2p +/- 1 based on Euler, Lagrange and Lifchitz
// fSophieGermain:
//   true:  n = 2p+1, p prime, aka Cunningham Chain of first kind
//   false: n = 2p-1, p prime, aka Cunningham Chain of second kind
// Return values
//   true: n is probable prime
//   false: n is composite; set fractional length in the nLength output
static bool EulerLagrangeLifchitzPrimalityTest(const mpz_class& n, bool fSophieGermain, unsigned int& nLength)
{
   // Faster GMP version
   mpz_t mpzN;mpz_t mpzE;mpz_t mpzR;

   mpz_init_set(mpzN, n.get_mpz_t());
   mpz_init(mpzE);
   mpz_sub_ui(mpzE, mpzN, 1);
   mpz_tdiv_q_2exp(mpzE, mpzE, 1);
   mpz_init(mpzR);
   mpz_powm(mpzR, mpzTwo.get_mpz_t(), mpzE, mpzN);
   unsigned int nMod8 = mpz_tdiv_ui(mpzN, 8);
   bool fPassedTest = false;
   if (fSophieGermain && (nMod8 == 7)) // Euler & Lagrange
      fPassedTest = !mpz_cmp_ui(mpzR, 1);
   else if (fSophieGermain && (nMod8 == 3)) // Lifchitz
   {
      mpz_t mpzRplusOne;
      mpz_init(mpzRplusOne);
      mpz_add_ui(mpzRplusOne, mpzR, 1);
      fPassedTest = !mpz_cmp(mpzRplusOne, mpzN);
      mpz_clear(mpzRplusOne);
   }
   else if ((!fSophieGermain) && (nMod8 == 5)) // Lifchitz
   {
      mpz_t mpzRplusOne;
      mpz_init(mpzRplusOne);
      mpz_add_ui(mpzRplusOne, mpzR, 1);
      fPassedTest = !mpz_cmp(mpzRplusOne, mpzN);
      mpz_clear(mpzRplusOne);
   }
   else if ((!fSophieGermain) && (nMod8 == 1)) // LifChitz
   {
      fPassedTest = !mpz_cmp_ui(mpzR, 1);
   } else {
      mpz_clear(mpzN);mpz_clear(mpzE);mpz_clear(mpzR);
      return error("EulerLagrangeLifchitzPrimalityTest() : invalid n %% 8 = %d, %s", nMod8, (fSophieGermain? "first kind" : "second kind"));
   }

   if (fPassedTest) { mpz_clear(mpzN);mpz_clear(mpzE);mpz_clear(mpzR);return true; }

   // Failed test, calculate fractional length
   mpz_mul(mpzE, mpzR, mpzR);
   mpz_tdiv_r(mpzR, mpzE, mpzN); // derive Fermat test remainder

   mpz_sub(mpzE, mpzN, mpzR);
   mpz_mul_2exp(mpzR, mpzE, nFractionalBits);
   mpz_tdiv_q(mpzE, mpzR, mpzN);
   unsigned int nFractionalLength = mpz_get_ui(mpzE);
   mpz_clear(mpzN);mpz_clear(mpzE);mpz_clear(mpzR);
   if (nFractionalLength >= (1 << nFractionalBits)) { return error("EulerLagrangeLifchitzPrimalityTest() : fractional assert"); }
   nLength = (nLength & TARGET_LENGTH_MASK) | nFractionalLength;
   return false;
}

// Proof-of-work Target (prime chain target):
//   format - 32 bit, 8 length bits, 24 fractional length bits

unsigned int nTargetInitialLength = 7; // initial chain length target
unsigned int nTargetMinLength = 6;     // minimum chain length target

unsigned int TargetGetLimit() { return (nTargetMinLength << nFractionalBits); }
unsigned int TargetGetInitial() { return (nTargetInitialLength << nFractionalBits); }
unsigned int TargetGetLength(unsigned int nBits) { return ((nBits & TARGET_LENGTH_MASK) >> nFractionalBits); }

bool TargetSetLength(unsigned int nLength, unsigned int& nBits) {
   if (nLength >= 0xff)
      return error("TargetSetLength() : invalid length=%u", nLength);
   nBits &= TARGET_FRACTIONAL_MASK;
   nBits |= (nLength << nFractionalBits);
   return true;
}

void TargetIncrementLength(unsigned int& nBits) { nBits += (1 << nFractionalBits); }
void TargetDecrementLength(unsigned int& nBits) { if (TargetGetLength(nBits) > nTargetMinLength) { nBits -= (1 << nFractionalBits); } }
unsigned int TargetGetFractional(unsigned int nBits) { return (nBits & TARGET_FRACTIONAL_MASK); }

uint64 TargetGetFractionalDifficulty(unsigned int nBits) { return (nFractionalDifficultyMax / (uint64) ((1ull<<nFractionalBits) - TargetGetFractional(nBits))); }

bool TargetSetFractionalDifficulty(uint64 nFractionalDifficulty, unsigned int& nBits) {
   if (nFractionalDifficulty < nFractionalDifficultyMin)
      return error("TargetSetFractionalDifficulty() : difficulty below min");
   uint64 nFractional = nFractionalDifficultyMax / nFractionalDifficulty;
   if (nFractional > (1u<<nFractionalBits))
      return error("TargetSetFractionalDifficulty() : fractional overflow: nFractionalDifficulty=%016I64d", nFractionalDifficulty);
   nFractional = (1u<<nFractionalBits) - nFractional;
   nBits &= TARGET_LENGTH_MASK;
   nBits |= (unsigned int)nFractional;
   return true;
}

std::string TargetToString(unsigned int nBits) { __debugbreak();return NULL; }
unsigned int TargetFromInt(unsigned int nLength) { return (nLength << nFractionalBits); }

// Test Probable Cunningham Chain for: n
// fSophieGermain:
//   true - Test for Cunningham Chain of first kind (n, 2n+1, 4n+3, ...)
//   false - Test for Cunningham Chain of second kind (n, 2n-1, 4n-3, ...)
// Return value:
//   true - Probable Cunningham Chain found (length at least 2)
//   false - Not Cunningham Chain
static bool ProbableCunninghamChainTestFast(const mpz_class& n, const bool fSophieGermain, const bool fFermatTest, unsigned int& nProbableChainLength, CPrimalityTestParams& testParams, bool fBiTwinTest) {
   nProbableChainLength = 0;
   if (!FermatProbablePrimalityTestFast(n, nProbableChainLength, testParams, true)) { return false; }
   const int chainIncremental = (fSophieGermain? 1 : (-1));
   mpz_class &N = testParams.N;
   N = n;
   unsigned int currentLength = 0;
   while (true) {
      TargetIncrementLength(nProbableChainLength);
      N <<= 1;
      N += chainIncremental;
      currentLength++;
      if (fFermatTest) {
		  if (!FermatProbablePrimalityTestFast(N, nProbableChainLength, testParams)) { break; }
      } else {
		  if (!EulerLagrangeLifchitzPrimalityTestFast(N, fSophieGermain, nProbableChainLength, testParams)) { break; }
      }
   }
   return (currentLength >= 2);
}

// Test Probable Cunningham Chain for: n
// fSophieGermain:
//   true - Test for Cunningham Chain of first kind (n, 2n+1, 4n+3, ...)
//   false - Test for Cunningham Chain of second kind (n, 2n-1, 4n-3, ...)
// Return value:
//   true - Probable Cunningham Chain found (length at least 2)
//   false - Not Cunningham Chain
static bool ProbableCunninghamChainTest(const mpz_class& n, bool fSophieGermain, bool fFermatTest, unsigned int& nProbableChainLength) {
   nProbableChainLength = 0;
   mpz_class N = n;
   if (!FermatProbablePrimalityTest(N, nProbableChainLength)) { return false; }
   while (true) {
      TargetIncrementLength(nProbableChainLength);
      N = N + N + (fSophieGermain? 1 : (-1));
      if (fFermatTest) {
		  if (!FermatProbablePrimalityTest(N, nProbableChainLength)) { break; }
      } else {
		  if (!EulerLagrangeLifchitzPrimalityTest(N, fSophieGermain, nProbableChainLength)) { break; }
      }
   }
   return (TargetGetLength(nProbableChainLength) >= 2);
}

// Test probable prime chain for: nOrigin
// Return value:
//   true - Probable prime chain found (one of nChainLength meeting target)
//   false - prime chain too short (none of nChainLength meeting target)
bool ProbablePrimeChainTest(const mpz_class& bnPrimeChainOrigin, unsigned int nBits, bool fFermatTest, unsigned int& nChainLengthCunningham1, unsigned int& nChainLengthCunningham2, unsigned int& nChainLengthBiTwin, bool fullTest)
{
   nChainLengthCunningham1 = 0;
   nChainLengthCunningham2 = 0;
   nChainLengthBiTwin = 0;

   // Test for Cunningham Chain of second kind
   ProbableCunninghamChainTest(bnPrimeChainOrigin+1, false, fFermatTest, nChainLengthCunningham2);
   if (nChainLengthCunningham2 >= nBits && !fullTest)
      return true;
   // Test for Cunningham Chain of first kind
   ProbableCunninghamChainTest(bnPrimeChainOrigin-1, true, fFermatTest, nChainLengthCunningham1);
   if (nChainLengthCunningham1 >= nBits && !fullTest)
      return true;
   // Figure out BiTwin Chain length
   // BiTwin Chain allows a single prime at the end for odd length chain
   nChainLengthBiTwin =
      (TargetGetLength(nChainLengthCunningham1) > TargetGetLength(nChainLengthCunningham2))?
      (nChainLengthCunningham2 + TargetFromInt(TargetGetLength(nChainLengthCunningham2)+1)) :
   (nChainLengthCunningham1 + TargetFromInt(TargetGetLength(nChainLengthCunningham1)));
   if (fullTest)
      return (nChainLengthCunningham1 >= nBits || nChainLengthCunningham2 >= nBits || nChainLengthBiTwin >= nBits);
   else
      return nChainLengthBiTwin >= nBits;
}


// Test probable prime chain for: nOrigin
// Return value:
//   true - Probable prime chain found (one of nChainLength meeting target)
//   false - prime chain too short (none of nChainLength meeting target)
static bool ProbablePrimeChainTestFast(const mpz_class& mpzPrimeChainOrigin, CPrimalityTestParams& testParams) {
   const unsigned int nBits = testParams.nBits;
   const unsigned int nCandidateType = testParams.nCandidateType;
   unsigned int& nChainLength = testParams.nChainLength;
   mpz_class& mpzOriginMinusOne = testParams.mpzOriginMinusOne;
   mpz_class& mpzOriginPlusOne = testParams.mpzOriginPlusOne;
   nChainLength = 0;

   // Test for Cunningham Chain of first kind
   if (nCandidateType == PRIME_CHAIN_CUNNINGHAM1) {
      mpzOriginMinusOne = mpzPrimeChainOrigin - 1;
      ProbableCunninghamChainTestFast(mpzOriginMinusOne, true, false, nChainLength, testParams, false);
   } else if (nCandidateType == PRIME_CHAIN_CUNNINGHAM2) {
      // Test for Cunningham Chain of second kind
      mpzOriginPlusOne = mpzPrimeChainOrigin + 1;
      ProbableCunninghamChainTestFast(mpzOriginPlusOne, false, false, nChainLength, testParams, false);
   } else {
      unsigned int nChainLengthCunningham1 = 0;
      unsigned int nChainLengthCunningham2 = 0;
      mpzOriginMinusOne = mpzPrimeChainOrigin - 1;
      if (ProbableCunninghamChainTestFast(mpzOriginMinusOne, true, false, nChainLengthCunningham1, testParams, true)) {
         mpzOriginPlusOne = mpzPrimeChainOrigin + 1;
         ProbableCunninghamChainTestFast(mpzOriginPlusOne, false, false, nChainLengthCunningham2, testParams, true);
         // Figure out BiTwin Chain length
         // BiTwin Chain allows a single prime at the end for odd length chain
         nChainLength =
            (TargetGetLength(nChainLengthCunningham1) > TargetGetLength(nChainLengthCunningham2))?
            (nChainLengthCunningham2 + TargetFromInt(TargetGetLength(nChainLengthCunningham2)+1)) :
         (nChainLengthCunningham1 + TargetFromInt(TargetGetLength(nChainLengthCunningham1)));
      }
   }
   primeStats.nTestRound ++;
   return (nChainLength >= nBits);
}

static bool doSubmitBlock(primecoinBlock_t* block, unsigned int nChainLength, mpz_class mpzFixedMultiplier, uint64 nTriedMultiplier, unsigned int nProbableChainLength, unsigned int nCandidateType,
						   unsigned int nPrimorialMultiplier, unsigned int threadIndex) {
	sint32 shareDifficultyMajor = (sint32)(nChainLength>>24);

	if (nProbableChainLength > primeStats.bestPrimeChainDifficulty) { primeStats.bestPrimeChainDifficulty = nProbableChainLength; }
    // Update Stats
    primeStats.chainCounter[0][min(shareDifficultyMajor,12)]++;
    primeStats.chainCounter2[nPrimorialMultiplier][min(shareDifficultyMajor,12)]++;
	primeStats.chainTotals[0]++;
	primeStats.chainTotals[nCandidateType]++;
    primeStats.nChainHit++;

	block->mpzPrimeChainMultiplier = mpzFixedMultiplier * nTriedMultiplier;
	block->serverData.client_shareBits = nProbableChainLength;
    // generate block raw data
    uint8 blockRawData[256] = {0};
    memcpy(blockRawData, block, 80);
    uint32 writeIndex = 80;
    sint32 lengthBN = 0;
    CBigNum bnPrimeChainMultiplier;
    bnPrimeChainMultiplier.SetHex(block->mpzPrimeChainMultiplier.get_str(16));
    std::vector<unsigned char> bnSerializeData = bnPrimeChainMultiplier.getvch();
    lengthBN = bnSerializeData.size();
    *(uint8*)(blockRawData+writeIndex) = (uint8)lengthBN;
    writeIndex += 1;
    memcpy(blockRawData+writeIndex, &bnSerializeData[0], lengthBN);
    writeIndex += lengthBN;
    // switch endianness
    for (uint32 f=0; f<256/4; f++) {
		*(uint32*)(blockRawData+f*4) = _swapEndianessU32(*(uint32*)(blockRawData+f*4));
    }
    time_t now = time(0);
    struct tm * timeinfo;
    timeinfo = localtime (&now);
    char sNow [80];
    strftime (sNow, 80, "%x-%X",timeinfo);

    double shareDiff = GetChainDifficulty(nProbableChainLength);

    printf("%s - %u - SHARE FOUND! - DIFF:%12.9Lf - Th#:%2u TYPE:%u\n", sNow, nPrimorialMultiplier, shareDiff, threadIndex, nCandidateType);
    if (nPrintDebugMessages) { printf("HashMultiplier : %llu\n", nTriedMultiplier); }

    jhMiner_pushShare_primecoin(blockRawData, block);
    primeStats.foundShareCount ++;
    RtlZeroMemory(blockRawData, 256);
	return true;
}

// Mine probable prime chain of form: n = h * p# +/- 1
bool MineProbablePrimeChain(CSieveOfEratosthenes*& psieve, primecoinBlock_t* block, mpz_class& mpzFixedMultiplier, bool& fNewBlock, uint64& nTriedMultiplier, unsigned int& nProbableChainLength, 
                            unsigned int& nTests, unsigned int& nPrimesHit, unsigned int threadIndex, mpz_class& mpzHash, unsigned int nPrimorialMultiplier)
{

   nProbableChainLength = 0;
   nTests = 0;
   nPrimesHit = 0;

   fNewBlock = false;
   unsigned int lSieveTarget, lSieveBTTarget;
   lSieveTarget = nOverrideTargetValue;
   lSieveBTTarget = nOverrideBTTargetValue;

   //int64 nStart, nCurrent; // microsecond timer
   if (psieve == NULL) {
      // Build sieve
      psieve = new CSieveOfEratosthenes(nMaxSieveSize, nPrimorialMultiplier, nSieveExtensions, nOverrideTargetValue, nOverrideBTTargetValue, mpzHash, mpzFixedMultiplier, primeStats.pMult);
	  psieve->Weave();
   } else {
      psieve->Init(nMaxSieveSize, nPrimorialMultiplier, nSieveExtensions, nOverrideTargetValue, nOverrideBTTargetValue, mpzHash, mpzFixedMultiplier, primeStats.pMult);
	  psieve->Weave();
   }

   primeStats.nSieveRounds++;
   primeStats.nCandidateCount += psieve->GetCandidateCount();

   //mpz_class mpzHashMultiplier = mpzHash * mpzFixedMultiplier;
   mpz_class mpzChainOrigin;

   // Determine the sequence number of the round primorial
   unsigned int nPrimorialSeq = 0;
   while (vPrimes[nPrimorialSeq + 1] <= nPrimorialMultiplier)
      nPrimorialSeq++;

   // Allocate GMP variables for primality tests
   CPrimalityTestParams testParams(block->serverData.nBitsForShare, nPrimorialSeq);

   // References to test parameters
   unsigned int& nChainLength = testParams.nChainLength;
   unsigned int& nCandidateType = testParams.nCandidateType;

   bool multipleShare = false;
   mpz_class mpzPrevPrimeChainMultiplier = 0;

   bool rtnValue = false;

   uint32 start = GetTickCount();
   while (block->serverData.blockHeight == jhMiner_getCurrentWorkBlockHeight(block->threadIndex)) {
      if (!psieve->GetNextCandidateMultiplier(nTriedMultiplier, nCandidateType)) {
		  // power tests completed for the sieve
		  fNewBlock = true; // notify caller to change nonce
		  rtnValue = false;
		  break;
      }

      nTests++;
      mpzChainOrigin = mpzHash * mpzFixedMultiplier * nTriedMultiplier;
      nChainLength = 0;
      ProbablePrimeChainTestFast(mpzChainOrigin, testParams);
      nProbableChainLength = nChainLength;

      primeStats.primeChainsFound++;
      if (nProbableChainLength >= block->serverData.nBitsForShare && IsXptClientConnected()) {
		  doSubmitBlock(block, nChainLength, mpzFixedMultiplier, nTriedMultiplier, nProbableChainLength, nCandidateType, nPrimorialMultiplier, threadIndex);
      } else {
         continue;
      }
   }
   uint32 end = GetTickCount(); 
   primeStats.nTestTime += end-start;
   return rtnValue; // stop as timed out
}

// prime target difficulty value for visualization
double GetPrimeDifficulty(unsigned int nBits) { return ((double) nBits / (double) (1 << nFractionalBits)); }

static unsigned int int_invert(unsigned int a, unsigned int nPrime) {
   // Extended Euclidean algorithm to calculate the inverse of a in finite field defined by nPrime
   int rem0 = nPrime, rem1 = a % nPrime, rem2;
   int aux0 = 0, aux1 = 1, aux2;
   int quotient, inverse;

   while (1) {
      if (rem1 <= 1) { inverse = aux1;break; }
      rem2 = rem0 % rem1;
      quotient = rem0 / rem1;
      aux2 = -quotient * aux1 + aux0;
      if (rem2 <= 1) { inverse = aux2;break; }
      rem0 = rem1 % rem2;
      quotient = rem1 / rem2;
      aux0 = -quotient * aux2 + aux1;
      if (rem0 <= 1) { inverse = aux0;break; }
      rem1 = rem2 % rem0;
      quotient = rem2 / rem0;
      aux1 = -quotient * aux0 + aux2;
   }
   return (inverse + nPrime) % nPrime;
}

void CSieveOfEratosthenes::ProcessMultiplier(sieve_word_t *vfComposites, const unsigned int nMinMultiplier, const unsigned int nMaxMultiplier, const std::vector<unsigned int>& vPrimes, unsigned int *vMultipliers, unsigned int nLayerSeq) {
   // Wipe the part of the array first
   if (nMinMultiplier < nMaxMultiplier)
      memset(vfComposites + GetWordNum(nMinMultiplier), 0, ((nMaxMultiplier - nMinMultiplier) / nWordBits) * sizeof(uint64));
   
   for (unsigned int nPrimeSeqLocal = nMinPrimeSeq; nPrimeSeqLocal < nPrimes; nPrimeSeqLocal++) {
      const unsigned int nPrime = vPrimes[nPrimeSeqLocal];
	  const unsigned int nMultiplierPos = nPrimeSeqLocal + (nLayerSeq * nPrimes); //Groms Fix
      unsigned int nVariableMultiplier = vMultipliers[nMultiplierPos];

	  if (nVariableMultiplier < nMinMultiplier) { nVariableMultiplier += ((nMinMultiplier - nVariableMultiplier + nPrime - 1) / nPrime) * nPrime; }

      for (; nVariableMultiplier < nMaxMultiplier; nVariableMultiplier += nPrime) {
         vfComposites[GetWordNum(nVariableMultiplier)] |= GetBitMask(nVariableMultiplier);
      }
      vMultipliers[nMultiplierPos] = nVariableMultiplier;
   }
}

void CSieveOfEratosthenes::ProcessMultiplier2(sieve_word_t *vfComposites, const unsigned int nMinMultiplier, const unsigned int nMaxMultiplier, const std::vector<unsigned int>& vPrimes, unsigned int *vMultipliers, unsigned int nLayerSeq) {
   // Wipe the part of the array first
   if (nMinMultiplier < nMaxMultiplier)
      memset(vfComposites + GetWordNum(nMinMultiplier), 0, ((nMaxMultiplier - nMinMultiplier) / nWordBits) * sizeof(uint64));
   
	for (unsigned int nPrimeSeqLocal = nMinPrimeSeq; nPrimeSeqLocal < nPrimes; nPrimeSeqLocal++) {
		const unsigned int nPrime = vPrimes[nPrimeSeqLocal];
		const unsigned int nMultiplierPos = nPrimeSeqLocal + (nLayerSeq * nPrimes); //Groms Fix
		unsigned int nVariableMultiplier = vMultipliers[nMultiplierPos];

		if (nVariableMultiplier < nMinMultiplier) { nVariableMultiplier += (nMinMultiplier - nVariableMultiplier + nPrime - 1) / nPrime * nPrime; }

		const unsigned int nRotateBits = (nPrime % nWordBits);

		sieve_word_t lBitMask = GetBitMask(nVariableMultiplier);
		for (; nVariableMultiplier < nMaxMultiplier; nVariableMultiplier += nPrime) {
			vfComposites[GetWordNum(nVariableMultiplier)] |= lBitMask;
			lBitMask = (lBitMask << nRotateBits) | (lBitMask >> (nWordBits - nRotateBits));
		}
		vMultipliers[nMultiplierPos] = nVariableMultiplier;
	}
}


// Weave sieve for the next prime in table
// Return values:
//   True  - weaved another prime; nComposite - number of composites removed
//   False - sieve already completed
bool CSieveOfEratosthenes::Weave() {
   // Faster GMP version
   uint32 start = GetTickCount(); 

   const unsigned int nChainLength = this->nChainLength;
   const unsigned int nBTChainLength = this->nBTChainLength;
   const unsigned int nSieveSize = this->nSieveSize;
   mpz_class mpzHash = this->mpzHash;
   mpz_class mpzFixedMultiplier = this->mpzFixedMultiplier;
   const unsigned int nPrimes = this->nPrimes;
   sieve_word_t *vfCandidates = this->vfCandidates;

   mpz_class mpzFixedFactor;
   mpzFixedFactor = mpzHash * mpzFixedMultiplier;

   unsigned int nCombinedEndSeq = this->nPrimeSeq;
   unsigned int nFixedFactorCombinedMod = 0;

   for (unsigned int nPrimeSeqLocal = this->nPrimeSeq; nPrimeSeqLocal < nPrimes; nPrimeSeqLocal++) {
      unsigned int nPrime = vPrimes[nPrimeSeqLocal];
      if (nPrimeSeqLocal >= nCombinedEndSeq) {
         // Combine multiple primes to produce a big divisor
         unsigned int nPrimeCombined = 1;
         while (nPrimeCombined < UINT_MAX / vPrimes[nCombinedEndSeq]) { nPrimeCombined *= vPrimes[nCombinedEndSeq];nCombinedEndSeq++; }
		 nFixedFactorCombinedMod = mpz_tdiv_ui(mpzFixedFactor.get_mpz_t(), nPrimeCombined);
      }

      unsigned int nFixedFactorMod = nFixedFactorCombinedMod % nPrime;
      if (nFixedFactorMod == 0) { continue; } // Nothing in the sieve is divisible by this prime
      
	  // Find the modulo inverse of fixed factor
      unsigned int nFixedInverse = int_invert(nFixedFactorMod, nPrime);
	  if (!nFixedInverse) { return error("CSieveOfEratosthenes::Weave(): int_invert of fixed factor failed for prime #%u=%u", nPrimeSeqLocal, vPrimes[nPrimeSeqLocal]); }
      
	  unsigned int nTwoInverse = (nPrime + 1) / 2;
	  
	  // Weave the sieve for the prime
	  for (unsigned int nChainSeq = 0; nChainSeq < nSieveLayers; nChainSeq++) {
		  unsigned int multiplierPos = (nPrimes*nChainSeq)+nPrimeSeqLocal; //Groms Fix
		  vCunningham1Multipliers[multiplierPos] = nFixedInverse;
		  vCunningham2Multipliers[multiplierPos] = nPrime - nFixedInverse;
		  nFixedInverse = nFixedInverse * nTwoInverse % nPrime;
	  }
   }

   // Number of elements that are likely to fit in L1 cache
   // NOTE: This needs to be a multiple of nWordBits
   const unsigned int nL1CacheElements = primeStats.nL1CacheElements;
   const unsigned int nArrayRounds = (nSieveSize + nL1CacheElements - 1) / nL1CacheElements;

   // Calculate the number of CC1 and CC2 layers needed for BiTwin candidates
   const unsigned int nBiTwinCC1Layers = (nBTChainLength + 1) / 2;
   const unsigned int nBiTwinCC2Layers = nBTChainLength / 2;

   // Only 50% of the array is used in extensions
   const unsigned int nExtensionsMinMultiplier = nSieveSize / 2;
   const unsigned int nExtensionsMinWord = nExtensionsMinMultiplier / nWordBits;

   // Loop over each array one at a time for optimal L1 cache performance
   for (unsigned int j = 0; j < nArrayRounds; j++) {
      const unsigned int nMinMultiplier = nL1CacheElements * j;
      const unsigned int nMaxMultiplier = min(nL1CacheElements * (j + 1), nSieveSize);
      const unsigned int nExtMinMultiplier = max(nMinMultiplier, nExtensionsMinMultiplier);
      const unsigned int nMinWord = nMinMultiplier / nWordBits;
      const unsigned int nMaxWord = (nMaxMultiplier + nWordBits - 1) / nWordBits;
      const unsigned int nExtMinWord = max(nMinWord, nExtensionsMinWord);

      // Loop over the layers
      for (unsigned int nLayerSeq = 0; nLayerSeq < nSieveLayers; nLayerSeq++) {
         //if (pindexPrev != pindexBest)
         //    break;  // new block
         if (nLayerSeq < nChainLength) {
            ProcessMultiplier(vfCompositeLayerCC1, nMinMultiplier, nMaxMultiplier, vPrimes, vCunningham1Multipliers, nLayerSeq);
            ProcessMultiplier(vfCompositeLayerCC2, nMinMultiplier, nMaxMultiplier, vPrimes, vCunningham2Multipliers, nLayerSeq);
         } else {
            // Optimize: First halves of the arrays are not needed in the extensions
            ProcessMultiplier(vfCompositeLayerCC1, nExtMinMultiplier, nMaxMultiplier, vPrimes, vCunningham1Multipliers, nLayerSeq);
            ProcessMultiplier(vfCompositeLayerCC2, nExtMinMultiplier, nMaxMultiplier, vPrimes, vCunningham2Multipliers, nLayerSeq);
         }

         // Apply the layer to the primary sieve arrays
         if (nLayerSeq < nChainLength) {
            if (nLayerSeq < nBiTwinCC1Layers && nLayerSeq < nBiTwinCC2Layers) {
               for (unsigned int nWord = nMinWord; nWord < nMaxWord; nWord++) {
                  vfCompositeCunningham1[nWord] |= vfCompositeLayerCC1[nWord];
                  vfCompositeCunningham2[nWord] |= vfCompositeLayerCC2[nWord];
                  vfCompositeBiTwin[nWord] |= vfCompositeLayerCC1[nWord] | vfCompositeLayerCC2[nWord];
               }
            } else if (nLayerSeq < nBiTwinCC1Layers) {
               for (unsigned int nWord = nMinWord; nWord < nMaxWord; nWord++) {
                  vfCompositeCunningham1[nWord] |= vfCompositeLayerCC1[nWord];
                  vfCompositeCunningham2[nWord] |= vfCompositeLayerCC2[nWord];
                  vfCompositeBiTwin[nWord] |= vfCompositeLayerCC1[nWord];
               }
            } else {
               for (unsigned int nWord = nMinWord; nWord < nMaxWord; nWord++) {
                  vfCompositeCunningham1[nWord] |= vfCompositeLayerCC1[nWord];
                  vfCompositeCunningham2[nWord] |= vfCompositeLayerCC2[nWord];
               }
            }
         }

         // Apply the layer to extensions
         for (unsigned int nExtensionSeq = 0; nExtensionSeq < nSieveExtensions; nExtensionSeq++) {
            const unsigned int nLayerOffset = nExtensionSeq + 1;
            if (nLayerSeq >= nLayerOffset && nLayerSeq < nChainLength + nLayerOffset) {
               const unsigned int nLayerExtendedSeq = nLayerSeq - nLayerOffset;
               sieve_word_t *vfExtCC1 = vfExtendedCompositeCunningham1 + nExtensionSeq * nCandidatesWords;
               sieve_word_t *vfExtCC2 = vfExtendedCompositeCunningham2 + nExtensionSeq * nCandidatesWords;
               sieve_word_t *vfExtTWN = vfExtendedCompositeBiTwin + nExtensionSeq * nCandidatesWords;
               if (nLayerExtendedSeq < nBiTwinCC1Layers && nLayerExtendedSeq < nBiTwinCC2Layers) {
                  for (unsigned int nWord = nExtMinWord; nWord < nMaxWord; nWord++) {
                     vfExtCC1[nWord] |= vfCompositeLayerCC1[nWord];
                     vfExtCC2[nWord] |= vfCompositeLayerCC2[nWord];
                     vfExtTWN[nWord] |= vfCompositeLayerCC1[nWord] | vfCompositeLayerCC2[nWord];
                  }
               } else if (nLayerExtendedSeq < nBiTwinCC1Layers) {
                  for (unsigned int nWord = nExtMinWord; nWord < nMaxWord; nWord++) {
                     vfExtCC1[nWord] |= vfCompositeLayerCC1[nWord];
                     vfExtCC2[nWord] |= vfCompositeLayerCC2[nWord];
                     vfExtTWN[nWord] |= vfCompositeLayerCC1[nWord];
                  }
               } else {
                  for (unsigned int nWord = nExtMinWord; nWord < nMaxWord; nWord++) {
                     vfExtCC1[nWord] |= vfCompositeLayerCC1[nWord];
                     vfExtCC2[nWord] |= vfCompositeLayerCC2[nWord];
                  }
               }
            }
         }
      }
		// Combine the bitsets
		for (unsigned int i = nMinWord; i < nMaxWord; i++) {
			vfCandidates[i] = ~(vfCompositeCunningham1[i] & vfCompositeCunningham2[i] & vfCompositeBiTwin[i]);
		}
		// Combine the extended bitsets
		for (unsigned int k = 0; k < nSieveExtensions; k++) {
			unsigned int kword = k * nCandidatesWords;
			for (unsigned int i = nExtMinWord; i < nMaxWord; i++) {
				vfExtendedCandidates[kword+i] = ~(vfExtendedCompositeCunningham1[kword+i] & vfExtendedCompositeCunningham2[kword+i] & vfExtendedCompositeBiTwin[kword+i]);
			}
		}
   }

   // The sieve has been partially weaved
   this->nPrimeSeq = nPrimes - 1;

   uint32 end = GetTickCount(); 
   primeStats.nWaveTime += end-start;
   primeStats.nWaveRound ++;
   return false;
}