// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/signin/sync_confirmation_ui.h"

#include <string>

#include "base/containers/enum_set.h"
#include "base/feature_list.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/buildflag.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/util/managed_browser_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_features.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/signin/profile_colors_util.h"
#include "chrome/browser/ui/webui/signin/signin_url_utils.h"
#include "chrome/browser/ui/webui/signin/sync_confirmation_handler.h"
#include "chrome/browser/ui/webui/webui_util.h"
#include "chrome/common/themes/autogenerated_theme_util.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/signin_resources.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/avatar_icon_util.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/strings/grit/components_strings.h"
#include "components/sync/base/sync_prefs.h"
#include "components/sync/base/user_selectable_type.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/webui/resource_path.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/native_theme.h"
#include "ui/resources/grit/webui_resources.h"

namespace {
const char kSyncBenefitAutofillStringName[] = "syncConfirmationAutofill";
const char kSyncBenefitBookmarksStringName[] = "syncConfirmationBookmarks";
const char kSyncBenefitReadingListStringName[] = "syncConfirmationReadingList";
const char kSyncBenefitExtensionsStringName[] = "syncConfirmationExtensions";
const char kSyncBenefitHistoryAndMoreStringName[] =
    "syncConfirmationHistoryAndMore";
const char kSyncBenefitIconNameKey[] = "iconName";
const char kSyncBenefitTitleKey[] = "title";

bool IsAnyTypeSyncable(PrefService& pref_service,
                       syncer::UserSelectableTypeSet types) {
  for (auto type : types) {
    const char* pref_name = syncer::SyncPrefs::GetPrefNameForType(type);
    if (!pref_service.IsManagedPreference(pref_name)) {
      return true;
    }
  }
  return false;
}
}  // namespace

// static
std::string SyncConfirmationUI::GetSyncBenefitsListJSON(
    PrefService& pref_service) {
  using syncer::UserSelectableType;
  base::Value::List sync_benefits_list;

  if (IsAnyTypeSyncable(pref_service, {UserSelectableType::kBookmarks,
                                       UserSelectableType::kReadingList})) {
    std::string titleKey;
    if (IsAnyTypeSyncable(pref_service, {UserSelectableType::kBookmarks})) {
      titleKey = kSyncBenefitBookmarksStringName;
    } else {
      titleKey = kSyncBenefitReadingListStringName;
    }

    base::Value::Dict bookmarks;
    bookmarks.Set(kSyncBenefitTitleKey, titleKey);
    bookmarks.Set(kSyncBenefitIconNameKey, "signin:star-outline");
    sync_benefits_list.Append(std::move(bookmarks));
  }

  if (IsAnyTypeSyncable(pref_service, {UserSelectableType::kAutofill,
                                       UserSelectableType::kPasswords})) {
    base::Value::Dict autofill;
    autofill.Set(kSyncBenefitTitleKey, kSyncBenefitAutofillStringName);
    autofill.Set(kSyncBenefitIconNameKey, "signin:assignment-outline");
    sync_benefits_list.Append(std::move(autofill));
  }

  if (IsAnyTypeSyncable(pref_service, {UserSelectableType::kExtensions,
                                       UserSelectableType::kApps})) {
    base::Value::Dict extensions;
    extensions.Set(kSyncBenefitTitleKey, kSyncBenefitExtensionsStringName);
    extensions.Set(kSyncBenefitIconNameKey, "signin:extension-outline");
    sync_benefits_list.Append(std::move(extensions));
  }

  // Even if no associated type is syncable, we still deliberately show "History
  // and more". So no need to check it.
  base::Value::Dict history_and_more;
  history_and_more.Set(kSyncBenefitTitleKey,
                       kSyncBenefitHistoryAndMoreStringName);
  history_and_more.Set(kSyncBenefitIconNameKey, "signin:devices");
  sync_benefits_list.Append(std::move(history_and_more));

  std::string json_benefits_list;
  base::JSONWriter::Write(sync_benefits_list, &json_benefits_list);
  return json_benefits_list;
}

