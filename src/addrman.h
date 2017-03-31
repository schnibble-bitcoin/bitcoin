// Copyright (c) 2012 Pieter Wuille
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITCOIN_ADDRMAN
#define _BITCOIN_ADDRMAN 1

#include "netbase.h"
#include "protocol.h"
#include "sync.h"
#include "util.h"

#include <map>
#include <set>
#include <stdint.h>
#include <vector>

#include <openssl/rand.h>

/** Extended statistics about a CAddress */
class CAddrInfo : public CAddress
{
private:
    // where knowledge about this address first came from
    CNetAddr source;

    // last successful connection by us
    int64_t nLastSuccess;

    // last try whatsoever by us:
    // int64_t CAddress::nLastTry

    // connection attempts since last successful attempt
    int nAttempts;

    // reference count in new sets (memory only)
    int nRefCount;

    // in tried set? (memory only)
    bool fInTried;

    // position in vRandom
    int nRandomPos;

    friend class CAddrMan;

public:

    IMPLEMENT_SERIALIZE(
        CAddress* pthis = (CAddress*)(this);
        READWRITE(*pthis);
        READWRITE(source);
        READWRITE(nLastSuccess);
        READWRITE(nAttempts);
    )

    void Init()
    {
        nLastSuccess = 0;
        nLastTry = 0;
        nAttempts = 0;
        nRefCount = 0;
        fInTried = false;
        nRandomPos = -1;
    }

    CAddrInfo(const CAddress &addrIn, const CNetAddr &addrSource) : CAddress(addrIn), source(addrSource)
    {
        Init();
    }

    CAddrInfo() : CAddress(), source()
    {
        Init();
    }

    // Calculate in which "tried" bucket this entry belongs
    int GetTriedBucket(const std::vector<unsigned char> &nKey) const;

    // Calculate in which "new" bucket this entry belongs, given a certain source
    int GetNewBucket(const std::vector<unsigned char> &nKey, const CNetAddr& src) const;

    // Calculate in which "new" bucket this entry belongs, using its default source
    int GetNewBucket(const std::vector<unsigned char> &nKey) const
    {
        return GetNewBucket(nKey, source);
    }

    // Determine whether the statistics about this entry are bad enough so that it can just be deleted
    bool IsTerrible(int64_t nNow = GetAdjustedTime()) const;

    // Calculate the relative chance this entry should be given when selecting nodes to connect to
    double GetChance(int64_t nNow = GetAdjustedTime()) const;

};

// Stochastic address manager
//
// Design goals:
//  * Only keep a limited number of addresses around, so that addr.dat and memory requirements do not grow without bound.
//  * Keep the address tables in-memory, and asynchronously dump the entire to able in addr.dat.
//  * Make sure no (localized) attacker can fill the entire table with his nodes/addresses.
//
// To that end:
//  * Addresses are organized into buckets.
//    * Address that have not yet been tried go into 256 "new" buckets.
//      * Based on the address range (/16 for IPv4) of source of the information, 32 buckets are selected at random
//      * The actual bucket is chosen from one of these, based on the range the address itself is located.
//      * One single address can occur in up to 4 different buckets, to increase selection chances for addresses that
//        are seen frequently. The chance for increasing this multiplicity decreases exponentially.
//      * When adding a new address to a full bucket, a randomly chosen entry (with a bias favoring less recently seen
//        ones) is removed from it first.
//    * Addresses of nodes that are known to be accessible go into 64 "tried" buckets.
//      * Each address range selects at random 4 of these buckets.
//      * The actual bucket is chosen from one of these, based on the full address.
//      * When adding a new good address to a full bucket, a randomly chosen entry (with a bias favoring less recently
//        tried ones) is evicted from it, back to the "new" buckets.
//    * Bucket selection is based on cryptographic hashing, using a randomly-generated 256-bit key, which should not
//      be observable by adversaries.
//    * Several indexes are kept for high performance. Defining DEBUG_ADDRMAN will introduce frequent (and expensive)
//      consistency checks for the entire data structure.

// total number of buckets for tried addresses
#define ADDRMAN_TRIED_BUCKET_COUNT 64

// maximum allowed number of entries in buckets for tried addresses
#define ADDRMAN_TRIED_BUCKET_SIZE 64

