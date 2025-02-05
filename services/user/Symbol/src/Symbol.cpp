#include <services/user/Symbol.hpp>

#include <services/system/Accounts.hpp>
#include <services/system/HttpServer.hpp>
#include <services/system/Transact.hpp>
#include <services/system/commonErrors.hpp>

#include <psibase/serveSimpleUI.hpp>
#include "services/user/Nft.hpp"
#include "services/user/Tokens.hpp"

using namespace UserService;
using namespace psibase;
using namespace UserService::Errors;
using namespace SystemService;
using std::nullopt;
using std::optional;
using std::string;
using Quantity_t = typename Quantity::Quantity_t;

namespace
{
   template <class Target, class Source>
   Target canCast(Source v)
   {
      auto r = static_cast<Target>(v);
      return (static_cast<Source>(r) == v);
   }

   constexpr uint32_t secondsInDay{24 * 60 * 60};

}  // namespace

namespace PricingDefaults
{
   uint8_t increasePct;
   uint8_t decreasePct;

}  // namespace PricingDefaults

Symbol::Symbol(psio::shared_view_ptr<psibase::Action> action)
{
   MethodNumber m{action->method()};
   if (m != MethodNumber{"init"})
   {
      auto initRecord = Tables().open<InitTable>().get(SingletonKey{});
      check(initRecord.has_value(), uninitialized);
   }
}

void Symbol::init()
{
   auto initTable = Tables().open<InitTable>();
   auto init      = (initTable.get(SingletonKey{}));
   check(not init.has_value(), alreadyInit);
   initTable.put(InitializedRecord{});

   // Configure manualDebit for self on Token and NFT
   to<Tokens>().setUserConf("manualDebit"_m, true);
   to<Nft>().setUserConf("manualDebit"_m, true);

   // Configure default symbol length records to establish initial prices
   auto nextSym = [](SymbolLengthRecord& s)
   {
      s.symbolLength++;
      s.targetCreatedPerDay += 8;
      s.floorPrice  = s.floorPrice * 2 / 3;
      s.activePrice = s.activePrice * 2 / 3;
      return s;
   };
   auto       symLengthTable = Tables().open<SymbolLengthTable>();
   Quantity_t initialPrice   = (Quantity_t)1000e8;
   auto       s              = SymbolLengthRecord{.symbolLength        = 3,
                                                  .targetCreatedPerDay = 24,
                                                  .floorPrice{(Quantity_t)100e8},
                                                  .activePrice{initialPrice}};
   symLengthTable.put(s);           // Length 3
   symLengthTable.put(nextSym(s));  // Length 4
   symLengthTable.put(nextSym(s));  // Length 5
   symLengthTable.put(nextSym(s));  // Length 6
   symLengthTable.put(nextSym(s));  // Length 7

   // Add initial configuration for Price Adjustment Record
   auto              priceAdjustmentSingleton = Tables().open<PriceAdjustmentSingleton>();
   constexpr uint8_t increasePct              = 5;
   constexpr uint8_t decreasePct              = increasePct;
   priceAdjustmentSingleton.put(PriceAdjustmentRecord{0, increasePct, decreasePct});

   // Create system token symbol
   to<Symbol>().create(sysTokenSymbol, initialPrice);

   // Offer system token symbol
   auto symbolOwnerNft = getSymbol(sysTokenSymbol);
   to<Nft>().credit(symbolOwnerNft.ownerNft, Tokens::service, "System token symbol ownership nft");
   to<Tokens>().mapSymbol(Tokens::sysToken, sysTokenSymbol);

   // Register serveSys handler
   to<SystemService::HttpServer>().registerServer(Symbol::service);
}

