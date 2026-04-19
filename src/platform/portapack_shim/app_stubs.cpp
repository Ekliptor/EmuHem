// EmuHem app constructor stubs
// Provides minimal constructors (and destructors) for all app View classes
// referenced by ui_navigation.cpp, so we can compile/link it without pulling
// in every individual app .cpp file.
//
// Each stub initialises reference members (e.g. NavigationView& nav_) correctly
// and leaves the body empty.  The real app logic will replace these later.

// Pull in every header that ui_navigation.cpp includes -- this gives us all the
// class declarations whose constructors we need to stub.
#include "ui_navigation.hpp"

// App headers included by ui_navigation.cpp
#include "ui_about_simple.hpp"
#include "ui_adsb_rx.hpp"
#include "ui_aprs_rx.hpp"
#include "ui_aprs_tx.hpp"
#include "ui_btle_rx.hpp"
#include "ui_debug.hpp"
#include "ui_encoders.hpp"
#include "ui_fileman.hpp"
#include "ui_flash_utility.hpp"
#include "ui_freqman.hpp"
#include "ui_iq_trim.hpp"
#include "ui_looking_glass_app.hpp"
#include "ui_mictx.hpp"
#include "ui_playlist.hpp"
#include "ui_rds.hpp"
#include "ui_recon.hpp"
#include "ui_search.hpp"
#include "ui_settings.hpp"
#include "ui_sonde.hpp"
#include "ui_ss_viewer.hpp"
#include "ui_text_editor.hpp"
#include "ui_touchtunes.hpp"
#include "ui_weatherstation.hpp"
#include "ui_subghzd.hpp"
#include "ui_battinfo.hpp"

#include "ais_app.hpp"
#include "analog_audio_app.hpp"
#include "ble_rx_app.hpp"
#include "ble_tx_app.hpp"
#include "capture_app.hpp"
#include "pocsag_app.hpp"

#include "view_factory_base.hpp"
#include "app_settings.hpp"
#include "recent_entries.hpp"
#include "ui_tabview.hpp"
#include "ui_receiver.hpp"

namespace ui {

// ---- ViewFactoryBase (virtual dtor needed for vtable) --------------------

ViewFactoryBase::~ViewFactoryBase() {}

// ---- DfuMenu / DfuMenu2 (vtable anchors -- need paint()) ----------------

DfuMenu::DfuMenu(NavigationView& nav) : nav_(nav) {}
void DfuMenu::paint(Painter&) {}

DfuMenu2::DfuMenu2(NavigationView& nav) : nav_(nav) {}
void DfuMenu2::paint(Painter&) {}

// ---- app_settings::SettingsManager (used as member in many app views) ----

} // namespace ui

namespace app_settings {

SettingsManager::SettingsManager(std::string_view, Mode, Options) {}
SettingsManager::SettingsManager(std::string_view, Mode, SettingBindings) {}
SettingsManager::SettingsManager(std::string_view, Mode, Options, SettingBindings) {}
SettingsManager::~SettingsManager() {}

} // namespace app_settings

