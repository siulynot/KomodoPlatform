
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
//  LP_prices.c
//  marketmaker
//

struct LP_orderbookentry { bits256 txid,txid2,pubkey; double price; uint64_t basesatoshis; int32_t vout,vout2,age; };

#define LP_MAXPRICEINFOS 256
struct LP_priceinfo
{
    char symbol[16];
    uint64_t coinbits;
    int32_t ind,pad;
    double diagval,high[2],low[2],last[2],bid[2],ask[2]; //volume,btcvolume,prevday; // mostly bittrex info
    double relvals[LP_MAXPRICEINFOS];
    double myprices[LP_MAXPRICEINFOS];
    double minprices[LP_MAXPRICEINFOS]; // autoprice
    double margins[LP_MAXPRICEINFOS];
    //double maxprices[LP_MAXPRICEINFOS]; // autofill of base/rel
    //double relvols[LP_MAXPRICEINFOS];
    FILE *fps[LP_MAXPRICEINFOS];
} LP_priceinfos[LP_MAXPRICEINFOS];
int32_t LP_numpriceinfos;

struct LP_cacheinfo
{
    UT_hash_handle hh;
    struct LP_quoteinfo Q;
    uint8_t key[sizeof(bits256)+sizeof(uint64_t)*2+sizeof(int32_t)];
    double price;
    uint32_t timestamp;
} *LP_cacheinfos;

struct LP_pubkeyinfo
{
    UT_hash_handle hh;
    bits256 pubkey;
    double matrix[LP_MAXPRICEINFOS][LP_MAXPRICEINFOS];
    uint32_t timestamp,istrusted,numerrors;
} *LP_pubkeyinfos;

int32_t LP_pricevalid(double price)
{
    if ( price > SMALLVAL && isnan(price) == 0 && price < SATOSHIDEN )
        return(1);
    else return(0);
}

struct LP_priceinfo *LP_priceinfofind(char *symbol)
{
    int32_t i; struct LP_priceinfo *pp; uint64_t coinbits;
    if ( symbol == 0 || symbol[0] == 0 )
        return(0);
    if ( LP_numpriceinfos > 0 )
    {
        coinbits = stringbits(symbol);
        pp = LP_priceinfos;
        for (i=0; i<LP_numpriceinfos; i++,pp++)
            if ( pp->coinbits == coinbits )
                return(pp);
    }
    return(0);
}

struct LP_priceinfo *LP_priceinfoptr(int32_t *indp,char *base,char *rel)
{
    struct LP_priceinfo *basepp,*relpp;
    if ( (basepp= LP_priceinfofind(base)) != 0 && (relpp= LP_priceinfofind(rel)) != 0 )
    {
        *indp = relpp->ind;
        return(basepp);
    }
    else
    {
        *indp = -1;
        return(0);
    }
}

int32_t LP_cachekey(uint8_t *key,char *base,char *rel,bits256 txid,int32_t vout)
{
    uint64_t basebits,relbits; int32_t offset = 0;
    basebits = stringbits(base);
    relbits = stringbits(rel);
    memcpy(&key[offset],&basebits,sizeof(basebits)), offset += sizeof(basebits);
    memcpy(&key[offset],&relbits,sizeof(relbits)), offset += sizeof(relbits);
    memcpy(&key[offset],&txid,sizeof(txid)), offset += sizeof(txid);
    memcpy(&key[offset],&vout,sizeof(vout)), offset += sizeof(vout);
    return(offset);
}

struct LP_cacheinfo *LP_cachefind(char *base,char *rel,bits256 txid,int32_t vout)
{
    struct LP_cacheinfo *ptr=0; uint8_t key[sizeof(bits256)+sizeof(uint64_t)*2+sizeof(vout)];
    if ( base == 0 || rel == 0 )
        return(0);
    if ( LP_cachekey(key,base,rel,txid,vout) == sizeof(key) )
    {
        portable_mutex_lock(&LP_cachemutex);
        HASH_FIND(hh,LP_cacheinfos,key,sizeof(key),ptr);
        portable_mutex_unlock(&LP_cachemutex);
    } else printf("LP_cachefind keysize mismatch?\n");
    if ( 0 && ptr != 0 && ptr->timestamp != 0 && ptr->timestamp < time(NULL)-LP_CACHEDURATION )
    {
        printf("expire price %.8f\n",ptr->price);
        ptr->price = 0.;
        ptr->timestamp = 0;
        memset(&ptr->Q,0,sizeof(ptr->Q));
    }
    return(ptr);
}

