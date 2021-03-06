/*
 * Copyright (C) 2016~2016 by CSSlayer
 * wengxt@gmail.com
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; see the file COPYING. If not,
 * see <http://www.gnu.org/licenses/>.
 */

#include "globalconfig.h"
#include "fcitx-config/configuration.h"

namespace fcitx {

namespace impl {

FCITX_CONFIGURATION(
    HotkeyConfig,
    Option<KeyList> triggerKeys{
        this,
        "TriggerKeys",
        "Trigger Input Method",
        {Key("Control+space"), Key("Zenkaku_Hankaku"), Key("Hangul")}};
    Option<KeyList> altTriggerKeys{
        this,
        "AltTriggerKeys",
        "Trigger Input Method Only after using it to deactivate",
        {Key("Shift_L")}};
    Option<KeyList> enumerateForwardKeys{this,
                                         "EnumerateForwardKeys",
                                         "Enumerate Input Method Forward",
                                         {Key("Control+Shift_R")}};
    Option<KeyList> enumerateBackwardKeys{this,
                                          "EnumerateBackwardKeys",
                                          "Enumerate Input Method Backward",
                                          {Key("Control+Shift_L")}};
    Option<KeyList> enumerateGroupForwardKeys{
        this,
        "EnumerateGroupForwardKeys",
        "Enumerate Input Method Group Forward",
        {Key("Super+space")}};
    Option<KeyList> enumerateGroupBackwardKeys{
        this,
        "EnumerateGroupBackwardKeys",
        "Enumerate Input Method Group Backward",
        {Key("Super+Shift+space")}};
    Option<KeyList> activateKeys{this,
                                 "ActivateKeys",
                                 "ActivateKeys",
                                 {
                                     Key("Hangul_Hanja"),
                                 }};
    Option<KeyList> deactivateKeys{
        this, "DeactivateKeys", "DeactivateKeys", {Key("Hangul_Romaja")}};
    Option<KeyList> defaultPrevPage{
        this, "PrevPage", "Default Previous page", {Key("Up")}};
    Option<KeyList> defaultNextPage{
        this, "NextPage", "Default Next page", {Key("Down")}};);

FCITX_CONFIGURATION(
    BehaviorConfig,
    Option<bool> activeByDefault{this, "ActiveByDefault", "Active By Default"};
    Option<bool> showInputMethodInformation{
        this, "ShowInputMethodInformation",
        "ShowInputMethodInformation when switch input method", true};
    Option<int, IntConstrain> defaultPageSize{
        this, "DefaultPageSize", "Default page size", 5, IntConstrain(1, 10)};);

FCITX_CONFIGURATION(GlobalConfig,
                    Option<HotkeyConfig> hotkey{this, "Hotkey", "Hotkey"};
                    Option<BehaviorConfig> behavior{this, "Behavior",
                                                    "Behavior"};);
}

class GlobalConfigPrivate : public impl::GlobalConfig {};

GlobalConfig::GlobalConfig() : d_ptr(std::make_unique<GlobalConfigPrivate>()) {}

GlobalConfig::~GlobalConfig() {}

void GlobalConfig::load(const RawConfig &config) {
    FCITX_D();
    d->load(config);
}

const KeyList &GlobalConfig::triggerKeys() const {
    FCITX_D();
    return *d->hotkey->triggerKeys;
}

const KeyList &GlobalConfig::activateKeys() const {
    FCITX_D();
    return *d->hotkey->activateKeys;
}

const KeyList &GlobalConfig::deactivateKeys() const {
    FCITX_D();
    return d->hotkey->deactivateKeys.value();
}

const KeyList &GlobalConfig::enumerateForwardKeys() const {
    FCITX_D();
    return d->hotkey->enumerateForwardKeys.value();
}

const KeyList &GlobalConfig::enumerateBackwardKeys() const {
    FCITX_D();
    return d->hotkey->enumerateBackwardKeys.value();
}

const KeyList &GlobalConfig::enumerateGroupForwardKeys() const {
    FCITX_D();
    return *d->hotkey->enumerateGroupForwardKeys;
}

const KeyList &GlobalConfig::enumerateGroupBackwardKeys() const {
    FCITX_D();
    return *d->hotkey->enumerateGroupBackwardKeys;
}

bool GlobalConfig::activeByDefault() const {
    FCITX_D();
    return d->behavior->activeByDefault.value();
}

bool GlobalConfig::showInputMethodInformation() const {
    FCITX_D();
    return d->behavior->showInputMethodInformation.value();
}

const KeyList &GlobalConfig::defaultPrevPage() const {
    FCITX_D();
    return d->hotkey->defaultPrevPage.value();
}

const KeyList &GlobalConfig::defaultNextPage() const {
    FCITX_D();
    return d->hotkey->defaultNextPage.value();
}

int GlobalConfig::defaultPageSize() const {
    FCITX_D();
    return d->behavior->defaultPageSize.value();
}
}