SyncConfirmationUI::SyncConfirmationUI(content::WebUI* web_ui)
    : SigninWebDialogUI(web_ui), profile_(Profile::FromWebUI(web_ui)) {
  const GURL& url = web_ui->GetWebContents()->GetVisibleURL();
  const bool is_sync_allowed = SyncServiceFactory::IsSyncAllowed(profile_);

  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile_, chrome::kChromeUISyncConfirmationHost);
  webui::SetJSModuleDefaults(source);
  webui::EnableTrustedTypesCSP(source);

  static constexpr webui::ResourcePath kResources[] = {
      {"icons.html.js", IDR_SIGNIN_ICONS_HTML_JS},
      {"signin_shared.css.js", IDR_SIGNIN_SIGNIN_SHARED_CSS_JS},
      {"signin_vars.css.js", IDR_SIGNIN_SIGNIN_VARS_CSS_JS},
      {"tangible_sync_style_shared.css.js",
       IDR_SIGNIN_TANGIBLE_SYNC_STYLE_SHARED_CSS_JS},
      {"sync_confirmation_browser_proxy.js",
       IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_CONFIRMATION_BROWSER_PROXY_JS},
      {"sync_confirmation.js",
       IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_CONFIRMATION_JS},
      {chrome::kChromeUISyncConfirmationLoadingPath,
       IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_LOADING_CONFIRMATION_HTML},
  };
  source->AddResourcePaths(kResources);

  AddStringResource(source, "syncLoadingConfirmationTitle",
                    IDS_SYNC_LOADING_CONFIRMATION_TITLE);
  webui::SetupChromeRefresh2023(source);

  if (is_sync_allowed) {
    InitializeForSyncConfirmation(source, GetSyncConfirmationStyle(url));
  } else {
    InitializeForSyncDisabled(source);
  }

  base::Value::Dict strings;
  webui::SetLoadTimeDataDefaults(
      g_browser_process->GetApplicationLocale(), &strings);
  source->AddLocalizedStrings(strings);

  if (url.query().find("debug") != std::string::npos) {
    // Not intended to be hooked to anything. The dialog will not initialize it
    // so we force it here.
    InitializeMessageHandlerWithBrowser(nullptr);
  }
}

SyncConfirmationUI::~SyncConfirmationUI() = default;

void SyncConfirmationUI::InitializeMessageHandlerWithBrowser(Browser* browser) {
  web_ui()->AddMessageHandler(std::make_unique<SyncConfirmationHandler>(
      profile_, js_localized_string_to_ids_map_, browser));
}

