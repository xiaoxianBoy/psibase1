// TODO: tables

#[crate::service(name = "proxy-sys", dispatch = false, psibase_mod = "crate")]
#[allow(non_snake_case, unused_variables, dead_code)]
mod service {
    use crate::AccountNumber;

    #[action]
    fn registerServer(server: AccountNumber) {
        unimplemented!()
    }
}