struct LP_pubkeyinfo *LP_pubkeyfind(bits256 pubkey)
{
    struct LP_pubkeyinfo *pubp=0;
    portable_mutex_lock(&LP_pubkeymutex);
    HASH_FIND(hh,LP_pubkeyinfos,&pubkey,sizeof(pubkey),pubp);
    portable_mutex_unlock(&LP_pubkeymutex);
    return(pubp);
}

struct LP_pubkeyinfo *LP_pubkeyadd(bits256 pubkey)
{
    struct LP_pubkeyinfo *pubp=0;
    if ( (pubp= LP_pubkeyfind(pubkey)) == 0 )
    {
        portable_mutex_lock(&LP_pubkeymutex);
        pubp = calloc(1,sizeof(*pubp));
        pubp->pubkey = pubkey;
        HASH_ADD_KEYPTR(hh,LP_pubkeyinfos,&pubp->pubkey,sizeof(pubp->pubkey),pubp);
        portable_mutex_unlock(&LP_pubkeymutex);
        if ( (pubp= LP_pubkeyfind(pubkey)) == 0 )
            printf("pubkeyadd find error after add\n");
    }
    return(pubp);
}

int32_t LP_pubkey_istrusted(bits256 pubkey)
{
    struct LP_pubkeyinfo *pubp;
    if ( (pubp= LP_pubkeyfind(pubkey)) != 0 )
        return(pubp->istrusted != 0);
    return(0);
}

char *LP_pubkey_trustset(bits256 pubkey,uint32_t trustval)
{
    struct LP_pubkeyinfo *pubp;
    if ( (pubp= LP_pubkeyfind(pubkey)) != 0 )
    {
        pubp->istrusted = trustval;
        return(clonestr("{\"result\":\"success\"}"));
    }
    return(clonestr("{\"error\":\"pubkey not found\"}"));
}

cJSON *LP_pubkeyjson(struct LP_pubkeyinfo *pubp)
{
    int32_t baseid,relid; char *base; double price; cJSON *item,*array,*obj;
    obj = cJSON_CreateObject();
    array = cJSON_CreateArray();
    for (baseid=0; baseid<LP_numpriceinfos; baseid++)
    {
        base = LP_priceinfos[baseid].symbol;
        for (relid=0; relid<LP_numpriceinfos; relid++)
        {
            price = pubp->matrix[baseid][relid];
            if ( LP_pricevalid(price) > 0 )
            {
                item = cJSON_CreateArray();
                jaddistr(item,base);
                jaddistr(item,LP_priceinfos[relid].symbol);
                jaddinum(item,price);
                jaddi(array,item);
            }
        }
    }
    jaddbits256(obj,"pubkey",pubp->pubkey);
    jaddnum(obj,"timestamp",pubp->timestamp);
    jadd(obj,"asks",array);
    if ( pubp->istrusted != 0 )
        jaddnum(obj,"istrusted",pubp->istrusted);
    return(obj);
}

char *LP_prices()
{
    struct LP_pubkeyinfo *pubp,*tmp; cJSON *array = cJSON_CreateArray();
    HASH_ITER(hh,LP_pubkeyinfos,pubp,tmp)
    {
        jaddi(array,LP_pubkeyjson(pubp));
    }
    return(jprint(array,1));
}

void LP_prices_parse(cJSON *obj)
{
    struct LP_pubkeyinfo *pubp; struct LP_priceinfo *basepp,*relpp; uint32_t timestamp; bits256 pubkey; cJSON *asks,*item; int32_t i,n,relid; char *base,*rel; double askprice;
    pubkey = jbits256(obj,"pubkey");
    if ( bits256_nonz(pubkey) != 0 && (pubp= LP_pubkeyadd(pubkey)) != 0 )
    {
        if ( (timestamp= juint(obj,"timestamp")) > pubp->timestamp && (asks= jarray(&n,obj,"asks")) != 0 )
        {
            pubp->timestamp = timestamp;
            for (i=0; i<n; i++)
            {
                item = jitem(asks,i);
                base = jstri(item,0);
                rel = jstri(item,1);
                askprice = jdoublei(item,2);
                if ( LP_pricevalid(askprice) > 0 )
                {
                    if ( (basepp= LP_priceinfoptr(&relid,base,rel)) != 0 )
                    {
                        char str[65]; printf("%s %s/%s (%d/%d) %.8f\n",bits256_str(str,pubkey),base,rel,basepp->ind,relid,askprice);
                        pubp->matrix[basepp->ind][relid] = askprice;
                        if ( (relpp= LP_priceinfofind(rel)) != 0 )
                        {
                            dxblend(&basepp->relvals[relpp->ind],askprice,0.9);
                            dxblend(&relpp->relvals[basepp->ind],1. / askprice,0.9);
                        }
                    }
                }
            }
        }
    }
}

