/************************************************************************
 * Copyright(c) 2011, One Unified. All rights reserved.                 *
 *                                                                      *
 * This file is provided as is WITHOUT ANY WARRANTY                     *
 *  without even the implied warranty of                                *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                *
 *                                                                      *
 * This software may not be used nor distributed without proper license *
 * agreement.                                                           *
 *                                                                      *
 * See the file LICENSE.txt for redistribution information.             *
 ************************************************************************/

#include <cassert>

#include <boost/bind.hpp>

#include "ScanHistory.h"
#include "App.h"

// following are specific for gold futures
InstrumentState::InstrumentState( void ):
  tdMarketOpen( time_duration( 19, 0, 0 ) ), // time relative to day  // pick up from symbol
  tdMarketOpenIdle( time_duration( 0, 0, 30 ) ),  // time relative to tdMarketOpen to allow initial collection of data
  tdCancelOrders( time_duration( 17, 50, 0 ) ),// time relative to day
  tdClosePositions( time_duration( 17, 51, 0 ) ),// time relative to day
  tdAfterMarket( time_duration( 18, 15, 0 ) ), // time relative to day
  tdMarketClosed( time_duration( 18, 15, 0 ) ), // time relative to day
  stochFast( &quotes, 60 ), stochMed( &quotes, 300 ), stochSlow( &quotes, 1800 ), // 1, 5, 30 min
  statsFast( &quotes, 60 ), statsMed( &quotes, 180 ), statsSlow( &quotes, 600 ), // 1, 3, 5 min
  bDaySession( true )
  {
    bMarketHoursCrossMidnight = tdMarketOpen > tdMarketClosed;
  }

App::App(void) 
  : m_mgrInstrument( ou::tf::CInstrumentManager::Instance() ),
  m_ptws( new ou::tf::CIBTWS ), m_piqfeed( new ou::tf::CIQFeedProvider )
{
}

App::~App(void) {
  m_ptws.reset();
  m_piqfeed.reset();
}

void App::Run( void ) {

  ou::tf::CProviderManager::Instance().Register( "ib01", static_cast<ou::tf::CProviderManager::pProvider_t>( m_ptws ) );
  ou::tf::CProviderManager::Instance().Register( "iq01", static_cast<ou::tf::CProviderManager::pProvider_t>( m_piqfeed ) );

  m_ptws->OnConnected.Add( MakeDelegate( this, &App::Connected ) );
  m_piqfeed->OnConnected.Add( MakeDelegate( this, &App::Connected ) );

  m_ptws->OnDisconnected.Add( MakeDelegate( this, &App::DisConnected ) );
  m_piqfeed->OnDisconnected.Add( MakeDelegate( this, &App::DisConnected ) );

  m_ptws->Connect();
  m_piqfeed->Connect();

  // start up worker thread here
  m_pwork = new boost::asio::io_service::work(m_io);  // keep the asio service running 
  m_asioThread = boost::thread( boost::bind( &App::WorkerThread, this ) );

  m_md.initiate();  // start state chart for market data
  m_md.process_event( ou::tf::EvInitialize() );

  // handle console input while thread is working in background
  // http://www.cplusplus.com/doc/tutorial/basic_io/
  std::string s;
  do {
    std::cout << "command: ";
    std::cin >> s;
    if ( "s" == s ) {  // stats
      std::cout << "Q:" << m_md.data.quotes.Size() << ", T:" << m_md.data.trades.Size() << std::endl;
    }
    if ( "c" == s ) { // close position
      m_md.data.pPosition->ClosePosition();
//      m_md.post_event(
    }
  } while ( "x" != s );

  // clean up 

  StopWatch();

  m_ptws->Disconnect();
  m_piqfeed->Disconnect();

  // wait for worker thread to end
//  delete m_pwork;  // stop the asio service (let it run out of work, which at this point should be none)
  m_asioThread.join();  // wait for i/o thread to cleanup and terminate

  ou::tf::CProviderManager::Instance().Release( "ib01" );
  ou::tf::CProviderManager::Instance().Release( "iq01" );

}

void App::Connected( int i ) {

  if ( m_ptws->Connected() ) {
  }

  if ( m_ptws->Connected() && m_piqfeed->Connected() ) {

    SelectTradeableSymbols();

    ou::tf::CIBTWS::Contract contract;
    contract.currency = "USD";
    //contract.exchange = "SMART";  in this case is NYMEX
    contract.secType = "FUT";
    contract.symbol = "GC";
    contract.expiry = "201112";
    // IB responds only when symbol is found, bad symbols will not illicit a response
    m_ptws->RequestContractDetails( contract, MakeDelegate( this, &App::HandleIBContractDetails ), MakeDelegate( this, &App::HandleIBContractDetailsDone ) );
  }
}

