// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018 Paul Ciarlo
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
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
#include "base58.h"
#include "mysqlinterface.hpp"
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/hex.hpp>

#include <signal.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>

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

class BlockHeaderInsertStatement : public MySqlPreparedStatement {
public:
        BlockHeaderInsertStatement(std::unique_ptr<MySqlDbConnection> &conn) :
            MySqlPreparedStatement(conn, "INSERT INTO BlockHeader(hash, version, hashPrevBlock, hashMerkleRoot, nonce, bits, time) VALUES(?, ?, ?, ?, ?, ?, ?)") {
        }

        uint64_t execute(const std::shared_ptr<const CBlock> &block, MySqlDbConnection& conn) {
            cout << "BlockHeaderInsertStatement::execute " << sql << endl;
            stmt->setString(1, block->GetHash().GetHex());
            stmt->setInt(2, block->nVersion);
            stmt->setString(3, block->hashPrevBlock.GetHex());
            stmt->setString(4, block->hashMerkleRoot.GetHex());
            stmt->setInt(5, block->nNonce);
            stmt->setInt(6, block->nBits);
            stmt->setInt(7, block->nTime);
           // sql::ResultSet *rs = stmt->executeQuery();
           stmt->execute();
            //cout << rs << endl;
            //rs->insert_id;
            //sql::mysql::MySQL_Prepared_ResultSet* myrs = (sql::mysql::MySQL_Prepared_ResultSet*)rs;
            //uint64_t insId = myrs->proxy->px->stmt->insert_id;
            //cout << rs->getInt64(1) << endl;
            return conn.getLastInsertId();
        }
};

class BlockDAO {
public:
    BlockDAO(std::unique_ptr<MySqlDbConnection> &conn) : hdrInsert(std::unique_ptr<BlockHeaderInsertStatement>(new BlockHeaderInsertStatement(conn))) {
    
    }
    uint64_t insertBlock(const std::shared_ptr<const CBlock> &block, MySqlDbConnection& conn) {
        return hdrInsert->execute(block, conn);
    }
private:
    std::unique_ptr<BlockHeaderInsertStatement> hdrInsert;
};

class TransactionInsertStatement : public MySqlPreparedStatement {
public:
        TransactionInsertStatement(std::unique_ptr<MySqlDbConnection> &conn) :
            MySqlPreparedStatement(conn, "INSERT INTO Transaction(blockId, txid, wtxid, locktime) VALUES(?, ?, ?, ?)") {
        }
    
        uint64_t execute(uint64_t blkId, const std::shared_ptr<const CTransaction> &tx, MySqlDbConnection& conn) {
            cout << "TransactionInsertStatement::execute " << sql << endl;
            stmt->setUInt64(1, blkId);
            stmt->setString(2, tx->GetHash().GetHex());
            stmt->setString(3, tx->GetWitnessHash().GetHex());
            stmt->setInt(4, tx->nLockTime);
            stmt->execute();
            return conn.getLastInsertId();
        }
};

void toHex(std::stringstream &out, std::vector<unsigned char> &in) {
    for (auto i = in.begin(); i != in.end(); i++) {
        switch(*i & 0xf0) {
            case 0:
                out << '0';
                break;
            case 1:
                out << '1';
                break;
            case 2:
                out << '2';
                break;
            case 3:
                out << '3';
                break;
            case 4:
                out << '4';
                break;
            case 5:
                out << '5';
                break;
            case 6:
                out << '6';
                break;
            case 7:
                out << '7';
                break;
            case 8:
                out << '8';
                break;
            case 9:
                out << '9';
                break;
            case 0xa:
                out << 'a';
                break;
            case 0xb:
                out << 'b';
                break;
            case 0xc:
                out << 'c';
                break;
            case 0xd:
                out << 'd';
                break;
            case 0xe:
                out << 'e';
                break;
            case 0xf:
                out << 'f';
                break;
        }
        switch(*i & 0x0f) {
            case 0:
                out << '0';
                break;
            case 1:
                out << '1';
                break;
            case 2:
                out << '2';
                break;
            case 3:
                out << '3';
                break;
            case 4:
                out << '4';
                break;
            case 5:
                out << '5';
                break;
            case 6:
                out << '6';
                break;
            case 7:
                out << '7';
                break;
            case 8:
                out << '8';
                break;
            case 9:
                out << '9';
                break;
            case 0xa:
                out << 'a';
                break;
            case 0xb:
                out << 'b';
                break;
            case 0xc:
                out << 'c';
                break;
            case 0xd:
                out << 'd';
                break;
            case 0xe:
                out << 'e';
                break;
            case 0xf:
                out << 'f';
                break;
        }
    }
}