// total number of buckets for new addresses
#define ADDRMAN_NEW_BUCKET_COUNT 256

// maximum allowed number of entries in buckets for new addresses
#define ADDRMAN_NEW_BUCKET_SIZE 64

// over how many buckets entries with tried addresses from a single group (/16 for IPv4) are spread
#define ADDRMAN_TRIED_BUCKETS_PER_GROUP 4

// over how many buckets entries with new addresses originating from a single group are spread
#define ADDRMAN_NEW_BUCKETS_PER_SOURCE_GROUP 32

// in how many buckets for entries with new addresses a single address may occur
#define ADDRMAN_NEW_BUCKETS_PER_ADDRESS 4

// how many entries in a bucket with tried addresses are inspected, when selecting one to replace
#define ADDRMAN_TRIED_ENTRIES_INSPECT_ON_EVICT 4

// how old addresses can maximally be
#define ADDRMAN_HORIZON_DAYS 30

// after how many failed attempts we give up on a new node
#define ADDRMAN_RETRIES 3

// how many successive failures are allowed ...
#define ADDRMAN_MAX_FAILURES 10

// ... in at least this many days
#define ADDRMAN_MIN_FAIL_DAYS 7

// the maximum percentage of nodes to return in a getaddr call
#define ADDRMAN_GETADDR_MAX_PCT 23

// the maximum number of nodes to return in a getaddr call
#define ADDRMAN_GETADDR_MAX 2500

/** Stochastical (IP) address manager */
class CAddrMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // secret key to randomize bucket select with
    std::vector<unsigned char> nKey;

    // last used nId
    int nIdCount;

    // table with information about all nIds
    std::map<int, CAddrInfo> mapInfo;

    // find an nId based on its network address
    std::map<CNetAddr, int> mapAddr;

    // randomly-ordered vector of all nIds
    std::vector<int> vRandom;

    // number of "tried" entries
    int nTried;

    // list of "tried" buckets
    std::vector<std::vector<int> > vvTried;

    // number of (unique) "new" entries
    int nNew;

    // list of "new" buckets
    std::vector<std::set<int> > vvNew;

protected:

    // Find an entry.
    CAddrInfo* Find(const CNetAddr& addr, int *pnId = NULL);

    // find an entry, creating it if necessary.
    // nTime and nServices of found node is updated, if necessary.
    CAddrInfo* Create(const CAddress &addr, const CNetAddr &addrSource, int *pnId = NULL);

    // Swap two elements in vRandom.
    void SwapRandom(unsigned int nRandomPos1, unsigned int nRandomPos2);

    // Return position in given bucket to replace.
    int SelectTried(int nKBucket);

    // Remove an element from a "new" bucket.
    // This is the only place where actual deletes occur.
    // They are never deleted while in the "tried" table, only possibly evicted back to the "new" table.
    int ShrinkNew(int nUBucket);

    // Move an entry from the "new" table(s) to the "tried" table
    // @pre vvUnkown[nOrigin].count(nId) != 0
    void MakeTried(CAddrInfo& info, int nId, int nOrigin);

    // Mark an entry "good", possibly moving it from "new" to "tried".
    void Good_(const CService &addr, int64_t nTime);

    // Add an entry to the "new" table.
    bool Add_(const CAddress &addr, const CNetAddr& source, int64_t nTimePenalty);

    // Mark an entry as attempted to connect.
    void Attempt_(const CService &addr, int64_t nTime);

    // Select an address to connect to.
    // nUnkBias determines how much to favor new addresses over tried ones (min=0, max=100)
    CAddress Select_(int nUnkBias);

#ifdef DEBUG_ADDRMAN
    // Perform consistency check. Returns an error code or zero.
    int Check_();
#endif

    // Select several addresses at once.
    void GetAddr_(std::vector<CAddress> &vAddr);

    // Mark an entry as currently-connected-to.
    void Connected_(const CService &addr, int64_t nTime);