void App::DisConnected( int i ) {
  if ( !m_ptws->Connected() && ! m_piqfeed->Connected() ) {
    delete m_pwork;  // stop the asio service (let it run out of work, which at this point should be none)
  }
}

void App::HandleQuote( const ou::tf::CQuote& quote ) {
  InstrumentState& is( m_md.data );
  if ( is.bMarketHoursCrossMidnight ) {
    is.bDaySession = quote.DateTime().time_of_day() <= is.tdMarketClosed;
  }
  assert( is.bDaySession || is.bMarketHoursCrossMidnight );
  is.quotes.Append( quote );
  is.stochFast.Update();
  is.stochMed.Update();
  is.stochSlow.Update();
  is.statsFast.Update();
  m_md.process_event( ou::tf::EvQuote( quote ) );
}

void App::HandleTrade( const ou::tf::CTrade& trade ) {
  InstrumentState& is( m_md.data );
  if ( is.bMarketHoursCrossMidnight ) {
    is.bDaySession = trade.DateTime().time_of_day() <= is.tdMarketClosed;
  }
  assert( is.bDaySession || is.bMarketHoursCrossMidnight );
  is.trades.Append( trade );
  m_md.process_event( ou::tf::EvTrade( trade ) );
}

void App::HandleOpen( const ou::tf::CTrade& trade ) {
}

// separate thread 
void App::WorkerThread( void ) {
  m_io.run();  // deal with the submitted work
}

void App::HandleIBContractDetails( const ou::tf::CIBTWS::ContractDetails& details, const pInstrument_t& pInstrument ) {
  m_pInstrument = pInstrument;
  m_pInstrument->SetAlternateName( m_piqfeed->ID(), "+GCZ11" );
  m_md.data.pPosition.reset( new ou::tf::CPosition( m_pInstrument, m_ptws, m_piqfeed ) );
  m_md.data.tdMarketOpen = m_pInstrument->GetTimeTrading().begin().time_of_day();
  m_md.data.tdMarketClosed = m_pInstrument->GetTimeTrading().end().time_of_day();
}

void App::HandleIBContractDetailsDone( void ) {
  this->Connect();
}

void App::StartStateMachine( void ) {
  m_io.post( boost::bind( &App::StartWatch, this ) );
}

void App::StartWatch( void ) {
  m_piqfeed->AddQuoteHandler( m_pInstrument, MakeDelegate( this, &App::HandleQuote ) );
  m_piqfeed->AddTradeHandler( m_pInstrument, MakeDelegate( this, &App::HandleTrade ) );
  m_piqfeed->AddOnOpenHandler( m_pInstrument, MakeDelegate( this, &App::HandleOpen ) );
}

void App::StopWatch( void ) {
  m_piqfeed->RemoveQuoteHandler( m_pInstrument, MakeDelegate( this, &App::HandleQuote ) );
  m_piqfeed->RemoveTradeHandler( m_pInstrument, MakeDelegate( this, &App::HandleTrade ) );
  m_piqfeed->RemoveOnOpenHandler( m_pInstrument, MakeDelegate( this, &App::HandleOpen ) );
}

sc::result App::StatePreMarket::Handle( const EvQuote& quote ) {  // requires quotes to come before trades.
  InstrumentState& is( context<App::MachineMarketStates>().data );
  if ( is.bMarketHoursCrossMidnight && is.bDaySession ) { // transit
    is.dtPreTradingStop = quote.Quote().DateTime() + is.tdMarketOpenIdle;
    is.dblMidQuoteAtOpen = ( quote.Quote().Ask() + quote.Quote().Bid() ) / 2.0;
    return transit<App::StateMarketOpen>();  // late but transit anyway
  }
  else { // test
    if ( quote.Quote().DateTime().time_of_day() >= is.tdMarketOpen ) {
      is.dtPreTradingStop = quote.Quote().DateTime() + is.tdMarketOpenIdle;
      is.dblMidQuoteAtOpen = ( quote.Quote().Ask() + quote.Quote().Bid() ) / 2.0;
      return transit<App::StateMarketOpen>();
    }
  }
  return discard_event();
}

sc::result App::StatePreMarket::Handle( const EvTrade& trade ) {
  InstrumentState& is( context<App::MachineMarketStates>().data ); 
  if ( is.bMarketHoursCrossMidnight && is.bDaySession ) { // transit
    //return transit<App::StateMarketOpen>();  // late but transit anyway
    return discard_event();  // see if we get a pre-market trade for GC futures, possibly when starting mid market
  }
  else { // test
    if ( trade.Trade().DateTime().time_of_day() >= is.tdMarketOpen ) {
      return discard_event();  // see if we get a pre-market trade for GC futures, possibly when starting mid market
    }
  }
  return discard_event();
}

