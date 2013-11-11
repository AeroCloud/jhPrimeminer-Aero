#include"global.h"

#include<intrin.h>
#include<ctime>
#include<map>
#include<conio.h>


primeStats_t primeStats = {0};
volatile unsigned int total_shares = 0;
volatile unsigned int valid_shares = 0;
unsigned int nMaxSieveSize;
unsigned int vPrimesSize;
unsigned int nonceStep;
bool nPrintDebugMessages;
bool nPrintSPSMessages;
unsigned int nOverrideTargetValue;
unsigned int nOverrideBTTargetValue;
unsigned int nSieveExtensions;
volatile unsigned int threadSNum = 0;
char* dt;

char* minerVersionString = "T16 v5 (AeroCloud)";

bool error(const char *format, ...) {
   puts(format);
   //__debugbreak();
   return false;
}

uint32 _swapEndianessU32(uint32 v) { return ((v>>24)&0xFF)|((v>>8)&0xFF00)|((v<<8)&0xFF0000)|((v<<24)&0xFF000000); }

void primecoinBlock_generateHeaderHash(primecoinBlock_t* primecoinBlock, uint8 hashOutput[32]) {
   uint8 blockHashDataInput[512];
   memcpy(blockHashDataInput, primecoinBlock, 80);
   sha256_context ctx;
   sha256_starts(&ctx);
   sha256_update(&ctx, (uint8*)blockHashDataInput, 80);
   sha256_finish(&ctx, hashOutput);
   sha256_starts(&ctx); // is this line needed?
   sha256_update(&ctx, hashOutput, 32);
   sha256_finish(&ctx, hashOutput);
}

void primecoinBlock_generateBlockHash(primecoinBlock_t* primecoinBlock, uint8 hashOutput[32]) {
   uint8 blockHashDataInput[512];
   memcpy(blockHashDataInput, primecoinBlock, 80);
   uint32 writeIndex = 80;
   uint32 lengthBN = 0;
   CBigNum bnPrimeChainMultiplier;
   bnPrimeChainMultiplier.SetHex(primecoinBlock->mpzPrimeChainMultiplier.get_str(16));
   std::vector<unsigned char> bnSerializeData = bnPrimeChainMultiplier.getvch();
   lengthBN = bnSerializeData.size();
   *(uint8*)(blockHashDataInput+writeIndex) = (uint8)lengthBN;
   writeIndex += 1;
   memcpy(blockHashDataInput+writeIndex, &bnSerializeData[0], lengthBN);
   writeIndex += lengthBN;
   sha256_context ctx;
   sha256_starts(&ctx);
   sha256_update(&ctx, (uint8*)blockHashDataInput, writeIndex);
   sha256_finish(&ctx, hashOutput);
   sha256_starts(&ctx); // is this line needed?
   sha256_update(&ctx, hashOutput, 32);
   sha256_finish(&ctx, hashOutput);
}

workData_t workData;

jsonRequestTarget_t jsonRequestTarget; // rpc login data
jsonRequestTarget_t jsonLocalPrimeCoin; // rpc login data

bool jhMiner_pushShare_primecoin(uint8 data[256], primecoinBlock_t* primecoinBlock) {
	xptShareToSubmit_t* xptShareToSubmit = (xptShareToSubmit_t*)malloc(sizeof(xptShareToSubmit_t));
	memset(xptShareToSubmit, 0x00, sizeof(xptShareToSubmit_t));
	memcpy(xptShareToSubmit->merkleRoot, primecoinBlock->merkleRoot, 32);
	memcpy(xptShareToSubmit->prevBlockHash, primecoinBlock->prevBlockHash, 32);
	xptShareToSubmit->version = primecoinBlock->version;
	xptShareToSubmit->nBits = primecoinBlock->nBits;
	xptShareToSubmit->nonce = primecoinBlock->nonce;
	xptShareToSubmit->nTime = primecoinBlock->timestamp;
	CBigNum bnPrimeChainMultiplier;
	bnPrimeChainMultiplier.SetHex(primecoinBlock->mpzPrimeChainMultiplier.get_str(16));
	std::vector<unsigned char> bnSerializeData = bnPrimeChainMultiplier.getvch();
	uint32 lengthBN = bnSerializeData.size();
	memcpy(xptShareToSubmit->chainMultiplier, &bnSerializeData[0], lengthBN);
	xptShareToSubmit->chainMultiplierSize = lengthBN;
	if (workData.xptClient && !workData.xptClient->disconnected) {
		xptClient_foundShare(workData.xptClient, xptShareToSubmit);
	} else {
		printf("Share submission failed. The client is not connected to the pool.\n");
	}
	return false;
}

bool IsXptClientConnected() {
   __try { if (workData.xptClient == NULL || workData.xptClient->disconnected) { return false; } }
   __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
   return true;
}