class TxInputInsertStatement : public MySqlPreparedStatement {
public:
    TxInputInsertStatement(std::unique_ptr<MySqlDbConnection> &conn) :
        MySqlPreparedStatement(conn, "INSERT INTO TxInput(txId, nSequence, prevoutHash, prevoutN, scriptSig, scriptWitness) VALUES(?, ?, ?, ?, ?, ?)")
    {}
    
    uint64_t execute(uint64_t txId, const CTxIn &txIn, MySqlDbConnection& conn) {
        std::vector<unsigned char> scriptSigSerialized = ToByteVector(txIn.scriptSig);
        //std::vector<unsigned char> scriptWitnessSerialized = ToByteVector(txIn.scriptWitness.);
        std::stringstream scriptSigAsHex;
        toHex(scriptSigAsHex, scriptSigSerialized);
        std::string scriptWitnessAsHex = "";
        cout << "TxInputInsertStatement::execute " << sql << ' ' << scriptSigAsHex.str() << endl;
        stmt->setUInt64(1, txId);
        stmt->setInt(2, txIn.nSequence);
        stmt->setString(3, txIn.prevout.hash.GetHex());
        stmt->setInt(4, txIn.prevout.n);
        stmt->setString(5, scriptSigAsHex.str());
        stmt->setString(6, scriptWitnessAsHex);
        stmt->execute();
        return conn.getLastInsertId();
    }
};

class TxOutputInsertStatement : public MySqlPreparedStatement {
public:
    TxOutputInsertStatement(std::unique_ptr<MySqlDbConnection> &conn) :
        MySqlPreparedStatement(conn, "INSERT INTO TxOutput(txId, indexN, value, scriptPubKey) VALUES(?, ?, ?, ?)")
        {}
    
    uint64_t execute(uint64_t txId, int n, const CTxOut &txOut, MySqlDbConnection& conn) {
        std::vector<unsigned char> scriptPubKeySerialized = ToByteVector(txOut.scriptPubKey);
        std::stringstream scriptPubKeyHex;
        toHex(scriptPubKeyHex, scriptPubKeySerialized);
        cout << "TxOutputInsertStatement::execute " << sql << ' ' << scriptPubKeyHex.str() << endl;
            //txOut.scriptPubKey.
        stmt->setUInt64(1, txId);
        stmt->setInt(2, n);
        stmt->setUInt64(3, txOut.nValue);
        stmt->setString(4, scriptPubKeyHex.str());
        stmt->execute();
        return conn.getLastInsertId();
    }
};

