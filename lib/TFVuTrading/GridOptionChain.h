/************************************************************************
 * Copyright(c) 2014, One Unified. All rights reserved.                 *
 * email: info@oneunified.net                                           *
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
// Started 2014/09/10

#pragma once

//#include <functional>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <wx/grid.h>

#include <OUCommon/FastDelegate.h>

#include <TFOptions/Option.h>

namespace ou { // One Unified
namespace tf { // TradeFrame

#define GRID_OPTIONCHAIN_STYLE wxWANTS_CHARS
#define GRID_OPTIONCHAIN_TITLE _("Grid Option Chain")
#define GRID_OPTIONCHAIN_IDNAME ID_GRID_OPTIONCHAIN
#define GRID_OPTIONCHAIN_SIZE wxSize(-1, -1)
#define GRID_OPTIONCHAIN_POSITION wxDefaultPosition

class GridOptionChain_impl;  // Forward Declaration

class GridOptionChain: public wxGrid {
  friend GridOptionChain_impl;
public:

  GridOptionChain(void);
  GridOptionChain( 
    wxWindow* parent, wxWindowID id = GRID_OPTIONCHAIN_IDNAME, 
    const wxPoint& pos = GRID_OPTIONCHAIN_POSITION, 
    const wxSize& size = GRID_OPTIONCHAIN_SIZE, 
    long style = GRID_OPTIONCHAIN_STYLE,
    const wxString& = GRID_OPTIONCHAIN_TITLE );
  virtual ~GridOptionChain(void);

  bool Create( wxWindow* parent, 
    wxWindowID id = GRID_OPTIONCHAIN_IDNAME, 
    const wxPoint& pos = GRID_OPTIONCHAIN_POSITION, 
    const wxSize& size = GRID_OPTIONCHAIN_SIZE, 
    long style = GRID_OPTIONCHAIN_STYLE,
    const wxString& = GRID_OPTIONCHAIN_TITLE );

  void Add( double strike, ou::tf::OptionSide::enumOptionSide side, const std::string& sSymbol );

  void SetSelected( double strike, bool bSelected );
  
  void TimerActivate();
  void TimerDeactivate();
  
//  struct DatumUpdateFunctions {
//    std::function<void( const ou::tf::Greek& )> fCallGreek;
//    std::function<void( const ou::tf::Quote& )> fCallQuote;
//    std::function<void( const ou::tf::Trade& )> fCallTrade;  
//    std::function<void( const ou::tf::Greek& )> fPutGreek;
//    std::function<void( const ou::tf::Quote& )> fPutQuote;
//    std::function<void( const ou::tf::Trade& )> fPutTrade;  
//  };
  
  struct OptionUpdateFunctions {
    std::string sSymbolName;
    fastdelegate::FastDelegate<void(const ou::tf::Quote&)> fQuote;
    fastdelegate::FastDelegate<void(const ou::tf::Trade&)> fTrade;
    fastdelegate::FastDelegate<void(const ou::tf::Greek&)> fGreek;
  };
  
  typedef std::function<void(double, const OptionUpdateFunctions&, const OptionUpdateFunctions& )> fOnRowClicked_t;
  fOnRowClicked_t m_fOnRowClicked; // called when a row is clicked (on/off)
  
protected:

  void Init();
  void CreateControls();

private:
  enum { 
    ID_Null=wxID_HIGHEST, ID_GRID_OPTIONCHAIN
  };

  std::unique_ptr<GridOptionChain_impl> m_pimpl;
  
  template<class Archive>
  void serialize(Archive & ar, const unsigned int file_version);

  void HandleSize( wxSizeEvent& event );
  void OnDestroy( wxWindowDestroyEvent& event );
  
  wxBitmap GetBitmapResource( const wxString& name );
  wxIcon GetIconResource( const wxString& name );
  static bool ShowToolTips() { return true; };

};

} // namespace tf
} // namespace ou