uint32 jhMiner_getCurrentWorkBlockHeight(unsigned int threadIndex) { return ((serverData_t*)workData.workEntry[threadIndex].serverData)->blockHeight; }

int jhMiner_workerThread_xpt(unsigned int threadIndex) {
	CSieveOfEratosthenes* psieve = NULL;
	while (true) {
		uint8 localBlockData[128];
		uint32 workDataHash = 0;
		uint8 serverData[32];
		while (workData.workEntry[threadIndex].dataIsValid == false) Sleep(50);
		EnterCriticalSection(&workData.cs);
		memcpy(localBlockData, workData.workEntry[threadIndex].data, 128);
		memcpy(serverData, workData.workEntry[threadIndex].serverData, 32);
		workDataHash = workData.workEntry[threadIndex].dataHash;
		LeaveCriticalSection(&workData.cs);
		primecoinBlock_t primecoinBlock = {0};
		memcpy(&primecoinBlock, localBlockData, 80);
		primecoinBlock.timestamp += threadIndex;
		primecoinBlock.threadIndex = threadIndex;
		primecoinBlock.xptMode = true;
		memcpy(&primecoinBlock.serverData, serverData, 32);
		if (!BitcoinMiner(&primecoinBlock, psieve, threadIndex, nonceStep)) { break; }
		primecoinBlock.mpzPrimeChainMultiplier = 0;
	}
	if (psieve) { delete psieve;psieve = NULL; }
	return 0;
}

typedef struct {
   char* workername;
   char* workerpass;
   char* host;
   unsigned int port;
   unsigned int numThreads;
   unsigned int sieveSize;
   unsigned int L1CacheElements;
   unsigned int targetOverride;
   unsigned int initialPrimorial;
   unsigned int sieveExtensions;
} commandlineInput_t;

commandlineInput_t commandlineInput = {0};

void jhMiner_printHelp() {
   puts("Usage: jhPrimeminer.exe [options]");
   puts("Options:");
   puts("   -o, -O                        The miner will connect to this url");
   puts("                                 You can specifiy an port after the url using -o url:port");
   puts("   -u                            The username (workername) used for login");
   puts("   -p                            The password used for login");
   puts("   -t <num>                      The number of threads for mining (default ALL Threads)");
   puts("                                     For most efficient mining, set to number of CPU cores");
   puts("   -layers <num>                 Set Sieve Layers: Allowed: 9 to 12");
   puts("   -split <num>                  Split Primorials by Thread (default 0)");
   puts("   -m <num>                      Primorial #1: Allowed: 31 to 107");
   puts("   -m2 <num>                     Primorial #2: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m3 <num>                     Primorial #3: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m4 <num>                     Primorial #4: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m5 <num>                     Primorial #5: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m6 <num>                     Primorial #6: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m7 to -m16 <num>               Additional Primorials");
   puts("                                   Recommended Primorials are: 31, 37, 41, 43, 47, 53");
   puts("   -s <num>                      Set MaxSieveSize: Minimum 512000, 64000 Increments");
   puts("   -c <num>                      Set Chunk Size: Minimum 64000, 64000 Increments");
   puts("Example usage:");
   puts("   jhPrimeminer.exe -o http://ypool.net:10034 -u workername.1 -p workerpass -t 4");
   puts("Press any key to continue...");
   _getch();
}

void jhMiner_printHelp2() {
   puts("Usage: jhPrimeminer.exe [options]");
   puts("Options:");
   puts("   -o, -O            The miner will connect to this url");
   puts("                       You can specifiy an port after the url using -o url:port");
   puts("   -u                The username (workername) used for login");
   puts("   -p                The password used for login");
   puts("   -t <num>          The number of threads for mining (default ALL Threads)");
   puts("                       For most efficient mining, set to number of CPU cores");
   puts("   -layers <num>     Set Sieve Layers: Allowed: 9 to 12");
   puts("   -split <num>      Split Primorials by Thread (default 0)");
   puts("   -m <num>          Primorial #1: Allowed: 31 to 107");
   puts("   -m2 <num>         Primorial #2: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m3 <num>         Primorial #3: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m4 <num>         Primorial #4: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m5 <num>         Primorial #5: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m6 <num>         Primorial #6: Allowed: 31 to 107 | 0 to Disable");
   puts("   -m7 to -m16 <num>   Additional Primorials");
   puts("                       Recommended Primorials are: 31, 37, 41, 43, 47, 53");
   puts("   -s <num>          Set MaxSieveSize: Minimum 512000, 64000 Increments");
   puts("   -c <num>          Set Chunk Size: Minimum 64000, 64000 Increments");
   puts("Additional In-Miner Commands:");
   puts("   <Ctrl-C>, <Q> - Quit");
   puts("   <s> - Print current settings");
   puts("   <h> - Print Help (This screen)");
   puts("   <m> - Toggle SPS Messages");
   puts("   <p> - Print Primorial Stats");
}