void LP_peer_pricesquery(char *destipaddr,uint16_t destport)
{
    char *retstr; cJSON *array; int32_t i,n;
    if ( (retstr= issue_LP_getprices(destipaddr,destport)) != 0 )
    {
        if ( (array= cJSON_Parse(retstr)) != 0 )
        {
            if ( is_cJSON_Array(array) && (n= cJSON_GetArraySize(array)) > 0 )
            {
                for (i=0; i<n; i++)
                    LP_prices_parse(jitem(array,i));
            }
            free_json(array);
        }
        free(retstr);
    }
}

double LP_pricecache(struct LP_quoteinfo *qp,char *base,char *rel,bits256 txid,int32_t vout)
{
    struct LP_cacheinfo *ptr;
    if ( (ptr= LP_cachefind(base,rel,txid,vout)) != 0 )
    {
        if ( qp != 0 )
            (*qp) = ptr->Q;
        if ( ptr->price == 0. && ptr->Q.satoshis != 0 )
        {
            ptr->price = (double)ptr->Q.destsatoshis / ptr->Q.satoshis;
            if ( LP_pricevalid(ptr->price) <= 0 )
                ptr->price = 0.;
            //printf("LP_pricecache: set %s/%s ptr->price %.8f\n",base,rel,ptr->price);
        }
        //printf("found %s/%s %.8f\n",base,rel,ptr->price);
        return(ptr->price);
    }
    //char str[65]; printf("cachemiss %s/%s %s/v%d\n",base,rel,bits256_str(str,txid),vout);
    return(0.);
}

void LP_priceinfoupdate(char *base,char *rel,double price)
{
    struct LP_priceinfo *basepp,*relpp;
    if ( LP_pricevalid(price) > 0 )
    {
        if ( (basepp= LP_priceinfofind(base)) != 0 && (relpp= LP_priceinfofind(rel)) != 0 )
        {
            //dxblend(&basepp->relvals[relpp->ind],price,0.9);
            //dxblend(&relpp->relvals[basepp->ind],1. / price,0.9);
            basepp->relvals[relpp->ind] = price;
            relpp->relvals[basepp->ind] = 1. / price;
        }
    }
}

double LP_myprice(double *bidp,double *askp,char *base,char *rel)
{
    struct LP_priceinfo *basepp,*relpp; double val;
    *bidp = *askp = 0.;
    if ( (basepp= LP_priceinfofind(base)) != 0 && (relpp= LP_priceinfofind(rel)) != 0 )
    {
        *askp = basepp->myprices[relpp->ind];
        if ( LP_pricevalid(*askp) > 0 )
        {
            val = relpp->myprices[basepp->ind];
            if ( LP_pricevalid(val) > 0 )
            {
                *bidp = 1. / val;
                return((*askp + *bidp) * 0.5);
            }
            else
            {
                *bidp = 0.;
                return(*askp);
            }
        }
        else
        {
            val = relpp->myprices[basepp->ind];
            if ( LP_pricevalid(val) > 0 )
            {
                *bidp = 1. / val;
                *askp = 0.;
                return(*bidp);
            }
        }
    }
    return(0.);
}

char *LP_myprices()
{
    int32_t baseid,relid; double bid,ask; char *base,*rel; cJSON *item,*array;
    array = cJSON_CreateArray();
    for (baseid=0; baseid<LP_numpriceinfos; baseid++)
    {
        base = LP_priceinfos[baseid].symbol;
        for (relid=0; relid<LP_numpriceinfos; relid++)
        {
            rel = LP_priceinfos[relid].symbol;
            if ( LP_myprice(&bid,&ask,base,rel) > SMALLVAL )
            {
                item = cJSON_CreateObject();
                jaddstr(item,"base",base);
                jaddstr(item,"rel",rel);
                jaddnum(item,"bid",bid);
                jaddnum(item,"ask",ask);
                jaddi(array,item);
            }
        }
    }
    return(jprint(array,1));
}

