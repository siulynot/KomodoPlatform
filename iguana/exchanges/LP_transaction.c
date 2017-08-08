
/******************************************************************************
 * Copyright © 2014-2017 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

//
//  LP_transaction.c
//  marketmaker
//

bits256 LP_broadcast(char *txname,char *symbol,char *txbytes,bits256 expectedtxid)
{
    char *retstr; bits256 txid; cJSON *retjson,*errorobj; int32_t i,sentflag = 0;
    memset(&txid,0,sizeof(txid));
    for (i=0; i<1; i++)
    {
        if ( (retstr= LP_sendrawtransaction(symbol,txbytes)) != 0 )
        {
            if ( is_hexstr(retstr,0) == 64 )
            {
                decode_hex(txid.bytes,32,retstr);
                if ( bits256_cmp(txid,expectedtxid) == 0 || (bits256_nonz(expectedtxid) == 0 && bits256_nonz(txid) != 0) )
                    sentflag = 1;
            }
            else if ( (retjson= cJSON_Parse(retstr)) != 0 )
            {
                if ( (errorobj= jobj(retjson,"error")) != 0 )
                {
                    if ( jint(errorobj,"code") == -27 ) // "transaction already in block chain"
                    {
                        txid = expectedtxid;
                        sentflag = 1;
                    }
                }
                free_json(retjson);
            }
            char str[65]; printf("sentflag.%d [%s] %s RETSTR.(%s) %s.%s\n",sentflag,txname,txbytes,retstr,symbol,bits256_str(str,txid));
            free(retstr);
        }
        if ( sentflag != 0 )
            break;
    }
    return(txid);
}

bits256 LP_broadcast_tx(char *name,char *symbol,uint8_t *data,int32_t datalen)
{
    bits256 txid; char *signedtx;
    memset(txid.bytes,0,sizeof(txid));
    if ( data != 0 && datalen != 0 )
    {
        signedtx = malloc(datalen*2 + 1);
        init_hexbytes_noT(signedtx,data,datalen);
        txid = bits256_doublesha256(0,data,datalen);
#ifdef BASILISK_DISABLESENDTX
        char str[65]; printf("%s <- dont sendrawtransaction (%s) %s\n",name,bits256_str(str,txid),signedtx);
#else
        txid = LP_broadcast(name,symbol,signedtx,txid);
#endif
        free(signedtx);
    }
    return(txid);
}

int32_t iguana_msgtx_Vset(uint8_t *serialized,int32_t maxlen,struct iguana_msgtx *msgtx,struct vin_info *V)
{
    int32_t vini,j,scriptlen,p2shlen,userdatalen,siglen,plen,need_op0=0,len = 0; uint8_t *script,*redeemscript=0,*userdata=0; struct vin_info *vp;
    for (vini=0; vini<msgtx->tx_in; vini++)
    {
        vp = &V[vini];
        if ( (userdatalen= vp->userdatalen) == 0 )
        {
            userdatalen = vp->userdatalen = msgtx->vins[vini].userdatalen;
            userdata = msgtx->vins[vini].userdata;
        } else userdata = vp->userdata;
        if ( (p2shlen= vp->p2shlen) == 0 )
        {
            p2shlen = vp->p2shlen = msgtx->vins[vini].p2shlen;
            redeemscript = msgtx->vins[vini].redeemscript;
        }
        else
        {
            redeemscript = vp->p2shscript;
            msgtx->vins[vini].redeemscript = redeemscript;
        }
        if ( msgtx->vins[vini].spendlen > 33 && msgtx->vins[vini].spendscript[msgtx->vins[vini].spendlen - 1] == SCRIPT_OP_CHECKMULTISIG )
        {
            need_op0 = 1;
            printf("found multisig spendscript\n");
        }
        if ( redeemscript != 0 && p2shlen > 33 && redeemscript[p2shlen - 1] == SCRIPT_OP_CHECKMULTISIG )
        {
            need_op0 = 1;
            //printf("found multisig redeemscript\n");
        }
        msgtx->vins[vini].vinscript = script = &serialized[len];
        msgtx->vins[vini].vinscript[0] = 0;
        scriptlen = need_op0;
        for (j=0; j<vp->N; j++)
        {
            if ( (siglen= vp->signers[j].siglen) > 0 )
            {
                script[scriptlen++] = siglen;
                memcpy(&script[scriptlen],vp->signers[j].sig,siglen);
                scriptlen += siglen;
            }
        }
        msgtx->vins[vini].scriptlen = scriptlen;
        if ( vp->suppress_pubkeys == 0 && (vp->N > 1 || bitcoin_pubkeylen(&vp->spendscript[1]) != vp->spendscript[0] || vp->spendscript[vp->spendlen-1] != 0xac) )
        {
            for (j=0; j<vp->N; j++)
            {
                if ( (plen= bitcoin_pubkeylen(vp->signers[j].pubkey)) > 0 )
                {
                    script[scriptlen++] = plen;
                    memcpy(&script[scriptlen],vp->signers[j].pubkey,plen);
                    scriptlen += plen;
                }
            }
            msgtx->vins[vini].scriptlen = scriptlen;
        }
        if ( userdatalen != 0 )
        {
            memcpy(&script[scriptlen],userdata,userdatalen);
            msgtx->vins[vini].userdata = &script[scriptlen];
            msgtx->vins[vini].userdatalen = userdatalen;
            scriptlen += userdatalen;
        }
        //printf("USERDATALEN.%d scriptlen.%d redeemlen.%d\n",userdatalen,scriptlen,p2shlen);
        if ( p2shlen != 0 )
        {
            if ( p2shlen < 76 )
                script[scriptlen++] = p2shlen;
            else if ( p2shlen <= 0xff )
            {
                script[scriptlen++] = 0x4c;
                script[scriptlen++] = p2shlen;
            }
            else if ( p2shlen <= 0xffff )
            {
                script[scriptlen++] = 0x4d;
                script[scriptlen++] = (p2shlen & 0xff);
                script[scriptlen++] = ((p2shlen >> 8) & 0xff);
            } else return(-1);
            msgtx->vins[vini].p2shlen = p2shlen;
            memcpy(&script[scriptlen],redeemscript,p2shlen);
            scriptlen += p2shlen;
        }
        len += scriptlen;
    }
    if ( (0) )
    {
        int32_t i; for (i=0; i<len; i++)
            printf("%02x",script[i]);
        printf(" <-script len.%d scriptlen.%d p2shlen.%d user.%d\n",len,scriptlen,p2shlen,userdatalen);
    }
    return(len);
}

int32_t iguana_interpreter(struct iguana_info *coin,cJSON *logarray,int64_t nLockTime,struct vin_info *V,int32_t numvins)
{
    uint8_t script[IGUANA_MAXSCRIPTSIZE],*activescript,savescript[IGUANA_MAXSCRIPTSIZE]; char str[IGUANA_MAXSCRIPTSIZE*2+1]; int32_t vini,scriptlen,activescriptlen,savelen,errs = 0; cJSON *spendscript,*item=0;
    for (vini=0; vini<numvins; vini++)
    {
        savelen = V[vini].spendlen;
        memcpy(savescript,V[vini].spendscript,savelen);
        if ( V[vini].p2shlen > 0 )
        {
            activescript = V[vini].p2shscript;
            activescriptlen = V[vini].p2shlen;
        }
        else
        {
            activescript = V[vini].spendscript;
            activescriptlen = V[vini].spendlen;
        }
        memcpy(V[vini].spendscript,activescript,activescriptlen);
        V[vini].spendlen = activescriptlen;
        spendscript = iguana_spendasm(activescript,activescriptlen);
        if ( activescriptlen < 16 )
            continue;
        //printf("interpreter.(%s)\n",jprint(spendscript,0));
        //printf("bitcoin_assembler ignore_cltverr.%d suppress.%d\n",V[vini].ignore_cltverr,V[vini].suppress_pubkeys);
        if ( (scriptlen= bitcoin_assembler(coin,logarray,script,spendscript,1,nLockTime,&V[vini])) < 0 )
        {
            //printf("bitcoin_assembler error scriptlen.%d\n",scriptlen);
            errs++;
        }
        else if ( scriptlen != activescriptlen || memcmp(script,activescript,scriptlen) != 0 )
        {
            if ( logarray != 0 )
            {
                item = cJSON_CreateObject();
                jaddstr(item,"error","script reconstruction failed");
            }
            init_hexbytes_noT(str,activescript,activescriptlen);
            //printf("activescript.(%s)\n",str);
            if ( logarray != 0 && item != 0 )
                jaddstr(item,"original",str);
            init_hexbytes_noT(str,script,scriptlen);
            //printf("reconstructed.(%s)\n",str);
            if ( logarray != 0 )
            {
                jaddstr(item,"reconstructed",str);
                jaddi(logarray,item);
            } else printf(" scriptlen mismatch.%d vs %d or miscompare\n",scriptlen,activescriptlen);
            errs++;
        }
        memcpy(V[vini].spendscript,savescript,savelen);
        V[vini].spendlen = savelen;
    }
    if ( errs != 0 )
        return(-errs);
    if ( logarray != 0 )
    {
        item = cJSON_CreateObject();
        jaddstr(item,"result","success");
        jaddi(logarray,item);
    }
    return(0);
}

bits256 iguana_str2priv(uint8_t wiftaddr,char *str)
{
    bits256 privkey; int32_t n; uint8_t addrtype; //struct iguana_waccount *wacct=0; struct iguana_waddress *waddr;
    memset(&privkey,0,sizeof(privkey));
    if ( str != 0 )
    {
        n = (int32_t)strlen(str) >> 1;
        if ( n == sizeof(bits256) && is_hexstr(str,sizeof(bits256)) > 0 )
            decode_hex(privkey.bytes,sizeof(privkey),str);
        else if ( bitcoin_wif2priv(wiftaddr,&addrtype,&privkey,str) != sizeof(bits256) )
        {
            //if ( (waddr= iguana_waddresssearch(&wacct,str)) != 0 )
            //    privkey = waddr->privkey;
            //else memset(privkey.bytes,0,sizeof(privkey));
        }
    }
    return(privkey);
}

int32_t iguana_vininfo_create(uint8_t taddr,uint8_t pubtype,uint8_t p2shtype,uint8_t isPoS,uint8_t *serialized,int32_t maxsize,struct iguana_msgtx *msgtx,cJSON *vins,int32_t numinputs,struct vin_info *V)
{
    int32_t i,plen,finalized = 1,len = 0; struct vin_info *vp; //struct iguana_waccount *wacct; struct iguana_waddress *waddr; uint32_t sigsize,pubkeysize,p2shsize,userdatalen;
    msgtx->tx_in = numinputs;
    maxsize -= (sizeof(struct iguana_msgvin) * msgtx->tx_in);
    msgtx->vins = (struct iguana_msgvin *)&serialized[maxsize];
    memset(msgtx->vins,0,sizeof(struct iguana_msgvin) * msgtx->tx_in);
    if ( msgtx->tx_in > 0 && msgtx->tx_in*sizeof(struct iguana_msgvin) < maxsize )
    {
        for (i=0; i<msgtx->tx_in; i++)
        {
            vp = &V[i];
            //printf("VINS.(%s)\n",jprint(jitem(vins,i),0));
            len += iguana_parsevinobj(&serialized[len],maxsize,&msgtx->vins[i],jitem(vins,i),vp);
            if ( msgtx->vins[i].sequence < IGUANA_SEQUENCEID_FINAL )
                finalized = 0;
            if ( msgtx->vins[i].spendscript == 0 )
            {
                /*if ( iguana_RTunspentindfind(coin,&outpt,vp->coinaddr,vp->spendscript,&vp->spendlen,&vp->amount,&vp->height,msgtx->vins[i].prev_hash,msgtx->vins[i].prev_vout,coin->bundlescount-1,0) == 0 )
                 {
                 vp->unspentind = outpt.unspentind;
                 msgtx->vins[i].spendscript = vp->spendscript;
                 msgtx->vins[i].spendlen = vp->spendlen;
                 vp->hashtype = iguana_vinscriptparse(coin,vp,&sigsize,&pubkeysize,&p2shsize,&userdatalen,vp->spendscript,vp->spendlen);
                 vp->userdatalen = userdatalen;
                 printf("V %.8f (%s) spendscript.[%d] userdatalen.%d\n",dstr(vp->amount),vp->coinaddr,vp->spendlen,userdatalen);
                 }*/
            }
            else
            {
                memcpy(vp->spendscript,msgtx->vins[i].spendscript,msgtx->vins[i].spendlen);
                vp->spendlen = msgtx->vins[i].spendlen;
                _iguana_calcrmd160(taddr,pubtype,p2shtype,vp);
                if ( (plen= bitcoin_pubkeylen(vp->signers[0].pubkey)) > 0 )
                    bitcoin_address(vp->coinaddr,taddr,pubtype,vp->signers[0].pubkey,plen);
            }
            if ( vp->M == 0 && vp->N == 0 )
                vp->M = vp->N = 1;
            /*if ( vp->coinaddr[i] != 0 && (waddr= iguana_waddresssearch(&wacct,vp->coinaddr)) != 0 )
             {
             vp->signers[0].privkey = waddr->privkey;
             if ( (plen= bitcoin_pubkeylen(waddr->pubkey)) != vp->spendscript[1] || vp->spendscript[vp->spendlen-1] != 0xac )
             {
             if ( plen > 0 && plen < sizeof(vp->signers[0].pubkey) )
             memcpy(vp->signers[0].pubkey,waddr->pubkey,plen);
             }
             }*/
        }
    }
    return(finalized);
}

