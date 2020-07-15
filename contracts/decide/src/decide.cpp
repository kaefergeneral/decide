#include <decide.hpp>
#include <utility.hpp>

using namespace decidespace;

//======================== admin actions ========================

ACTION decide::init(string app_version) {
    
    //authenticate
    require_auth(get_self());

    //open config singleton
    config_singleton configs(get_self(), get_self().value);

    //validate
    check(!configs.exists(), "contract already initialized");

    //initialize
    string app_name = "Decide";
    map<name, asset> new_fees;
    map<name, uint32_t> new_times;
        
    //set default fees
    new_fees[name("ballot")] = asset(10'00000000, WAX_SYM); //10 WAX
    new_fees[name("treasury")] = asset(500'00000000, WAX_SYM); //500 WAX
    new_fees[name("archival")] = asset(1'00000000, WAX_SYM); //1 WAX (per day)
    new_fees[name("committee")] = asset(100'00000000, WAX_SYM); //100 WAX

    //set default times
    new_times[name("minballength")] = uint32_t(60); //1 minute in seconds
    new_times[name("balcooldown")] = uint32_t(86400); //1 day in seconds
    new_times[name("forfeittime")] = uint32_t(864000); //10 days in seconds

    //build new configs
    config new_config = {
        app_name, //app_name
        app_version, //app_version
        asset(0, WAX_SYM), //total_deposits
        new_fees, //fees
        new_times //times
    };

    //set new config
    configs.set(new_config, get_self());

}

ACTION decide::setversion(string new_app_version) {

    //authenticate
    require_auth(get_self());

    //open configs singleton
    config_singleton configs(get_self(), get_self().value);
    auto conf = configs.get();

    //set new version
    conf.app_version = new_app_version;

    //set config
    configs.set(conf, get_self());

}

ACTION decide::updatefee(name fee_name, asset fee_amount) {
    
    //authenticate
    require_auth(get_self());

    //open configs singleton
    config_singleton configs(get_self(), get_self().value);
    auto conf = configs.get();
    
    //validate
    check(fee_amount.symbol == WAX_SYM,  "fee symbol must be WAX");
    check(fee_amount >= asset(0, WAX_SYM), "fee amount must be a positive number");

    //initialize
    config new_conf = conf;

    //update fee
    new_conf.fees[fee_name] = fee_amount;

    //update fee
    configs.set(new_conf, get_self());

}

ACTION decide::updatetime(name time_name, uint32_t length) {
    
    //authenticate
    require_auth(get_self());

    //open configs singleton
    config_singleton configs(get_self(), get_self().value);
    auto conf = configs.get();

    //validate
    check(length >= 1, "length must be a positive number");

    //initialize
    config new_conf = conf;

    //update time name with new length
    new_conf.times[time_name] = length;

    //update time
    configs.set(new_conf, get_self());

}

ACTION decide::updatenvoter(name nonvoter, string comment, bool remove){
        
    //authenticate
    require_auth(get_self());
    check(is_account(nonvoter), "Account doesn't exist.");

    //open nonvoters table, look up account
    nonvoters_table nonvoters(get_self(), get_self().value);
    auto nvtr = nonvoters.find(nonvoter.value);

    //if new entry
    if (nvtr == nonvoters.end()) {
        check(!comment.empty(), "Comment required.");
        nonvoters.emplace(get_self(), [&]( auto& row ) {
            row.nvoter = nonvoter;
            row.comment = comment;
        });
    } else {
         if (remove) {
            nonvoters.erase(nvtr);
        } else {
            nonvoters.modify(nvtr, get_self(), [&](auto& row) {
            row.comment = comment;});
        }
    }
}

ACTION decide::withdraw(name voter, asset quantity) {
    
    //authenticate
    require_auth(voter);
    
    //open accounts table, get account
    accounts_table wax_accounts(get_self(), voter.value);
    auto& acct = wax_accounts.get(WAX_SYM.code().raw(), "account not found");

    //validate
    check(quantity.symbol == WAX_SYM, "can only withdraw WAX");
    check(acct.balance >= quantity, "insufficient balance");
    check(quantity > asset(0, WAX_SYM), "must withdraw a positive amount");

    //update balances
    wax_accounts.modify(acct, same_payer, [&](auto& col) {
        col.balance -= quantity;
    });

    config_singleton configs(get_self(), get_self().value);
    auto config = configs.get();

    config.total_deposits -= quantity;

    configs.set(config, get_self());

    //transfer to eosio.token
    //inline trx requires wax.decide@active to have wax.decide@eosio.code
    //TODO: replace with action handler
    // token::transfer_action transfer_act("eosio.token"_n, { get_self(), active_permission });
    // transfer_act.send(get_self(), voter, quantity, std::string("Wax Decide Withdrawal"));

    //inline transfer
    action(permission_level{get_self(), name("active")}, name("eosio.token"), name("transfer"), make_tuple(
        get_self(), //from
        voter, //to
        quantity, //quantity
        std::string("Wax Decide Withdrawal") //memo
    )).send();

}


//========== notification methods ==========

void decide::catch_delegatebw(name from, name receiver, asset stake_net_quantity, asset stake_cpu_quantity, bool transfer) {
    //sync external stake with internal stake
    sync_external_account(from, VOTE_SYM, stake_net_quantity.symbol);
}

void decide::catch_undelegatebw(name from, name receiver, asset unstake_net_quantity, asset unstake_cpu_quantity) {
    //sync external stake with internal stake
    sync_external_account(from, VOTE_SYM, unstake_net_quantity.symbol);
}

void decide::catch_transfer(name from, name to, asset quantity, string memo) {
    //get initial receiver contract
    name rec = get_first_receiver();

    //validate
    if (rec == name("eosio.token") && from != get_self() && quantity.symbol == WAX_SYM) {
        
        //parse memo
        //NOTE: keyword should maybe be "refund" or "return"
        if (memo == std::string("skip")) //skips emplacement if memo is skip
            return;
        
        //open accounts table, search for account
        accounts_table accounts(get_self(), from.value);
        auto acct = accounts.find(WAX_SYM.code().raw());

        //emplace account if not found, update if exists
        if (acct == accounts.end()) { //no account
            accounts.emplace(get_self(), [&](auto& col) {
                col.balance = quantity;
            });
        } else { //exists
            accounts.modify(*acct, same_payer, [&](auto& col) {
                col.balance += quantity;
            });
        }

        config_singleton configs(get_self(), get_self().value);
        auto config = configs.get();

        config.total_deposits += quantity;

        configs.set(config, get_self());
    } else if (rec == name("eosio.token") && from == get_self() && quantity.symbol == WAX_SYM) {
        config_singleton configs(get_self(), get_self().value);
        auto config = configs.get();

        eosio_accounts_table eosio_accounts(name("eosio.token"), get_self().value);
        auto& eosio_acct = eosio_accounts.get(WAX_SYM.code().raw(), "WAX balance not found");

        asset total_transferable = (eosio_acct.balance + quantity) - config.total_deposits;
        
        check(total_transferable >= quantity, "Decide lacks the liquid WAX to make this transfer");
    }
}

//========== utility methods ==========

void decide::add_liquid(name voter, asset quantity) {
    //open voters table, get voter
    voters_table to_voters(get_self(), voter.value);
    auto& to_voter = to_voters.get(quantity.symbol.code().raw(), "add_liquid: voter not found");

    //add quantity to liquid
    to_voters.modify(to_voter, same_payer, [&](auto& col) {
        col.liquid += quantity;
    });
}

void decide::sub_liquid(name voter, asset quantity) {
    //open voters table, get voter
    voters_table from_voters(get_self(), voter.value);
    auto& from_voter = from_voters.get(quantity.symbol.code().raw(), "sub_liquid: voter not found");

    //validate
    check(from_voter.liquid >= quantity, "insufficient liquid amount");

    //subtract quantity from liquid
    from_voters.modify(from_voter, same_payer, [&](auto& col) {
        col.liquid -= quantity;
    });
}

void decide::add_stake(name voter, asset quantity) {
    //open voters table, get voter
    voters_table to_voters(get_self(), voter.value);
    auto& to_voter = to_voters.get(quantity.symbol.code().raw(), "add_stake: voter not found");

    //add quantity to stake
    to_voters.modify(to_voter, same_payer, [&](auto& col) {
        col.staked += quantity;
        col.staked_time = time_point_sec(current_time_point());
    });
}

void decide::sub_stake(name voter, asset quantity) {
    //open voters table, get voter
    voters_table from_voters(get_self(), voter.value);
    auto& from_voter = from_voters.get(quantity.symbol.code().raw(), "sub_stake: voter not found");

    //validate
    check(from_voter.staked >= quantity, "insufficient staked amount");

    //subtract quantity from stake
    from_voters.modify(from_voter, same_payer, [&](auto& col) {
        col.staked -= quantity;
        col.staked_time = time_point_sec(current_time_point());
    });
}

bool decide::valid_category(name category) {
    switch (category.value) {
        case (name("proposal").value):
            return true;
        case (name("referendum").value):
            return true;
        case (name("election").value):
            return true;
        case (name("poll").value):
            return true;
        case (name("leaderboard").value):
            return true;
        default:
            return false;
    }
}

bool decide::valid_voting_method(name voting_method) {
    switch (voting_method.value) {
        case (name("1acct1vote").value):
            return true;
        case (name("1tokennvote").value):
            return true;
        case (name("1token1vote").value):
            return true;
        case (name("1tsquare1v").value):
            return true;
        case (name("quadratic").value):
            return true;
        case (name("ranked").value):
            check(false, "ranked voting method feature under development");
            return true;
        default:
            return false;
    }
}

bool decide::valid_access_method(name access_method) {
    switch (access_method.value) {
        case (name("public").value):
            return true;
        case (name("private").value):
            return true;
        case (name("invite").value):
            return true;
        case (name("membership").value):
            check(false, "membership access feature under development");
            return true;
        default:
            return false;
    }
}

void decide::require_fee(name account_name, asset fee) {
    //open accounts table, get WAX balance
    accounts_table wax_accounts(get_self(), account_name.value);
    auto& wax_acct = wax_accounts.get(WAX_SYM.code().raw(), "WAX balance not found");

    //validate
    check(wax_acct.balance >= fee, "insufficient funds to cover fee");

    config_singleton configs(get_self(), get_self().value);
    auto config = configs.get();

    config.total_deposits -= fee;

    configs.set(config, get_self());

    //charge fee
    wax_accounts.modify(wax_acct, same_payer, [&](auto& col) {
        col.balance -= fee;
    });
}

void decide::sync_external_account(name voter, symbol internal_symbol, symbol external_symbol) {
    
    //initialize
    asset wax_stake;

    //check external symbol is WAX, if not then return
    if (external_symbol == WAX_SYM) {
        wax_stake = get_staked(voter);
        check(internal_symbol == VOTE_SYM, "internal symbol must be VOTE for external WAX");
    } else {
        check(false, "syncing other external accounts is disabled");
        return;
    }

    //open voters table, search for voter
    voters_table voters(get_self(), voter.value);
    auto vtr_itr = voters.find(internal_symbol.code().raw());

    //if voter exists
    if (vtr_itr != voters.end()) {

        //open treasuries table, search for treasury
        treasuries_table treasuries(get_self(), get_self().value);
        auto trs_itr = treasuries.find(internal_symbol.code().raw());

        //check if treasury exists (should always be true)
        if (trs_itr == treasuries.end())
            return;
            
        //calc delta
        asset delta = asset(wax_stake.amount - vtr_itr->staked.amount, internal_symbol);

        //apply delta to supply
        treasuries.modify(*trs_itr, same_payer, [&](auto& col) {
            col.supply += asset(delta.amount, internal_symbol);
        });

        //mirror wax_stake to internal_symbol stake
        voters.modify(*vtr_itr, same_payer, [&](auto& col) {
            col.staked = asset(wax_stake.amount, internal_symbol);
        });

    }
}