namespace ui {

// ==========================================================================
// Top-level app views referenced in NavigationView::appList
// ==========================================================================

// ---- ADSBRxView (no nav_ member) ----------------------------------------
ADSBRxView::ADSBRxView(NavigationView&) {}
ADSBRxView::~ADSBRxView() {}
void ADSBRxView::focus() {}

// ---- AISAppView (has nav_) ----------------------------------------------
AISAppView::AISAppView(NavigationView& nav) : nav_(nav) {}
AISAppView::~AISAppView() {}
void AISAppView::focus() {}
void AISAppView::set_parent_rect(const Rect) {}

// ---- APRSRXView (has nav_, member views: APRSRxView, APRSTableView) -----
APRSRXView::APRSRXView(NavigationView& nav) : nav_(nav) {}
APRSRXView::~APRSRXView() {}
void APRSRXView::focus() {}

// ---- APRSTXView (no nav_ member) ----------------------------------------
APRSTXView::APRSTXView(NavigationView&) {}
APRSTXView::~APRSTXView() {}
void APRSTXView::focus() {}

// ---- AnalogAudioView (has nav_) -----------------------------------------
AnalogAudioView::AnalogAudioView(NavigationView& nav) : nav_(nav) {}
AnalogAudioView::AnalogAudioView(NavigationView& nav, ReceiverModel::settings_t) : nav_(nav) {}
AnalogAudioView::~AnalogAudioView() {}
void AnalogAudioView::focus() {}
void AnalogAudioView::set_parent_rect(Rect) {}

// ---- BLERxView (has nav_) -----------------------------------------------
BLERxView::BLERxView(NavigationView& nav) : nav_(nav) {}
BLERxView::~BLERxView() {}
void BLERxView::focus() {}
void BLERxView::set_parent_rect(const Rect) {}

// ---- BLETxView (has nav_) -----------------------------------------------
BLETxView::BLETxView(NavigationView& nav) : nav_(nav) {}
BLETxView::BLETxView(NavigationView& nav, BLETxPacket) : nav_(nav) {}
BLETxView::~BLETxView() {}
void BLETxView::focus() {}
void BLETxView::set_parent_rect(const Rect) {}

// ---- BTLERxView (has nav_) ----------------------------------------------
BTLERxView::BTLERxView(NavigationView& nav) : nav_(nav) {}
BTLERxView::~BTLERxView() {}
void BTLERxView::focus() {}

// ---- CaptureAppView (has nav_) ------------------------------------------
CaptureAppView::CaptureAppView(NavigationView& nav) : nav_(nav) {}
CaptureAppView::CaptureAppView(NavigationView& nav, ReceiverModel::settings_t) : nav_(nav) {}
CaptureAppView::~CaptureAppView() {}
void CaptureAppView::focus() {}
void CaptureAppView::set_parent_rect(Rect) {}

// ---- POCSAGAppView (has nav_) -------------------------------------------
POCSAGAppView::POCSAGAppView(NavigationView& nav) : nav_(nav) {}
POCSAGAppView::~POCSAGAppView() {}
void POCSAGAppView::focus() {}

// ---- PlaylistView (has nav_) --------------------------------------------
PlaylistView::PlaylistView(NavigationView& nav) : nav_(nav) {}
PlaylistView::PlaylistView(NavigationView& nav, const std::filesystem::path&) : nav_(nav) {}
PlaylistView::~PlaylistView() {}
void PlaylistView::focus() {}
void PlaylistView::set_parent_rect(Rect) {}
void PlaylistView::on_hide() {}

// ---- RDSView (has nav_, members: RDSPSNView, RDSRadioTextView, etc.) ----
RDSView::RDSView(NavigationView& nav) : nav_(nav) {}
RDSView::~RDSView() {}
void RDSView::focus() {}

// ---- EncodersView (has nav_, members: EncodersConfigView, EncodersScanView)
EncodersView::EncodersView(NavigationView& nav) : nav_(nav) {}
EncodersView::~EncodersView() {}
void EncodersView::focus() {}

// ---- TouchTunesView (no nav_ member) ------------------------------------
TouchTunesView::TouchTunesView(NavigationView&) {}
TouchTunesView::~TouchTunesView() {}
void TouchTunesView::focus() {}

// ---- MicTXView (no nav_ member) -----------------------------------------
MicTXView::MicTXView(NavigationView&) {}
MicTXView::MicTXView(NavigationView&, ReceiverModel::settings_t) {}
MicTXView::~MicTXView() {}
void MicTXView::focus() {}

// ---- SondeView (has nav_) -----------------------------------------------
SondeView::SondeView(NavigationView& nav) : nav_(nav) {}
SondeView::~SondeView() {}
void SondeView::focus() {}

// ---- GlassView (has nav_) -----------------------------------------------
GlassView::GlassView(NavigationView& nav) : nav_(nav) {}
GlassView::~GlassView() {}
void GlassView::focus() {}
void GlassView::on_show() {}
void GlassView::on_hide() {}

// ---- ReconView (has nav_) -----------------------------------------------
ReconView::ReconView(NavigationView& nav) : nav_(nav) {}
ReconView::~ReconView() {}
void ReconView::focus() {}

// ---- SearchView (has nav_) ----------------------------------------------
SearchView::SearchView(NavigationView& nav) : nav_(nav) {}
SearchView::~SearchView() {}
void SearchView::focus() {}
void SearchView::on_show() {}
void SearchView::on_hide() {}

// ---- WeatherView (has nav_) ---------------------------------------------
WeatherView::WeatherView(NavigationView& nav) : nav_(nav) {}
WeatherView::~WeatherView() {}
void WeatherView::focus() {}

// ---- SubGhzDView (has nav_) ---------------------------------------------
SubGhzDView::SubGhzDView(NavigationView& nav) : nav_(nav) {}
SubGhzDView::~SubGhzDView() {}
void SubGhzDView::focus() {}

// ---- FileManagerView (base FileManBaseView has nav_) --------------------
FileManagerView::FileManagerView(NavigationView& nav)
    : FileManBaseView(nav, "") {}

// ---- FrequencyManagerView (base FreqManBaseView has nav_) ---------------
FrequencyManagerView::FrequencyManagerView(NavigationView& nav)
    : FreqManBaseView(nav) {}

// ---- IQTrimView (has nav_) ----------------------------------------------
IQTrimView::IQTrimView(NavigationView& nav) : nav_(nav) {}
IQTrimView::IQTrimView(NavigationView& nav, const std::filesystem::path&) : nav_(nav) {}
void IQTrimView::focus() {}
void IQTrimView::paint(Painter&) {}

// ---- TextEditorView (has nav_) ------------------------------------------
TextEditorView::TextEditorView(NavigationView& nav) : nav_(nav) {}
TextEditorView::TextEditorView(NavigationView& nav, const std::filesystem::path&) : nav_(nav) {}
TextEditorView::~TextEditorView() {}
void TextEditorView::on_show() {}

// ---- FlashUtilityView (has nav_) ----------------------------------------
FlashUtilityView::FlashUtilityView(NavigationView& nav) : nav_(nav) {}
void FlashUtilityView::focus() {}

// ---- DebugMenuView (BtnGridView, has nav_) ------------------------------
DebugMenuView::DebugMenuView(NavigationView& nav) : nav_(nav) {}
void DebugMenuView::on_populate() {}

// ---- SettingsMenuView (BtnGridView, has nav_) ---------------------------
SettingsMenuView::SettingsMenuView(NavigationView& nav) : nav_(nav) {}
void SettingsMenuView::on_populate() {}

// ---- AboutView (no nav_ member) -----------------------------------------
AboutView::AboutView(NavigationView&) {}
void AboutView::focus() {}
bool AboutView::on_key(const KeyEvent) { return false; }
bool AboutView::on_encoder(const EncoderEvent) { return false; }
bool AboutView::on_touch(const TouchEvent) { return false; }

// ---- BattinfoView (has nav_) --------------------------------------------
BattinfoView::BattinfoView(NavigationView& nav) : nav_(nav) {}
BattinfoView::~BattinfoView() {}
void BattinfoView::focus() {}

// ---- SetSDCardView (no nav_ member) -------------------------------------
SetSDCardView::SetSDCardView(NavigationView&) {}
void SetSDCardView::focus() {}
void SetSDCardView::on_show() {}
void SetSDCardView::on_hide() {}

// ==========================================================================
// Inner / member views (constructed as member variables of the above)
// ==========================================================================

// ---- AISRecentEntryDetailView (member of AISAppView, has no nav_ but takes nav) --
AISRecentEntryDetailView::AISRecentEntryDetailView(NavigationView&) {}
// copy ctor/assignment deleted by unique_ptr member -- removed stubs
void AISRecentEntryDetailView::focus() {}
void AISRecentEntryDetailView::paint(Painter&) {}
void AISRecentEntryDetailView::set_entry(const AISRecentEntry&) {}
void AISRecentEntryDetailView::update_position() {}
bool AISRecentEntryDetailView::add_map_marker(const AISRecentEntry&) { return false; }
void AISRecentEntryDetailView::update_map_markers(AISRecentEntries&) {}

// ---- APRSDetailsView (member of APRSTableView) --------------------------
APRSDetailsView::APRSDetailsView(NavigationView&) {}
APRSDetailsView::~APRSDetailsView() {}
void APRSDetailsView::focus() {}

// ---- APRSTableView (member of APRSRXView) --------------------------------
APRSTableView::APRSTableView(NavigationView& nav, Rect) : nav_(nav) {}
APRSTableView::~APRSTableView() {}
void APRSTableView::focus() {}
void APRSTableView::on_show() {}
void APRSTableView::on_hide() {}
void APRSTableView::on_pkt(const APRSPacketMessage*) {}

// ---- APRSRxView (member of APRSRXView) ----------------------------------
APRSRxView::APRSRxView(NavigationView& nav, Rect) : nav_(nav) {}
APRSRxView::~APRSRxView() {}
void APRSRxView::focus() {}
void APRSRxView::on_show() {}
void APRSRxView::on_packet(const APRSPacketMessage*) {}
void APRSRxView::on_freqchg(int64_t) {}

// ---- RDSPSNView (member of RDSView, base OptionTabView) -----------------
RDSPSNView::RDSPSNView(NavigationView&, Rect parent_rect)
    : OptionTabView(parent_rect) {}

// ---- RDSRadioTextView (member of RDSView, base OptionTabView) -----------
RDSRadioTextView::RDSRadioTextView(NavigationView&, Rect parent_rect)
    : OptionTabView(parent_rect) {}

// ---- RDSDateTimeView (member of RDSView, base OptionTabView) ------------
RDSDateTimeView::RDSDateTimeView(Rect parent_rect)
    : OptionTabView(parent_rect) {}

// ---- RDSAudioView (member of RDSView, base OptionTabView) ---------------
RDSAudioView::RDSAudioView(Rect parent_rect)
    : OptionTabView(parent_rect) {}

// ---- EncodersConfigView (member of EncodersView) ------------------------
EncodersConfigView::EncodersConfigView(NavigationView&, Rect) {}
void EncodersConfigView::focus() {}
void EncodersConfigView::on_show() {}

// ---- EncodersScanView (member of EncodersView) --------------------------
EncodersScanView::EncodersScanView(NavigationView&, Rect) {}
void EncodersScanView::focus() {}

// ---- FileManBaseView (base of FileManagerView) --------------------------
FileManBaseView::FileManBaseView(NavigationView& nav, std::string) : nav_(nav) {}
void FileManBaseView::focus() {}

// ---- FreqManBaseView (base of FrequencyManagerView) ---------------------
FreqManBaseView::FreqManBaseView(NavigationView& nav) : nav_(nav) {}
void FreqManBaseView::focus() {}

// ==========================================================================
// Detail views pushed (not held as members) but whose constructors may
// be instantiated through nav.push<T>() templates in the compiled code.
// ==========================================================================

// ---- BleRecentEntryDetailView (pushed by BLERxView) ---------------------
BleRecentEntryDetailView::BleRecentEntryDetailView(NavigationView& nav, const BleRecentEntry&)
    : nav_(nav) {}
void BleRecentEntryDetailView::focus() {}
void BleRecentEntryDetailView::paint(Painter&) {}
void BleRecentEntryDetailView::set_entry(const BleRecentEntry&) {}
void BleRecentEntryDetailView::update_data() {}

// ---- WeatherRecentEntryDetailView (pushed by WeatherView) ---------------
WeatherRecentEntryDetailView::WeatherRecentEntryDetailView(NavigationView& nav, const WeatherRecentEntry&)
    : nav_(nav) {}
void WeatherRecentEntryDetailView::focus() {}
void WeatherRecentEntryDetailView::update_data() {}

// ---- SubGhzDRecentEntryDetailView (pushed by SubGhzDView) --------------
SubGhzDRecentEntryDetailView::SubGhzDRecentEntryDetailView(NavigationView& nav, const SubGhzDRecentEntry&)
    : nav_(nav) {}
void SubGhzDRecentEntryDetailView::focus() {}
void SubGhzDRecentEntryDetailView::update_data() {}

// ---- ADSBRxDetailsView (pushed by ADSBRxView) --------------------------
ADSBRxDetailsView::ADSBRxDetailsView(NavigationView&, const AircraftRecentEntry&) {}
void ADSBRxDetailsView::focus() {}

// ---- ADSBRxAircraftDetailsView (pushed by ADSBRxDetailsView) ------------
ADSBRxAircraftDetailsView::ADSBRxAircraftDetailsView(NavigationView&, const AircraftRecentEntry&) {}
void ADSBRxAircraftDetailsView::focus() {}

// ---- POCSAGSettingsView (pushed by POCSAGAppView) -----------------------
POCSAGSettingsView::POCSAGSettingsView(NavigationView&, POCSAGSettings& s) : settings_(s) {}
// focus() already defined via POCSAGAppView -- skip if duplicated

// ==========================================================================
// AMOptionsView / NBFMOptionsView / WFMOptionsView etc. -- member views of
// AnalogAudioView that are created dynamically, but their constructors may
// still be needed.
// ==========================================================================

AMOptionsView::AMOptionsView(AnalogAudioView*, Rect, const Style*) {}
AMFMAptOptionsView::AMFMAptOptionsView(AnalogAudioView*, Rect, const Style*) {}
NBFMOptionsView::NBFMOptionsView(Rect, const Style*) {}
WFMOptionsView::WFMOptionsView(Rect, const Style*) {}
WFMAMAptOptionsView::WFMAMAptOptionsView(Rect, const Style*) {}
SPECOptionsView::SPECOptionsView(AnalogAudioView*, Rect, const Style*) {}

// ==========================================================================
// FrequencyField (base of RxFrequencyField/TxFrequencyField template)
// ==========================================================================

FrequencyField::FrequencyField(Point parent_pos) : length_(11), range_{0, 7250000000} {}
FrequencyField::FrequencyField(Point parent_pos, rf::FrequencyRange range) : length_(11), range_(range) {}
FrequencyField::~FrequencyField() {}
rf::Frequency FrequencyField::value() const { return value_; }
void FrequencyField::set_value(rf::Frequency) {}
void FrequencyField::set_step(rf::Frequency) {}
void FrequencyField::set_allow_digit_mode(bool) {}
void FrequencyField::paint(Painter&) {}
bool FrequencyField::on_key(KeyEvent) { return false; }
bool FrequencyField::on_encoder(EncoderEvent) { return false; }
bool FrequencyField::on_keyboard(KeyboardEvent) { return false; }
bool FrequencyField::on_touch(TouchEvent) { return false; }
void FrequencyField::on_focus() {}
void FrequencyField::on_blur() {}
void FrequencyField::getAccessibilityText(std::string&) {}
void FrequencyField::getWidgetName(std::string&) {}

// ==========================================================================
// FrequencyKeypadView (pushed by BoundFrequencyField<> template on_edit)
// ==========================================================================

FrequencyKeypadView::FrequencyKeypadView(NavigationView&, const rf::Frequency) {}
void FrequencyKeypadView::focus() {}
void FrequencyKeypadView::paint(Painter&) {}
rf::Frequency FrequencyKeypadView::value(FrequencyUnit) const { return 0; }
void FrequencyKeypadView::set_value(const rf::Frequency) {}
bool FrequencyKeypadView::on_encoder(const EncoderEvent) { return false; }
bool FrequencyKeypadView::on_keyboard(const KeyboardEvent) { return false; }

// ==========================================================================
// RecentEntriesColumns / RecentEntriesHeader (used by multiple app views)
// ==========================================================================

RecentEntriesColumns::RecentEntriesColumns(
    std::initializer_list<RecentEntriesColumn> columns)
    : _columns{columns} {}

RecentEntriesHeader::RecentEntriesHeader(RecentEntriesColumns& columns)
    : _columns{columns} {}
void RecentEntriesHeader::paint(Painter&) {}

// ==========================================================================
// TabView / Tab (used as members in APRSRXView, RDSView, etc.)
// ==========================================================================

Tab::Tab() {}
void Tab::paint(Painter&) {}
bool Tab::on_key(const KeyEvent) { return false; }
bool Tab::on_touch(const TouchEvent) { return false; }
void Tab::set(uint32_t, Dim, std::string, Color) {}

TabView::TabView(std::initializer_list<TabDef>) {}
TabView::~TabView() {}
void TabView::focus() {}
void TabView::on_show() {}
void TabView::set_selected(uint32_t) {}

// ==========================================================================
// SDCardStatusView virtual methods (vtable anchor -- already constructed in
// phase2_stubs.cpp but on_show/on_hide may still need definitions)
// ==========================================================================

void SDCardStatusView::on_show() {}
void SDCardStatusView::on_hide() {}
void SDCardStatusView::paint(Painter&) {}

// ==========================================================================
// RDSThread (member used by RDSView, not a View but needs ctor/dtor)
// ==========================================================================

RDSThread::RDSThread(std::vector<RDSGroup>**) {}
RDSThread::~RDSThread() {}

} /* namespace ui */
