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

#include "userinterfacemanager.h"
#include "action.h"
#include "inputcontext.h"
#include "userinterface.h"
#include <set>

namespace fcitx {

struct UserInterfaceComponentHash {
    template <typename T>
    std::underlying_type_t<T> operator()(T t) const {
        return static_cast<std::underlying_type_t<T>>(t);
    }
};

class IdAllocator {
public:
    int allocId() {
        if (freeList_.empty()) {
            ++maxId_;
            return maxId_;
        } else {
            auto value = *freeList_.begin();
            freeList_.erase(freeList_.begin());
            return value;
        }
    }
    void returnId(int id) {
        assert(id <= maxId_ && freeList_.count(id) == 0);
        freeList_.insert(id);
    }

    std::set<int> freeList_;
    int maxId_ = 0;
};

class UserInterfaceManagerPrivate {
public:
    UserInterfaceManagerPrivate(AddonManager *addonManager)
        : addonManager_(addonManager) {}

    UserInterface *ui_ = nullptr;
    std::string uiName_;
    std::vector<std::string> uis_;

    std::unordered_map<std::string, std::pair<Action *, ScopedConnection>>
        actions_;
    std::unordered_map<int, Action *> idToAction_;

    typedef std::list<std::pair<
        InputContext *, std::unordered_set<UserInterfaceComponent, EnumHash>>>
        UIUpdateList;
    UIUpdateList updateList_;
    std::unordered_map<InputContext *, UIUpdateList::iterator> updateIndex_;
    AddonManager *addonManager_;

    IdAllocator ids_;
};

UserInterfaceManager::UserInterfaceManager(AddonManager *addonManager)
    : d_ptr(std::make_unique<UserInterfaceManagerPrivate>(addonManager)) {}

UserInterfaceManager::~UserInterfaceManager() {}

void UserInterfaceManager::load(const std::string &uiName) {
    FCITX_D();
    auto names = d->addonManager_->addonNames(AddonCategory::UI);

    d->uis_.clear();
    if (names.count(uiName)) {
        auto ui = d->addonManager_->addon(uiName, true);
        if (ui) {
            d->uis_.push_back(uiName);
        }
    }

    if (d->uis_.empty()) {
        d->uis_.insert(d->uis_.end(), names.begin(), names.end());
        std::sort(d->uis_.begin(), d->uis_.end(),
                  [d](const std::string &lhs, const std::string &rhs) {
                      auto lp = d->addonManager_->addonInfo(lhs)->uiPriority();
                      auto rp = d->addonManager_->addonInfo(rhs)->uiPriority();
                      if (lp == rp) {
                          return lhs > rhs;
                      } else {
                          return lp > rp;
                      }
                  });
    }
    updateAvailability();
}

bool UserInterfaceManager::registerAction(const std::string &name,
                                          Action *action) {
    FCITX_D();
    if (!action->name().empty() || name.empty()) {
        return false;
    }
    auto iter = d->actions_.find(name);
    if (iter != d->actions_.end()) {
        return false;
    }
    ScopedConnection conn = action->connect<ObjectDestroyed>(
        [this, action](void *) { unregisterAction(action); });
    action->setName(name);
    action->setId(d->ids_.allocId());
    d->actions_.emplace(name, std::make_pair(action, std::move(conn)));
    d->idToAction_.emplace(action->id(), action);
    return true;
}

void UserInterfaceManager::unregisterAction(Action *action) {
    FCITX_D();
    auto iter = d->actions_.find(action->name());
    if (iter == d->actions_.end()) {
        return;
    }
    if (std::get<0>(iter->second) != action) {
        return;
    }
    d->actions_.erase(iter);
    d->idToAction_.erase(action->id());
    d->ids_.returnId(action->id());
    action->setName(std::string());
    action->setId(0);
}

Action *UserInterfaceManager::lookupAction(const std::string &name) const {
    FCITX_D();
    auto iter = d->actions_.find(name);
    if (iter == d->actions_.end()) {
        return nullptr;
    }
    return std::get<0>(iter->second);
}

Action *UserInterfaceManager::lookupActionById(int id) const {
    FCITX_D();
    auto iter = d->idToAction_.find(id);
    if (iter == d->idToAction_.end()) {
        return nullptr;
    }
    return iter->second;
}

void UserInterfaceManager::update(UserInterfaceComponent component,
                                  InputContext *inputContext) {
    FCITX_D();
    auto iter = d->updateIndex_.find(inputContext);
    decltype(d->updateList_)::iterator listIter;
    if (d->updateIndex_.end() == iter) {
        d->updateList_.emplace_back(std::piecewise_construct,
                                    std::forward_as_tuple(inputContext),
                                    std::forward_as_tuple());
        d->updateIndex_[inputContext] = listIter =
            std::prev(d->updateList_.end());
    } else {
        listIter = iter->second;
    }
    listIter->second.insert(component);
}

void UserInterfaceManager::expire(InputContext *inputContext) {
    FCITX_D();
    auto iter = d->updateIndex_.find(inputContext);
    if (d->updateIndex_.end() != iter) {
        d->updateList_.erase(iter->second);
        d->updateIndex_.erase(iter);
    }
}

void UserInterfaceManager::flush() {
    FCITX_D();
    if (!d->ui_) {
        return;
    }
    for (auto &p : d->updateList_) {
        for (auto comp : p.second) {
            d->ui_->update(comp, p.first);
        }
    }
    d->updateIndex_.clear();
    d->updateList_.clear();
}
void UserInterfaceManager::updateAvailability() {
    FCITX_D();
    auto oldUI = d->ui_;
    UserInterface *newUI = nullptr;
    std::string newUIName;
    for (auto &name : d->uis_) {
        auto ui =
            static_cast<UserInterface *>(d->addonManager_->addon(name, true));
        if (ui && ui->available()) {
            newUI = static_cast<UserInterface *>(ui);
            newUIName = name;
            break;
        }
    }
    if (oldUI != newUI) {
        if (oldUI) {
            oldUI->suspend();
        }
        if (newUI) {
            newUI->resume();
        }
        d->ui_ = newUI;
        d->uiName_ = newUIName;
    }
}

std::string UserInterfaceManager::currentUI() const {
    FCITX_D();
    return d->uiName_;
}
}