public:

    IMPLEMENT_SERIALIZE
    (({
        // serialized format:
        // * version byte (currently 0)
        // * nKey
        // * nNew
        // * nTried
        // * number of "new" buckets
        // * all nNew addrinfos in vvNew
        // * all nTried addrinfos in vvTried
        // * for each bucket:
        //   * number of elements
        //   * for each element: index
        //
        // Notice that vvTried, mapAddr and vVector are never encoded explicitly;
        // they are instead reconstructed from the other information.
        //
        // vvNew is serialized, but only used if ADDRMAN_UNKOWN_BUCKET_COUNT didn't change,
        // otherwise it is reconstructed as well.
        //
        // This format is more complex, but significantly smaller (at most 1.5 MiB), and supports
        // changes to the ADDRMAN_ parameters without breaking the on-disk structure.
        {
            LOCK(cs);
            unsigned char nVersion = 0;
            READWRITE(nVersion);
            READWRITE(nKey);
            READWRITE(nNew);
            READWRITE(nTried);

            CAddrMan *am = const_cast<CAddrMan*>(this);
            if (fWrite)
            {
                int nUBuckets = ADDRMAN_NEW_BUCKET_COUNT;
                READWRITE(nUBuckets);
                std::map<int, int> mapUnkIds;
                int nIds = 0;
                for (std::map<int, CAddrInfo>::iterator it = am->mapInfo.begin(); it != am->mapInfo.end(); it++)
                {
                    if (nIds == nNew) break; // this means nNew was wrong, oh ow
                    mapUnkIds[(*it).first] = nIds;
                    CAddrInfo &info = (*it).second;
                    if (info.nRefCount)
                    {
                        READWRITE(info);
                        nIds++;
                    }
                }
                nIds = 0;
                for (std::map<int, CAddrInfo>::iterator it = am->mapInfo.begin(); it != am->mapInfo.end(); it++)
                {
                    if (nIds == nTried) break; // this means nTried was wrong, oh ow
                    CAddrInfo &info = (*it).second;
                    if (info.fInTried)
                    {
                        READWRITE(info);
                        nIds++;
                    }
                }
                for (std::vector<std::set<int> >::iterator it = am->vvNew.begin(); it != am->vvNew.end(); it++)
                {
                    const std::set<int> &vNew = (*it);
                    int nSize = vNew.size();
                    READWRITE(nSize);
                    for (std::set<int>::iterator it2 = vNew.begin(); it2 != vNew.end(); it2++)
                    {
                        int nIndex = mapUnkIds[*it2];
                        READWRITE(nIndex);
                    }
                }
            } else {
                int nUBuckets = 0;
                READWRITE(nUBuckets);
                am->nIdCount = 0;
                am->mapInfo.clear();
                am->mapAddr.clear();
                am->vRandom.clear();
                am->vvTried = std::vector<std::vector<int> >(ADDRMAN_TRIED_BUCKET_COUNT, std::vector<int>(0));
                am->vvNew = std::vector<std::set<int> >(ADDRMAN_NEW_BUCKET_COUNT, std::set<int>());
                for (int n = 0; n < am->nNew; n++)
                {
                    CAddrInfo &info = am->mapInfo[n];
                    READWRITE(info);
                    am->mapAddr[info] = n;
                    info.nRandomPos = vRandom.size();
                    am->vRandom.push_back(n);
                    if (nUBuckets != ADDRMAN_NEW_BUCKET_COUNT)
                    {
                        am->vvNew[info.GetNewBucket(am->nKey)].insert(n);
                        info.nRefCount++;
                    }
                }
                am->nIdCount = am->nNew;
                int nLost = 0;
                for (int n = 0; n < am->nTried; n++)
                {
                    CAddrInfo info;
                    READWRITE(info);
                    std::vector<int> &vTried = am->vvTried[info.GetTriedBucket(am->nKey)];
                    if (vTried.size() < ADDRMAN_TRIED_BUCKET_SIZE)
                    {
                        info.nRandomPos = vRandom.size();
                        info.fInTried = true;
                        am->vRandom.push_back(am->nIdCount);
                        am->mapInfo[am->nIdCount] = info;
                        am->mapAddr[info] = am->nIdCount;
                        vTried.push_back(am->nIdCount);
                        am->nIdCount++;
                    } else {
                        nLost++;
                    }
                }
                am->nTried -= nLost;
                for (int b = 0; b < nUBuckets; b++)
                {
                    std::set<int> &vNew = am->vvNew[b];
                    int nSize = 0;
                    READWRITE(nSize);
                    for (int n = 0; n < nSize; n++)
                    {
                        int nIndex = 0;
                        READWRITE(nIndex);
                        CAddrInfo &info = am->mapInfo[nIndex];
                        if (nUBuckets == ADDRMAN_NEW_BUCKET_COUNT && info.nRefCount < ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
                        {
                            info.nRefCount++;
                            vNew.insert(nIndex);
                        }
                    }
                }
            }
        }
    });)

    CAddrMan() : vRandom(0), vvTried(ADDRMAN_TRIED_BUCKET_COUNT, std::vector<int>(0)), vvNew(ADDRMAN_NEW_BUCKET_COUNT, std::set<int>())
    {
         nKey.resize(32);
         RAND_bytes(&nKey[0], 32);

         nIdCount = 0;
         nTried = 0;
         nNew = 0;
    }

    // Return the number of (unique) addresses in all tables.
    int size()
    {
        return vRandom.size();
    }

    // Consistency check
    void Check()
    {
#ifdef DEBUG_ADDRMAN
        {
            LOCK(cs);
            int err;
            if ((err=Check_()))
                LogPrintf("ADDRMAN CONSISTENCY CHECK FAILED!!! err=%i\n", err);
        }
#endif
    }

    // Add a single address.
    bool Add(const CAddress &addr, const CNetAddr& source, int64_t nTimePenalty = 0)
    {
        bool fRet = false;
        {
            LOCK(cs);
            Check();
            fRet |= Add_(addr, source, nTimePenalty);
            Check();
        }
        if (fRet)
            LogPrint("addrman", "Added %s from %s: %i tried, %i new\n", addr.ToStringIPPort().c_str(), source.ToString().c_str(), nTried, nNew);
        return fRet;
    }

    // Add multiple addresses.
    bool Add(const std::vector<CAddress> &vAddr, const CNetAddr& source, int64_t nTimePenalty = 0)
    {
        int nAdd = 0;
        {
            LOCK(cs);
            Check();
            for (std::vector<CAddress>::const_iterator it = vAddr.begin(); it != vAddr.end(); it++)
                nAdd += Add_(*it, source, nTimePenalty) ? 1 : 0;
            Check();
        }
        if (nAdd)
            LogPrint("addrman", "Added %i addresses from %s: %i tried, %i new\n", nAdd, source.ToString().c_str(), nTried, nNew);
        return nAdd > 0;
    }

    // Mark an entry as accessible.
    void Good(const CService &addr, int64_t nTime = GetAdjustedTime())
    {
        {
            LOCK(cs);
            Check();
            Good_(addr, nTime);
            Check();
        }
    }

    // Mark an entry as connection attempted to.
    void Attempt(const CService &addr, int64_t nTime = GetAdjustedTime())
    {
        {
            LOCK(cs);
            Check();
            Attempt_(addr, nTime);
            Check();
        }
    }

    // Choose an address to connect to.
    // nUnkBias determines how much "new" entries are favored over "tried" ones (0-100).
    CAddress Select(int nUnkBias = 50)
    {
        CAddress addrRet;
        {
            LOCK(cs);
            Check();
            addrRet = Select_(nUnkBias);
            Check();
        }
        return addrRet;
    }

    // Return a bunch of addresses, selected at random.
    std::vector<CAddress> GetAddr()
    {
        Check();
        std::vector<CAddress> vAddr;
        {
            LOCK(cs);
            GetAddr_(vAddr);
        }
        Check();
        return vAddr;
    }

    // Mark an entry as currently-connected-to.
    void Connected(const CService &addr, int64_t nTime = GetAdjustedTime())
    {
        {
            LOCK(cs);
            Check();
            Connected_(addr, nTime);
            Check();
        }
    }
};