int32_t LP_mypriceset(int32_t *changedp,char *base,char *rel,double price)
{
    struct LP_priceinfo *basepp,*relpp; struct LP_pubkeyinfo *pubp;
    *changedp = 0;
    if ( base != 0 && rel != 0 && LP_pricevalid(price) > 0 && (basepp= LP_priceinfofind(base)) != 0 && (relpp= LP_priceinfofind(rel)) != 0 )
    {
        if ( fabs(basepp->myprices[relpp->ind] - price) > SMALLVAL )
            *changedp = 1;
        basepp->myprices[relpp->ind] = price;          // ask
        //printf("LP_mypriceset base.%s rel.%s <- price %.8f\n",base,rel,price);
        //relpp->myprices[basepp->ind] = (1. / price);   // bid
        if ( (pubp= LP_pubkeyadd(LP_mypub25519)) != 0 )
        {
            pubp->matrix[basepp->ind][relpp->ind] = price;
            //pubp->matrix[relpp->ind][basepp->ind] = (1. / price);
            pubp->timestamp = (uint32_t)time(NULL);
        }
        return(0);
    } else return(-1);
}

double LP_price(char *base,char *rel)
{
    struct LP_priceinfo *basepp; int32_t relind; double price = 0.;
    if ( (basepp= LP_priceinfoptr(&relind,base,rel)) != 0 )
    {
        if ( (price= basepp->myprices[relind]) == 0. )
            price = basepp->relvals[relind];
    }
    return(price);
}

cJSON *LP_priceinfomatrix(int32_t usemyprices)
{
    int32_t i,j,n,m; double total,sum,val; struct LP_priceinfo *pp; uint32_t now; struct LP_cacheinfo *ptr,*tmp; cJSON *vectorjson = cJSON_CreateObject();
    now = (uint32_t)time(NULL);
    HASH_ITER(hh,LP_cacheinfos,ptr,tmp)
    {
        if ( ptr->timestamp < now-3600*2 || ptr->price == 0. )
            continue;
        LP_priceinfoupdate(ptr->Q.srccoin,ptr->Q.destcoin,ptr->price);
    }
    pp = LP_priceinfos;
    total = m = 0;
    for (i=0; i<LP_numpriceinfos; i++,pp++)
    {
        pp->diagval = sum = n = 0;
        for (j=0; j<LP_numpriceinfos; j++)
        {
            if ( usemyprices == 0 || (val= pp->myprices[j]) == 0. )
                val = pp->relvals[j];
            if ( val > SMALLVAL )
            {
                sum += val;
                n++;
            }
        }
        if ( n > 0 )
        {
            pp->diagval = sum / n;
            total += pp->diagval, m++;
        }
    }
    if ( m > 0 )
    {
        pp = LP_priceinfos;
        for (i=0; i<LP_numpriceinfos; i++,pp++)
        {
            if ( pp->diagval > SMALLVAL )
            {
                pp->diagval /= total;
                jaddnum(vectorjson,pp->symbol,pp->diagval);
            }
        }
    }
    return(vectorjson);
}

struct LP_priceinfo *LP_priceinfoadd(char *symbol)
{
    struct LP_priceinfo *pp; cJSON *retjson;
    if ( symbol == 0 )
        return(0);
    if ( LP_numpriceinfos >= sizeof(LP_priceinfos)/sizeof(*LP_priceinfos) )
    {
        printf("cant add any more priceinfos\n");
        return(0);
    }
    pp = &LP_priceinfos[LP_numpriceinfos];
    memset(pp,0,sizeof(*pp));
    safecopy(pp->symbol,symbol,sizeof(pp->symbol));
    pp->coinbits = stringbits(symbol);
    pp->ind = LP_numpriceinfos++;
    LP_numpriceinfos++;
    if ( (retjson= LP_priceinfomatrix(0)) != 0 )
        free_json(retjson);
    return(pp);
}

struct LP_cacheinfo *LP_cacheadd(char *base,char *rel,bits256 txid,int32_t vout,double price,struct LP_quoteinfo *qp)
{
    char str[65]; struct LP_cacheinfo *ptr=0;
    if ( base == 0 || rel == 0 )
        return(0);
    if ( LP_pricevalid(price) > 0 )
    {
        if ( (ptr= LP_cachefind(base,rel,txid,vout)) == 0 )
        {
            ptr = calloc(1,sizeof(*ptr));
            if ( LP_cachekey(ptr->key,base,rel,txid,vout) == sizeof(ptr->key) )
            {
                portable_mutex_lock(&LP_cachemutex);
                HASH_ADD(hh,LP_cacheinfos,key,sizeof(ptr->key),ptr);
                portable_mutex_unlock(&LP_cachemutex);
            } else printf("LP_cacheadd keysize mismatch?\n");
        }
        ptr->Q = *qp;
        ptr->timestamp = (uint32_t)time(NULL);
        if ( price != ptr->price )
        {
            ptr->price = price;
            LP_priceinfoupdate(base,rel,price);
            printf("updated %s/v%d %s/%s %llu price %.8f\n",bits256_str(str,txid),vout,base,rel,(long long)qp->satoshis,price);
        } else ptr->price = price;
    }
    return(ptr);
}