void PrintPrimorialStats() {
	double statsPassedTime = (double)(GetTickCount() - primeStats.primeLastUpdate);
	printf("\n\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\n");
	printf("        [    6ch] [    7ch] [    8ch] [    9ch] [   10ch] [   11ch] [  12ch+]\n");
	for (int i=0;i<112;i++) {
		if (primeStats.chainCounter2[i][0]>0) {
			printf("%6d: [%7d] [%7d] [%7d] [%7d] [%7d] [%7d] [%7d]\n",
				i,
				primeStats.chainCounter2[i][6],
				primeStats.chainCounter2[i][7],
				primeStats.chainCounter2[i][8],
				primeStats.chainCounter2[i][9],
				primeStats.chainCounter2[i][10],
				primeStats.chainCounter2[i][11],
				primeStats.chainCounter2[i][12]
			);
		}
	}
	printf("\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\n");
}

void jhMiner_parseCommandline(int argc, char **argv) {
   sint32 cIdx = 1;
   while (cIdx < argc) {
      char* argument = argv[cIdx];
      cIdx++;
      if (memcmp(argument, "-o", 3)==0 || memcmp(argument, "-O", 3)==0) {
         if (cIdx >= argc) { printf("Missing URL after -o option\n");ExitProcess(0); }
         if (strstr(argv[cIdx], "http://"))
            commandlineInput.host = fStrDup(strstr(argv[cIdx], "http://")+7);
         else
            commandlineInput.host = fStrDup(argv[cIdx]);
         char* portStr = strstr(commandlineInput.host, ":");
         if (portStr) {
            *portStr = '\0';
            commandlineInput.port = atoi(portStr+1);
         }
         cIdx++;
      } else if (memcmp(argument, "-u", 3)==0) {
         if (cIdx >= argc) { printf("Missing username/workername after -u option\n");ExitProcess(0); }
         commandlineInput.workername = fStrDup(argv[cIdx], 64);
         cIdx++;
      } else if (memcmp(argument, "-p", 3)==0) {
         if (cIdx >= argc) { printf("Missing password after -p option\n");ExitProcess(0); }
         commandlineInput.workerpass = fStrDup(argv[cIdx], 64);
         cIdx++;
      } else if (memcmp(argument, "-t", 3)==0) {
         commandlineInput.numThreads = atoi(argv[cIdx]);
		 if (commandlineInput.numThreads < 1) { commandlineInput.numThreads = 1; }
         if (commandlineInput.numThreads > 128) { commandlineInput.numThreads = 128; }
         cIdx++;
      } else if (memcmp(argument, "-m", 3)==0) {
         commandlineInput.initialPrimorial = atoi(argv[cIdx]);
		 if (commandlineInput.initialPrimorial < 11)  { commandlineInput.initialPrimorial = 11; }
		 if (commandlineInput.initialPrimorial > 111)  { commandlineInput.initialPrimorial = 111; }
         cIdx++;
      } else if (memcmp(argument, "-m2", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
		 cIdx++;
      } else if (memcmp(argument, "-m3", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m4", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m5", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m6", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m7", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m8", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m9", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m10", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m11", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m12", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m13", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m14", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m15", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-m16", 4)==0) {
         unsigned int tempMult = atoi(argv[cIdx]);
		 if (tempMult > 0 && tempMult < 11)  { tempMult = 11; }
		 if (tempMult > 111)  { tempMult = 111; }
		 if (tempMult > 0) { primeStats.nPrimorials.push_back(tempMult); }
         cIdx++;
      } else if (memcmp(argument, "-se", 4)==0) {
         commandlineInput.sieveExtensions = atoi(argv[cIdx]);
		 if (commandlineInput.sieveExtensions < 0) { commandlineInput.sieveExtensions = 0; }
		 if (commandlineInput.sieveExtensions > 12)  { commandlineInput.sieveExtensions = 12; }
         cIdx++;
      } else if (memcmp(argument, "-admnFunc", 10)==0) {
         primeStats.adminFunc = ((atoi(argv[cIdx])==45635352432543)?(true):(false));
         cIdx++;
      } else if (memcmp(argument, "-layers", 8)==0) {
		  commandlineInput.targetOverride = atoi(argv[cIdx]);
		  if (commandlineInput.targetOverride < 9)  { commandlineInput.targetOverride = 9; }
		  if (commandlineInput.targetOverride > 12)  { commandlineInput.targetOverride = 12; }
		  cIdx++;
	  } else if (memcmp(argument, "-split", 7)==0) {
		  int tSplit = atoi(argv[cIdx]);
		  if (tSplit <= 0)  { primeStats.tSplit = false; } else  { primeStats.tSplit = true; }
		  cIdx++;
	  } else if (memcmp(argument, "-s", 3)==0) {
         commandlineInput.sieveSize = atoi(argv[cIdx]);
         if (commandlineInput.sieveSize < 512000) { commandlineInput.sieveSize=512000; }
		 commandlineInput.sieveSize = ceil(commandlineInput.sieveSize/64000)*64000;
         cIdx++;
      } else if (memcmp(argument, "-c", 3)==0) {
         commandlineInput.L1CacheElements = atoi(argv[cIdx]);
		 if (commandlineInput.L1CacheElements < 64000) { commandlineInput.L1CacheElements=64000; }
		 commandlineInput.L1CacheElements = ceil(commandlineInput.L1CacheElements/64000)*64000;
         cIdx++;
      } else if (memcmp(argument, "-help", 6)==0 || memcmp(argument, "--help", 7)==0) {
         jhMiner_printHelp();
         ExitProcess(0);
      } else {
		 cIdx++;
      }
   }
}

typedef std::pair <DWORD, HANDLE> thMapKeyVal;
DWORD * threadHearthBeat;

static void watchdog_thread(std::map<DWORD, HANDLE> threadMap) {
   DWORD maxIdelTime = 30 * 1000;
   std::map <DWORD, HANDLE> :: const_iterator thMap_Iter;
   while(true) {
      if (!IsXptClientConnected()) { Sleep(5000);continue; }
      DWORD currentTick = GetTickCount();
      for (int i = 0; i < threadMap.size(); i++) {
         DWORD heartBeatTick = threadHearthBeat[i];
         if (currentTick - heartBeatTick > maxIdelTime) {
            printf("Restarting thread %d\n", i);
            thMap_Iter = threadMap.find(i);
            if (thMap_Iter != threadMap.end()) {
               HANDLE h = thMap_Iter->second;
               TerminateThread( h, 0);
               Sleep(1000);
               CloseHandle(h);
               Sleep(1000);
               threadHearthBeat[i] = GetTickCount();
               threadMap.erase(thMap_Iter);
               h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)jhMiner_workerThread_xpt, (LPVOID)i, 0, 0);
               SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
               threadMap.insert(thMapKeyVal(i,h));
            }
         }
      }
      Sleep(1*1000);
   }
}