void SyncConfirmationUI::InitializeForSyncConfirmation(
    content::WebUIDataSource* source,
    SyncConfirmationStyle style) {
  int title_id = IDS_SYNC_CONFIRMATION_TITLE;
  int info_title_id = IDS_SYNC_CONFIRMATION_SYNC_INFO_TITLE;
  int info_desc_id = IDS_SYNC_CONFIRMATION_SYNC_INFO_DESC;
  int confirm_label_id = IDS_SYNC_CONFIRMATION_CONFIRM_BUTTON_LABEL;
  int undo_label_id = IDS_CANCEL;
  int settings_label_id = IDS_SYNC_CONFIRMATION_SETTINGS_BUTTON_LABEL;
  int illustration_id =
      IDR_SIGNIN_SYNC_CONFIRMATION_IMAGES_SYNC_CONFIRMATION_ILLUSTRATION_SVG;
  int illustration_dark_id =
      IDR_SIGNIN_SYNC_CONFIRMATION_IMAGES_SYNC_CONFIRMATION_ILLUSTRATION_DARK_SVG;
  std::string illustration_path = "images/sync_confirmation_illustration.svg";
  std::string illustration_dark_path =
      "images/sync_confirmation_illustration_dark.svg";

  source->AddResourcePath(
      "sync_confirmation_app.js",
      IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_CONFIRMATION_APP_JS);
  source->AddResourcePath(
      "sync_confirmation_app.html.js",
      IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_CONFIRMATION_APP_HTML_JS);
  source->SetDefaultResource(
      IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_CONFIRMATION_HTML);

  bool isTangibleSync = base::FeatureList::IsEnabled(switches::kTangibleSync);
  bool isSigninInterceptFre =
      style == SyncConfirmationStyle::kSigninInterceptModal;
  source->AddBoolean("isModalDialog",
                     style == SyncConfirmationStyle::kDefaultModal ||
                         style == SyncConfirmationStyle::kSigninInterceptModal);
  source->AddBoolean("isSigninInterceptFre", isSigninInterceptFre);
  source->AddBoolean("isTangibleSync", isTangibleSync);

  source->AddString("accountPictureUrl",
                    profiles::GetPlaceholderAvatarIconUrl());

  source->AddString("syncBenefitsList",
                    GetSyncBenefitsListJSON(*profile_->GetPrefs()));

  // Default overrides without placeholders
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  title_id = IDS_SYNC_CONFIRMATION_TITLE_LACROS_NON_FORCED;
#endif
  // TODO(crbug.com/1374702): Rename SyncConfirmationStyle enum based on the
  // purpose instead of what kind of container the page is displayed in.
  if (isSigninInterceptFre) {
    DCHECK(base::FeatureList::IsEnabled(kSyncPromoAfterSigninIntercept));
    info_title_id = IDS_SYNC_CONFIRMATION_SYNC_INFO_SIGNIN_INTERCEPT;
    confirm_label_id = IDS_SYNC_CONFIRMATION_TURN_ON_SYNC_BUTTON_LABEL;
    undo_label_id = IDS_NO_THANKS;
    illustration_path =
        "images/sync_confirmation_signin_intercept_illustration.svg";
    illustration_id =
        IDR_SIGNIN_SYNC_CONFIRMATION_IMAGES_SYNC_CONFIRMATION_SIGNIN_INTERCEPT_ILLUSTRATION_SVG;
    illustration_dark_path =
        "images/sync_confirmation_signin_intercept_illustration_dark.svg";
    illustration_dark_id =
        IDR_SIGNIN_SYNC_CONFIRMATION_IMAGES_SYNC_CONFIRMATION_SIGNIN_INTERCEPT_ILLUSTRATION_DARK_SVG;
  } else if (style == SyncConfirmationStyle::kWindow) {
    undo_label_id = IDS_NO_THANKS;
    settings_label_id = IDS_SYNC_CONFIRMATION_REFRESHED_SETTINGS_BUTTON_LABEL;
    illustration_path = "images/sync_confirmation_refreshed_illustration.svg";
    illustration_id =
        IDR_SIGNIN_SYNC_CONFIRMATION_IMAGES_SYNC_CONFIRMATION_REFRESHED_ILLUSTRATION_SVG;
    illustration_dark_path =
        "images/sync_confirmation_refreshed_illustration_dark.svg";
    illustration_dark_id =
        IDR_SIGNIN_SYNC_CONFIRMATION_IMAGES_SYNC_CONFIRMATION_REFRESHED_ILLUSTRATION_DARK_SVG;
  }

  if (isTangibleSync) {
    title_id = IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_TITLE;
    info_desc_id = IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_INFO_DESC;
    settings_label_id = IDS_SYNC_CONFIRMATION_SETTINGS_BUTTON_LABEL;

#if BUILDFLAG(IS_CHROMEOS_LACROS)
    // The sign-in intercept feature isn't enabled on Lacros. Revisit the title
    // when enabling it.
    DCHECK(!isSigninInterceptFre);
    info_title_id = IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_INFO_TITLE_LACROS;
#else
    info_title_id =
        isSigninInterceptFre
            ? IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_INFO_TITLE_SIGNIN_INTERCEPT_V2
            : IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_INFO_TITLE;
#endif

    illustration_path = "images/tangible_sync_dialog_illustration.svg";
    illustration_dark_path =
        "images/tangible_sync_dialog_illustration_dark.svg";

    illustration_id = IDR_SIGNIN_IMAGES_SHARED_DIALOG_ILLUSTRATION_SVG;
    illustration_dark_id =
        IDR_SIGNIN_IMAGES_SHARED_DIALOG_ILLUSTRATION_DARK_SVG;

    source->AddResourcePath("images/tangible_sync_window_left_illustration.svg",
                            IDR_SIGNIN_IMAGES_SHARED_LEFT_BANNER_SVG);
    source->AddResourcePath(
        "images/tangible_sync_window_left_illustration_dark.svg",
        IDR_SIGNIN_IMAGES_SHARED_LEFT_BANNER_DARK_SVG);
    source->AddResourcePath(
        "images/tangible_sync_window_right_illustration.svg",
        IDR_SIGNIN_IMAGES_SHARED_RIGHT_BANNER_SVG);
    source->AddResourcePath(
        "images/tangible_sync_window_right_illustration_dark.svg",
        IDR_SIGNIN_IMAGES_SHARED_RIGHT_BANNER_DARK_SVG);
  }

  // Registering and resolving the strings with placeholders
  if (isSigninInterceptFre) {
    ProfileAttributesEntry* entry =
        g_browser_process->profile_manager()
            ->GetProfileAttributesStorage()
            .GetProfileAttributesWithPath(profile_->GetPath());
    DCHECK(entry);
    std::u16string gaia_name = entry->GetGAIANameToDisplay();
    if (gaia_name.empty())
      gaia_name = entry->GetLocalProfileName();
    AddStringResourceWithPlaceholder(
        source, "syncConfirmationTitle",
        IDS_SYNC_CONFIRMATION_WELCOME_TITLE_SIGNIN_INTERCEPT, gaia_name);
  } else {
    AddStringResource(source, "syncConfirmationTitle", title_id);
  }

  // Registering and resolving the strings without placeholders
  AddStringResource(source, "syncConfirmationSyncInfoTitle", info_title_id);
  AddStringResource(source, "syncConfirmationConfirmLabel", confirm_label_id);
  AddStringResource(source, "syncConfirmationUndoLabel", undo_label_id);
  AddStringResource(source, "syncConfirmationSettingsLabel", settings_label_id);
  AddStringResource(source, "syncConfirmationSyncInfoDesc", info_desc_id);
  AddStringResource(source, "syncConfirmationSettingsInfo",
                    IDS_SYNC_CONFIRMATION_SETTINGS_INFO);
  AddStringResource(source, kSyncBenefitBookmarksStringName,
                    IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_BOOKMARKS);
  AddStringResource(source, kSyncBenefitReadingListStringName,
                    IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_READING_LIST);
  AddStringResource(source, kSyncBenefitAutofillStringName,
                    IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_AUTOFILL);
  AddStringResource(source, kSyncBenefitExtensionsStringName,
                    IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_EXTENSIONS);
  AddStringResource(source, kSyncBenefitHistoryAndMoreStringName,
                    IDS_SYNC_CONFIRMATION_TANGIBLE_SYNC_HISTORY_AND_MORE);

  source->AddResourcePath(illustration_path, illustration_id);
  source->AddResourcePath(illustration_dark_path, illustration_dark_id);
}

