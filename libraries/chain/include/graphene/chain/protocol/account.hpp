/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/buyback.hpp>
#include <graphene/chain/protocol/ext.hpp>
#include <graphene/chain/protocol/special_authority.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/chain/protocol/vote.hpp>

namespace graphene { namespace chain {

   bool is_valid_name( const string& s );
   bool is_cheap_name( const string& n );

   /// These are the fields which can be updated by the active authority.
   struct account_options
   {
      /// The memo key is the key this account will typically use to encrypt/sign transaction memos and other non-
      /// validated account activities. This field is here to prevent confusion if the active authority has zero or
      /// multiple keys in it.
      public_key_type  memo_key;
      /// If this field is set to an account ID other than GRAPHENE_PROXY_TO_SELF_ACCOUNT,
      /// then this account's votes will be ignored; its stake
      /// will be counted as voting for the referenced account's selected votes instead.
      account_id_type voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;

      /// The number of active witnesses this account votes the blockchain should appoint
      /// Must not exceed the actual number of witnesses voted for in @ref votes
      uint16_t num_witness = 0;
      /// The number of active committee members this account votes the blockchain should appoint
      /// Must not exceed the actual number of committee members voted for in @ref votes
      uint16_t num_committee = 0;
      /// This is the list of vote IDs this account votes for. The weight of these votes is determined by this
      /// account's balance of core asset.
      flat_set<vote_id_type> votes;
      extensions_type        extensions;

      void validate()const;
   };

   /**
    *  @ingroup operations
    *  @extensions brief create a regular (wallet) account.
    */
   struct account_create_operation : public base_operation
   {
      struct ext
      {
         optional< void_t >            null_ext;
         optional< special_authority > owner_special_authority;
         optional< special_authority > active_special_authority;
         optional< buyback_account_options > buyback_options;
      };

      struct fee_parameters_type { };

      asset           fee;

      /// The account kind: wallet, vault, special...
      uint8_t kind;

      /// This MUST BE the current registrar chain authority.
      account_id_type registrar;

      /// This account receives a portion of the fee split between registrar and referrer. Must be a member.
      account_id_type referrer;
      /// Of the fee split between registrar and referrer, this percentage goes to the referrer. The rest goes to the
      /// registrar.
      uint16_t        referrer_percent = 0;

      string          name;
      authority       owner;
      authority       active;

      account_options options;
      extension< ext > extensions;

      account_id_type fee_payer()const { return registrar; }
      void            validate()const;
      share_type calculate_fee(const fee_parameters_type&) const { return 0; }

      void get_required_active_authorities( flat_set<account_id_type>& a )const
      {
         // registrar should be required anyway as it is the fee_payer(), but we insert it here just to be sure
         a.insert( registrar );
         if( extensions.value.buyback_options.valid() )
            a.insert( extensions.value.buyback_options->asset_to_buy_issuer );
      }
   };

   struct ext_account_update_operation
   {
      optional< void_t >            null_ext;
      optional< special_authority > owner_special_authority;
      optional< special_authority > active_special_authority;
   };

   /**
    * @ingroup operations
    * @brief Update an existing account
    *
    * This operation is used to update an existing account. It can be used to update the authorities, or adjust the options on the account.
    * See @ref account_object::options_type for the options which may be updated.
    */
   struct account_update_operation : public base_operation
   {


      struct fee_parameters_type
      {
         share_type fee             = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t   price_per_kbyte = GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset fee;
      /// The account to update
      account_id_type account;

      /// New owner authority. If set, this operation requires owner authority to execute.
      optional<authority> owner;
      /// New active authority. This can be updated by the current active authority.
      optional<authority> active;

      /// New account options
      optional<account_options> new_options;
      extension<ext_account_update_operation> extensions;

      account_id_type fee_payer()const { return account; }
      void       validate()const;
      share_type calculate_fee( const fee_parameters_type& k )const;

      bool is_owner_update()const
      { return owner || extensions.value.owner_special_authority.valid(); }

      void get_required_owner_authorities( flat_set<account_id_type>& a )const
      { if( is_owner_update() ) a.insert( account ); }

      void get_required_active_authorities( flat_set<account_id_type>& a )const
      { if( !is_owner_update() ) a.insert( account ); }
   };

