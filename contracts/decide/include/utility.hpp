// Utility file with functions and definitions for interacting with external contracts.
// 
// @author Craig Branscom

#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace std;
using namespace eosio;
using namespace decidespace;

// using user_resources = eosiosystem::user_resources;
// using del_bandwidth_table = eosiosystem::del_bandwidth_table;

//eosio.token account table
//code: eosio.token
//scope: account.value
struct eosio_account {
    asset balance;
    uint64_t primary_key() const { return balance.symbol.code().raw(); }
    EOSLIB_SERIALIZE(eosio_account, (balance))
};
typedef multi_index<name("accounts"), eosio_account> eosio_accounts_table;

//table containing staked system tokens
//code: eosio
//scope: from.value
struct delegated_bandwidth {
    name from;
    name to;
    asset net_weight;
    asset cpu_weight;
    // bool is_empty() const { return net_weight.amount == 0 && cpu_weight.amount == 0; }
    uint64_t  primary_key() const { return to.value; }
    EOSLIB_SERIALIZE(delegated_bandwidth, (from)(to)(net_weight)(cpu_weight))
};
typedef multi_index<name("delband"), delegated_bandwidth> del_bandwidth_table;

//get staked blaance from delband table
asset get_staked(name owner) {
    del_bandwidth_table delband(name("eosio"), owner.value);
    auto r = delband.find(owner.value);
    int64_t amount = 0;
    if (r != delband.end()) {
        amount = (r->cpu_weight.amount + r->net_weight.amount);
    }
    return asset(amount, decide::WAX_SYM);
}