void SyncConfirmationUI::InitializeForSyncDisabled(
    content::WebUIDataSource* source) {
  source->SetDefaultResource(
      IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_DISABLED_CONFIRMATION_HTML);
  source->AddResourcePath(
      "sync_disabled_confirmation_app.js",
      IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_DISABLED_CONFIRMATION_APP_JS);
  source->AddResourcePath(
      "sync_disabled_confirmation_app.html.js",
      IDR_SIGNIN_SYNC_CONFIRMATION_SYNC_DISABLED_CONFIRMATION_APP_HTML_JS);

  bool managed_account_signout_disallowed =
      base::FeatureList::IsEnabled(kDisallowManagedProfileSignout) &&
      chrome::enterprise_util::UserAcceptedAccountManagement(profile_);

  source->AddBoolean("signoutDisallowed", managed_account_signout_disallowed);
  AddStringResource(source, "syncDisabledConfirmationTitle",
                    IDS_SYNC_DISABLED_CONFIRMATION_CHROME_SYNC_TITLE);
  AddStringResource(source, "syncDisabledConfirmationDetails",
                    IDS_SYNC_DISABLED_CONFIRMATION_DETAILS);
  AddStringResource(
      source, "syncDisabledConfirmationConfirmLabel",
      managed_account_signout_disallowed
          ? IDS_SYNC_DISABLED_CONFIRMATION_CONFIRM_BUTTON_MANAGED_ACCOUNT_SIGNOUT_DISALLOWED_LABEL
          : IDS_SYNC_DISABLED_CONFIRMATION_CONFIRM_BUTTON_LABEL);
  AddStringResource(source, "syncDisabledConfirmationUndoLabel",
                    IDS_SYNC_DISABLED_CONFIRMATION_UNDO_BUTTON_LABEL);
}

void SyncConfirmationUI::AddStringResource(content::WebUIDataSource* source,
                                           const std::string& name,
                                           int ids) {
  source->AddLocalizedString(name, ids);
  AddLocalizedStringToIdsMap(l10n_util::GetStringUTF8(ids), ids);
}

void SyncConfirmationUI::AddStringResourceWithPlaceholder(
    content::WebUIDataSource* source,
    const std::string& name,
    int ids,
    const std::u16string& parameter) {
  std::string localized_string = l10n_util::GetStringFUTF8(ids, parameter);
  source->AddString(name, localized_string);
  AddLocalizedStringToIdsMap(localized_string, ids);
}

void SyncConfirmationUI::AddLocalizedStringToIdsMap(
    const std::string& localized_string,
    int ids) {
  // When the strings are passed to the HTML, the Unicode NBSP symbol (\u00A0)
  // will be automatically replaced with "&nbsp;". This change must be mirrored
  // in the string-to-ids map. Note that "\u00A0" is actually two characters,
  // so we must use base::ReplaceSubstrings* rather than base::ReplaceChars.
  // TODO(msramek): Find a more elegant solution.
  std::string sanitized_string = localized_string;
  base::ReplaceSubstringsAfterOffset(&sanitized_string, 0, "\u00A0" /* NBSP */,
                                     "&nbsp;");
  js_localized_string_to_ids_map_[sanitized_string] = ids;
}