   /**
    * @brief This operation is used to whitelist and blacklist accounts, primarily for transacting in whitelisted assets
    * @ingroup operations
    *
    * Accounts can freely specify opinions about other accounts, in the form of either whitelisting or blacklisting
    * them. This information is used in chain validation only to determine whether an account is authorized to transact
    * in an asset type which enforces a whitelist, but third parties can use this information for other uses as well,
    * as long as it does not conflict with the use of whitelisted assets.
    *
    * An asset which enforces a whitelist specifies a list of accounts to maintain its whitelist, and a list of
    * accounts to maintain its blacklist. In order for a given account A to hold and transact in a whitelisted asset S,
    * A must be whitelisted by at least one of S's whitelist_authorities and blacklisted by none of S's
    * blacklist_authorities. If A receives a balance of S, and is later removed from the whitelist(s) which allowed it
    * to hold S, or added to any blacklist S specifies as authoritative, A's balance of S will be frozen until A's
    * authorization is reinstated.
    *
    * This operation requires authorizing_account's signature, but not account_to_list's. The fee is paid by
    * authorizing_account.
    */
   struct account_whitelist_operation : public base_operation
   {
      struct fee_parameters_type { share_type fee = 300000; };
      enum account_listing {
         no_listing = 0x0, ///< No opinion is specified about this account
         white_listed = 0x1, ///< This account is whitelisted, but not blacklisted
         black_listed = 0x2, ///< This account is blacklisted, but not whitelisted
         white_and_black_listed = white_listed | black_listed ///< This account is both whitelisted and blacklisted
      };

      /// Paid by authorizing_account
      asset           fee;
      /// The account which is specifying an opinion of another account
      account_id_type authorizing_account;
      /// The account being opined about
      account_id_type account_to_list;
      /// The new white and blacklist status of account_to_list, as determined by authorizing_account
      /// This is a bitfield using values defined in the account_listing enum
      uint8_t new_listing = no_listing;
      extensions_type extensions;

      account_id_type fee_payer()const { return authorizing_account; }
      void validate()const { FC_ASSERT( fee.amount >= 0 ); FC_ASSERT(new_listing < 0x4); }
   };

   /**
    * @brief Manage an account's membership status
    * @ingroup operations
    *
    * This operation is used to upgrade an account to a member, or renew its subscription. If an account which is an
    * unexpired annual subscription member publishes this operation with @ref upgrade_to_lifetime_member set to false,
    * the account's membership expiration date will be pushed backward one year. If a basic account publishes it with
    * @ref upgrade_to_lifetime_member set to false, the account will be upgraded to a subscription member with an
    * expiration date one year after the processing time of this operation.
    *
    * Any account may use this operation to become a lifetime member by setting @ref upgrade_to_lifetime_member to
    * true. Once an account has become a lifetime member, it may not use this operation anymore.
    */
   struct account_upgrade_operation : public base_operation
   {
      struct fee_parameters_type {
         uint64_t membership_annual_fee   =  2000 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint64_t membership_lifetime_fee = 10000 * GRAPHENE_BLOCKCHAIN_PRECISION; ///< the cost to upgrade to a lifetime member
      };

      asset             fee;
      /// The account to upgrade; must not already be a lifetime member
      account_id_type   account_to_upgrade;
      /// If true, the account will be upgraded to a lifetime member; otherwise, it will add a year to the subscription
      bool              upgrade_to_lifetime_member = false;
      extensions_type   extensions;

      account_id_type fee_payer()const { return account_to_upgrade; }
      void       validate()const;
      share_type calculate_fee( const fee_parameters_type& k )const;
   };

   /**
    * @brief transfers the account to another account while clearing the white list
    * @ingroup operations
    *
    * In theory an account can be transferred by simply updating the authorities, but that kind
    * of transfer lacks semantic meaning and is more often done to rotate keys without transferring
    * ownership.   This operation is used to indicate the legal transfer of title to this account and
    * a break in the operation history.
    *
    * The account_id's owner/active/voting/memo authority should be set to new_owner
    *
    * This operation will clear the account's whitelist statuses, but not the blacklist statuses.
    */
   struct account_transfer_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = 500 * GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset           fee;
      account_id_type account_id;
      account_id_type new_owner;
      extensions_type extensions;

      account_id_type fee_payer()const { return account_id; }
      void        validate()const;
   };

   /**
    * @brief tethers a vault and wallet account together.
    * @ingroup operations
    */
   struct tether_accounts_operation : public base_operation
   {
      struct fee_parameters_type {};