int32_t bitcoin_verifyvins(void *ctx,char *symbol,uint8_t taddr,uint8_t pubtype,uint8_t p2shtype,uint8_t isPoS,int32_t height,bits256 *signedtxidp,char **signedtx,struct iguana_msgtx *msgtx,uint8_t *serialized,int32_t maxlen,struct vin_info *V,uint32_t sighash,int32_t signtx,int32_t suppress_pubkeys)
{
    bits256 sigtxid; uint8_t *sig,*script; struct vin_info *vp; char vpnstr[64]; int32_t scriptlen,complete=0,j,vini=0,flag=0,siglen,numvouts,numsigs;
    numvouts = msgtx->tx_out;
    vpnstr[0] = 0;
    *signedtx = 0;
    memset(signedtxidp,0,sizeof(*signedtxidp));
    for (vini=0; vini<msgtx->tx_in; vini++)
    {
        if ( V->p2shscript[0] != 0 && V->p2shlen != 0 )
        {
            script = V->p2shscript;
            scriptlen = V->p2shlen;
            //for (j=0; j<scriptlen; j++)
            //    printf("%02x",script[j]);
            //printf(" V->p2shlen.%d\n",V->p2shlen);
        }
        else
        {
            script = msgtx->vins[vini].spendscript;
            scriptlen = msgtx->vins[vini].spendlen;
        }
        sigtxid = bitcoin_sigtxid(taddr,pubtype,p2shtype,isPoS,height,serialized,maxlen,msgtx,vini,script,scriptlen,sighash,vpnstr,suppress_pubkeys);
        if ( bits256_nonz(sigtxid) != 0 )
        {
            vp = &V[vini];
            vp->sigtxid = sigtxid;
            for (j=numsigs=0; j<vp->N; j++)
            {
                sig = vp->signers[j].sig;
                siglen = vp->signers[j].siglen;
                if ( signtx != 0 && bits256_nonz(vp->signers[j].privkey) != 0 )
                {
                    siglen = bitcoin_sign(ctx,symbol,sig,sigtxid,vp->signers[j].privkey,0);
                    //if ( (plen= bitcoin_pubkeylen(vp->signers[j].pubkey)) <= 0 )
                    bitcoin_pubkey33(ctx,vp->signers[j].pubkey,vp->signers[j].privkey);
                    sig[siglen++] = sighash;
                    vp->signers[j].siglen = siglen;
                    /*char str[65]; printf("SIGTXID.(%s) ",bits256_str(str,sigtxid));
                     int32_t i; for (i=0; i<siglen; i++)
                     printf("%02x",sig[i]);
                     printf(" sig, ");
                     for (i=0; i<plen; i++)
                     printf("%02x",vp->signers[j].pubkey[i]);
                     // s2 = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141 - s1;
                     printf(" SIGNEDTX.[%02x] siglen.%d priv.%s\n",sig[siglen-1],siglen,bits256_str(str,vp->signers[j].privkey));*/
                }
                if ( sig == 0 || siglen == 0 )
                {
                    memset(vp->signers[j].pubkey,0,sizeof(vp->signers[j].pubkey));
                    continue;
                }
                if ( bitcoin_verify(ctx,sig,siglen-1,sigtxid,vp->signers[j].pubkey,bitcoin_pubkeylen(vp->signers[j].pubkey)) < 0 )
                {
                    int32_t k; for (k=0; k<bitcoin_pubkeylen(vp->signers[j].pubkey); k++)
                        printf("%02x",vp->signers[j].pubkey[k]);
                    printf(" SIG.%d.%d ERROR siglen.%d\n",vini,j,siglen);
                }
                else
                {
                    flag++;
                    numsigs++;
                    /*int32_t z; char tmpaddr[64];
                    for (z=0; z<siglen-1; z++)
                        printf("%02x",sig[z]);
                    printf(" <- sig[%d]\n",j);
                    for (z=0; z<33; z++)
                        printf("%02x",vp->signers[j].pubkey[z]);
                    bitcoin_address(tmpaddr,60,vp->signers[j].pubkey,33);
                    printf(" <- pub, SIG.%d.%d VERIFIED numsigs.%d vs M.%d %s\n",vini,j,numsigs,vp->M,tmpaddr);*/
                }
            }
            if ( numsigs >= vp->M )
                complete = 1;
        }
    }
    iguana_msgtx_Vset(serialized,maxlen,msgtx,V);
    cJSON *txobj = cJSON_CreateObject();
    *signedtx = iguana_rawtxbytes(taddr,pubtype,p2shtype,isPoS,height,txobj,msgtx,suppress_pubkeys);
    //printf("SIGNEDTX.(%s)\n",jprint(txobj,1));
    *signedtxidp = msgtx->txid;
    return(complete);
}

int64_t iguana_lockval(int32_t finalized,int64_t locktime)
{
    int64_t lockval = -1;
    if ( finalized == 0 )
        return(locktime);
    return(lockval);
}