sc::result App::StateMarketOpen::Handle( const EvTrade& trade ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );
  is.dblOpeningTrade = trade.Trade().Trade();
  is.vZeroMarks.push_back( is.dblOpeningTrade );
  std::sort( is.vZeroMarks.begin(), is.vZeroMarks.end() );
  is.iterZeroMark = is.vZeroMarks.begin();
  while ( is.dblOpeningTrade != *is.iterZeroMark ) {
    is.iterZeroMark++;
    if ( is.vZeroMarks.end() == is.iterZeroMark ) 
      throw std::runtime_error( "can't find our zero mark" );
  }
  is.iterNextMark = is.iterZeroMark;
  //return transit<App::StatePreTrading>();
  return transit<App::StateTrading>();
}

sc::result App::StatePreTrading::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );

  if ( quote.Quote().DateTime() >= is.dtPreTradingStop ) {
    return transit<App::StateTrading>();
  }

  return discard_event();
}

sc::result App::StateCancelOrders::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );
  return transit<App::StateCancelOrdersIdle>();
}

sc::result App::StateCancelOrdersIdle::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );

  if ( is.bDaySession ) { // transit
    if ( quote.Quote().DateTime().time_of_day() >= is.tdClosePositions ) {
      return transit<App::StateClosePositions>();
    }
  }
  return discard_event();
}

sc::result App::StateClosePositions::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );
  return transit<App::StateClosePositionsIdle>();
}

sc::result App::StateClosePositionsIdle::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );

  if ( is.bDaySession ) { // transit
    if ( quote.Quote().DateTime().time_of_day() >= is.tdAfterMarket ) {
      return transit<App::StateAfterMarket>();
    }
  }
  return discard_event();
}

sc::result App::StateAfterMarket::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );

  if ( is.bDaySession ) { // transit
    if ( quote.Quote().DateTime().time_of_day() >= is.tdMarketClosed ) {
      return transit<App::StateMarketClosed>();
    }
  }
  return discard_event();
}

sc::result App::StateMarketClosed::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );
  return discard_event();
}

sc::result App::StateZeroPosition::Handle( const EvQuote& quote ) {

  InstrumentState& is( context<App::MachineMarketStates>().data );
  if ( is.bDaySession ) { // transit
    if ( quote.Quote().DateTime().time_of_day() >= is.tdCancelOrders ) {
      return transit<App::StateCancelOrders>();
    }
  }

  //double mid = ( quote.Quote().Ask() + quote.Quote().Bid() ) / 2.0;
  //if ( ( 0 < is.statsFast.Slope() ) && ( mid > is.dblOpeningTrade ) ) {
  if ( quote.Quote().Bid() > *is.iterZeroMark ) {
    if ( is.vZeroMarks.end() != is.iterZeroMark ) is.iterNextMark++;
    std::cout << "Zero Position going long" << std::endl;
    // go long
    is.pPosition->PlaceOrder( ou::tf::OrderType::Market, ou::tf::OrderSide::Buy, 1 );
    return transit<App::StateLong>();
  }
  else {
    //if ( ( 0 > is.statsFast.Slope() ) && ( mid < is.dblOpeningTrade ) ) {
    if ( quote.Quote().Ask() < *is.iterZeroMark ) {
      if ( is.vZeroMarks.begin() != is.iterZeroMark ) is.iterNextMark--;
      std::cout << "Zero Position going short" << std::endl;
      // go short
      is.pPosition->PlaceOrder( ou::tf::OrderType::Market, ou::tf::OrderSide::Sell, 1 );
      return transit<App::StateShort>();
    }
  }

  return discard_event();
}

sc::result App::StateLong::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );
  if ( is.bDaySession ) { // transit
    if ( quote.Quote().DateTime().time_of_day() >= is.tdCancelOrders ) {
      return transit<App::StateCancelOrders>();
    }
  }

  if ( is.pPosition->OrdersPending() ) {
    return discard_event();
  }

  //double mid = ( quote.Quote().Ask() + quote.Quote().Bid() ) / 2.0;

  //if ( ( 0 > is.statsFast.Slope() ) && ( mid < is.dblOpeningTrade ) ) {
  if ( quote.Quote().Ask() < *is.iterZeroMark ) {
    is.iterNextMark = is.iterZeroMark;
    if ( is.vZeroMarks.begin() != is.iterZeroMark ) is.iterNextMark--;
    std::cout << "long going short" << std::endl;
    // go short
    is.pPosition->PlaceOrder( ou::tf::OrderType::Market, ou::tf::OrderSide::Sell, 1 );
    is.pPosition->PlaceOrder( ou::tf::OrderType::Market, ou::tf::OrderSide::Sell, 1 );
    return transit<App::StateShort>();
  }
  else {
    if ( quote.Quote().Ask() >= *is.iterNextMark ) {
      if ( is.iterZeroMark != is.iterNextMark ) {
        is.iterZeroMark = is.iterNextMark;
        std::cout << "long crossing next zero: " << *is.iterZeroMark << std::endl;
        if ( is.vZeroMarks.end() != is.iterZeroMark ) is.iterNextMark++;
      }
    }
  }

  return discard_event();
}

