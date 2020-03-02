// Copyright 2020 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <QObject>

#include "ui/model/app_model.h"
#include "wallet/client/extensions/news_channels/interface.h"

class ExchangeRatesManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString      beamRate        READ getBeamRate        NOTIFY  beamRateChanged)
    Q_PROPERTY(QString      btcRate         READ getBtcRate         NOTIFY  btcRateChanged)
    Q_PROPERTY(QString      ltcRate         READ getLtcRate         NOTIFY  ltcRateChanged)
    Q_PROPERTY(QString      qtumRate        READ getQtumRate        NOTIFY  qtumRateChanged)

public:
    ExchangeRatesManager();

    QString getBeamRate();
    QString getBtcRate();
    QString getLtcRate();
    QString getQtumRate();

signals:
    void beamRateChanged();
    void btcRateChanged();
    void ltcRateChanged();
    void qtumRateChanged();

public slots:
    void onExchangeRatesUpdate(const std::vector<beam::wallet::ExchangeRate>& rates);

private:
    WalletModel& m_walletModel;
    // WalletSettings& m_settings;

    beam::wallet::ExchangeRate::Currency m_rateUnit;
    std::map<beam::wallet::ExchangeRate::Currency, beam::Amount> m_rates;
};