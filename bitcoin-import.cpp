//
//  bitcoin-import.cpp
//  bitcoin
//
//  Created by Paul Ciarlo on 12/24/17.
//

#include "bitcoin-import.hpp"

#include "scheduler.h"
#include "txdb.h"
#include "chainparamsbase.h"
#include "chainparams.h"
#include "consensus/validation.h"
#include "validation.h"
#include "validationinterface.h"
//#include "validationstate.h"
#include "coins.h"
#include "script/sigcache.h"
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>

#include <signal.h>
#include <thread>
#include <mutex>
#include <condition_variable>

std::atomic_bool running;
std::mutex m;
std::condition_variable interrupt;

void sigintHandler(int) {
    std::unique_lock<std::mutex> l(m);
    running = false;
    interrupt.notify_all();
}

/**
 * This is a minimally invasive approach to shutdown on LevelDB read errors from the
 * chainstate, while keeping user interface out of the common library, which is shared
 * between bitcoind, and bitcoin-qt and non-server tools.
*/
class CCoinsViewErrorCatcher final : public CCoinsViewBacked
{
public:
    explicit CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override {
        try {
            return CCoinsViewBacked::GetCoin(outpoint, coin);
        } catch(const std::runtime_error& e) {
            //uiInterface.ThreadSafeMessageBox(_("Error reading from database, shutting down."), "", CClientUIInterface::MSG_ERROR);
            LogPrintf("Error reading from database: %s\n", e.what());
            // Starting the shutdown sequence and returning false to the caller would be
            // interpreted as 'entry not found' (as opposed to unable to read data), and
            // could lead to invalid interpretation. Just exit immediately, as we can't
            // continue anyway, and all writes should be atomic.
            abort();
        }
    }
    // Writes do not need similar protection, as failure to write is handled by the caller.
};

extern std::unique_ptr<CCoinsViewCache> pcoinsTip;
extern std::unique_ptr<CCoinsViewDB> pcoinsdbview;
extern std::unique_ptr<CBlockTreeDB> pblocktree;
extern bool fPrintToConsole;

class MyValidationInterface : public CValidationInterface {
public:
    MyValidationInterface() {}
protected:
    /**
     * Notifies listeners of updated block chain tip
     *
     * Called on a background thread.
     */
    virtual void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override {
        LogPrintf("MyValidationInterface::UpdatedBlockTip(%p,%p,%b)\n", pindexNew, pindexFork, fInitialDownload);
    
    }
    /**
     * Notifies listeners of a transaction having been added to mempool.
     *
     * Called on a background thread.
     */
    virtual void TransactionAddedToMempool(const CTransactionRef &ptxn) override {
        LogPrintf("MyValidationInterface::UpdatedBlockTip(%s)\n", ptxn->GetHash().GetHex());
    }
    /**
     * Notifies listeners of a transaction leaving mempool.
     *
     * This only fires for transactions which leave mempool because of expiry,
     * size limiting, reorg (changes in lock times/coinbase maturity), or
     * replacement. This does not include any transactions which are included
     * in BlockConnectedDisconnected either in block->vtx or in txnConflicted.
     *
     * Called on a background thread.
     */
    virtual void TransactionRemovedFromMempool(const CTransactionRef &ptx) override {
        LogPrintf("MyValidationInterface::TransactionRemovedFromMempool(%s)\n", ptx->GetHash().GetHex());
    }
    /**
     * Notifies listeners of a block being connected.
     * Provides a vector of transactions evicted from the mempool as a result.
     *
     * Called on a background thread.
     */
    virtual void BlockConnected(const std::shared_ptr<const CBlock> &block, const CBlockIndex *pindex, const std::vector<CTransactionRef> &txnConflicted) override {
        LogPrintf("MyValidationInterface::BlockConnected(%s,%s,%d)\n", block->ToString(), pindex->GetBlockHash().GetHex(), txnConflicted.size());
        //block->Get
    }
    /**
     * Notifies listeners of a block being disconnected
     *
     * Called on a background thread.
     */
    virtual void BlockDisconnected(const std::shared_ptr<const CBlock> &block) override {
        LogPrintf("MyValidationInterface::BlockDisconnected(%s)\n", block->ToString());
    }
    /**
     * Notifies listeners of the new active block chain on-disk.
     *
     * Called on a background thread.
     */
    virtual void SetBestChain(const CBlockLocator &locator) override {
        LogPrintf("MyValidationInterface::SetBestChain(%%s)\n");
    }
    /**
     * Notifies listeners about an inventory item being seen on the network.
     *
     * Called on a background thread.
     */
    virtual void Inventory(const uint256 &hash) override {
        LogPrintf("MyValidationInterface::Inventory(%s)\n", hash.GetHex());
    }
    /** Tells listeners to broadcast their data. */
    virtual void ResendWalletTransactions(int64_t nBestBlockTime, CConnman* connman) override {
        LogPrintf("MyValidationInterface::ResendWalletTransactions(%d, %s)\n", nBestBlockTime, connman);
    }
    /**
     * Notifies listeners of a block validation result.
     * If the provided CValidationState IsValid, the provided block
     * is guaranteed to be the current best block at the time the
     * callback was generated (not necessarily now)
     */
    virtual void BlockChecked(const CBlock& blk, const CValidationState& state) override {
        LogPrintf("MyValidationInterface::BlockChecked(%s, %s)\n", blk.ToString(), state.GetDebugMessage());
    }
    /**
     * Notifies listeners that a block which builds directly on our current tip
     * has been received and connected to the headers tree, though not validated yet */
    virtual void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock>& block) override {
        LogPrintf("MyValidationInterface::NewPoWValidBlock(%s, %s)\n", pindex->GetBlockHash().GetHex(), block->ToString());
    };
};