int32_t iguana_signrawtransaction(void *ctx,char *symbol,uint8_t wiftaddr,uint8_t taddr,uint8_t pubtype,uint8_t p2shtype,uint8_t isPoS,int32_t height,struct iguana_msgtx *msgtx,char **signedtxp,bits256 *signedtxidp,struct vin_info *V,int32_t numinputs,char *rawtx,cJSON *vins,cJSON *privkeysjson)
{
    uint8_t *serialized,*serialized2,*serialized3,*serialized4,*extraspace,pubkeys[64][33]; int32_t finalized,i,len,n,z,plen,maxsize,complete = 0,extralen = 65536; char *privkeystr,*signedtx = 0; bits256 privkeys[64],privkey,txid; cJSON *item; cJSON *txobj = 0;
    maxsize = 1000000;
    memset(privkey.bytes,0,sizeof(privkey));
    if ( rawtx != 0 && rawtx[0] != 0 && (len= (int32_t)strlen(rawtx)>>1) < maxsize )
    {
        serialized = malloc(maxsize);
        serialized2 = malloc(maxsize);
        serialized3 = malloc(maxsize);
        serialized4 = malloc(maxsize);
        extraspace = malloc(extralen);
        memset(msgtx,0,sizeof(*msgtx));
        decode_hex(serialized,len,rawtx);
        // printf("call hex2json.(%s) vins.(%s)\n",rawtx,jprint(vins,0));
        if ( (txobj= bitcoin_hex2json(taddr,pubtype,p2shtype,isPoS,height,&txid,msgtx,rawtx,extraspace,extralen,serialized4,vins,V->suppress_pubkeys)) != 0 )
        {
            //printf("back from bitcoin_hex2json (%s)\n",jprint(vins,0));
        } else fprintf(stderr,"no txobj from bitcoin_hex2json\n");
        if ( (numinputs= cJSON_GetArraySize(vins)) > 0 )
        {
            //printf("numinputs.%d msgtx.%d\n",numinputs,msgtx->tx_in);
            memset(msgtx,0,sizeof(*msgtx));
            if ( iguana_rwmsgtx(taddr,pubtype,p2shtype,isPoS,height,0,0,serialized,maxsize,msgtx,&txid,"",extraspace,65536,vins,V->suppress_pubkeys) > 0 && numinputs == msgtx->tx_in )
            {
                memset(pubkeys,0,sizeof(pubkeys));
                memset(privkeys,0,sizeof(privkeys));
                if ( (n= cJSON_GetArraySize(privkeysjson)) > 0 )
                {
                    for (i=0; i<n; i++)
                    {
                        item = jitem(privkeysjson,i);
                        privkeystr = jstr(item,0);
                        if ( privkeystr == 0 || privkeystr[0] == 0 )
                            continue;
                        privkeys[i] = privkey = iguana_str2priv(wiftaddr,privkeystr);
                        bitcoin_pubkey33(ctx,pubkeys[i],privkey);
                        //if ( bits256_nonz(privkey) != 0 )
                        //    iguana_ensure_privkey(coin,privkey);
                    }
                }
                //printf("after privkeys tx_in.%d\n",msgtx->tx_in);
                for (i=0; i<msgtx->tx_in; i++)
                {
                    if ( msgtx->vins[i].p2shlen != 0 )
                    {
                        char coinaddr[64]; uint32_t userdatalen,sigsize,pubkeysize; uint8_t *userdata; int32_t j,k,hashtype,type,flag; struct vin_info mvin,mainvin; bits256 zero;
                        memset(zero.bytes,0,sizeof(zero));
                        coinaddr[0] = 0;
                        sigsize = 0;
                        flag = (msgtx->vins[i].vinscript[0] == 0);
                        type = bitcoin_scriptget(taddr,pubtype,p2shtype,&hashtype,&sigsize,&pubkeysize,&userdata,&userdatalen,&mainvin,msgtx->vins[i].vinscript+flag,msgtx->vins[i].scriptlen-flag,0);
                        //printf("i.%d flag.%d type.%d scriptlen.%d\n",i,flag,type,msgtx->vins[i].scriptlen);
                        if ( msgtx->vins[i].redeemscript != 0 )
                        {
                            //for (j=0; j<msgtx->vins[i].p2shlen; j++)
                            //    printf("%02x",msgtx->vins[i].redeemscript[j]);
                            bitcoin_address(coinaddr,taddr,p2shtype,msgtx->vins[i].redeemscript,msgtx->vins[i].p2shlen);
                            type = iguana_calcrmd160(taddr,pubtype,p2shtype,0,&mvin,msgtx->vins[i].redeemscript,msgtx->vins[i].p2shlen,zero,0,0);
                            for (j=0; j<mvin.N; j++)
                            {
                                if ( V->suppress_pubkeys == 0 )
                                {
                                    for (z=0; z<33; z++)
                                        V[i].signers[j].pubkey[z] = mvin.signers[j].pubkey[z];
                                }
                                if ( flag != 0 && pubkeysize == 33 && mainvin.signers[0].siglen != 0 ) // jl777: need to generalize
                                {
                                    if ( memcmp(mvin.signers[j].pubkey,mainvin.signers[0].pubkey,33) == 0 )
                                    {
                                        for (z=0; z<mainvin.signers[0].siglen; z++)
                                            V[i].signers[j].sig[z] = mainvin.signers[0].sig[z];
                                        V[i].signers[j].siglen = mainvin.signers[j].siglen;
                                        printf("[%d].signer[%d] <- from mainvin.[0]\n",i,j);
                                    }
                                }
                                for (k=0; k<n; k++)
                                {
                                    if ( V[i].signers[j].siglen == 0 && memcmp(mvin.signers[j].pubkey,pubkeys[k],33) == 0 )
                                    {
                                        V[i].signers[j].privkey = privkeys[k];
                                        if ( V->suppress_pubkeys == 0 )
                                        {
                                            for (z=0; z<33; z++)
                                                V[i].signers[j].pubkey[z] = pubkeys[k][z];
                                        }
                                        //printf("%s -> V[%d].signer.[%d] <- privkey.%d\n",mvin.signers[j].coinaddr,i,j,k);
                                        break;
                                    }
                                }
                            }
                            //printf("type.%d p2sh.[%d] -> %s M.%d N.%d\n",type,i,mvin.coinaddr,mvin.M,mvin.N);
                        }
                    }
                    if ( i < V->N )
                        V->signers[i].privkey = privkey;
                    if ( i < numinputs )
                        V[i].signers[0].privkey = privkey;
                    plen = bitcoin_pubkeylen(V->signers[i].pubkey);
                    if ( V->suppress_pubkeys == 0 && plen <= 0 )
                    {
                        if ( i < numinputs )
                        {
                            for (z=0; z<plen; z++)
                                V[i].signers[0].pubkey[z] = V->signers[i].pubkey[z];
                        }
                    }
                }
                finalized = iguana_vininfo_create(taddr,pubtype,p2shtype,isPoS,serialized2,maxsize,msgtx,vins,numinputs,V);
                //printf("finalized.%d ignore_cltverr.%d suppress.%d\n",finalized,V[0].ignore_cltverr,V[0].suppress_pubkeys);
                if ( (complete= bitcoin_verifyvins(ctx,symbol,taddr,pubtype,p2shtype,isPoS,height,signedtxidp,&signedtx,msgtx,serialized3,maxsize,V,SIGHASH_ALL,1,V->suppress_pubkeys)) > 0 && signedtx != 0 )
                {
                    /*int32_t tmp; //char str[65];
                    if ( (tmp= iguana_interpreter(ctx,cJSON_CreateArray(),iguana_lockval(finalized,jint(txobj,"locktime")),V,numinputs)) < 0 )
                    {
                        printf("iguana_interpreter %d error.(%s)\n",tmp,signedtx);
                        complete = 0;
                    } else printf("interpreter passed\n");*/
                } else printf("complete.%d\n",complete);
            } else printf("rwmsgtx error\n");
        } else fprintf(stderr,"no inputs in vins.(%s)\n",vins!=0?jprint(vins,0):"null");
        free(extraspace);
        free(serialized), free(serialized2), free(serialized3), free(serialized4);
    } else return(-1);
    if ( txobj != 0 )
        free_json(txobj);
    *signedtxp = signedtx;
    return(complete);
}

char *basilisk_swap_bobtxspend(bits256 *signedtxidp,uint64_t txfee,char *name,char *symbol,uint8_t wiftaddr,uint8_t taddr,uint8_t pubtype,uint8_t p2shtype,uint8_t isPoS,uint8_t wiftype,void *ctx,bits256 privkey,bits256 *privkey2p,uint8_t *redeemscript,int32_t redeemlen,uint8_t *userdata,int32_t userdatalen,bits256 utxotxid,int32_t vout,char *destaddr,uint8_t *pubkey33,int32_t finalseqid,uint32_t expiration,int64_t *destamountp,uint64_t satoshis,char *changeaddr,char *vinaddr,int32_t suppress_pubkeys)
{
    char *rawtxbytes=0,*signedtx=0,tmpaddr[64],hexstr[999],wifstr[128],txdestaddr[64],_destaddr[64]; uint8_t spendscript[512],addrtype,rmd160[20]; cJSON *txobj,*vins,*item,*privkeys; int32_t completed,spendlen,ignore_cltverr=1; struct vin_info V[2]; uint32_t timestamp,locktime = 0,sequenceid = 0xffffffff * finalseqid; bits256 txid; uint64_t value,change = 0; struct iguana_msgtx msgtx;
    *destamountp = 0;
    memset(signedtxidp,0,sizeof(*signedtxidp));
    if ( finalseqid == 0 )
        locktime = expiration;
    //printf("bobtxspend.%s redeem.[%d]\n",symbol,redeemlen);
    if ( redeemlen < 0 )
        return(0);
#ifndef BASILISK_DISABLESENDTX
    if ( (value= LP_txvalue(txdestaddr,symbol,utxotxid,vout)) == 0 )
    {
        char str[65];
        printf("basilisk_swap_bobtxspend.%s %s utxo.(%s) already spent or doesnt exist\n",name,symbol,bits256_str(str,utxotxid));
        return(0);
    }
#else
    value = satoshis;
#endif
    if ( satoshis != 0 )
    {
        if ( value < satoshis+txfee )
        {
            if ( satoshis > value-txfee/2 )
            {
                satoshis = value - txfee;
                printf("reduce satoshis by txfee %.8f to %.8f\n",dstr(txfee),dstr(satoshis));
            }
            else
            {
                printf("utxo %.8f too small for %.8f + %.8f\n",dstr(value),dstr(satoshis),dstr(txfee));
                return(0);
            }
        }
        if ( value > satoshis+txfee )
            change = value - (satoshis + txfee);
        printf("utxo %.8f, destamount %.8f change %.8f txfee %.8f\n",dstr(value),dstr(satoshis),dstr(change),dstr(txfee));
    } else if ( value > txfee )
        satoshis = value - txfee;
    else
    {
        printf("unexpected small value %.8f vs txfee %.8f\n",dstr(value),dstr(txfee));
        change = 0;
        satoshis = value >> 1;
        txfee = (value - satoshis);
        printf("unexpected small value %.8f vs txfee %.8f -> %.8f %.8f\n",dstr(value),dstr(txfee),dstr(satoshis),dstr(txfee));
    }
    if ( change < 6000 )
    {
        satoshis += change;
        change = 0;
    }
    if ( destamountp != 0 )
        *destamountp = satoshis;
    timestamp = (uint32_t)time(NULL);
    memset(V,0,sizeof(V));
    privkeys = cJSON_CreateArray();
    if ( privkey2p != 0 )
    {
        V[0].signers[1].privkey = *privkey2p;
        bitcoin_pubkey33(ctx,V[0].signers[1].pubkey,*privkey2p);
        bitcoin_priv2wif(wiftaddr,wifstr,*privkey2p,wiftype);
        jaddistr(privkeys,wifstr);
        V[0].N = V[0].M = 2;
    } else V[0].N = V[0].M = 1;
    V[0].signers[0].privkey = privkey;
    bitcoin_pubkey33(ctx,V[0].signers[0].pubkey,privkey);
    bitcoin_priv2wif(wiftaddr,wifstr,privkey,wiftype);
    jaddistr(privkeys,wifstr);
    V[0].suppress_pubkeys = suppress_pubkeys;
    V[0].ignore_cltverr = ignore_cltverr;
    if ( redeemlen != 0 )
        memcpy(V[0].p2shscript,redeemscript,redeemlen), V[0].p2shlen = redeemlen;
    txobj = bitcoin_txcreate(symbol,isPoS,locktime,1,timestamp);
    vins = cJSON_CreateArray();
    item = cJSON_CreateObject();
    if ( userdata != 0 && userdatalen > 0 )
    {
        memcpy(V[0].userdata,userdata,userdatalen);
        V[0].userdatalen = userdatalen;
        init_hexbytes_noT(hexstr,userdata,userdatalen);
        jaddstr(item,"userdata",hexstr);
    }
    jaddbits256(item,"txid",utxotxid);
    jaddnum(item,"vout",vout);
    bitcoin_address(tmpaddr,taddr,pubtype,pubkey33,33);
    bitcoin_addr2rmd160(taddr,&addrtype,rmd160,tmpaddr);
    if ( redeemlen != 0 )
    {
        init_hexbytes_noT(hexstr,redeemscript,redeemlen);
        jaddstr(item,"redeemScript",hexstr);
        if ( vinaddr != 0 )
            bitcoin_addr2rmd160(taddr,&addrtype,rmd160,vinaddr);
        spendlen = bitcoin_p2shspend(spendscript,0,rmd160);
        //printf("P2SH path.%s\n",vinaddr!=0?vinaddr:0);
    } else spendlen = bitcoin_standardspend(spendscript,0,rmd160);
    init_hexbytes_noT(hexstr,spendscript,spendlen);
    jaddstr(item,"scriptPubKey",hexstr);
    jaddnum(item,"suppress",suppress_pubkeys);
    jaddnum(item,"sequence",sequenceid);
    jaddi(vins,item);
    jdelete(txobj,"vin");
    jadd(txobj,"vin",vins);
    if ( destaddr == 0 )
    {
        destaddr = _destaddr;
        bitcoin_address(destaddr,taddr,pubtype,pubkey33,33);
    }
    bitcoin_addr2rmd160(taddr,&addrtype,rmd160,destaddr);
    if ( addrtype == p2shtype )
        spendlen = bitcoin_p2shspend(spendscript,0,rmd160);
    else spendlen = bitcoin_standardspend(spendscript,0,rmd160);
    if ( change != 0 && strcmp(changeaddr,destaddr) == 0 )
    {
        printf("combine change %.8f -> %s\n",dstr(change),changeaddr);
        satoshis += change;
        change = 0;
    }
    txobj = bitcoin_txoutput(txobj,spendscript,spendlen,satoshis);
    if ( change != 0 )
    {
        int32_t changelen; uint8_t changescript[1024],changetype,changermd160[20];
        bitcoin_addr2rmd160(taddr,&changetype,changermd160,changeaddr);
        changelen = bitcoin_standardspend(changescript,0,changermd160);
        txobj = bitcoin_txoutput(txobj,changescript,changelen,change);
    }
    if ( (rawtxbytes= bitcoin_json2hex(isPoS,&txid,txobj,V)) != 0 )
    {
        char str[65];
        completed = 0;
        memset(signedtxidp,0,sizeof(*signedtxidp));
        //printf("locktime.%u sequenceid.%x rawtx.(%s) vins.(%s)\n",locktime,sequenceid,rawtxbytes,jprint(vins,0));
        if ( (completed= iguana_signrawtransaction(ctx,symbol,wiftaddr,taddr,pubtype,p2shtype,isPoS,1000000,&msgtx,&signedtx,signedtxidp,V,1,rawtxbytes,vins,privkeys)) < 0 )
        //if ( (signedtx= LP_signrawtx(symbol,signedtxidp,&completed,vins,rawtxbytes,privkeys,V)) == 0 )
            printf("couldnt sign transaction.%s %s\n",name,bits256_str(str,*signedtxidp));
        else if ( completed == 0 )
        {
            printf("incomplete signing suppress.%d %s (%s)\n",suppress_pubkeys,name,jprint(vins,0));
            if ( signedtx != 0 )
                free(signedtx), signedtx = 0;
        } else printf("basilisk_swap_bobtxspend %s -> %s\n",name,bits256_str(str,*signedtxidp));
        free(rawtxbytes);
    } else printf("error making rawtx suppress.%d\n",suppress_pubkeys);
    free_json(privkeys);
    free_json(txobj);
    return(signedtx);
}