      asset fee;
      account_id_type wallet_account;
      account_id_type vault_account;

      extensions_type extensions;

      account_id_type fee_payer()const { return wallet_account; }
      void validate()const;
      share_type calculate_fee(const fee_parameters_type&)const { return 0; }
      void get_required_active_authorities( flat_set<account_id_type>& a )const
      {
        a.insert( wallet_account ); a.insert( vault_account );
      }
   };

   struct change_public_keys_operation : public base_operation
   {
     struct fee_parameters_type {};
     asset fee;

     account_id_type account;
     // New active authority. This can be updated by the current active authority.
     optional<authority> active;
     // New owner authority. If set, this operation requires owner authority to execute.
     optional<authority> owner;

     extensions_type extensions;

     change_public_keys_operation() = default;
     explicit change_public_keys_operation(account_id_type account, optional<authority> active,
                                           optional<authority> owner)
         : account(account)
         , active(active)
         , owner(owner)
     {
     }

     bool is_owner_update() const { return owner.valid(); }

     void get_required_owner_authorities(flat_set<account_id_type>& a)const
     { if(is_owner_update()) a.insert(account); }

     void get_required_active_authorities(flat_set<account_id_type>& a)const
     { if(!is_owner_update()) a.insert(account); }

     account_id_type fee_payer() const { return account; }
     void validate() const;
     share_type calculate_fee(const fee_parameters_type&) const { return 0; }
   };

    struct set_roll_back_enabled_operation : public base_operation
    {
      struct fee_parameters_type {};
      asset fee;

      account_id_type account;
      bool roll_back_enabled;

      extensions_type extensions;

      set_roll_back_enabled_operation() = default;
      explicit set_roll_back_enabled_operation(account_id_type account, bool roll_back_enabled) : account(account), roll_back_enabled(roll_back_enabled) {}

      account_id_type fee_payer() const { return account; }
      void validate() const;
      share_type calculate_fee(const fee_parameters_type&) const { return 0; }
    };

    struct roll_back_public_keys_operation : public base_operation
    {
      struct fee_parameters_type {};
      asset fee;

      account_id_type authority;
      account_id_type account;

      extensions_type extensions;

      roll_back_public_keys_operation() = default;
      explicit roll_back_public_keys_operation(account_id_type authority, account_id_type account) : authority(authority), account(account) {}

      account_id_type fee_payer() const { return authority; }
      void validate() const;
      share_type calculate_fee(const fee_parameters_type&) const { return 0; }
    };

   struct upgrade_account_cycles_operation : public base_operation
   {
     struct fee_parameters_type {};
     asset fee;

     account_id_type account;
     string description;

     extensions_type extensions;

     upgrade_account_cycles_operation() = default;
     explicit upgrade_account_cycles_operation(account_id_type account)
       : account(account) {}

     account_id_type fee_payer() const { return account; }
     void validate() const { FC_ASSERT( false ); }
     share_type calculate_fee(const fee_parameters_type&) const { return 0; }
   };

   /**
   * @brief sets global value for starting amount of cycles on new accounts
   * @ingroup operations
   *
   * Changes the value of global property starting_cycle_asset_amount, that represents a number of cycles
   * that is given to each new wallet or custodian account.
   */
   struct set_starting_cycle_asset_amount_operation : public base_operation
   {
     struct fee_parameters_type {};
     asset fee;

     /// Operation issuer, must be root authority
     account_id_type issuer;

     /// A value to set the amount to
     uint32_t new_amount = DASCOIN_DEFAULT_STARTING_CYCLE_ASSET_AMOUNT;

     extensions_type extensions;

     set_starting_cycle_asset_amount_operation() = default;
     explicit set_starting_cycle_asset_amount_operation(account_id_type issuer, uint32_t new_amount)
       : issuer(issuer)
       , new_amount(new_amount) {}

     account_id_type fee_payer() const { return issuer; }
     void validate() const {};
     share_type calculate_fee(const fee_parameters_type&) const { return 0; }
   };

   /**
    *
    */
   struct set_chain_authority_operation : public base_operation
   {
     struct fee_parameters_type {};
     asset fee;

     /// Operation issuer, must be root authority
     account_id_type issuer;

     /// Account to assign authority role to
     account_id_type account;

