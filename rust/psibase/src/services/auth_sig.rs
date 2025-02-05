// TODO: tables

#[crate::service(name = "auth-sig", dispatch = false, psibase_mod = "crate")]
#[allow(non_snake_case, unused_variables)]
mod service {
    use crate::{services::transact::ServiceMethod, AccountNumber, Action, Claim};

    #[action]
    fn checkAuthSys(
        flags: u32,
        requester: AccountNumber,
        action: Action,
        allowedActions: Vec<ServiceMethod>,
        claims: Vec<Claim>,
    ) {
        unimplemented!()
    }

    #[action]
    fn canAuthUserSys(user: AccountNumber) {
        unimplemented!()
    }

    #[action]
    fn setKey(key: Vec<u8>) {
        unimplemented!()
    }
}