int32_t basilisk_rawtx_gen(void *ctx,char *str,uint32_t swapstarted,uint8_t *pubkey33,int32_t iambob,int32_t lockinputs,struct basilisk_rawtx *rawtx,uint32_t locktime,uint8_t *script,int32_t scriptlen,int64_t txfee,int32_t minconf,int32_t delay,bits256 privkey,uint8_t *changermd160,char *vinaddr)
{
    int32_t retval=-1,len,iter; char *signedtx,*changeaddr = 0,_changeaddr[64]; struct iguana_info *coin; int64_t newtxfee=0,destamount;
    char str2[65]; printf("%s rawtxgen.(%s/v%d)\n",rawtx->name,bits256_str(str2,rawtx->utxotxid),rawtx->utxovout);
    if ( (coin= rawtx->coin) == 0 )
        return(-1);
    //return(_basilisk_rawtx_gen(str,swapstarted,pubkey33,iambob,lockinputs,rawtx,locktime,script,scriptlen,txfee,minconf,delay,privkey));
    if ( changermd160 != 0 )
    {
        changeaddr = _changeaddr;
        bitcoin_address(changeaddr,coin->taddr,coin->pubtype,changermd160,20);
        //printf("changeaddr.(%s) vs destaddr.(%s)\n",changeaddr,rawtx->I.destaddr);
    }
    if ( strcmp(str,"myfee") == 0 && strcmp(coin->symbol,"BTC") == 0 )
        txfee = LP_MIN_TXFEE;
    for (iter=0; iter<2; iter++)
    {
        if ( (signedtx= basilisk_swap_bobtxspend(&rawtx->I.signedtxid,iter == 0 ? txfee : newtxfee,str,coin->symbol,coin->wiftaddr,coin->taddr,coin->pubtype,coin->p2shtype,coin->isPoS,coin->wiftype,ctx,privkey,0,0,0,0,0,rawtx->utxotxid,rawtx->utxovout,rawtx->I.destaddr,pubkey33,1,0,&destamount,rawtx->I.amount,changeaddr,vinaddr,rawtx->I.suppress_pubkeys)) != 0 )
        {
            rawtx->I.datalen = (int32_t)strlen(signedtx) >> 1;
            if ( rawtx->I.datalen <= sizeof(rawtx->txbytes) )
            {
                decode_hex(rawtx->txbytes,rawtx->I.datalen,signedtx);
                rawtx->I.completed = 1;
                retval = 0;
            }
            free(signedtx);
            if ( strcmp(coin->symbol,"BTC") != 0 )
                return(retval);
            len = rawtx->I.datalen;
            newtxfee = LP_txfeecalc(coin->symbol,0);
            printf("txfee %.8f -> newtxfee %.8f\n",dstr(txfee),dstr(newtxfee));
        } else break;
        if ( strcmp(str,"myfee") == 0 )
            break;
    }
    return(retval);
}

int32_t basilisk_rawtx_sign(char *symbol,uint8_t wiftaddr,uint8_t taddr,uint8_t pubtype,uint8_t p2shtype,uint8_t isPoS,uint8_t wiftype,struct basilisk_swap *swap,struct basilisk_rawtx *dest,struct basilisk_rawtx *rawtx,bits256 privkey,bits256 *privkey2,uint8_t *userdata,int32_t userdatalen,int32_t ignore_cltverr,uint8_t *changermd160,char *vinaddr)
{
    char *signedtx,*changeaddr = 0,_changeaddr[64]; int64_t txfee,newtxfee=0,destamount; uint32_t timestamp,locktime=0,sequenceid = 0xffffffff; int32_t iter,retval = -1; double estimatedrate;
    //char str2[65]; printf("%s rawtxsign.(%s/v%d)\n",dest->name,bits256_str(str2,dest->utxotxid),dest->utxovout);
    timestamp = swap->I.started;
    if ( dest == &swap->aliceclaim )
        locktime = swap->bobdeposit.I.locktime + 1, sequenceid = 0;
    else if ( dest == &swap->bobreclaim )
        locktime = swap->bobpayment.I.locktime + 1, sequenceid = 0;
    txfee = strcmp("BTC",symbol) == 0 ? 0 : LP_MIN_TXFEE;
    if ( changermd160 != 0 )
    {
        changeaddr = _changeaddr;
        bitcoin_address(changeaddr,taddr,pubtype,changermd160,20);
        //printf("changeaddr.(%s)\n",changeaddr);
    }
    for (iter=0; iter<2; iter++)
    {
        if ( (signedtx= basilisk_swap_bobtxspend(&dest->I.signedtxid,iter == 0 ? txfee : newtxfee,rawtx->name,symbol,wiftaddr,taddr,pubtype,p2shtype,isPoS,wiftype,swap->ctx,privkey,privkey2,rawtx->redeemscript,rawtx->I.redeemlen,userdata,userdatalen,dest->utxotxid,dest->utxovout,dest->I.destaddr,rawtx->I.pubkey33,1,0,&destamount,rawtx->I.amount,changeaddr,vinaddr,dest->I.suppress_pubkeys)) != 0 )
        {
            dest->I.datalen = (int32_t)strlen(signedtx) >> 1;
            if ( dest->I.datalen <= sizeof(dest->txbytes) )
            {
                decode_hex(dest->txbytes,dest->I.datalen,signedtx);
                dest->I.completed = 1;
                retval = 0;
            }
            free(signedtx);
            if ( strcmp(symbol,"BTC") != 0 )
                return(retval);
            estimatedrate = LP_getestimatedrate(symbol);
            newtxfee = estimatedrate * dest->I.datalen;
        } else break;
    }
    return(retval);
    //return(_basilisk_rawtx_sign(symbol,pubtype,p2shtype,isPoS,wiftype,swap,timestamp,locktime,sequenceid,dest,rawtx,privkey,privkey2,userdata,userdatalen,ignore_cltverr));
}

int32_t basilisk_alicescript(uint8_t *redeemscript,int32_t *redeemlenp,uint8_t *script,int32_t n,char *msigaddr,uint8_t taddr,uint8_t altps2h,bits256 pubAm,bits256 pubBn)
{
    uint8_t p2sh160[20]; struct vin_info V;
    memset(&V,0,sizeof(V));
    memcpy(&V.signers[0].pubkey[1],pubAm.bytes,sizeof(pubAm)), V.signers[0].pubkey[0] = 0x02;
    memcpy(&V.signers[1].pubkey[1],pubBn.bytes,sizeof(pubBn)), V.signers[1].pubkey[0] = 0x03;
    V.M = V.N = 2;
    *redeemlenp = bitcoin_MofNspendscript(p2sh160,redeemscript,n,&V);
    bitcoin_address(msigaddr,taddr,altps2h,p2sh160,sizeof(p2sh160));
    n = bitcoin_p2shspend(script,0,p2sh160);
    //for (i=0; i<*redeemlenp; i++)
    //    printf("%02x",redeemscript[i]);
    //printf(" <- redeemscript alicetx\n");
    return(n);
}

char *basilisk_swap_Aspend(char *name,char *symbol,uint64_t Atxfee,uint8_t wiftaddr,uint8_t taddr,uint8_t pubtype,uint8_t p2shtype,uint8_t isPoS,uint8_t wiftype,void *ctx,bits256 privAm,bits256 privBn,bits256 utxotxid,int32_t vout,uint8_t pubkey33[33],uint32_t expiration,int64_t *destamountp,char *vinaddr)
{
    char msigaddr[64],*signedtx = 0; int32_t spendlen,redeemlen; uint8_t tmp33[33],redeemscript[512],spendscript[128]; bits256 pubAm,pubBn,signedtxid; uint64_t txfee;
    if ( bits256_nonz(privAm) != 0 && bits256_nonz(privBn) != 0 )
    {
        pubAm = bitcoin_pubkey33(ctx,tmp33,privAm);
        pubBn = bitcoin_pubkey33(ctx,tmp33,privBn);
        //char str[65];
        //printf("pubAm.(%s)\n",bits256_str(str,pubAm));
        //printf("pubBn.(%s)\n",bits256_str(str,pubBn));
        spendlen = basilisk_alicescript(redeemscript,&redeemlen,spendscript,0,msigaddr,taddr,p2shtype,pubAm,pubBn);
        //char str[65]; printf("%s utxo.(%s) redeemlen.%d spendlen.%d\n",msigaddr,bits256_str(str,utxotxid),redeemlen,spendlen);
        /*rev = privAm;
         for (i=0; i<32; i++)
         privAm.bytes[i] = rev.bytes[31 - i];
         rev = privBn;
         for (i=0; i<32; i++)
         privBn.bytes[i] = rev.bytes[31 - i];*/
        if ( (txfee= Atxfee) == 0 )
        {
            if ( (txfee= LP_getestimatedrate(symbol) * LP_AVETXSIZE) < LP_MIN_TXFEE )
                txfee = LP_MIN_TXFEE;
        }
        //txfee = LP_txfee(symbol);
        signedtx = basilisk_swap_bobtxspend(&signedtxid,txfee,name,symbol,wiftaddr,taddr,pubtype,p2shtype,isPoS,wiftype,ctx,privAm,&privBn,redeemscript,redeemlen,0,0,utxotxid,vout,0,pubkey33,1,expiration,destamountp,0,0,vinaddr,1);
    }
    return(signedtx);
}