void Symbol::create(SID newSymbol, Quantity maxDebit)
{
   auto sender = getSender();
   auto newSym = getSymbol(newSymbol);

   auto symString = newSymbol.str();

   auto symType = getSymbolType(symString.length());
   auto cost    = symType.activePrice;

   check(newSym.ownerNft == 0, symbolAlreadyExists);
   check(cost <= maxDebit, insufficientBalance);

   // Debit the sender the cost of the new symbol
   auto debitMemo = "This transfer created the new symbol: " + symString;
   if (sender != getReceiver())  // Symbol itself doesn't need to pay
   {
      to<Tokens>().debit(Tokens::sysToken, sender, cost, debitMemo);
   }

   // Mint and offer ownership NFT
   newSym.ownerNft    = to<Nft>().mint();
   auto nftCreditMemo = "This NFT conveys ownership of symbol: " + symString;
   if (sender != getReceiver())
   {
      to<Nft>().credit(newSym.ownerNft, sender, nftCreditMemo);
   }

   // Update symbol type statistics
   symType.createCounter++;
   symType.lastPriceUpdateTime = to<Transact>().headBlockTime();

   newSym.eventHead = emit().history().symCreated(0, newSymbol, sender, cost);

   Tables().open<SymbolTable>().put(newSym);
   Tables().open<SymbolLengthTable>().put(symType);
}

void Symbol::listSymbol(SID symbol, Quantity price)
{
   auto seller       = getSender();
   auto symbolRecord = getSymbol(symbol);
   auto nft          = symbolRecord.ownerNft;
   auto nftService   = to<Nft>();

   check(price.value != 0, priceTooLow);
   check(nft != 0, symbolDNE);
   check(seller == nftService.getNft(nft).owner, missingRequiredAuth);
   check(nftService.getCredRecord(nft).debitor != Accounts::nullAccount, creditSymbolRequired);

   auto debitMemo = "Symbol " + symbol.str() + " is listed for sale.";

   nftService.debit(nft, debitMemo);
   symbolRecord.saleDetails.salePrice = price;
   symbolRecord.saleDetails.seller    = seller;

   symbolRecord.eventHead =
       emit().history().symListed(symbolRecord.eventHead, symbol, seller, price);

   Tables().open<SymbolTable>().put(symbolRecord);
}

void Symbol::buySymbol(SID symbol)
{
   auto buyer               = getSender();
   auto symbolRecord        = getSymbol(symbol);
   auto [salePrice, seller] = symbolRecord.saleDetails;
   auto tokenService        = to<Tokens>();

   check(symbolRecord.ownerNft != 0, symbolDNE);
   check(buyer != seller, buyerIsSeller);

   auto buyerMemo  = "Buying symbol " + symbol.str();
   auto sellerMemo = "Symbol " + symbol.str() + " sold";
   tokenService.debit(Tokens::sysToken, buyer, salePrice, buyerMemo);
   tokenService.credit(Tokens::sysToken, seller, salePrice, sellerMemo);
   to<Nft>().credit(symbolRecord.ownerNft, buyer, buyerMemo);

   symbolRecord.saleDetails.seller    = AccountNumber{0};
   symbolRecord.saleDetails.salePrice = 0;

   symbolRecord.eventHead =
       emit().history().symSold(symbolRecord.eventHead, symbol, buyer, seller, salePrice);

   Tables().open<SymbolTable>().put(symbolRecord);
}

void Symbol::unlistSymbol(SID symbol)
{
   auto seller       = getSender();
   auto symbolRecord = getSymbol(symbol);

   check(symbolRecord.ownerNft != 0, symbolDNE);
   check(seller == symbolRecord.saleDetails.seller, missingRequiredAuth);

   auto unlistMemo = "Unlisting symbol " + symbol.str();
   to<Nft>().credit(symbolRecord.ownerNft, seller, unlistMemo);

   symbolRecord.saleDetails = SaleDetails{};
   symbolRecord.eventHead   = emit().history().symUnlisted(symbolRecord.eventHead, symbol, seller);

   Tables().open<SymbolTable>().put(symbolRecord);
}

SymbolRecord Symbol::getSymbol(SID symbol)
{
   check(symbol.value != 0, invalidSymbol);

   auto symOpt = Tables().open<SymbolTable>().get(symbol);

   if (symOpt.has_value())
   {
      return *symOpt;
   }
   else
   {
      check(SymbolRecord::isValidKey(symbol), invalidSymbol);

      return SymbolRecord{
          .symbolId{symbol},
      };
   }
}