void PrintCurrentSettings() {
	unsigned long uptime = (GetTickCount() - primeStats.startTime);
	unsigned int days = uptime / (24 * 60 * 60 * 1000);
	uptime %= (24 * 60 * 60 * 1000);
	unsigned int hours = uptime / (60 * 60 * 1000);
	uptime %= (60 * 60 * 1000);
	unsigned int minutes = uptime / (60 * 1000);
	uptime %= (60 * 1000);
	unsigned int seconds = uptime / (1000);

	printf("\n\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\n");	
	printf("Worker name (-u): %s\n", commandlineInput.workername);
	printf("Number of mining threads (-t): %u\n", commandlineInput.numThreads);
	printf("Primorials: %u",primeStats.nPrimorials[0]);
	for (unsigned int i=1;i<primeStats.nPrimorialsSize;i++) { printf(", %u", primeStats.nPrimorials[i]); }
	printf("\n");
	printf("Sieve Size (-s): %u\n", nMaxSieveSize);
	printf("Chunk Size (-c): %u\n", primeStats.nL1CacheElements);
	printf("Max Primes: Variable\n");
	printf("Cunninghame Layers (-layers): %u\n", nOverrideTargetValue);
	printf("BiTwin Layers: %u\n", nOverrideBTTargetValue);
	printf("Sieve Extensions (-se): %u\n", nSieveExtensions);
	printf("Total Runtime: %u Days, %u Hours, %u minutes, %u seconds\n", days, hours, minutes, seconds);
	printf("Total Share Value submitted to the Pool: %.05f\n", primeStats.fTotalSubmittedShareValue);
	printf("\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\n\n");
}

bool appQuitSignal = false;

