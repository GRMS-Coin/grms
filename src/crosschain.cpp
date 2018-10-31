#include "cc/eval.h"
#include "crosschain.h"
#include "importcoin.h"
#include "main.h"
#include "notarisationdb.h"

/*
 * The crosschain workflow.
 *
 * 3 chains, A, B, and KMD. We would like to prove TX on B.
 * There is a notarisation, nA0, which will include TX via an MoM.
 * The notarisation nA0 must fall between 2 notarisations of B,
 * ie, nB0 and nB1. An MoMoM including this range is propagated to
 * B in notarisation receipt (backnotarisation) bnB2.
 *
 * A:                 TX   bnA0
 *                     \   /
 * KMD:      nB0        nA0     nB1      nB2
 *              \                 \       \
 * B:          bnB0              bnB1     bnB2
 */

// XXX: There are potential crashes wherever we access chainActive without a lock,
// because it might be disconnecting blocks at the same time.


int NOTARISATION_SCAN_LIMIT_BLOCKS = 1440;


/* On KMD */
uint256 CalculateProofRoot(const char* symbol, uint32_t targetCCid, int kmdHeight,
        std::vector<uint256> &moms, uint256 &destNotarisationTxid)
{
    /*
     * Notaries don't wait for confirmation on KMD before performing a backnotarisation,
     * but we need a determinable range that will encompass all merkle roots. Include MoMs
     * including the block height of the last notarisation until the height before the
     * previous notarisation.
     *
     *    kmdHeight      notarisations-0      notarisations-1
     *                         *********************|
     *        > scan backwards >
     */

    if (targetCCid < 2)
        return uint256();

    if (kmdHeight < 0 || kmdHeight > chainActive.Height())
        return uint256();

    int seenOwnNotarisations = 0;

    int authority = GetSymbolAuthority(symbol);

    for (int i=0; i<NOTARISATION_SCAN_LIMIT_BLOCKS; i++) {
        if (i > kmdHeight) break;
        NotarisationsInBlock notarisations;
        uint256 blockHash = *chainActive[kmdHeight-i]->phashBlock;
        if (!GetBlockNotarisations(blockHash, notarisations))
            continue;

        // See if we have an own notarisation in this block
        BOOST_FOREACH(Notarisation& nota, notarisations) {
            if (strcmp(nota.second.symbol, symbol) == 0)
            {
                seenOwnNotarisations++;
                if (seenOwnNotarisations == 1)
                    destNotarisationTxid = nota.first;
                else if (seenOwnNotarisations == 2)
                    goto end;
                fprintf(stderr, "kmd heigt notarisation added: %d\n",kmdHeight-i);
                break;
            }
        }

        if (seenOwnNotarisations == 1) {
            BOOST_FOREACH(Notarisation& nota, notarisations) {
                if (GetSymbolAuthority(nota.second.symbol) == authority)
                    if (nota.second.ccId == targetCCid) {
                      moms.push_back(nota.second.MoM);
                      fprintf(stderr, "added mom: %s\n",nota.second.MoM.GetHex().data());
                    }
            }
        }
    }

    // Not enough own notarisations found to return determinate MoMoM
    destNotarisationTxid = uint256();
    moms.clear();
    return uint256();

end:
    return GetMerkleRoot(moms);
}


/*
 * Get a notarisation from a given height
 *
 * Will scan notarisations leveldb up to a limit
 */
template <typename IsTarget>
int ScanNotarisationsFromHeight(int nHeight, const IsTarget f, Notarisation &found)
{
    int limit = std::min(nHeight + NOTARISATION_SCAN_LIMIT_BLOCKS, chainActive.Height());

    for (int h=nHeight; h<limit; h++) {
        NotarisationsInBlock notarisations;

        if (!GetBlockNotarisations(*chainActive[h]->phashBlock, notarisations))
            continue;

        BOOST_FOREACH(found, notarisations) {
            if (f(found)) {
                return h;
            }
        }
    }
    return 0;
}