Quantity Symbol::getPrice(uint8_t numChars)
{
   return getSymbolType(numChars).activePrice;
}

SymbolLengthRecord Symbol::getSymbolType(uint8_t numChars)
{
   updatePrices();

   auto symbolType = Tables().open<SymbolLengthTable>().get(numChars);
   check(symbolType.has_value(), invalidSymbol);

   return *symbolType;
}

void Symbol::updatePrices()
{
   auto symLengthTable = Tables().open<SymbolLengthTable>();
   auto symLengthIndex = symLengthTable.getIndex<0>();

   auto priceAdjustmentIdx = Tables().open<PriceAdjustmentSingleton>().getIndex<0>();
   auto priceAdjustmentRec = *priceAdjustmentIdx.get(uint8_t{0});
   auto dec                = static_cast<uint64_t>((uint8_t)100 - priceAdjustmentRec.decreasePct);
   auto inc                = static_cast<uint64_t>((uint8_t)100 + priceAdjustmentRec.increasePct);

   auto lastBlockTime = to<Transact>().headBlockTime();
   for (auto symbolType : symLengthIndex)
   {
      bool priceChanged = false;
      if (lastBlockTime.seconds - symbolType.lastPriceUpdateTime.seconds > secondsInDay)
      {  // Decrease price if needed
         if (symbolType.createCounter < symbolType.targetCreatedPerDay)
         {
            auto newPrice = static_cast<Quantity_t>(symbolType.activePrice * dec / 100);
            newPrice      = std::max(newPrice, symbolType.floorPrice.value);
            if (newPrice != symbolType.activePrice)
               priceChanged = true;
            symbolType.activePrice = Quantity{newPrice};
         }

         // Price staying the same here is also an "update"
         symbolType.lastPriceUpdateTime = lastBlockTime;
         symbolType.createCounter       = 0;
      }

      if (symbolType.createCounter > symbolType.targetCreatedPerDay)
      {  // Increase price
         const auto newPrice    = static_cast<Quantity_t>(symbolType.activePrice * inc / 100);
         symbolType.activePrice = Quantity{newPrice};
         symbolType.lastPriceUpdateTime = lastBlockTime;
         symbolType.createCounter       = 0;
         priceChanged                   = true;
      }

      if (priceChanged)
      {
         auto blockNum        = to<Transact>().currentBlock().blockNum;
         symbolType.eventHead = emit().history().newCreatePrice(
             symbolType.eventHead, symbolType.symbolLength, blockNum, symbolType.activePrice);
      }

      symLengthTable.put(symbolType);
   }
}

bool Symbol::exists(SID symbol)
{
   return Tables().open<SymbolTable>().get(symbol).has_value();
}

auto symbolService = QueryableService<Symbol::Tables, Symbol::Events>{Symbol::service};
struct SymbolQuery
{
   auto events() const
   {  //
      return symbolService.allEvents();
   }
   auto symbolEvents(SID symbolId, optional<uint32_t> first, const optional<string>& after) const
   {
      return symbolService.eventIndex<Symbol::SymbolEvents>(symbolId, first, after);
   }
   auto lengthEvents(uint8_t length, optional<uint32_t> first, const optional<string>& after) const
   {
      return symbolService.eventIndex<Symbol::SymbolTypeEvents>(length, first, after);
   }
   auto symbolTypes() const
   {  //
      return symbolService.index<SymbolLengthTable, 0>();
   }
   auto symbols() const
   {  //
      return symbolService.index<SymbolTable, 0>();
   }
};
PSIO_REFLECT(SymbolQuery,
             method(events),
             method(symbolEvents, symbolId, first, after),
             method(lengthEvents, length, first, after),
             method(symbolTypes),
             method(symbols));

optional<HttpReply> Symbol::serveSys(HttpRequest request)
{
   if (auto result = serveSimpleUI<Symbol, true>(request))
      return result;
   if (auto result = serveGraphQL(request, SymbolQuery{}))
      return result;
   return nullopt;
}

PSIBASE_DISPATCH(UserService::Symbol)