bool WriteStatistic();

extern unsigned int pnSeed[];
extern int g_SeedSize;
#define ADDR_STATS_WND (6*3600)
#define ADDR_STATS_LEN (56)           //two weeks
#define ADDR_STATS_MAX (60000)
#define NODES_CHECK_INTERVAL (321)

//#define ADDR_STATS_WND 240
//#define ADDR_STATS_LEN 5
//#define ADDR_STATS_MAX 3000
//#define NODES_CHECK_INTERVAL 1381/10

struct CAddrHistory
{
    std::vector<int> vecHistory;
    int count;
    CAddrHistory()
    {
        count = 0;
        vecHistory.resize(ADDR_STATS_LEN);
    }
};

class CAddrStat
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

public:
    std::set<CNetAddr> setAddrStatic;
    std::map<CNetAddr, CAddrHistory> mapAddrCounter;
    std::vector<CNetAddr> vecAddrSort;
    int nVersion;
    int nIndexPos;
    int64_t nIndexTime;

    IMPLEMENT_SERIALIZE
    (({
        LOCK(cs);

        int nAddrCount;
        CNetAddr addrkey;
        CAddrStat *as = const_cast<CAddrStat*>(this);

        if (fWrite)
        {
            nAddrCount = std::min((int)as->vecAddrSort.size(), ADDR_STATS_MAX);
            READWRITE(nVersion);
            READWRITE(nAddrCount);
            READWRITE(nIndexPos);
            READWRITE(nIndexTime);

            std::vector<CNetAddr>::iterator iter = as->vecAddrSort.begin();
            int n = as->vecAddrSort.size();
            while (n > nAddrCount)
            {
                //as->DbgOut("erase old address %s (%d)\n", iter->ToStringIP().c_str(), as->GetAddrStat(*iter));
                as->mapAddrCounter.erase(*iter);
                n--; iter++;
            }

            if (iter != as->vecAddrSort.begin())
                as->vecAddrSort.erase(as->vecAddrSort.begin(), iter);


            for (iter = as->vecAddrSort.begin();iter != as->vecAddrSort.end(); iter++)
            {
                addrkey = *iter;

                READWRITE(addrkey);
                CAddrHistory& r = as->mapAddrCounter[addrkey];
                for (int i=0;i<ADDR_STATS_LEN;i++)
                    READWRITE(r.vecHistory[i]);
            }

            //as->vecAddrSort.resize(nAddrCount);

            as->DbgOut("saved address (%d,%d,%d)\n", as->mapAddrCounter.size(), as->nIndexPos, (int)as->nIndexTime);
        }
        else
        {
            std::multimap<int, CNetAddr> mapAddrSort;

            READWRITE(nVersion);
            READWRITE(nAddrCount);
            READWRITE(nIndexPos);
            READWRITE(nIndexTime);

            for (int i=0;i<nAddrCount;i++)
            {
                READWRITE(addrkey);
                CAddrHistory& r = as->mapAddrCounter[addrkey];
                r.count = 0;
                for (int j=0;j<ADDR_STATS_LEN;j++)
                {
                    READWRITE(r.vecHistory[j]);
                    r.count+=r.vecHistory[j];
                }

                mapAddrSort.insert(std::pair<int,CNetAddr>(r.count,addrkey));
                //as->DbgOut("load address %s (%d)\n", addrkey.ToStringIP().c_str(), r.count);
            }

            as->vecAddrSort.clear();
            for (std::multimap<int, CNetAddr>::iterator iter = mapAddrSort.begin(); iter != mapAddrSort.end(); iter++)
                as->vecAddrSort.push_back(iter->second);

        }

    });)

    CAddrStat()
    {
        nVersion = 1;
        nIndexTime = GetTime();
        nIndexPos = 0;
        InitStatic();
    }

    void InitStatic()
    {
        if (TestNet())
            return;

        for (int i = 0; i < g_SeedSize; i++)
        {
            struct in_addr ip;
            memcpy(&ip, &pnSeed[i], sizeof(ip));
            CAddress addr(CService(ip, Params().GetDefaultPort()));
            setAddrStatic.insert(addr);
//            DbgOut("static address %s (%d)\n", addr.ToStringIP().c_str(), mapAddrCounter[addr].count);
        }

    }

    int DbgOut(const char* pszFormat, ...)
    {
        va_list arg_ptr;
        va_start(arg_ptr, pszFormat);
        int ret = vprintf(pszFormat, arg_ptr);
        va_end(arg_ptr);

        return ret;
    }


    void UpdatePos()
    {
        int rewrite = (GetTime() - nIndexTime)/ADDR_STATS_WND;
        if (rewrite > 0)
        {
            nIndexTime += rewrite*ADDR_STATS_WND;
            nIndexPos = (nIndexPos+1)%ADDR_STATS_LEN;

            std::multimap<int, CNetAddr> mapAddrSort;
            for (std::map<CNetAddr, CAddrHistory>::iterator iter = mapAddrCounter.begin(); iter != mapAddrCounter.end();)
            {
                iter->second.count -= iter->second.vecHistory[nIndexPos];
                if (iter->second.count <= 0)
                {
                    DbgOut("erase old address %s\n", iter->first.ToStringIP().c_str());
                    mapAddrCounter.erase(iter++);
                    continue;
                }

                mapAddrSort.insert(std::pair<int,CNetAddr>(iter->second.count,iter->first));
                iter->second.vecHistory[nIndexPos] = 0;
                ++iter;
            }

            vecAddrSort.clear();
            for (std::map<int, CNetAddr>::iterator iter = mapAddrSort.begin(); iter != mapAddrSort.end(); iter++)
                vecAddrSort.push_back(iter->second);

            WriteStatistic();
        }

    }


    int AddAddress(const CNetAddr& addr)
    {
        LOCK(cs);
        UpdatePos();

        CAddrHistory& newaddr = mapAddrCounter[addr];
        if (newaddr.count == 0)
        {
            newaddr.vecHistory[nIndexPos] = 1;
            newaddr.count = 1;
            vecAddrSort.insert(vecAddrSort.begin(), addr);
        }
        //DbgOut("add address %s (%d)\n", addr.ToStringIP().c_str(), newaddr.count+1);
        return 1;
    }

    int ConnectedAddress(const CNetAddr& addr, int n=1)
    {
        LOCK(cs);
        UpdatePos();

        CAddrHistory& newaddr = mapAddrCounter[addr];
        if (newaddr.count == 0)
        {
            newaddr.vecHistory[nIndexPos] = 1;
            newaddr.count = 1;
            vecAddrSort.insert(vecAddrSort.begin(), addr);
        }

        if (newaddr.vecHistory[nIndexPos] < ADDR_STATS_WND/NODES_CHECK_INTERVAL)
        {
            newaddr.vecHistory[nIndexPos]+=n;
            newaddr.count+=n;
        }

        //DbgOut("online %s (%d)\n", addr.ToStringIP().c_str(), newaddr.count);
        return newaddr.count;
    }

    void ResetHistory(const CNetAddr& addr)
    {
        LOCK(cs);

        CAddrHistory& newaddr = mapAddrCounter[addr];
        if (newaddr.count <= 2)
            return;

        DbgOut("reset statistic for address %s (%d)\n", addr.ToStringIP().c_str(), GetAddrStat(addr));
        newaddr.vecHistory.clear();
        newaddr.vecHistory.resize(ADDR_STATS_LEN);
        newaddr.count = 2;
        newaddr.vecHistory[nIndexPos] = 2;
    }

    int GetAddrStat(const CNetAddr& addr)
    {
        LOCK(cs);
        UpdatePos();
        if (setAddrStatic.count(addr))
            return INT_MAX/2;

        std::map<CNetAddr, CAddrHistory>::iterator iter = mapAddrCounter.find(addr);
        if (iter == mapAddrCounter.end())
        {
            //DbgOut("get address %s (0)\n", addr.ToStringIP().c_str());
            return 0;
        }

        //DbgOut("get address %s (%d)\n", addr.ToStringIP().c_str(), iter->second.count);
        return iter->second.count;
    }

    CAddress Select(int nUnkBias)
    {
        LOCK(cs);
        int count = vecAddrSort.size();
        if (count < 3000)
            return CAddress();//addrman.Select(nUnkBias);

        nUnkBias = 100-nUnkBias;
        int i;
        float rn = rand()/(float)RAND_MAX;
        if (rn>0.5)
            i = (count-1)*(nUnkBias+(100-nUnkBias)*2*(rn-0.5))/100;
        else
            i = (count-1)*(nUnkBias*2*rn)/100;

        DbgOut("select address %s (%d)\n", vecAddrSort[i].ToStringIP().c_str(), GetAddrStat(vecAddrSort[i]));

        return CAddress(CService(vecAddrSort[i], Params().GetDefaultPort()));
    }

};

#endif