static int _cmp_orderbook(const void *a,const void *b)
{
    int32_t retval = 0;
#define ptr_a (*(struct LP_orderbookentry **)a)->price
#define ptr_b (*(struct LP_orderbookentry **)b)->price
    if ( ptr_b > ptr_a )
        retval = -1;
    else if ( ptr_b < ptr_a )
        retval = 1;
    else
    {
#undef ptr_a
#undef ptr_b
#define ptr_a ((struct LP_orderbookentry *)a)->basesatoshis
#define ptr_b ((struct LP_orderbookentry *)b)->basesatoshis
        if ( ptr_b > ptr_a )
            return(-1);
        else if ( ptr_b < ptr_a )
            return(1);
    }
   // printf("%.8f vs %.8f -> %d\n",ptr_a,ptr_b,retval);
    return(retval);
#undef ptr_a
#undef ptr_b
}

cJSON *LP_orderbookjson(struct LP_orderbookentry *op)
{
    cJSON *item = cJSON_CreateObject();
    if ( LP_pricevalid(op->price) > 0 )
    {
        jaddnum(item,"price",op->price);
        jaddnum(item,"volume",dstr(op->basesatoshis));
        jaddbits256(item,"txid",op->txid);
        jaddnum(item,"vout",op->vout);
        jaddbits256(item,"pubkey",op->pubkey);
        jaddnum(item,"age",op->age);
    }
    return(item);
}

struct LP_orderbookentry *LP_orderbookentry(char *base,char *rel,bits256 txid,int32_t vout,bits256 txid2,int32_t vout2,double price,uint64_t basesatoshis,bits256 pubkey,int32_t age)
{
    struct LP_orderbookentry *op;
    if ( (op= calloc(1,sizeof(*op))) != 0 )
    {
        op->txid = txid;
        op->vout = vout;
        op->txid2 = txid2;
        op->vout2 = vout2;
        op->price = price;
        op->basesatoshis = basesatoshis;
        op->pubkey = pubkey;
        op->age = age;
    }
    return(op);
}

int32_t LP_orderbookfind(struct LP_orderbookentry **array,int32_t num,bits256 txid,int32_t vout)
{
    int32_t i;
    for (i=0; i<num; i++)
        if ( (array[i]->vout == vout && bits256_cmp(array[i]->txid,txid) == 0) || (array[i]->vout2 == vout && bits256_cmp(array[i]->txid2,txid) == 0) )
            return(i);
    return(-1);
}