     /// Kind of chain authority that will be assigned
     string kind;

     extensions_type extensions;

     set_chain_authority_operation() = default;
     explicit set_chain_authority_operation(account_id_type issuer, account_id_type account, string kind)
       : issuer(issuer)
       , account(account)
       , kind(kind) {}

     account_id_type fee_payer() const { return issuer; }
     void validate() const {};
     share_type calculate_fee(const fee_parameters_type&) const { return 0; }
   };

} } // graphene::chain

////////////////////////////////
/// REFLECTIONS:              //
////////////////////////////////

FC_REFLECT(graphene::chain::account_options, (memo_key)(voting_account)(num_witness)(num_committee)(votes)(extensions))
FC_REFLECT_TYPENAME( graphene::chain::account_whitelist_operation::account_listing)
FC_REFLECT_ENUM( graphene::chain::account_whitelist_operation::account_listing,
                (no_listing)(white_listed)(black_listed)(white_and_black_listed))

/// account_create_operation:

FC_REFLECT( graphene::chain::account_create_operation::fee_parameters_type,  )
FC_REFLECT( graphene::chain::account_create_operation::ext,
            (null_ext)
            (owner_special_authority)
            (active_special_authority)
            (buyback_options)
          )
FC_REFLECT( graphene::chain::account_create_operation,
            (fee)
            (kind)
            (registrar)
            (referrer)
            (referrer_percent)
            (name)
            (owner)
            (active)
            (options)
          )

// account_update_operation:

FC_REFLECT( graphene::chain::account_update_operation::fee_parameters_type,
            (fee)
            (price_per_kbyte)
          )
FC_REFLECT( graphene::chain::ext_account_update_operation,
            (null_ext)
            (owner_special_authority)
            (active_special_authority)
          )
FC_REFLECT( graphene::chain::account_update_operation,
            (fee)
            (account)
            (owner)
            (active)
            (new_options)
          )

FC_REFLECT( graphene::chain::account_upgrade_operation,
            (fee)(account_to_upgrade)(upgrade_to_lifetime_member)(extensions) )

FC_REFLECT( graphene::chain::account_whitelist_operation, (fee)(authorizing_account)(account_to_list)(new_listing)(extensions))

FC_REFLECT( graphene::chain::account_whitelist_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::account_upgrade_operation::fee_parameters_type, (membership_annual_fee)(membership_lifetime_fee) )
FC_REFLECT( graphene::chain::account_transfer_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::account_transfer_operation, (fee)(account_id)(new_owner)(extensions) )

// tether_accounts_operation:
FC_REFLECT( graphene::chain::tether_accounts_operation::fee_parameters_type,  )
FC_REFLECT( graphene::chain::tether_accounts_operation,
            (fee)
            (wallet_account)
            (vault_account)
            (extensions)
          )

// upgrade_account_cycles_operation:
FC_REFLECT( graphene::chain::upgrade_account_cycles_operation::fee_parameters_type,  )
FC_REFLECT( graphene::chain::upgrade_account_cycles_operation,
            (fee)
            (account)
            (description)
            (extensions)
          )

FC_REFLECT( graphene::chain::change_public_keys_operation::fee_parameters_type, )
FC_REFLECT( graphene::chain::change_public_keys_operation,
            (fee)
            (account)
            (active)
            (owner)
          )

FC_REFLECT( graphene::chain::set_roll_back_enabled_operation::fee_parameters_type, )
FC_REFLECT( graphene::chain::set_roll_back_enabled_operation,
            (fee)
            (account)
            (roll_back_enabled)
            (extensions)
          )

FC_REFLECT( graphene::chain::roll_back_public_keys_operation::fee_parameters_type, )
FC_REFLECT( graphene::chain::roll_back_public_keys_operation,
            (fee)
            (authority)
            (account)
            (extensions)
          )

FC_REFLECT( graphene::chain::set_starting_cycle_asset_amount_operation::fee_parameters_type, )
FC_REFLECT( graphene::chain::set_starting_cycle_asset_amount_operation,
            (fee)
            (issuer)
            (new_amount)
            (extensions)
          )

FC_REFLECT( graphene::chain::set_chain_authority_operation::fee_parameters_type, )
FC_REFLECT( graphene::chain::set_chain_authority_operation,
            (fee)
            (issuer)
            (account)
            (kind)
            (extensions)
          )
