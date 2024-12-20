// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/login/signin/oauth2_login_manager_factory.h"

#include "chrome/browser/ash/login/signin/oauth2_login_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/account_reconcilor_factory.h"
#include "chrome/browser/signin/identity_manager_factory.h"

namespace ash {

OAuth2LoginManagerFactory::OAuth2LoginManagerFactory()
    : ProfileKeyedServiceFactory(
          "OAuth2LoginManager",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              // TODO(crbug.com/1418376): Check if this service is needed in
              // Guest mode.
              .WithGuest(ProfileSelection::kOriginalOnly)
              .Build()) {
  DependsOn(IdentityManagerFactory::GetInstance());
  DependsOn(AccountReconcilorFactory::GetInstance());
}

OAuth2LoginManagerFactory::~OAuth2LoginManagerFactory() {}

// static
OAuth2LoginManager* OAuth2LoginManagerFactory::GetForProfile(Profile* profile) {
  return static_cast<OAuth2LoginManager*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
OAuth2LoginManagerFactory* OAuth2LoginManagerFactory::GetInstance() {
  return base::Singleton<OAuth2LoginManagerFactory>::get();
}

KeyedService* OAuth2LoginManagerFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = static_cast<Profile*>(context);
  OAuth2LoginManager* service;
  service = new OAuth2LoginManager(profile);
  return service;
}

}  // namespace ash