static void input_thread() {
   while (true) {
      int input;
      input = _getch();		
      switch (input) {
      case 'q': case 'Q': case 3: //case 27:
         appQuitSignal = true;
         Sleep(2200);
         std::exit(0);
         return;
         break;
	  case 's': case 'S':
         PrintCurrentSettings();
         break;
	  case 'h': case 'H':
         jhMiner_printHelp2();
         break;
	  case 'p':
		  PrintPrimorialStats();
		  break;
	  case 'd':
		  if (primeStats.adminFunc) {
			if (nPrintDebugMessages == true) { nPrintDebugMessages = false;printf("Debug Messages: Disabled\n"); } else { nPrintDebugMessages = true;printf("Debug Messages: Enabled\n"); }
		  }
		  break;
	  case 'm':
		  if (nPrintSPSMessages == true) { nPrintSPSMessages = false;printf("SPS Messages: Disabled\n"); } else { nPrintSPSMessages = true;printf("SPS Messages: Enabled\n"); }
		  break;
	  case '1':
		 if (primeStats.adminFunc) {
			if (nMaxSieveSize > 64000) { nMaxSieveSize -= 64000; }
			printf("SieveSize: %u\n", nMaxSieveSize);
		 }
         break;
	  case '2':
		 if (primeStats.adminFunc) {
			if (nMaxSieveSize < 64000000) { nMaxSieveSize += 64000; }
			printf("SieveSize: %u\n", nMaxSieveSize);
		 }
         break;
	  case 'e':
		 if (primeStats.adminFunc) {
			if (nMaxSieveSize > 640000) { nMaxSieveSize -= 640000; }
			printf("SieveSize: %u\n", nMaxSieveSize);
		 }
		 break;
	  case 'r':
		 if (primeStats.adminFunc) {
			if (nMaxSieveSize < 64000000) { nMaxSieveSize += 640000; }
			printf("SieveSize: %u\n", nMaxSieveSize);
		 }
         break;
      case 0: case 224: {
            input = _getch();	
            switch (input) {
            case 72: // key up
				if (primeStats.adminFunc) {
					if (nOverrideTargetValue<12) { nOverrideTargetValue++;nOverrideBTTargetValue=nOverrideTargetValue; }
					printf("Layers: %u\n", nOverrideTargetValue);
				}
				break;
            case 80: // key down
				if (primeStats.adminFunc) {
					if (nOverrideTargetValue>5) { nOverrideTargetValue--;nOverrideBTTargetValue=nOverrideTargetValue; }
					printf("Layers: %u\n", nOverrideTargetValue);
				}
				break;
			case 75: // key left
				if (primeStats.adminFunc) {
					if (primeStats.pMult>20) { primeStats.pMult -= 10; }
					printf("Primes Adjustment: %u\n", primeStats.pMult);
				}
				break;
			case 77: // key right
				if (primeStats.adminFunc) {
					if (primeStats.pMult<20000) { primeStats.pMult += 10; }
					printf("Primes Adjustment: %u\n", primeStats.pMult);
				}
				break;
            }
         }
      }
   }
   return;
}