int32_t LP_orderbook_utxoentries(uint32_t now,int32_t polarity,char *base,char *rel,struct LP_orderbookentry *(**arrayp),int32_t num,int32_t cachednum,int32_t duration)
{
    struct LP_utxoinfo *utxo,*tmp; struct LP_pubkeyinfo *pubp=0; struct LP_priceinfo *basepp; struct LP_orderbookentry *op; uint32_t oldest; double price; int32_t baseid,relid; uint64_t basesatoshis,val,val2;
    if ( (basepp= LP_priceinfoptr(&relid,base,rel)) != 0 )
        baseid = basepp->ind;
    else return(num);
    now = (uint32_t)time(NULL);
    oldest = now - duration;
    HASH_ITER(hh,LP_utxoinfos[1],utxo,tmp)
    {
        if ( pubp == 0 || bits256_cmp(pubp->pubkey,utxo->pubkey) != 0 )
            pubp = LP_pubkeyfind(utxo->pubkey);
        if ( pubp != 0 && pubp->numerrors >= LP_MAXPUBKEY_ERRORS )
            continue;
        //char str[65],str2[65]; printf("check utxo.%s/v%d from %s\n",bits256_str(str,utxo->payment.txid),utxo->payment.vout,bits256_str(str2,utxo->pubkey));
        if ( strcmp(base,utxo->coin) == 0 && LP_isavailable(utxo) > 0 && pubp != 0 && (price= pubp->matrix[baseid][relid]) > SMALLVAL && pubp->timestamp > oldest && pubp->timestamp <= now )
        {
            if ( LP_orderbookfind(*arrayp,cachednum,utxo->payment.txid,utxo->payment.vout) < 0 )
            {
                if ( LP_iseligible(&val,&val2,utxo->iambob,utxo->coin,utxo->payment.txid,utxo->payment.vout,utxo->S.satoshis,utxo->deposit.txid,utxo->deposit.vout) == 0 )
                    continue;
                if ( polarity > 0 )
                    basesatoshis = utxo->S.satoshis;
                else basesatoshis = utxo->S.satoshis * price;
                //char str[65]; printf("found utxo not in orderbook %s/v%d %.8f %.8f\n",bits256_str(str,utxo->payment.txid),utxo->payment.vout,dstr(basesatoshis),polarity > 0 ? price : 1./price);
                if ( (op= LP_orderbookentry(base,rel,utxo->payment.txid,utxo->payment.vout,utxo->deposit.txid,utxo->deposit.vout,polarity > 0 ? price : 1./price,basesatoshis,utxo->pubkey,now - pubp->timestamp)) != 0 )
                {
                    *arrayp = realloc(*arrayp,sizeof(*(*arrayp)) * (num+1));
                    (*arrayp)[num++] = op;
                    if ( LP_ismine(utxo) > 0 && utxo->T.lasttime == 0 )
                        LP_utxo_clientpublish(utxo);
                }
            }
        }
    }
    return(num);
}

char *LP_orderbook(char *base,char *rel,int32_t duration)
{
    uint32_t now,i; struct LP_priceinfo *basepp=0,*relpp=0; struct LP_orderbookentry **bids = 0,**asks = 0; cJSON *retjson,*array; int32_t numbids=0,numasks=0,cachenumbids,cachenumasks,baseid,relid;
    if ( (basepp= LP_priceinfofind(base)) == 0 || (relpp= LP_priceinfofind(rel)) == 0 )
        return(clonestr("{\"error\":\"base or rel not added\"}"));
    if ( duration <= 0 )
        duration = LP_ORDERBOOK_DURATION;
    baseid = basepp->ind;
    relid = relpp->ind;
    now = (uint32_t)time(NULL);
    cachenumbids = numbids, cachenumasks = numasks;
    //printf("start cache.(%d %d) numbids.%d numasks.%d\n",cachenumbids,cachenumasks,numbids,numasks);
    numasks = LP_orderbook_utxoentries(now,1,base,rel,&asks,numasks,cachenumasks,duration);
    numbids = LP_orderbook_utxoentries(now,-1,rel,base,&bids,numbids,cachenumbids,duration);
    retjson = cJSON_CreateObject();
    array = cJSON_CreateArray();
    if ( numbids > 1 )
        qsort(bids,numbids,sizeof(*bids),_cmp_orderbook);
    if ( numasks > 1 )
    {
        //for (i=0; i<numasks; i++)
        //    printf("%.8f ",asks[i]->price);
        //printf(" -> ");
        qsort(asks,numasks,sizeof(*asks),_cmp_orderbook);
        //for (i=0; i<numasks; i++)
        //    printf("%.8f ",asks[i]->price);
        //printf("sorted asks.%d\n",numasks);
    }
    for (i=0; i<numbids; i++)
    {
        jaddi(array,LP_orderbookjson(bids[i]));
        free(bids[i]);
        bids[i] = 0;
    }
    jadd(retjson,"bids",array);
    jaddnum(retjson,"numbids",numbids);
    array = cJSON_CreateArray();
    for (i=0; i<numasks; i++)
    {
        jaddi(array,LP_orderbookjson(asks[i]));
        free(asks[i]);
        asks[i] = 0;
    }
    jadd(retjson,"asks",array);
    jaddnum(retjson,"numasks",numasks);
    jaddstr(retjson,"base",base);
    jaddstr(retjson,"rel",rel);
    jaddnum(retjson,"timestamp",now);
    if ( bids != 0 )
        free(bids);
    if ( asks != 0 )
        free(asks);
    return(jprint(retjson,1));
}