int32_t LP_swap_txdestaddr(char *destaddr,bits256 txid,int32_t vout,cJSON *txobj)
{
    int32_t n,m,retval = -1; cJSON *vouts,*item,*addresses,*skey; char *addr;
    if ( (vouts= jarray(&n,txobj,"vout")) != 0 && vout < n )
    {
        item = jitem(vouts,vout);
        if ( (skey= jobj(item,"scriptPubKey")) != 0 && (addresses= jarray(&m,skey,"addresses")) != 0 )
        {
            item = jitem(addresses,0);
            if ( (addr= jstr(item,0)) != 0 )
            {
                safecopy(destaddr,addr,64);
                retval = 0;
            }
            //printf("item.(%s) -> dest.(%s)\n",jprint(item,0),destaddr);
        }
    }
    return(retval);
}

int32_t LP_swap_getcoinaddr(char *symbol,char *coinaddr,bits256 txid,int32_t vout)
{
    cJSON *retjson;
    coinaddr[0] = 0;
    if ( (retjson= LP_gettx(symbol,txid)) != 0 )
    {
        LP_swap_txdestaddr(coinaddr,txid,vout,retjson);
        free_json(retjson);
    }
    return(coinaddr[0] != 0);
}

int32_t basilisk_swap_getsigscript(char *symbol,uint8_t *script,int32_t maxlen,bits256 txid,int32_t vini)
{
    cJSON *retjson,*vins,*item,*skey; int32_t n,scriptlen = 0; char *hexstr;
    if ( (retjson= LP_gettx(symbol,txid)) != 0 )
    {
        if ( (vins= jarray(&n,retjson,"vin")) != 0 && vini < n )
        {
            item = jitem(vins,vini);
            if ( (skey= jobj(item,"scriptSig")) != 0 && (hexstr= jstr(skey,"hex")) != 0 && (scriptlen= (int32_t)strlen(hexstr)) < maxlen*2 )
            {
                scriptlen >>= 1;
                decode_hex(script,scriptlen,hexstr);
                //char str[65]; printf("%s/v%d sigscript.(%s)\n",bits256_str(str,txid),vini,hexstr);
            }
        }
        free_json(retjson);
    }
    return(scriptlen);
}

#ifdef notnow
bits256 _LP_swap_spendtxid(char *symbol,char *destaddr,char *coinaddr,bits256 utxotxid,int32_t vout)
{
    char *retstr,*addr; cJSON *array,*item,*array2; int32_t i,n,m; bits256 spendtxid,txid;
    memset(&spendtxid,0,sizeof(spendtxid));
    if ( (retstr= blocktrail_listtransactions(symbol,coinaddr,100,0)) != 0 )
    {
        if ( (array= cJSON_Parse(retstr)) != 0 )
        {
            if ( (n= cJSON_GetArraySize(array)) > 0 )
            {
                for (i=0; i<n; i++)
                {
                    if ( (item= jitem(array,i)) == 0 )
                        continue;
                    txid = jbits256(item,"txid");
                    if ( bits256_nonz(txid) == 0 )
                    {
                        if ( (array2= jarray(&m,item,"inputs")) != 0 && m == 1 )
                        {
                            //printf("found inputs with %s\n",bits256_str(str,spendtxid));
                            txid = jbits256(jitem(array2,0),"output_hash");
                            if ( bits256_cmp(txid,utxotxid) == 0 )
                            {
                                //printf("matched %s\n",bits256_str(str,txid));
                                if ( (array2= jarray(&m,item,"outputs")) != 0 && m == 1 && (addr= jstr(jitem(array2,0),"address")) != 0 )
                                {
                                    spendtxid = jbits256(item,"hash");
                                    strcpy(destaddr,addr);
                                    //printf("set spend addr.(%s) <- %s\n",addr,jprint(item,0));
                                    break;
                                }
                            }
                        }
                    }
                    else if ( bits256_cmp(txid,utxotxid) == 0 )
                    {
                        spendtxid = jbits256(item,"spendtxid");
                        if ( bits256_nonz(spendtxid) != 0 )
                        {
                            LP_swap_getcoinaddr(symbol,destaddr,spendtxid,0);
                            //char str[65]; printf("found spendtxid.(%s) -> %s\n",bits256_str(str,spendtxid),destaddr);
                            break;
                        }
                    }
                }
            }
            free_json(array);
        }
        free(retstr);
    }
    return(spendtxid);
}
#endif