sc::result App::StateShort::Handle( const EvQuote& quote ) {
  InstrumentState& is( context<App::MachineMarketStates>().data );
  if ( is.bDaySession ) { // transit
    if ( quote.Quote().DateTime().time_of_day() >= is.tdCancelOrders ) {
      return transit<App::StateCancelOrders>();
    }
  }

  if ( is.pPosition->OrdersPending() ) {
    return discard_event();
  }

  //double mid = ( quote.Quote().Ask() + quote.Quote().Bid() ) / 2.0;

  //if ( ( 0 < is.statsFast.Slope() ) && ( mid > is.dblOpeningTrade ) ) {
  if ( quote.Quote().Bid() > *is.iterZeroMark ) {
    is.iterNextMark = is.iterZeroMark;
    if ( is.vZeroMarks.end() != is.iterZeroMark ) is.iterNextMark++;
    std::cout << "short going long" << std::endl;
    // go long
    is.pPosition->PlaceOrder( ou::tf::OrderType::Market, ou::tf::OrderSide::Buy, 1 );
    is.pPosition->PlaceOrder( ou::tf::OrderType::Market, ou::tf::OrderSide::Buy, 1 );
    return transit<App::StateLong>();
  }
  else {
    if ( quote.Quote().Bid() <= *is.iterNextMark ) {
      if ( is.iterZeroMark != is.iterNextMark ) {
        is.iterZeroMark = is.iterNextMark;
        std::cout << "short crossing next zero: " << *is.iterZeroMark << std::endl;
        if ( is.vZeroMarks.begin() != is.iterZeroMark ) is.iterNextMark --;
      }
    }
  }

  return discard_event();
}

void App::OnHistoryConnected( void ) {
  InstrumentState& is( m_md.data );
  is.dblOpen = is.dblHigh = is.dblLow = is.dblClose = 0.0;
  ptime dtStart = m_pInstrument->GetTimeTrading().begin();
  ptime dtEnd = m_pInstrument->GetTimeTrading().end();
  if ( 0 == dtStart.date().day_of_week() ) {
    RetrieveDatedRangeOfDataPoints( 
      m_pInstrument->GetInstrumentName( m_piqfeed->ID() ), dtStart - date_duration( 3 ), dtEnd - date_duration( 3 ) );
  }
  else {
    RetrieveDatedRangeOfDataPoints( 
      m_pInstrument->GetInstrumentName( m_piqfeed->ID() ), dtStart - date_duration( 1 ), dtEnd - date_duration( 1 ) );
  }
}

void App::OnHistoryDisconnected( void ) {
}

void App::OnHistoryTickDataPoint( structTickDataPoint* pDP ) {
  InstrumentState& is( m_md.data );
  if ( 0 == is.dblOpen ) {
    is.dblOpen = is.dblHigh = is.dblLow = is.dblClose = pDP->Last;
  }
  else {
    if ( pDP->Last > is.dblHigh ) is.dblHigh = pDP->Last;
    if ( pDP->Last < is.dblLow ) is.dblLow = pDP->Last;
    is.dblClose = pDP->Last;
  }
  is.history.Append( ou::tf::CTrade( pDP->DateTime, pDP->Last, pDP->LastSize ) );

}

void App::OnHistoryRequestDone( void ) {
  InstrumentState& is( m_md.data );
  this->Disconnect();
  is.pivots.CalcPivots( "eod", is.dblHigh, is.dblLow, is.dblClose );
  is.vZeroMarks.push_back( is.pivots.GetPivotValue( ou::tf::CPivotSet::R3 ) );
  is.vZeroMarks.push_back( is.pivots.GetPivotValue( ou::tf::CPivotSet::R2 ) );
  is.vZeroMarks.push_back( is.pivots.GetPivotValue( ou::tf::CPivotSet::R1 ) );
  is.vZeroMarks.push_back( is.pivots.GetPivotValue( ou::tf::CPivotSet::PV ) );
  is.vZeroMarks.push_back( is.pivots.GetPivotValue( ou::tf::CPivotSet::S1 ) );
  is.vZeroMarks.push_back( is.pivots.GetPivotValue( ou::tf::CPivotSet::S2 ) );
  is.vZeroMarks.push_back( is.pivots.GetPivotValue( ou::tf::CPivotSet::S3 ) );
  std::cout << "History complete" << std::endl;
  StartStateMachine();
}

void App::SelectTradeableSymbols( void ) {
  ScanHistory sh;
  sh.Run();
}