/*
* Mainloop when using xpt mode
*/
int jhMiner_main_xptMode() {
   // start the Auto Tuning thread
   CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)input_thread, NULL, 0, 0);

   std::map<DWORD, HANDLE> threadMap;
   threadHearthBeat = (DWORD *)malloc(commandlineInput.numThreads * sizeof(DWORD));
   // start threads
   for(unsigned int threadIdx=0; threadIdx<commandlineInput.numThreads; threadIdx++) {
      HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)jhMiner_workerThread_xpt, (LPVOID)threadIdx, 0, 0);
      SetThreadPriority(hThread, THREAD_PRIORITY_BELOW_NORMAL);
      threadMap.insert(thMapKeyVal(threadIdx,hThread));
      threadHearthBeat[threadIdx] = GetTickCount();
   }

   CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)watchdog_thread, (LPVOID)&threadMap, 0, 0);

   sint32 loopCounter = 0;
   uint32 xptWorkIdentifier = 0xFFFFFFFF;
   while (true) {
	   if (appQuitSignal) { return 0; }

      if (loopCounter % 3 == 0) {
         double totalRunTime = (double)(GetTickCount() - primeStats.startTime);
         double statsPassedTime = (double)(GetTickCount() - primeStats.primeLastUpdate);
		 if (statsPassedTime < 1.0) { statsPassedTime = 1.0; } // avoid division by zero
         double primesPerSecond = (double)primeStats.primeChainsFound / (statsPassedTime / 1000.0);
         primeStats.primeLastUpdate = GetTickCount();
         primeStats.primeChainsFound = 0;
         double avgCandidatesPerRound = (double)primeStats.nCandidateCount / primeStats.nSieveRounds;
         double sievesPerSecond = (double)primeStats.nSieveRounds / (statsPassedTime / 1000.0);
         primeStats.primeLastUpdate = GetTickCount();
         primeStats.nCandidateCount = 10;
         primeStats.nSieveRounds = 0;
         primeStats.primeChainsFound = 0;

         if (workData.workEntry[0].dataIsValid) {
            statsPassedTime = (double)(GetTickCount() - primeStats.blockStartTime);
			if (statsPassedTime < 1.0) { statsPassedTime = 1.0; } // avoid division by zero
			if (nPrintSPSMessages) {
				double shareValuePerHour = primeStats.fShareValue / totalRunTime * 3600000.0;
				uint64 NPS = ((nMaxSieveSize * ((nSieveExtensions/2)+1)) * sievesPerSecond);
				printf("Val/h:%8f - PPS:%d - SPS:%.03f - ACC:%d - NPS:%u\n", shareValuePerHour, (sint32)primesPerSecond, sievesPerSecond, (sint32)avgCandidatesPerRound, (uint64)NPS);
			}
         }
      }
      // wait and check some stats
      uint32 time_updateWork = GetTickCount();
      while (true) {
         uint32 tickCount = GetTickCount();
         uint32 passedTime = tickCount - time_updateWork;

		 if (passedTime >= 4000) { break; }
         xptClient_process(workData.xptClient);
         char* disconnectReason = false;
         if (workData.xptClient == NULL || xptClient_isDisconnected(workData.xptClient, &disconnectReason)) {
            // disconnected, mark all data entries as invalid
			 for (uint32 i=0;i<128;i++) { workData.workEntry[i].dataIsValid = false; }
			 printf("xpt: Disconnected, auto reconnect in 30 seconds\n");
			 if (workData.xptClient && disconnectReason) { printf("xpt: Disconnect reason: %s\n", disconnectReason); }
			 Sleep(30*1000);
			 if (workData.xptClient) { xptClient_free(workData.xptClient); }
			 xptWorkIdentifier = 0xFFFFFFFF;
			 while (true) {
				 workData.xptClient = xptClient_connect(&jsonRequestTarget, commandlineInput.numThreads);
				 if (workData.xptClient) { break; }
			 }
         }
         // has the block data changed?
         if (workData.xptClient && xptWorkIdentifier != workData.xptClient->workDataCounter) {
            // printf("New work\n");
            xptWorkIdentifier = workData.xptClient->workDataCounter;
            for(uint32 i=0; i<workData.xptClient->payloadNum; i++) {
               uint8 blockData[256];
               memset(blockData, 0x00, sizeof(blockData));
               *(uint32*)(blockData+0) = workData.xptClient->blockWorkInfo.version;
               memcpy(blockData+4, workData.xptClient->blockWorkInfo.prevBlock, 32);
               memcpy(blockData+36, workData.xptClient->workData[i].merkleRoot, 32);
               *(uint32*)(blockData+68) = workData.xptClient->blockWorkInfo.nTime;
               *(uint32*)(blockData+72) = workData.xptClient->blockWorkInfo.nBits;
               *(uint32*)(blockData+76) = 0; // nonce
               memcpy(workData.workEntry[i].data, blockData, 80);
               ((serverData_t*)workData.workEntry[i].serverData)->blockHeight = workData.xptClient->blockWorkInfo.height;
               ((serverData_t*)workData.workEntry[i].serverData)->nBitsForShare = workData.xptClient->blockWorkInfo.nBitsShare;

               // is the data really valid?
               if( workData.xptClient->blockWorkInfo.nTime > 0 )
                  workData.workEntry[i].dataIsValid = true;
               else
                  workData.workEntry[i].dataIsValid = false;
            }
            if (workData.xptClient->blockWorkInfo.height > 0) {
			   uint32 bestDifficulty = primeStats.bestPrimeChainDifficulty;
			   double primeDifficulty = GetChainDifficulty(bestDifficulty);
			   primeStats.bestPrimeChainDifficultySinceLaunch = max(primeStats.bestPrimeChainDifficultySinceLaunch, primeDifficulty);

               double totalRunTime = (double)(GetTickCount() - primeStats.startTime);
               double statsPassedTime = (double)(GetTickCount() - primeStats.primeLastUpdate);
               if (statsPassedTime < 1.0) statsPassedTime = 1.0; // avoid division by zero
               double poolDiff = GetPrimeDifficulty(workData.xptClient->blockWorkInfo.nBitsShare);
               double blockDiff = GetPrimeDifficulty(workData.xptClient->blockWorkInfo.nBits);
               printf("\n\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\n");
               printf("New Block: %u - Diff: %.06f / %.06f\n", workData.xptClient->blockWorkInfo.height, blockDiff, poolDiff);
			   printf("Valid/Total shares: [ %d / %d ]  -  Max diff:%12.09Lf\n", valid_shares, total_shares, primeStats.bestPrimeChainDifficultySinceLaunch);
			   printf("        [    6ch] [    7ch] [    8ch] [    9ch] [   10ch] [   11ch] [  12ch+]\n");
               statsPassedTime = (double)(GetTickCount() - primeStats.blockStartTime);
               if (statsPassedTime < 1.0) statsPassedTime = 1.0; // avoid division by zero
			   printf(" Total: [%7d] [%7d] [%7d] [%7d] [%7d] [%7d] [%7d]\n",
				   primeStats.chainCounter[0][6],
				   primeStats.chainCounter[0][7],
				   primeStats.chainCounter[0][8],
				   primeStats.chainCounter[0][9],
				   primeStats.chainCounter[0][10],
				   primeStats.chainCounter[0][11],
				   primeStats.chainCounter[0][12]
			   );
			   printf("  ch/h: [%7.02f] [%7.03f] [%7.03f] [%7.03f] [%7.03f] [%7.03f] [%7.03f]\n",
				   ((double)primeStats.chainCounter[0][6] / statsPassedTime) * 3600000.0,
				   ((double)primeStats.chainCounter[0][7] / statsPassedTime) * 3600000.0,
				   ((double)primeStats.chainCounter[0][8] / statsPassedTime) * 3600000.0,
				   ((double)primeStats.chainCounter[0][9] / statsPassedTime) * 3600000.0,
				   ((double)primeStats.chainCounter[0][10] / statsPassedTime) * 3600000.0,
				   ((double)primeStats.chainCounter[0][11] / statsPassedTime) * 3600000.0,
				   ((double)primeStats.chainCounter[0][12] / statsPassedTime) * 3600000.0
			   );
			   printf("\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\n");
			   double shareValuePerHour = primeStats.fShareValue / totalRunTime * 3600000.0;
			   printf("  Val/h: %8f                     Last Block/Total: %0.6f / %0.6f \n", shareValuePerHour, primeStats.fBlockShareValue, primeStats.fTotalSubmittedShareValue);               
               printf("\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\n");

               primeStats.fBlockShareValue = 0;
            }
         }
         Sleep(10);
      }
      loopCounter++;
   }

   return 0;
}