bits256 LP_swap_spendtxid(char *symbol,char *destaddr,bits256 utxotxid,int32_t vout)
{
    bits256 spendtxid,txid; char *catstr,*addr; cJSON *array,*item,*item2,*txobj,*vins; int32_t i,n,m,spendvin; char coinaddr[64],str[65];
    // listtransactions or listspents
    destaddr[0] = 0;
    coinaddr[0] = 0;
    memset(&spendtxid,0,sizeof(spendtxid));
    if ( LP_spendsearch(&spendtxid,&spendvin,symbol,utxotxid,vout) > 0 )
        printf("spend of %s/v%d detected\n",bits256_str(str,utxotxid),vout);
    return(spendtxid);
    //char str[65]; printf("swap %s spendtxid.(%s)\n",symbol,bits256_str(str,utxotxid));
    if ( (0) && strcmp("BTC",symbol) == 0 )
    {
        //[{"type":"sent","confirmations":379,"height":275311,"timestamp":1492084664,"txid":"8703c5517bc57db38134058370a14e99b8e662b99ccefa2061dea311bbd02b8b","vout":0,"amount":117.50945263,"spendtxid":"cf2509e076fbb9b22514923df916b7aacb1391dce9c7e1460b74947077b12510","vin":0,"paid":{"type":"paid","txid":"cf2509e076fbb9b22514923df916b7aacb1391dce9c7e1460b74947077b12510","height":275663,"timestamp":1492106024,"vouts":[{"RUDpN6PEBsE7ZFbGjUxk1W3QVsxnjBLYw6":117.50935263}]}}]
        /*LP_swap_getcoinaddr(symbol,coinaddr,utxotxid,vout);
        if ( coinaddr[0] != 0 )
            spendtxid = _LP_swap_spendtxid(symbol,destaddr,coinaddr,utxotxid,vout);*/
    }
    else
    {
        if ( (array= LP_listtransactions(symbol,destaddr,1000,0)) != 0 )
        {
            if ( (n= cJSON_GetArraySize(array)) > 0 )
            {
                for (i=0; i<n; i++)
                {
                    if ( (item= jitem(array,i)) == 0 )
                        continue;
                    txid = jbits256(item,"txid");
                    if ( vout == juint(item,"vout") && bits256_cmp(txid,utxotxid) == 0 && (addr= jstr(item,"address")) != 0 )
                    {
                        if ( (catstr= jstr(item,"category")) != 0 )
                        {
                            if (strcmp(catstr,"send") == 0 )
                            {
                                strncpy(destaddr,addr,63);
                                //printf("(%s) <- (%s) item.%d.[%s]\n",destaddr,coinaddr,i,jprint(item,0));
                                if ( coinaddr[0] != 0 )
                                    break;
                            }
                            if (strcmp(catstr,"receive") == 0 )
                            {
                                strncpy(coinaddr,addr,63);
                                //printf("receive dest.(%s) <- (%s)\n",destaddr,coinaddr);
                                if ( destaddr[0] != 0 )
                                    break;
                            }
                        }
                    }
                }
            }
            free_json(array);
        }
        if ( destaddr[0] != 0 )
        {
            if ( (array= LP_listtransactions(symbol,destaddr,1000,0)) != 0 )
            {
                if ( (n= cJSON_GetArraySize(array)) > 0 )
                {
                    for (i=0; i<n; i++)
                    {
                        if ( (item= jitem(array,i)) == 0 )
                            continue;
                        if ( (catstr= jstr(item,"category")) != 0 && strcmp(catstr,"send") == 0 )
                        {
                            txid = jbits256(item,"txid");
                            if ( (txobj= LP_gettx(symbol,txid)) != 0 )
                            {
                                if ( (vins= jarray(&m,txobj,"vin")) != 0 && m > jint(item,"vout") )
                                {
                                    item2 = jitem(vins,jint(item,"vout"));
                                    if ( bits256_cmp(utxotxid,jbits256(item2,"txid")) == 0 && vout == jint(item2,"vout") )
                                    {
                                        spendtxid = txid;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if ( i == n )
                        printf("dpowlist: native couldnt find spendtxid for %s\n",bits256_str(str,utxotxid));
                }
                free_json(array);
            }
            if ( bits256_nonz(spendtxid) != 0 )
                return(spendtxid);
        }
        /*if ( iguana_isnotarychain(symbol) >= 0 )
        {
            LP_swap_getcoinaddr(symbol,coinaddr,utxotxid,vout);
            printf("fallback use DEX for native (%s) (%s)\n",coinaddr,bits256_str(str,utxotxid));
            if ( coinaddr[0] != 0 )
            {
                spendtxid = _LP_swap_spendtxid(symbol,destaddr,coinaddr,utxotxid,vout);
                printf("spendtxid.(%s)\n",bits256_str(str,spendtxid));
            }
        }*/
    }
    return(spendtxid);
}

int32_t basilisk_swap_bobredeemscript(int32_t depositflag,int32_t *secretstartp,uint8_t *redeemscript,uint32_t locktime,bits256 pubA0,bits256 pubB0,bits256 pubB1,bits256 privAm,bits256 privBn,uint8_t *secretAm,uint8_t *secretAm256,uint8_t *secretBn,uint8_t *secretBn256)
{
    int32_t i,n=0; bits256 cltvpub,destpub,privkey; uint8_t pubkeyA[33],pubkeyB[33],secret160[20],secret256[32];
    if ( depositflag != 0 )
    {
        pubkeyA[0] = 0x02, cltvpub = pubA0;
        pubkeyB[0] = 0x03, destpub = pubB0;
        privkey = privBn;
        memcpy(secret160,secretBn,20);
        memcpy(secret256,secretBn256,32);
    }
    else
    {
        pubkeyA[0] = 0x03, cltvpub = pubB1;
        pubkeyB[0] = 0x02, destpub = pubA0;
        privkey = privAm;
        memcpy(secret160,secretAm,20);
        memcpy(secret256,secretAm256,32);
    }
    //for (i=0; i<32; i++)
    //    printf("%02x",secret256[i]);
    //printf(" <- secret256 depositflag.%d nonz.%d\n",depositflag,bits256_nonz(privkey));
    if ( bits256_nonz(cltvpub) == 0 || bits256_nonz(destpub) == 0 )
        return(-1);
    for (i=0; i<20; i++)
        if ( secret160[i] != 0 )
            break;
    if ( i == 20 )
        return(-1);
    memcpy(pubkeyA+1,cltvpub.bytes,sizeof(cltvpub));
    memcpy(pubkeyB+1,destpub.bytes,sizeof(destpub));
    redeemscript[n++] = SCRIPT_OP_IF;
    n = bitcoin_checklocktimeverify(redeemscript,n,locktime);
#ifdef DISABLE_CHECKSIG
    n = bitcoin_secret256spend(redeemscript,n,cltvpub);
#else
    n = bitcoin_pubkeyspend(redeemscript,n,pubkeyA);
#endif
    redeemscript[n++] = SCRIPT_OP_ELSE;
    if ( secretstartp != 0 )
        *secretstartp = n + 2;
    if ( 1 )
    {
        if ( 1 && bits256_nonz(privkey) != 0 )
        {
            uint8_t bufA[20],bufB[20];
            revcalc_rmd160_sha256(bufA,privkey);
            calc_rmd160_sha256(bufB,privkey.bytes,sizeof(privkey));
            /*if ( memcmp(bufA,secret160,sizeof(bufA)) == 0 )
             printf("MATCHES BUFA\n");
             else if ( memcmp(bufB,secret160,sizeof(bufB)) == 0 )
             printf("MATCHES BUFB\n");
             else printf("secret160 matches neither\n");
             for (i=0; i<20; i++)
             printf("%02x",bufA[i]);
             printf(" <- revcalc\n");
             for (i=0; i<20; i++)
             printf("%02x",bufB[i]);
             printf(" <- calc\n");*/
            memcpy(secret160,bufB,20);
        }
        n = bitcoin_secret160verify(redeemscript,n,secret160);
    }
    else
    {
        redeemscript[n++] = 0xa8;//IGUANA_OP_SHA256;
        redeemscript[n++] = 0x20;
        memcpy(&redeemscript[n],secret256,0x20), n += 0x20;
        redeemscript[n++] = 0x88; //SCRIPT_OP_EQUALVERIFY;
    }
#ifdef DISABLE_CHECKSIG
    n = bitcoin_secret256spend(redeemscript,n,destpub);
#else
    n = bitcoin_pubkeyspend(redeemscript,n,pubkeyB);
#endif
    redeemscript[n++] = SCRIPT_OP_ENDIF;
    return(n);
}

int32_t basilisk_bobscript(uint8_t *rmd160,uint8_t *redeemscript,int32_t *redeemlenp,uint8_t *script,int32_t n,uint32_t *locktimep,int32_t *secretstartp,struct basilisk_swapinfo *swap,int32_t depositflag)
{
    if ( depositflag != 0 )
        *locktimep = swap->started + swap->putduration + swap->callduration;
    else *locktimep = swap->started + swap->putduration;
    *redeemlenp = n = basilisk_swap_bobredeemscript(depositflag,secretstartp,redeemscript,*locktimep,swap->pubA0,swap->pubB0,swap->pubB1,swap->privAm,swap->privBn,swap->secretAm,swap->secretAm256,swap->secretBn,swap->secretBn256);
    if ( n > 0 )
    {
        calc_rmd160_sha256(rmd160,redeemscript,n);
        n = bitcoin_p2shspend(script,0,rmd160);
        int32_t i; for (i=0; i<n; i++)
            printf("%02x",script[i]);
        printf(" <- redeem.%d bobtx dflag.%d spendscript.[%d]\n",*redeemlenp,depositflag,n);
    }
    return(n);
}

int32_t basilisk_swapuserdata(uint8_t *userdata,bits256 privkey,int32_t ifpath,bits256 signpriv,uint8_t *redeemscript,int32_t redeemlen)
{
    int32_t i,len = 0;
#ifdef DISABLE_CHECKSIG
    userdata[len++] = sizeof(signpriv);
    for (i=0; i<sizeof(privkey); i++)
        userdata[len++] = signpriv.bytes[i];
#endif
    if ( bits256_nonz(privkey) != 0 )
    {
        userdata[len++] = sizeof(privkey);
        for (i=0; i<sizeof(privkey); i++)
            userdata[len++] = privkey.bytes[i];
    }
    userdata[len++] = 0x51 * ifpath; // ifpath == 1 -> if path, 0 -> else path
    return(len);
}

/*Bob paytx:
 OP_IF
 <now + INSTANTDEX_LOCKTIME> OP_CLTV OP_DROP <bob_pubB1> OP_CHECKSIG
 OP_ELSE
 OP_HASH160 <hash(alice_privM)> OP_EQUALVERIFY <alice_pubA0> OP_CHECKSIG
 OP_ENDIF*/

int32_t basilisk_bobpayment_reclaim(struct basilisk_swap *swap,int32_t delay)
{
    uint8_t userdata[512]; int32_t retval,len = 0; static bits256 zero;
    //printf("basilisk_bobpayment_reclaim\n");
    len = basilisk_swapuserdata(userdata,zero,1,swap->I.myprivs[1],swap->bobpayment.redeemscript,swap->bobpayment.I.redeemlen);
    memcpy(swap->I.userdata_bobreclaim,userdata,len);
    swap->I.userdata_bobreclaimlen = len;
    if ( (retval= basilisk_rawtx_sign(swap->bobcoin.symbol,swap->bobcoin.wiftaddr,swap->bobcoin.taddr,swap->bobcoin.pubtype,swap->bobcoin.p2shtype,swap->bobcoin.isPoS,swap->bobcoin.wiftype,swap,&swap->bobreclaim,&swap->bobpayment,swap->I.myprivs[1],0,userdata,len,1,swap->changermd160,swap->bobpayment.I.destaddr)) == 0 )
    {
        //for (i=0; i<swap->bobreclaim.I.datalen; i++)
        //    printf("%02x",swap->bobreclaim.txbytes[i]);
        //printf(" <- bobreclaim\n");
        //basilisk_txlog(swap,&swap->bobreclaim,delay);
        return(retval);
    }
    return(-1);
}

int32_t basilisk_bobdeposit_refund(struct basilisk_swap *swap,int32_t delay)
{
    uint8_t userdata[512]; int32_t i,retval,len = 0; char str[65];
    len = basilisk_swapuserdata(userdata,swap->I.privBn,0,swap->I.myprivs[0],swap->bobdeposit.redeemscript,swap->bobdeposit.I.redeemlen);
    memcpy(swap->I.userdata_bobrefund,userdata,len);
    swap->I.userdata_bobrefundlen = len;
    if ( (retval= basilisk_rawtx_sign(swap->bobcoin.symbol,swap->bobcoin.wiftaddr,swap->bobcoin.taddr,swap->bobcoin.pubtype,swap->bobcoin.p2shtype,swap->bobcoin.isPoS,swap->bobcoin.wiftype,swap,&swap->bobrefund,&swap->bobdeposit,swap->I.myprivs[0],0,userdata,len,0,swap->changermd160,swap->bobdeposit.I.destaddr)) == 0 )
    {
        for (i=0; i<swap->bobrefund.I.datalen; i++)
            printf("%02x",swap->bobrefund.txbytes[i]);
        printf(" <- bobrefund.(%s)\n",bits256_str(str,swap->bobrefund.I.txid));
        //basilisk_txlog(swap,&swap->bobrefund,delay);
        return(retval);
    }
    return(-1);
}

void LP_swap_coinaddr(struct basilisk_swap *swap,struct iguana_info *coin,char *coinaddr,uint8_t *data,int32_t datalen)
{
    cJSON *txobj,*vouts,*vout,*addresses,*item,*skey; uint8_t extraspace[8192]; bits256 signedtxid; struct iguana_msgtx msgtx; char *addr; int32_t n,m,suppress_pubkeys = 0;
    if ( (txobj= bitcoin_data2json(coin->taddr,coin->pubtype,coin->p2shtype,coin->isPoS,coin->longestchain,&signedtxid,&msgtx,extraspace,sizeof(extraspace),data,datalen,0,suppress_pubkeys)) != 0 )
    {
        //char str[65]; printf("got txid.%s (%s)\n",bits256_str(str,signedtxid),jprint(txobj,0));
        if ( (vouts= jarray(&n,txobj,"vout")) != 0 && n > 0 )
        {
            vout = jitem(vouts,0);
            //printf("VOUT.(%s)\n",jprint(vout,0));
            if ( (skey= jobj(vout,"scriptPubKey")) != 0 && (addresses= jarray(&m,skey,"addresses")) != 0 )
            {
                item = jitem(addresses,0);
                //printf("item.(%s)\n",jprint(item,0));
                if ( (addr= jstr(item,0)) != 0 )
                {
                    safecopy(coinaddr,addr,64);
                    //printf("extracted.(%s)\n",coinaddr);
                }
            }
        }
        free_json(txobj);
    }
}

int32_t basilisk_bobscripts_set(struct basilisk_swap *swap,int32_t depositflag,int32_t genflag)
{
    int32_t j; char coinaddr[64],checkaddr[64];
    bitcoin_address(coinaddr,swap->bobcoin.taddr,swap->bobcoin.pubtype,swap->changermd160,20);
    if ( genflag != 0 && swap->I.iambob == 0 )
        printf("basilisk_bobscripts_set WARNING: alice generating BOB tx\n");
    if ( depositflag == 0 )
    {
        swap->bobpayment.I.spendlen = basilisk_bobscript(swap->bobpayment.I.rmd160,swap->bobpayment.redeemscript,&swap->bobpayment.I.redeemlen,swap->bobpayment.spendscript,0,&swap->bobpayment.I.locktime,&swap->bobpayment.I.secretstart,&swap->I,0);
        bitcoin_address(swap->bobpayment.p2shaddr,swap->bobcoin.taddr,swap->bobcoin.p2shtype,swap->bobpayment.redeemscript,swap->bobpayment.I.redeemlen);
        strcpy(swap->bobpayment.I.destaddr,swap->bobpayment.p2shaddr);
        //LP_importaddress(swap->bobcoin.symbol,swap->bobpayment.I.destaddr);
        //int32_t i; for (i=0; i<swap->bobpayment.I.redeemlen; i++)
        //    printf("%02x",swap->bobpayment.redeemscript[i]);
        //printf(" <- bobpayment redeem %d %s\n",i,swap->bobpayment.I.destaddr);
        if ( genflag != 0 && bits256_nonz(*(bits256 *)swap->I.secretBn256) != 0 && swap->bobpayment.I.datalen == 0 )
        {
            basilisk_rawtx_gen(swap->ctx,"payment",swap->I.started,swap->persistent_pubkey33,1,1,&swap->bobpayment,swap->bobpayment.I.locktime,swap->bobpayment.spendscript,swap->bobpayment.I.spendlen,swap->bobpayment.coin->txfee,1,0,swap->persistent_privkey,swap->changermd160,coinaddr);
            if ( swap->bobpayment.I.spendlen == 0 || swap->bobpayment.I.datalen == 0 )
            {
                printf("error bob generating %p payment.%d\n",swap->bobpayment.txbytes,swap->bobpayment.I.spendlen);
                sleep(DEX_SLEEP);
            }
            else
            {
                for (j=0; j<swap->bobpayment.I.datalen; j++)
                    printf("%02x",swap->bobpayment.txbytes[j]);
                printf(" <- bobpayment.%d\n",swap->bobpayment.I.datalen);
                for (j=0; j<swap->bobpayment.I.redeemlen; j++)
                    printf("%02x",swap->bobpayment.redeemscript[j]);
                printf(" <- redeem.%d\n",swap->bobpayment.I.redeemlen);
                printf(" <- GENERATED BOB PAYMENT.%d destaddr.(%s)\n",swap->bobpayment.I.datalen,swap->bobpayment.I.destaddr);
                LP_swap_coinaddr(swap,&swap->bobcoin,checkaddr,swap->bobpayment.txbytes,swap->bobpayment.I.datalen);
                if ( strcmp(swap->bobpayment.I.destaddr,checkaddr) != 0 )
                {
                    printf("BOBPAYMENT REDEEMADDR MISMATCH??? %s != %s\n",swap->bobpayment.I.destaddr,checkaddr);
                    return(-1);
                }
                LP_unspents_mark(swap->bobcoin.symbol,swap->bobpayment.vins);
                //printf("bobscripts set completed\n");
                return(0);
            }
        }
    }
    else
    {
        swap->bobdeposit.I.spendlen = basilisk_bobscript(swap->bobdeposit.I.rmd160,swap->bobdeposit.redeemscript,&swap->bobdeposit.I.redeemlen,swap->bobdeposit.spendscript,0,&swap->bobdeposit.I.locktime,&swap->bobdeposit.I.secretstart,&swap->I,1);
        bitcoin_address(swap->bobdeposit.p2shaddr,swap->bobcoin.taddr,swap->bobcoin.p2shtype,swap->bobdeposit.redeemscript,swap->bobdeposit.I.redeemlen);
        strcpy(swap->bobdeposit.I.destaddr,swap->bobdeposit.p2shaddr);
        //LP_importaddress(swap->bobcoin.symbol,swap->bobdeposit.I.destaddr);
        int32_t i; for (i=0; i<swap->bobdeposit.I.redeemlen; i++)
            printf("%02x",swap->bobdeposit.redeemscript[i]);
        printf(" <- bobdeposit redeem %d %s\n",i,swap->bobdeposit.I.destaddr);
        if ( genflag != 0 && (swap->bobdeposit.I.datalen == 0 || swap->bobrefund.I.datalen == 0) )
        {
            basilisk_rawtx_gen(swap->ctx,"deposit",swap->I.started,swap->persistent_pubkey33,1,1,&swap->bobdeposit,swap->bobdeposit.I.locktime,swap->bobdeposit.spendscript,swap->bobdeposit.I.spendlen,swap->bobdeposit.coin->txfee,1,0,swap->persistent_privkey,swap->changermd160,coinaddr);
            if ( swap->bobdeposit.I.datalen == 0 || swap->bobdeposit.I.spendlen == 0 )
            {
                printf("error bob generating %p deposit.%d\n",swap->bobdeposit.txbytes,swap->bobdeposit.I.spendlen);
                sleep(DEX_SLEEP);
            }
            else
            {
                for (j=0; j<swap->bobdeposit.I.datalen; j++)
                    printf("%02x",swap->bobdeposit.txbytes[j]);
                printf(" <- GENERATED BOB DEPOSIT.%d (%s)\n",swap->bobdeposit.I.datalen,swap->bobdeposit.I.destaddr);
                LP_swap_coinaddr(swap,&swap->bobcoin,checkaddr,swap->bobdeposit.txbytes,swap->bobdeposit.I.datalen);
                if ( strcmp(swap->bobdeposit.I.destaddr,checkaddr) != 0 )
                {
                    printf("BOBDEPOSIT REDEEMADDR MISMATCH??? %s != %s\n",swap->bobdeposit.I.destaddr,checkaddr);
                    return(-1);
                }
                LP_unspents_mark(swap->bobcoin.symbol,swap->bobdeposit.vins);
                printf("bobscripts set completed\n");
                return(0);
            }
        }
    }
    return(0);
}

/**/

#ifdef old
int32_t basilisk_alicepayment_spend(struct basilisk_swap *swap,struct basilisk_rawtx *dest)
{
    int32_t i,retval;
    printf("alicepayment_spend\n");
    swap->alicepayment.I.spendlen = basilisk_alicescript(swap->alicepayment.redeemscript,&swap->alicepayment.I.redeemlen,swap->alicepayment.spendscript,0,swap->alicepayment.I.destaddr,swap->alicecoin.p2shtype,swap->I.pubAm,swap->I.pubBn);
    printf("alicepayment_spend len.%d\n",swap->alicepayment.I.spendlen);
    if ( swap->I.iambob == 0 )
    {
        memcpy(swap->I.userdata_alicereclaim,swap->alicepayment.redeemscript,swap->alicepayment.I.spendlen);
        swap->I.userdata_alicereclaimlen = swap->alicepayment.I.spendlen;
    }
    else
    {
        memcpy(swap->I.userdata_bobspend,swap->alicepayment.redeemscript,swap->alicepayment.I.spendlen);
        swap->I.userdata_bobspendlen = swap->alicepayment.I.spendlen;
    }
    if ( (retval= basilisk_rawtx_sign(swap->alicecoin.symbol,swap->alicecoin.pubtype,swap->alicecoin.p2shtype,swap->alicecoin.isPoS,swap->alicecoin.wiftype,swap,dest,&swap->alicepayment,swap->I.privAm,&swap->I.privBn,0,0,1,swap->changermd160)) == 0 )
    {
        for (i=0; i<dest->I.datalen; i++)
            printf("%02x",dest->txbytes[i]);
        printf(" <- msigspend\n\n");
        if ( dest == &swap->bobspend )
            swap->I.bobspent = 1;
        //basilisk_txlog(swap,dest,0); // bobspend or alicereclaim
        return(retval);
    }
    return(-1);
}
#endif

void basilisk_alicepayment(struct basilisk_swap *swap,struct iguana_info *coin,struct basilisk_rawtx *alicepayment,bits256 pubAm,bits256 pubBn)
{
    char coinaddr[64];
    alicepayment->I.spendlen = basilisk_alicescript(alicepayment->redeemscript,&alicepayment->I.redeemlen,alicepayment->spendscript,0,alicepayment->I.destaddr,coin->taddr,coin->p2shtype,pubAm,pubBn);
    /*for (i=0; i<33; i++)
        printf("%02x",swap->persistent_pubkey33[i]);
    printf(" pubkey33, ");
    for (i=0; i<20; i++)
        printf("%02x",swap->changermd160[i]);
    printf(" rmd160, ");*/
    bitcoin_address(coinaddr,coin->taddr,coin->pubtype,swap->changermd160,20);
    //printf("%s suppress.%d fee.%d\n",coinaddr,alicepayment->I.suppress_pubkeys,swap->myfee.I.suppress_pubkeys);
    basilisk_rawtx_gen(swap->ctx,"alicepayment",swap->I.started,swap->persistent_pubkey33,0,1,alicepayment,alicepayment->I.locktime,alicepayment->spendscript,alicepayment->I.spendlen,swap->I.Atxfee,1,0,swap->persistent_privkey,swap->changermd160,coinaddr);
}

int32_t basilisk_alicetxs(int32_t pairsock,struct basilisk_swap *swap,uint8_t *data,int32_t maxlen)
{
    char coinaddr[64]; int32_t i,retval = -1;
    if ( swap->alicepayment.I.datalen == 0 )
        basilisk_alicepayment(swap,swap->alicepayment.coin,&swap->alicepayment,swap->I.pubAm,swap->I.pubBn);
    if ( swap->alicepayment.I.datalen == 0 || swap->alicepayment.I.spendlen == 0 )
        printf("error alice generating payment.%d\n",swap->alicepayment.I.spendlen);
    else
    {
        bitcoin_address(swap->alicepayment.I.destaddr,swap->alicecoin.taddr,swap->alicecoin.p2shtype,swap->alicepayment.redeemscript,swap->alicepayment.I.redeemlen);
        //LP_importaddress(swap->alicecoin.symbol,swap->alicepayment.I.destaddr);
        strcpy(swap->alicepayment.p2shaddr,swap->alicepayment.I.destaddr);
        retval = 0;
        for (i=0; i<swap->alicepayment.I.datalen; i++)
            printf("%02x",swap->alicepayment.txbytes[i]);
        printf(" ALICE PAYMENT created.(%s)\n",swap->alicepayment.I.destaddr);
        LP_unspents_mark(swap->alicecoin.symbol,swap->alicepayment.vins);
        //LP_importaddress(swap->alicecoin.symbol,swap->alicepayment.I.destaddr);
        //basilisk_txlog(swap,&swap->alicepayment,-1);
    }
    if ( swap->myfee.I.datalen == 0 )
    {
        printf("generate fee %.8f\n",dstr(strcmp(swap->myfee.coin->symbol,"BTC") == 0 ? LP_MIN_TXFEE : swap->myfee.coin->txfee));
        bitcoin_address(coinaddr,swap->alicecoin.taddr,swap->alicecoin.pubtype,swap->changermd160,20);
        if ( basilisk_rawtx_gen(swap->ctx,"myfee",swap->I.started,swap->persistent_pubkey33,swap->I.iambob,1,&swap->myfee,0,swap->myfee.spendscript,swap->myfee.I.spendlen,strcmp(swap->myfee.coin->symbol,"BTC") == 0 ? LP_MIN_TXFEE : swap->myfee.coin->txfee,1,0,swap->persistent_privkey,swap->changermd160,coinaddr) == 0 )
        {
            printf("rawtxsend %s %.8f\n",swap->myfee.coin->symbol,dstr(strcmp(swap->myfee.coin->symbol,"BTC") == 0 ? LP_MIN_TXFEE : swap->myfee.coin->txfee));
            swap->I.statebits |= LP_swapdata_rawtxsend(pairsock,swap,0x80,data,maxlen,&swap->myfee,0x40,0);
            LP_unspents_mark(swap->I.iambob!=0?swap->bobcoin.symbol:swap->alicecoin.symbol,swap->myfee.vins);
            //basilisk_txlog(swap,&swap->myfee,-1);
            for (i=0; i<swap->myfee.I.datalen; i++)
                printf("%02x",swap->myfee.txbytes[i]);
            printf(" <- fee state.%x\n",swap->I.statebits);
            swap->I.statebits |= 0x40;
        }
        else
        {
            printf("error creating myfee\n");
            return(-2);
        }
    }
    if ( swap->alicepayment.I.datalen != 0 && swap->alicepayment.I.spendlen > 0 && swap->myfee.I.datalen != 0 && swap->myfee.I.spendlen > 0 )
        return(0);
    return(-1);
}

int32_t LP_verify_otherfee(struct basilisk_swap *swap,uint8_t *data,int32_t datalen)
{
    if ( LP_rawtx_spendscript(swap,swap->bobcoin.longestchain,&swap->otherfee,0,data,datalen,0) == 0 )
    {
        printf("otherfee amount %.8f -> %s vs %s\n",dstr(swap->otherfee.I.amount),swap->otherfee.p2shaddr,swap->otherfee.I.destaddr);
        if ( strcmp(swap->otherfee.I.destaddr,swap->otherfee.p2shaddr) == 0 )
        {
            printf("dexfee verified\n");
            return(0);
        }
    }
    return(-1);
}

/*    Bob deposit:
 OP_IF
 <now + INSTANTDEX_LOCKTIME*2> OP_CLTV OP_DROP <alice_pubA0> OP_CHECKSIG
 OP_ELSE
 OP_HASH160 <hash(bob_privN)> OP_EQUALVERIFY <bob_pubB0> OP_CHECKSIG
 OP_ENDIF*/

int32_t LP_verify_bobdeposit(struct basilisk_swap *swap,uint8_t *data,int32_t datalen)
{
    uint8_t userdata[512]; int32_t retval=-1,len = 0; static bits256 zero;
    if ( LP_rawtx_spendscript(swap,swap->bobcoin.longestchain,&swap->bobdeposit,0,data,datalen,0) == 0 )
    {
        swap->aliceclaim.utxovout = 0;
        swap->aliceclaim.utxotxid = swap->bobdeposit.I.signedtxid = LP_broadcast_tx(swap->bobdeposit.name,swap->bobcoin.symbol,swap->bobdeposit.txbytes,swap->bobdeposit.I.datalen);
        if ( bits256_nonz(swap->bobdeposit.I.signedtxid) != 0 )
            swap->depositunconf = 1;
        len = basilisk_swapuserdata(userdata,zero,1,swap->I.myprivs[0],swap->bobdeposit.redeemscript,swap->bobdeposit.I.redeemlen);
        memcpy(swap->I.userdata_aliceclaim,userdata,len);
        swap->I.userdata_aliceclaimlen = len;
        bitcoin_address(swap->bobdeposit.p2shaddr,swap->bobcoin.taddr,swap->bobcoin.p2shtype,swap->bobdeposit.redeemscript,swap->bobdeposit.I.redeemlen);
        strcpy(swap->bobdeposit.I.destaddr,swap->bobdeposit.p2shaddr);
        basilisk_dontforget_update(swap,&swap->bobdeposit);
        //LP_importaddress(swap->bobcoin.symbol,swap->bobdeposit.I.destaddr);
        /*for (i=0; i<swap->bobdeposit.I.datalen; i++)
            printf("%02x",swap->bobdeposit.txbytes[i]);
        printf(" <- bobdeposit.%d %s\n",swap->bobdeposit.I.datalen,bits256_str(str,swap->bobdeposit.I.signedtxid));
        for (i=0; i<swap->bobdeposit.I.redeemlen; i++)
            printf("%02x",swap->bobdeposit.redeemscript[i]);
        printf(" <- bobdeposit redeem %d %s suppress.%d\n",i,swap->bobdeposit.I.destaddr,swap->aliceclaim.I.suppress_pubkeys);*/
        memcpy(swap->aliceclaim.redeemscript,swap->bobdeposit.redeemscript,swap->bobdeposit.I.redeemlen);
        swap->aliceclaim.I.redeemlen = swap->bobdeposit.I.redeemlen;
        memcpy(swap->aliceclaim.I.pubkey33,swap->persistent_pubkey33,33);
        bitcoin_address(swap->aliceclaim.I.destaddr,swap->alicecoin.taddr,swap->alicecoin.pubtype,swap->persistent_pubkey33,33);
        retval = 0;
        if ( (retval= basilisk_rawtx_sign(swap->bobcoin.symbol,swap->bobcoin.wiftaddr,swap->bobcoin.taddr,swap->bobcoin.pubtype,swap->bobcoin.p2shtype,swap->bobcoin.isPoS,swap->bobcoin.wiftype,swap,&swap->aliceclaim,&swap->bobdeposit,swap->I.myprivs[0],0,userdata,len,1,swap->changermd160,swap->bobdeposit.I.destaddr)) == 0 )
        {
            /*for (i=0; i<swap->bobdeposit.I.datalen; i++)
                printf("%02x",swap->bobdeposit.txbytes[i]);
            printf(" <- bobdeposit\n");
            for (i=0; i<swap->aliceclaim.I.datalen; i++)
                printf("%02x",swap->aliceclaim.txbytes[i]);
            printf(" <- aliceclaim\n");*/
            //basilisk_txlog(swap,&swap->aliceclaim,swap->I.putduration+swap->I.callduration);
            return(LP_waitmempool(swap->bobcoin.symbol,swap->bobdeposit.I.signedtxid,10));
        } else printf("error signing aliceclaim suppress.%d vin.(%s)\n",swap->aliceclaim.I.suppress_pubkeys,swap->bobdeposit.I.destaddr);
    }
    printf("error with bobdeposit\n");
    return(retval);
}

int32_t LP_verify_alicepayment(struct basilisk_swap *swap,uint8_t *data,int32_t datalen)
{
    if ( LP_rawtx_spendscript(swap,swap->alicecoin.longestchain,&swap->alicepayment,0,data,datalen,0) == 0 )
    {
        swap->bobspend.utxovout = 0;
        swap->bobspend.utxotxid = swap->alicepayment.I.signedtxid = LP_broadcast_tx(swap->alicepayment.name,swap->alicecoin.symbol,swap->alicepayment.txbytes,swap->alicepayment.I.datalen);
        bitcoin_address(swap->alicepayment.p2shaddr,swap->alicecoin.taddr,swap->alicecoin.p2shtype,swap->alicepayment.redeemscript,swap->alicepayment.I.redeemlen);
        strcpy(swap->alicepayment.I.destaddr,swap->alicepayment.p2shaddr);
        if ( bits256_nonz(swap->alicepayment.I.signedtxid) != 0 )
            swap->aliceunconf = 1;
        basilisk_dontforget_update(swap,&swap->alicepayment);
        return(LP_waitmempool(swap->alicecoin.symbol,swap->alicepayment.I.signedtxid,10));
        //printf("import alicepayment address.(%s)\n",swap->alicepayment.p2shaddr);
        //LP_importaddress(swap->alicecoin.symbol,swap->alicepayment.p2shaddr);
        return(0);
    }
    printf("error validating alicepayment\n");
    return(-1);
}

int32_t LP_verify_bobpayment(struct basilisk_swap *swap,uint8_t *data,int32_t datalen)
{
    uint8_t userdata[512]; int32_t i,retval=-1,len = 0; bits256 revAm;
    memset(revAm.bytes,0,sizeof(revAm));
    if ( LP_rawtx_spendscript(swap,swap->bobcoin.longestchain,&swap->bobpayment,0,data,datalen,0) == 0 )
    {
        swap->alicespend.utxovout = 0;
        swap->alicespend.utxotxid = swap->bobpayment.I.signedtxid = LP_broadcast_tx(swap->bobpayment.name,swap->bobpayment.coin->symbol,swap->bobpayment.txbytes,swap->bobpayment.I.datalen);
        if ( bits256_nonz(swap->bobpayment.I.signedtxid) != 0 )
            swap->paymentunconf = 1;
        for (i=0; i<32; i++)
            revAm.bytes[i] = swap->I.privAm.bytes[31-i];
        len = basilisk_swapuserdata(userdata,revAm,0,swap->I.myprivs[0],swap->bobpayment.redeemscript,swap->bobpayment.I.redeemlen);
        bitcoin_address(swap->bobpayment.p2shaddr,swap->bobcoin.taddr,swap->bobcoin.p2shtype,swap->bobpayment.redeemscript,swap->bobpayment.I.redeemlen);
        strcpy(swap->bobpayment.I.destaddr,swap->bobpayment.p2shaddr);
        basilisk_dontforget_update(swap,&swap->bobpayment);
        //LP_importaddress(swap->bobcoin.symbol,swap->bobpayment.I.destaddr);
        /*for (i=0; i<swap->bobpayment.I.datalen; i++)
            printf("%02x",swap->bobpayment.txbytes[i]);
        printf(" <- bobpayment.%d\n",swap->bobpayment.I.datalen);
        for (i=0; i<swap->bobpayment.I.redeemlen; i++)
            printf("%02x",swap->bobpayment.redeemscript[i]);
        printf(" <- bobpayment redeem %d %s %s\n",i,swap->bobpayment.I.destaddr,bits256_str(str,swap->bobpayment.I.signedtxid));*/
        memcpy(swap->I.userdata_alicespend,userdata,len);
        swap->I.userdata_alicespendlen = len;
        retval = 0;
        memcpy(swap->alicespend.I.pubkey33,swap->persistent_pubkey33,33);
        bitcoin_address(swap->alicespend.I.destaddr,swap->bobcoin.taddr,swap->bobcoin.pubtype,swap->persistent_pubkey33,33);
        //char str[65],str2[65]; printf("bobpaid privAm.(%s) myprivs[0].(%s)\n",bits256_str(str,swap->I.privAm),bits256_str(str2,swap->I.myprivs[0]));
        if ( (retval= basilisk_rawtx_sign(swap->bobcoin.symbol,swap->bobcoin.wiftaddr,swap->bobcoin.taddr,swap->bobcoin.pubtype,swap->bobcoin.p2shtype,swap->bobcoin.isPoS,swap->bobcoin.wiftype,swap,&swap->alicespend,&swap->bobpayment,swap->I.myprivs[0],0,userdata,len,1,swap->changermd160,swap->alicepayment.I.destaddr)) == 0 )
        {
            /*for (i=0; i<swap->bobpayment.I.datalen; i++)
                printf("%02x",swap->bobpayment.txbytes[i]);
            printf(" <- bobpayment\n");
            for (i=0; i<swap->alicespend.I.datalen; i++)
                printf("%02x",swap->alicespend.txbytes[i]);
            printf(" <- alicespend\n\n");*/
            swap->I.alicespent = 1;
            return(LP_waitmempool(swap->bobcoin.symbol,swap->bobpayment.I.signedtxid,10));
        } else printf("error signing aliceclaim suppress.%d vin.(%s)\n",swap->alicespend.I.suppress_pubkeys,swap->bobpayment.I.destaddr);
    }
    printf("error validating bobpayment\n");
    return(-1);
}