char *LP_pricestr(char *base,char *rel,double origprice)
{
    cJSON *retjson; double price = 0.;
    if ( base != 0 && base[0] != 0 && rel != 0 && rel[0] != 0 )
    {
        price = LP_price(base,rel);
        if ( origprice > SMALLVAL && origprice < price )
            price = origprice;
    }
    if ( LP_pricevalid(price) > 0 )
    {
        retjson = cJSON_CreateObject();
        jaddstr(retjson,"result","success");
        jaddstr(retjson,"method","postprice");
        jaddbits256(retjson,"pubkey",LP_mypub25519);
        jaddstr(retjson,"base",base);
        jaddstr(retjson,"rel",rel);
        jaddnum(retjson,"price",price);
        jadd(retjson,"theoretical",LP_priceinfomatrix(0));
        jadd(retjson,"quotes",LP_priceinfomatrix(1));
        return(jprint(retjson,1));
    } else return(clonestr("{\"error\":\"cant find baserel pair\"}"));
}

void LP_priceupdate(char *base,char *rel,double price,double avebid,double aveask,double highbid,double lowask,double PAXPRICES[32])
{
    LP_priceinfoupdate(base,rel,price);
}

void LP_pricefname(char *fname,char *base,char *rel)
{
    sprintf(fname,"%s/PRICES/%s_%s",GLOBAL_DBDIR,base,rel);
    OS_compatible_path(fname);
}

void LP_priceitemadd(cJSON *retarray,uint32_t timestamp,double avebid,double aveask,double highbid,double lowask)
{
    cJSON *item = cJSON_CreateArray();
    jaddinum(item,timestamp);
    jaddinum(item,avebid);
    jaddinum(item,aveask);
    jaddinum(item,highbid);
    jaddinum(item,lowask);
    jaddi(retarray,item);
}

cJSON *LP_pricearray(char *base,char *rel,uint32_t firsttime,uint32_t lasttime,int32_t timescale)
{
    cJSON *retarray; char askfname[1024],bidfname[1024]; uint64_t bidprice64,askprice64; uint32_t bidnow,asknow,bidi,aski,lastbidi,lastaski; int32_t numbids,numasks; double bidemit,askemit,bidsum,asksum,bid,ask,highbid,lowbid,highask,lowask,bidemit2,askemit2; FILE *askfp=0,*bidfp=0;
    if ( timescale <= 0 )
        timescale = 60;
    if ( lasttime == 0 )
        lasttime = (uint32_t)-1;
    LP_pricefname(askfname,base,rel);
    LP_pricefname(bidfname,rel,base);
    retarray = cJSON_CreateArray();
    lastbidi = lastaski = 0;
    numbids = numasks = 0;
    bidsum = asksum = askemit = bidemit = highbid = lowbid = highask = lowask = 0.;
    if ( (bidfp= fopen(bidfname,"rb")) != 0 && (askfp= fopen(askfname,"rb")) != 0 )
    {
        while ( bidfp != 0 || askfp != 0 )
        {
            bidi = aski = 0;
            bidemit = askemit = bidemit2 = askemit2 = 0.;
            if ( bidfp != 0 && fread(&bidnow,1,sizeof(bidnow),bidfp) == sizeof(bidnow) && fread(&bidprice64,1,sizeof(bidprice64),bidfp) == sizeof(bidprice64) )
            {
                //printf("bidnow.%u %.8f\n",bidnow,dstr(bidprice64));
                if ( bidnow != 0 && bidprice64 != 0 && bidnow >= firsttime && bidnow <= lasttime )
                {
                    bidi = bidnow / timescale;
                    if ( bidi != lastbidi )
                    {
                        if ( bidsum != 0. && numbids != 0 )
                        {
                            bidemit = bidsum / numbids;
                            bidemit2 = highbid;
                        }
                        bidsum = highbid = lowbid = 0.;
                        numbids = 0;
                    }
                    if ( (bid= 1. / dstr(bidprice64)) != 0. )
                    {
                        if ( bid > highbid )
                            highbid = bid;
                        if ( lowbid == 0. || bid < lowbid )
                            lowbid = bid;
                        bidsum += bid;
                        numbids++;
                        //printf("bidi.%u num.%d %.8f [%.8f %.8f]\n",bidi,numbids,bid,lowbid,highbid);
                    }
                }
            } else fclose(bidfp), bidfp = 0;
            if ( askfp != 0 && fread(&asknow,1,sizeof(asknow),askfp) == sizeof(asknow) && fread(&askprice64,1,sizeof(askprice64),askfp) == sizeof(askprice64) )
            {
                //printf("asknow.%u %.8f\n",asknow,dstr(askprice64));
                if ( asknow != 0 && askprice64 != 0 && asknow >= firsttime && asknow <= lasttime )
                {
                    aski = asknow / timescale;
                    if ( aski != lastaski )
                    {
                        if ( asksum != 0. && numasks != 0 )
                        {
                            askemit = asksum / numasks;
                            askemit2 = lowask;
                        }
                        asksum = highask = lowask = 0.;
                        numasks = 0;
                    }
                    if ( (ask= dstr(askprice64)) != 0. )
                    {
                        if ( ask > highask )
                            highask = ask;
                        if ( lowask == 0. || ask < lowask )
                            lowask = ask;
                        asksum += ask;
                        numasks++;
                        //printf("aski.%u num.%d %.8f [%.8f %.8f]\n",aski,numasks,ask,lowask,highask);
                    }
                }
            } else fclose(askfp), askfp = 0;
            if ( bidemit != 0. || askemit != 0. )
            {
                if ( bidemit != 0. && askemit != 0. && lastbidi == lastaski )
                {
                    LP_priceitemadd(retarray,lastbidi * timescale,bidemit,askemit,bidemit2,askemit2);
                    highbid = lowbid = highask = lowask = 0.;
                }
                else
                {
                    if ( bidemit != 0. )
                    {
                        printf("bidonly %.8f %.8f\n",bidemit,highbid);
                        LP_priceitemadd(retarray,lastbidi * timescale,bidemit,0.,bidemit2,0.);
                        highbid = lowbid = 0.;
                    }
                    if ( askemit != 0. )
                    {
                        printf("askonly %.8f %.8f\n",askemit,lowask);
                        LP_priceitemadd(retarray,lastaski * timescale,0.,askemit,0.,askemit2);
                        highask = lowask = 0.;
                    }
                }
            }
            if ( bidi != 0 )
                lastbidi = bidi;
            if ( aski != 0 )
                lastaski = aski;
        }
    } else printf("couldnt open either %s %p or %s %p\n",bidfname,bidfp,askfname,askfp);
    if ( bidfp != 0 )
        fclose(bidfp);
    if ( askfp != 0 )
        fclose(askfp);
    return(retarray);
}

