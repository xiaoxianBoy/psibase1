#pragma once

#include <psibase/Table.hpp>
#include <psibase/crypto.hpp>
#include <psibase/serveContent.hpp>
#include <services/system/Transact.hpp>

namespace SystemService
{
   namespace AuthK1Record
   {  // Using a namespace here disambiguates this class from the one in AuthSig so docs don't get confused
      /// A record containing the authorization claims needed for an account using this auth service.
      struct AuthRecord
      {
         /// The account whose transactions will be required to contain the specified public key.
         psibase::AccountNumber account;

         /// The public key included in the claims for each transaction sent by this account.
         psibase::PublicKey pubkey;

         auto byPubkey() const { return std::tuple{pubkey, account}; }
      };
      PSIO_REFLECT(AuthRecord, account, pubkey)
   }  // namespace AuthK1Record

   /// The `auth-k1` service is an auth service that can be used to authenticate actions for accounts
   ///
   /// Any account using this auth service must store in this service an ECDCSA public key that they own.
   /// This service will ensure that the specified public key is included in the transaction claims for any
   /// transaction sent by this account.
   ///
   /// This service only supports K1 keys (Secp256K1) keys.
   class AuthK1 : public psibase::Service<AuthK1>
   {
     public:
      static constexpr auto service = psibase::AccountNumber("auth-k1");
      using AuthTable               = psibase::Table<AuthK1Record::AuthRecord,
                                       &AuthK1Record::AuthRecord::account,
                                       &AuthK1Record::AuthRecord::byPubkey>;
      using Tables                  = psibase::ServiceTables<AuthTable>;

      /// This is an implementation of the standard auth service interface defined in [SystemService::AuthInterface]
      ///
      /// This action is automatically called by `transact` when an account using this auth service submits a
      /// transaction.
      ///
      /// This action verifies that the transaction contains a claim for the user's public key.
      void checkAuthSys(uint32_t                    flags,
                        psibase::AccountNumber      requester,
                        psibase::AccountNumber      sender,
                        ServiceMethod               action,
                        std::vector<ServiceMethod>  allowedActions,
                        std::vector<psibase::Claim> claims);

      /// This is an implementation of the standard auth service interface defined by [SystemService::AuthInterface]
      ///
      /// This action is automatically called by `accounts` when an account is configured to use this auth service.
      ///
      /// Verifies that a particular user is allowed to use a particular auth service.
      ///
      /// This action allows any user who has already set a public key using `AuthK1::setKey`.
      void canAuthUserSys(psibase::AccountNumber user);

      /// Set the sender's public key
      ///
      /// This is the public key that must be claimed by the transaction whenever a sender using this auth service
      /// submits a transaction. Only accepts K1 keys.
      void setKey(psibase::PublicKey key);

     private:
      Tables db{psibase::getReceiver()};
   };
   PSIO_REFLECT(AuthK1,  //
                method(checkAuthSys, flags, requester, sender, action, allowedActions, claims),
                method(canAuthUserSys, user),
                method(setKey, key)
                //
   )
}  // namespace SystemService