class TxDAO {
public:
    TxDAO(std::unique_ptr<MySqlDbConnection> &conn) :
        txInsert(std::unique_ptr<TransactionInsertStatement>(new TransactionInsertStatement(conn))),
        txInInsert(std::unique_ptr<TxInputInsertStatement>(new TxInputInsertStatement(conn))),
        txOutInsert(std::unique_ptr<TxOutputInsertStatement>(new TxOutputInsertStatement(conn)))
    {}
    uint64_t insertTransaction(uint64_t blkId, const std::shared_ptr<const CTransaction> &tx, MySqlDbConnection& conn) {
        uint64_t txId = txInsert->execute(blkId, tx, conn);
        for (auto txIn : tx->vin) {
            txInInsert->execute(txId, txIn, conn);
        }
        //for (auto txOut : tx->vout) {
        for (int i = 0; i < tx->vout.size(); ++i) {
            txOutInsert->execute(txId, i, tx->vout[i], conn);
        }
        return txId;
    }
    private:
    std::unique_ptr<TransactionInsertStatement> txInsert;
    std::unique_ptr<TxInputInsertStatement> txInInsert;
    std::unique_ptr<TxOutputInsertStatement> txOutInsert;
};

class MyValidationInterface : public CValidationInterface {
public:
    MyValidationInterface() {
        drv = std::unique_ptr<MySqlDbDriver>(new MySqlDbDriver());
        conn = drv->getConnection();
        blockDao = std::unique_ptr<BlockDAO>(new BlockDAO(conn));
        txDao = std::unique_ptr<TxDAO>(new TxDAO(conn));
    }
protected:
    std::unique_ptr<BlockDAO> blockDao;
    std::unique_ptr<TxDAO> txDao;
    std::unique_ptr<MySqlDbConnection> conn;
    std::unique_ptr<MySqlDbDriver> drv;
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
     //std::stringstream dontdothis;
     //std::
    
    virtual void BlockConnected(const std::shared_ptr<const CBlock> &block, const CBlockIndex *pindex, const std::vector<CTransactionRef> &txnConflicted) override {
        LogPrintf("MyValidationInterface::BlockConnected(%s,%s,%d)\n", block->ToString(), pindex->GetBlockHash().GetHex(), txnConflicted.size());
        conn->tryTransaction([block,this](MySqlDbConnection& conn) {
            uint64_t blkId = blockDao->insertBlock(block, conn);
            for (auto tx : block->vtx) {
                txDao->insertTransaction(blkId, tx, conn);
            }
        });
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

int main1(int argc, char **argv) {
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
    
    std::unique_ptr<MySqlDbDriver> dbDrv = std::unique_ptr<MySqlDbDriver>(new MySqlDbDriver);

    {
        //std::mutex local;
        //std::condition_variable op_wait;
        std::vector<fs::path> vImportFiles = { "/Users/paulyc/.bitcoin/blocks/blk00000.dat" };
        //std::unique_lock<std::mutex> lock(local);
        //bool done = false;
        CValidationState state;
        
        
        //boost::thread *dbThread = threadGroup.create_thread(MySqlDbDriver::threadfun);
        
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

class BTC2BTG : public CBase58Data {

static const uint8_t vBTC = 0;
static const uint8_t vBTG = 38;
static const uint8_t vBTC_S = 5;
static const uint8_t vBTG_S = 23;

public:
    //BTC2BTG() : CBase58Data() {}

    static std::string convAddr(const std::string &btc) {
        std::vector<unsigned char> vchRet;
        if (DecodeBase58Check(btc, vchRet)) {
            switch (vchRet[0]) {
                case vBTC:
                    vchRet[0] = vBTG;
                    break;
                case vBTG:
                    vchRet[0] = vBTC;
                    break;
                case vBTC_S:
                    vchRet[0] = vBTG_S;
                    break;
                case vBTG_S:
                    vchRet[0] = vBTC_S;
                    break;
                default:
                    cerr << "Unknown address type " << vchRet[0] << endl;
                    return "";
            }
            return EncodeBase58Check(vchRet);
        } else {
            cerr << "Failed DecodeBase58Check(" << btc << ")" << endl;
            return "";
        }
    }
};

#include "pubkeys.hpp"

int main2(int argc, char **argv) {
    for (std::string &pubkey : pubkeys) {
        cout << BTC2BTG::convAddr(pubkey) << endl;
    }
    return 0;
}

int main(int argc, char **argv) {
    return main2(argc, argv);
}