/* On KMD */
TxProof GetCrossChainProof(const uint256 txid, const char* targetSymbol, uint32_t targetCCid,
        const TxProof assetChainProof)
{
    /*
     * Here we are given a proof generated by an assetchain A which goes from given txid to
     * an assetchain MoM. We need to go from the notarisationTxid for A to the MoMoM range of the
     * backnotarisation for B (given by kmdheight of notarisation), find the MoM within the MoMs for
     * that range, and finally extend the proof to lead to the MoMoM (proof root).
     */
    EvalRef eval;
    uint256 MoM = assetChainProof.second.Exec(txid);

    // Get a kmd height for given notarisation Txid
    int kmdHeight;
    {
        CTransaction sourceNotarisation;
        uint256 hashBlock;
        CBlockIndex blockIdx;
        if (!eval->GetTxConfirmed(assetChainProof.first, sourceNotarisation, blockIdx))
            throw std::runtime_error("Notarisation not found");
        kmdHeight = blockIdx.nHeight;
    }

    // We now have a kmdHeight of the notarisation from chain A. So we know that a MoM exists
    // at that height.
    // If we call CalculateProofRoot with that height, it'll scan backwards, until it finds
    // a notarisation from B, and it might not include our notarisation from A
    // at all. So, the thing we need to do is scan forwards to find the notarisation for B,
    // that is inclusive of A.
    Notarisation nota;
    auto isTarget = [&](Notarisation &nota) {
        return strcmp(nota.second.symbol, targetSymbol) == 0;
    };
    kmdHeight = ScanNotarisationsFromHeight(kmdHeight, isTarget, nota);
    if (!kmdHeight)
        throw std::runtime_error("Cannot find notarisation for target inclusive of source");

    // Get MoMs for kmd height and symbol
    std::vector<uint256> moms;
    uint256 targetChainNotarisationTxid;
    uint256 MoMoM = CalculateProofRoot(targetSymbol, targetCCid, kmdHeight, moms, targetChainNotarisationTxid);
    if (MoMoM.IsNull())
        throw std::runtime_error("No MoMs found");

    printf("[%s] GetCrossChainProof MoMoM: %s\n", targetSymbol,MoMoM.GetHex().data());
    FILE * fptr;
    fptr = fopen("/home/cc/momom_on_kmd", "a+");
    fprintf(fptr, "%s\n", MoMoM.GetHex().data());
    fclose(fptr);

    // Find index of source MoM in MoMoM
    int nIndex;
    for (nIndex=0; nIndex<moms.size(); nIndex++) {
        if (moms[nIndex] == MoM)
            goto cont;
    }
    throw std::runtime_error("Couldn't find MoM within MoMoM set");
cont:

    // Create a branch
    std::vector<uint256> vBranch;
    {
        CBlock fakeBlock;
        for (int i=0; i<moms.size(); i++) {
            CTransaction fakeTx;
            // first value in CTransaction memory is it's hash
            memcpy((void*)&fakeTx, moms[i].begin(), 32);
            fakeBlock.vtx.push_back(fakeTx);
        }
        vBranch = fakeBlock.GetMerkleBranch(nIndex);
    }

    // Concatenate branches
    MerkleBranch newBranch = assetChainProof.second;
    newBranch << MerkleBranch(nIndex, vBranch);

    // Check proof
    if (newBranch.Exec(txid) != MoMoM)
        throw std::runtime_error("Proof check failed");

    return std::make_pair(targetChainNotarisationTxid,newBranch);
}


/*
 * Takes an importTx that has proof leading to assetchain root
 * and extends proof to cross chain root
 */
void CompleteImportTransaction(CTransaction &importTx)
{
    TxProof proof;
    CTransaction burnTx;
    std::vector<CTxOut> payouts;
    if (!UnmarshalImportTx(importTx, proof, burnTx, payouts))
        throw std::runtime_error("Couldn't parse importTx");

    std::string targetSymbol;
    uint32_t targetCCid;
    uint256 payoutsHash;
    if (!UnmarshalBurnTx(burnTx, targetSymbol, &targetCCid, payoutsHash))
        throw std::runtime_error("Couldn't parse burnTx");

    proof = GetCrossChainProof(burnTx.GetHash(), targetSymbol.data(), targetCCid, proof);

    importTx = MakeImportCoinTransaction(proof, burnTx, payouts);
}


bool IsSameAssetChain(const Notarisation &nota) {
    return strcmp(nota.second.symbol, ASSETCHAINS_SYMBOL) == 0;
};