int main(int argc, char **argv) {
    fPrintToConsole = true;
    {
        signal(SIGINT, sigintHandler);
        std::lock_guard<std::mutex> g(m);
        running = true;
    }
    
    boost::thread_group threadGroup;
    CScheduler scheduler;
    
    SelectBaseParams(CBaseChainParams::MAIN);
    SelectParams(CBaseChainParams::MAIN);
    
    //std::unique_ptr<CBlockTreeDB> pblocktree;
    
    //std::unique_ptr<CCoinsViewCache> pcoinsTip;
    //std::unique_ptr<CCoinsViewErrorCatcher> pcoinscatcher;
    
    //std::unique_ptr<MyValidationInterface> pivalidation;
    MyValidationInterface ivalidation;
    
    // Start the lightweight task scheduler thread
    CScheduler::Function serviceLoop = boost::bind(&CScheduler::serviceQueue, &scheduler);
    threadGroup.create_thread(boost::bind(&TraceThread<CScheduler::Function>, "scheduler", serviceLoop));

    //GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);
    //GetMainSignals().RegisterWithMempoolSignals(mempool);
    
    
    //CScheduler *scheduler = new CScheduler;
    //boost::thread* t = new boost::thread(boost::bind(CScheduler::serviceQueue, s));
    GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);
    
    RegisterValidationInterface(&ivalidation);

    pblocktree.reset(new CBlockTreeDB(1024, false, false));
    pcoinsdbview.reset(new CCoinsViewDB(1024, false, false));
    //pcoinscatcher.reset(new CCoinsViewErrorCatcher(pcoinsdbview.get()));
    pcoinsTip.reset(new CCoinsViewCache(pcoinsdbview.get()));
    
    //Params().Set
    
    //LoadGenesisBlock(Params());
    //LoadBlockIndex(Params());
    //LoadExternalBlockFile(Params(), fopen("/Users/paulyc/.bitcoin/blocks/blk00000.dat", "rb"));
    //LoadExternalBlockFile(Params(), fopen("/Users/paulyc/.bitcoin/blocks/blk00001.dat", "rb"));
    //LoadExternalBlockFile(Params(), fopen("/Users/paulyc/.bitcoin/blocks/blk00002.dat", "rb"));
    
    InitScriptExecutionCache();
    InitSignatureCache();
    
    

    {
        //std::mutex local;
        //std::condition_variable op_wait;
        std::vector<fs::path> vImportFiles = { "/Users/paulyc/.bitcoin/blocks/blk00000.dat" };
        //std::unique_lock<std::mutex> lock(local);
        //bool done = false;
        CValidationState state;
        
        boost::thread *th = threadGroup.create_thread([&](){
        //scheduler.schedule([&](){
            //std::unique_lock<std::mutex> l(local);
            //int nFile = 0;
            //CDiskBlockPos pos(nFile, 0);
            //if (!fs::exists(GetBlockPosFilename(pos, "blk")))
            //    break; // No block files left to reindex
            for (auto path : vImportFiles) {
                FILE *f = fopen(path.c_str(), "rb");
                //FILE *f = OpenBlockFile(pos, true);
                LoadExternalBlockFile(Params(), f);//, &pos);
                //++nFile;
            }
            ActivateBestChain(state, Params());
            //done = true;
            //op_wait.notify_all();
        });

        //while (!done) {
        //    op_wait.wait(lock);
        //}
        th->join();
        cout << "done w block load, moving on.." << endl;
    }
    /*
    {
        CValidationState state;
        std::mutex local;
        std::condition_variable op_wait;
        std::unique_lock<std::mutex> lock(local);
        bool done = false;
        boost::thread *th = threadGroup.create_thread([&](){
            std::unique_lock<std::mutex> l(local);
            ActivateBestChain(state, Params());
            done = true;
            op_wait.notify_all();
        });
        while (!done) {
            op_wait.wait(lock);
        }
        th->join();
        cout << "done w activate, moving on.." << endl;
    }*/
    
    /*boost::system_time xt;
    
    int tm = 0;
    while (tm < 30000) {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
        tm += 100;
    }*/
    
    /*CBlockFileInfo info;
    cout << "here" << endl;
    pblocktree->ReadBlockFileInfo(0, info);
    cout << "there" << endl;
    /*tm = 0;
    while (tm < 0) {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
        tm += 100;
    }*/
    /*cout << info.ToString() << endl;
    cout << "elsewhere" << endl;
    /*
    CDBIterator *iter = pblocktree->NewIterator();
    iter->SeekToFirst();
    
    do {
        int key=0;
        CBlockFileInfo val;
        if (! iter->GetValue(val)) break;
        cout << "Key: " << key << " value: " << val.ToString() << endl;
    } while (iter->Next(), true);
    //iter->GetKey
    */
    /*cout << "Waiting for interrupt...\n" << endl;
    while (true) {
        std::unique_lock<std::mutex> l(m);
        if (running) {
            interrupt.wait(l);
        } else {
            break;
        }
    }*/
    
    cout << "stopping" << endl;
    scheduler.stop();
    cout << "shutting down thread group" << endl;
    threadGroup.interrupt_all();
    threadGroup.join_all();
    cout << "Joined" << endl;
    return 0;
}