int main(int argc, char **argv) {
	// setup some default values
	commandlineInput.port = 10034;
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	commandlineInput.workername = "x";
	commandlineInput.workerpass = "x";
	commandlineInput.host = "ypool.net";
	commandlineInput.numThreads = sysinfo.dwNumberOfProcessors;
	commandlineInput.numThreads = max(commandlineInput.numThreads, 1);
	commandlineInput.sieveSize = 1536000; // default maxSieveSize
	commandlineInput.L1CacheElements = 256000;
	commandlineInput.targetOverride = 9;
	commandlineInput.initialPrimorial = 67;
	commandlineInput.sieveExtensions = 9;
	primeStats.adminFunc = false;
	primeStats.tSplit = true;

	nPrintSPSMessages = false;

	jhMiner_parseCommandline(argc, argv); //Parse Commandline

	nMaxSieveSize = commandlineInput.sieveSize;
	nSieveExtensions = commandlineInput.sieveExtensions;

	if (commandlineInput.targetOverride==9) {
		primeStats.pMult = 450;
	} else {
		primeStats.pMult = 180;
	}

	nOverrideTargetValue = commandlineInput.targetOverride;
	nOverrideBTTargetValue = commandlineInput.targetOverride;
   
	primeStats.nL1CacheElements = commandlineInput.L1CacheElements;

	printf("\n");
	printf("\xC9\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBB\n");
	printf("\xBA  jhPrimeMiner - mod by AeroCloud - T16 beta                   \xBA\n");
	printf("\xBA     optimised from rdebourbon 3.3 build + HP11 updates        \xBA\n");
	printf("\xBA  author: JH (http://ypool.net)                                \xBA\n");
	printf("\xBA  contributors: x3maniac, rdebourbon                           \xBA\n");
	printf("\xBA  Credits: Sunny King for the original Primecoin client&miner  \xBA\n");
	printf("\xBA  Credits: mikaelh for the performance optimizations           \xBA\n");
	printf("\xBA                                                               \xBA\n");
	printf("\xBA  Donations:                                                   \xBA\n");
	printf("\xBA        XPM: AFv6FpGBqzGUW8puYzitUwZKjSHKczmteY                \xBA\n");
	printf("\xBA        BTC: 1Ca9qP6tkAEo6EpgtXvuANr936c9FbgBrH                \xBA\n");
	printf("\xC8\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBC\n");
	printf("Launching miner...\n");

	// set priority lower so the user still can do other things
	SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
	if (pctx == NULL) { pctx = BN_CTX_new(); }
	// init prime table
	GeneratePrimeTable(10000000);
	// init winsock
	WSADATA wsa;
	WSAStartup(MAKEWORD(2,2),&wsa);
	// init critical section
	InitializeCriticalSection(&workData.cs);
	// connect to host
	hostent* hostInfo = gethostbyname(commandlineInput.host);
	if (hostInfo == NULL) { printf("Cannot resolve '%s'. Is it a valid URL?\n", commandlineInput.host);ExitProcess(-1); }
	void** ipListPtr = (void**)hostInfo->h_addr_list;
	uint32 ip = 0xFFFFFFFF;
	if (ipListPtr[0]) { ip = *(uint32*)ipListPtr[0]; }
	char ipText[32];
	esprintf(ipText, "%d.%d.%d.%d", ((ip>>0)&0xFF), ((ip>>8)&0xFF), ((ip>>16)&0xFF), ((ip>>24)&0xFF));
	if (((ip>>0)&0xFF) != 255) { printf("Connecting to '%s' (%d.%d.%d.%d)\n", commandlineInput.host, ((ip>>0)&0xFF), ((ip>>8)&0xFF), ((ip>>16)&0xFF), ((ip>>24)&0xFF)); }

	// setup RPC connection data (todo: Read from command line)
	jsonRequestTarget.ip = ipText;
	jsonRequestTarget.port = commandlineInput.port;
	jsonRequestTarget.authUser = commandlineInput.workername;
	jsonRequestTarget.authPass = commandlineInput.workerpass;

	// init stats
	primeStats.primeLastUpdate = primeStats.blockStartTime = primeStats.startTime = GetTickCount();
	primeStats.shareFound = false;
	primeStats.shareRejected = false;
	primeStats.primeChainsFound = 0;
	primeStats.foundShareCount = 0;
	for(unsigned int i = 0;i < sizeof(primeStats.chainCounter[0])/sizeof(uint32);i++) {
		primeStats.chainCounter[0][i] = 0;
		primeStats.chainCounter[1][i] = 0;
		primeStats.chainCounter[2][i] = 0;
		primeStats.chainCounter[3][i] = 0;
	}
	for (unsigned int i=0;i<112;i++) {
		primeStats.chainCounter2[i][0] = 0;
		primeStats.chainCounter2[i][1] = 0;
		primeStats.chainCounter2[i][2] = 0;
		primeStats.chainCounter2[i][3] = 0;
		primeStats.chainCounter2[i][4] = 0;
		primeStats.chainCounter2[i][5] = 0;
		primeStats.chainCounter2[i][6] = 0;
		primeStats.chainCounter2[i][7] = 0;
		primeStats.chainCounter2[i][8] = 0;
		primeStats.chainCounter2[i][9] = 0;
		primeStats.chainCounter2[i][10] = 0;
		primeStats.chainCounter2[i][11] = 0;
		primeStats.chainCounter2[i][12] = 0;
	}
	primeStats.fShareValue = 0;
	primeStats.fBlockShareValue = 0;
	primeStats.fTotalSubmittedShareValue = 0;
	primeStats.nWaveTime = 0;
	primeStats.nWaveRound = 0;

	primeStats.nPrimorials.push_back(commandlineInput.initialPrimorial);

	std::set<unsigned int> pSet(primeStats.nPrimorials.begin(),primeStats.nPrimorials.end());
	primeStats.nPrimorials.clear();
	primeStats.nPrimorials.assign(pSet.begin(),pSet.end());
	primeStats.nPrimorialsSize = primeStats.nPrimorials.size();
	for (unsigned int i=0;i<primeStats.nPrimorialsSize;i++) {
		primeStats.chainCounter2[primeStats.nPrimorials[i]][0] = 1;
	}

	// setup thread count and print info
	nonceStep = commandlineInput.numThreads;
	printf("Using %d threads\n", commandlineInput.numThreads);
	printf("Username: %s\n", jsonRequestTarget.authUser);
	printf("Password: %s\n", jsonRequestTarget.authPass);

	workData.xptClient = NULL;
	// x.pushthrough initial connect & login sequence
	while (true) {
		// repeat connect & login until it is successful (with 30 seconds delay)
		while (true) {
			workData.xptClient = xptClient_connect(&jsonRequestTarget, commandlineInput.numThreads);
			if (workData.xptClient != NULL) { break; }
			printf("Failed to connect, retry in 30 seconds\n");
			Sleep(30000);
		}
		// make sure we are successfully authenticated
		while (xptClient_isDisconnected(workData.xptClient, NULL) == false && xptClient_isAuthenticated(workData.xptClient) == false ) { xptClient_process(workData.xptClient);Sleep(1); }
		char* disconnectReason = NULL;
		if (xptClient_isDisconnected(workData.xptClient, &disconnectReason) == true) { xptClient_free(workData.xptClient);workData.xptClient = NULL;break; }
		if (xptClient_isAuthenticated(workData.xptClient) == true) { break; }
		if (disconnectReason) { printf("xpt error: %s\n", disconnectReason); }
		// delete client
		xptClient_free(workData.xptClient);
		// try again in 30 seconds
		printf("x.pushthrough authentication sequence failed, retry in 30 seconds\n");
		Sleep(30*1000);
	}

	printf("===============================================================\n");
	printf("Keyboard shortcuts:\n");
	printf("   <Ctrl-C>, <Q>     - Quit\n");
	printf("   <s> - Print current settings\n");
	printf("   <h> - Print Help\n");
	printf("   <m> - Toggle SPS Messages\n");
	printf("   <p> - Print Primorial Stats\n");

	return jhMiner_main_xptMode();
}