/* On assetchain */
bool GetNextBacknotarisation(uint256 kmdNotarisationTxid, Notarisation &out)
{
    /*
     * Here we are given a txid, and a proof.
     * We go from the KMD notarisation txid to the backnotarisation,
     * then jump to the next backnotarisation, which contains the corresponding MoMoM.
     */
    Notarisation bn;
    if (!GetBackNotarisation(kmdNotarisationTxid, bn))
        return false;

    // Need to get block height of that backnotarisation
    EvalRef eval;
    CBlockIndex block;
    CTransaction tx;
    if (!eval->GetTxConfirmed(bn.first, tx, block)){
        fprintf(stderr, "Can't get height of backnotarisation, this should not happen\n");
        return false;
    }

    return (bool) ScanNotarisationsFromHeight(block.nHeight+1, &IsSameAssetChain, out);
}


/*
 * On assetchain
 * in: txid
 * out: pair<notarisationTxHash,merkleBranch>
 */
TxProof GetAssetchainProof(uint256 hash)
{
    int nIndex;
    CBlockIndex* blockIndex;
    Notarisation nota;
    std::vector<uint256> branch;

    {
        uint256 blockHash;
        CTransaction tx;
        if (!GetTransaction(hash, tx, blockHash, true))
            throw std::runtime_error("cannot find transaction");

        if (blockHash.IsNull())
            throw std::runtime_error("tx still in mempool");

        blockIndex = mapBlockIndex[blockHash];
        int h = blockIndex->nHeight;
        // The assumption here is that the first notarisation for a height GTE than
        // the transaction block height will contain the corresponding MoM. If there
        // are sequence issues with the notarisations this may fail.
        auto isTarget = [&](Notarisation &nota) {
            if (!IsSameAssetChain(nota)) return false;
            return nota.second.height >= blockIndex->nHeight;
        };
        if (!ScanNotarisationsFromHeight(blockIndex->nHeight, isTarget, nota))
            throw std::runtime_error("backnotarisation not yet confirmed");

        // index of block in MoM leaves
        nIndex = nota.second.height - blockIndex->nHeight;
    }

    // build merkle chain from blocks to MoM
    {
        std::vector<uint256> leaves, tree;
        for (int i=0; i<nota.second.MoMDepth; i++) {
            uint256 mRoot = chainActive[nota.second.height - i]->hashMerkleRoot;
            leaves.push_back(mRoot);
        }
        bool fMutated;
        BuildMerkleTree(&fMutated, leaves, tree);
        branch = GetMerkleBranch(nIndex, leaves.size(), tree);

        // Check branch
        uint256 ourResult = SafeCheckMerkleBranch(blockIndex->hashMerkleRoot, branch, nIndex);
        if (nota.second.MoM != ourResult)
            throw std::runtime_error("Failed merkle block->MoM");
    }

    // Now get the tx merkle branch
    {
        CBlock block;

        if (fHavePruned && !(blockIndex->nStatus & BLOCK_HAVE_DATA) && blockIndex->nTx > 0)
            throw std::runtime_error("Block not available (pruned data)");

        if(!ReadBlockFromDisk(block, blockIndex,1))
            throw std::runtime_error("Can't read block from disk");

        // Locate the transaction in the block
        int nTxIndex;
        for (nTxIndex = 0; nTxIndex < (int)block.vtx.size(); nTxIndex++)
            if (block.vtx[nTxIndex].GetHash() == hash)
                break;

        if (nTxIndex == (int)block.vtx.size())
            throw std::runtime_error("Error locating tx in block");

        std::vector<uint256> txBranch = block.GetMerkleBranch(nTxIndex);

        // Check branch
        if (block.hashMerkleRoot != CBlock::CheckMerkleBranch(hash, txBranch, nTxIndex))
            throw std::runtime_error("Failed merkle tx->block");

        // concatenate branches
        nIndex = (nIndex << txBranch.size()) + nTxIndex;
        branch.insert(branch.begin(), txBranch.begin(), txBranch.end());
    }

    // Check the proof
    if (nota.second.MoM != CBlock::CheckMerkleBranch(hash, branch, nIndex))
        throw std::runtime_error("Failed validating MoM");

    // All done!
    CDataStream ssProof(SER_NETWORK, PROTOCOL_VERSION);
    return std::make_pair(nota.second.txHash, MerkleBranch(nIndex, branch));
}
