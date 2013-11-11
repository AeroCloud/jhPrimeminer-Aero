#pragma comment(lib,"Ws2_32.lib")
#include<Winsock2.h>
#include<ws2tcpip.h>
#include"jhlib/JHLib.h"

#include<stdio.h>
#include<time.h>
#include<set>

#include"sha256.h"
#include"ripemd160.h"
static const int PROTOCOL_VERSION = 70001;

#include<openssl/bn.h>


// our own improved versions of BN functions
BIGNUM *BN2_mod_inverse(BIGNUM *in,	const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx);
int BN2_div(BIGNUM *dv, BIGNUM *rm, const BIGNUM *num, const BIGNUM *divisor);
int BN2_num_bits(const BIGNUM *a);
int BN2_rshift(BIGNUM *r, const BIGNUM *a, int n);
int BN2_lshift(BIGNUM *r, const BIGNUM *a, int n);
int BN2_uadd(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);

#define fastInitBignum(bignumVar, bignumData) \
	bignumVar.d = (BN_ULONG*)bignumData; \
	bignumVar.dmax = 0x200/4; \
	bignumVar.flags = BN_FLG_STATIC_DATA; \
	bignumVar.neg = 0; \
	bignumVar.top = 1; 

// original primecoin BN stuff
#include"uint256.h"
#include"bignum2.h"

#include"prime.h"
#include"jsonrpc.h"

#include "mpirxx.h"
#include "mpir.h"
#include<stdint.h>
#include"xptServer.h"
#include"xptClient.h"

#define BEGIN(a) ((char*)&(a))
#define END(a) ((char*)&((&(a))[1]))

static inline double GetChainDifficulty(unsigned int nChainLength) { return (double)nChainLength / 16777216.0; }

template<typename T>
std::string HexStr(const T itbegin, const T itend, bool fSpaces=false) {
    std::string rv;
    static const char hexmap[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    rv.reserve((itend-itbegin)*3);
    for (T it = itbegin; it < itend; ++it) {
        unsigned char val = (unsigned char)(*it);
        if(fSpaces && it != itbegin)
            rv.push_back(' ');
        rv.push_back(hexmap[val>>4]);
        rv.push_back(hexmap[val&15]);
    }
    return rv;
}

typedef struct {
	/* +0x00 */ uint32 seed;
	/* +0x04 */ uint32 nBitsForShare;
	/* +0x08 */ uint32 blockHeight;
	/* +0x0C */ uint32 padding1;
	/* +0x10 */ uint32 padding2;
	/* +0x14 */ uint32 client_shareBits;
	/* +0x18 */ uint32 serverStuff1;
	/* +0x1C */ uint32 serverStuff2;
} serverData_t;

typedef struct {
	volatile uint32 primeChainsFound;
	volatile uint32 foundShareCount;
	volatile float fShareValue;
	volatile float fBlockShareValue;
	volatile float fTotalSubmittedShareValue;
	volatile uint32 chainCounter[4][13];
	volatile uint32 chainCounter2[112][13];
	volatile uint32 chainTotals[4];
	volatile uint32 nWaveTime;
	volatile unsigned int nWaveRound;
	volatile uint32 nTestTime;
	volatile unsigned int nTestRound;

	volatile float nChainHit;
	volatile float nPrevChainHit;
	volatile unsigned int nPrimorialMultiplier;
	
	std::vector<unsigned int> nPrimorials;
	volatile unsigned int nPrimorialsSize;

	volatile float nSieveRounds;
	volatile float nSPS;
	volatile int pMult;
	volatile float nCandidateCount;

	CRITICAL_SECTION cs;

	// since we can generate many (useless) primes ultra fast if we simply set sieve size low, 
	// its better if we only count primes with at least a given difficulty
	//volatile uint32 qualityPrimesFound;
	volatile uint32 bestPrimeChainDifficulty;
	volatile double bestPrimeChainDifficultySinceLaunch;
	uint32 primeLastUpdate;
	uint32 blockStartTime;
	uint32 startTime;
	bool shareFound;
	bool shareRejected;
	volatile unsigned int nL1CacheElements;
	volatile bool tSplit;
	volatile bool adminFunc;

} primeStats_t;

extern primeStats_t primeStats;

typedef struct {
	uint32	version;
	uint8	prevBlockHash[32];
	uint8	merkleRoot[32];
	uint32	timestamp;
	uint32	nBits;
	uint32	nonce;
	// GetHeaderHash() goes up to this offset (4+32+32+4+4+4=80 bytes)
	uint256 blockHeaderHash;
	CBigNum bnPrimeChainMultiplierBN;
	mpz_class mpzPrimeChainMultiplier;
	// other
	serverData_t serverData;
	unsigned int threadIndex; // the index of the miner thread
	bool xptMode;
} primecoinBlock_t;

typedef struct {
   bool dataIsValid;
   uint8 data[128];
   uint32 dataHash; // used to detect work data changes
   uint8 serverData[32]; // contains data from the server 
} workDataEntry_t;

typedef struct {
   CRITICAL_SECTION cs;
   workDataEntry_t workEntry[128]; // work data for each thread (up to 128)
   xptClient_t* xptClient;
} workData_t;

extern jsonRequestTarget_t jsonRequestTarget; // rpc login data

// prototypes from main.cpp
bool error(const char *format, ...);
bool jhMiner_pushShare_primecoin(uint8 data[256], primecoinBlock_t* primecoinBlock);
void primecoinBlock_generateHeaderHash(primecoinBlock_t* primecoinBlock, uint8 hashOutput[32]);
uint32 _swapEndianessU32(uint32 v);
uint32 jhMiner_getCurrentWorkBlockHeight(unsigned int threadIndex);

bool BitcoinMiner(primecoinBlock_t* primecoinBlock, CSieveOfEratosthenes*& psieve, unsigned int threadIndex, unsigned int nonceStep);

// direct access to share counters
extern volatile unsigned int total_shares;
extern volatile unsigned int valid_shares;
extern bool appQuitSignal;

extern __declspec( thread ) BN_CTX* pctx;
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#if !HAVE_DECL_LE32DEC
static inline uint32_t le32dec(const void *pp) {
	const uint8_t *p = (uint8_t const *)pp;
	return ((uint32_t)(p[0]) + ((uint32_t)(p[1]) << 8) +
	    ((uint32_t)(p[2]) << 16) + ((uint32_t)(p[3]) << 24));
}
#endif