void LP_pricefeedupdate(bits256 pubkey,char *base,char *rel,double price)
{
    struct LP_priceinfo *basepp,*relpp; uint32_t now; uint64_t price64; struct LP_pubkeyinfo *pubp; char str[65],fname[512]; FILE *fp;
    //printf("check PRICEFEED UPDATE.(%s/%s) %.8f %s\n",base,rel,price,bits256_str(str,pubkey));
    if ( LP_pricevalid(price) > 0 && (basepp= LP_priceinfofind(base)) != 0 && (relpp= LP_priceinfofind(rel)) != 0 )
    {
        if ( (fp= basepp->fps[relpp->ind]) == 0 )
        {
            LP_pricefname(fname,base,rel);
            fp = basepp->fps[relpp->ind] = OS_appendfile(fname);
        }
        if ( fp != 0 )
        {
            now = (uint32_t)time(NULL);
            price64 = price * SATOSHIDEN;
            fwrite(&now,1,sizeof(now),fp);
            fwrite(&price64,1,sizeof(price64),fp);
            fflush(fp);
        }
        if ( (fp= relpp->fps[basepp->ind]) == 0 )
        {
            sprintf(fname,"%s/PRICES/%s_%s",GLOBAL_DBDIR,rel,base);
            fp = relpp->fps[basepp->ind] = OS_appendfile(fname);
        }
        if ( fp != 0 )
        {
            now = (uint32_t)time(NULL);
            price64 = (1. / price) * SATOSHIDEN;
            fwrite(&now,1,sizeof(now),fp);
            fwrite(&price64,1,sizeof(price64),fp);
            fflush(fp);
        }
        if ( (pubp= LP_pubkeyadd(pubkey)) != 0 )
        {
            if ( (rand() % 100) == 0 && fabs(pubp->matrix[basepp->ind][relpp->ind] - price) > SMALLVAL )
                printf("PRICEFEED UPDATE.(%-6s/%6s) %12.8f %s %12.8f\n",base,rel,price,bits256_str(str,pubkey),1./price);
            {
                pubp->matrix[basepp->ind][relpp->ind] = price;
                dxblend(&basepp->relvals[relpp->ind],price,0.9);
                dxblend(&relpp->relvals[basepp->ind],1. / price,0.9);
            }
            pubp->timestamp = (uint32_t)time(NULL);
        } else printf("error creating pubkey entry\n");
    }
    else if ( (rand() % 100) == 0 )
        printf("error finding %s/%s %.8f\n",base,rel,price);
}

