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

#include "addonmanager.h"
#include "addonloader.h"
#include "addonloader_p.h"
#include "fcitx-config/iniparser.h"
#include "fcitx-utils/log.h"
#include "instance.h"
#include "misc_p.h"
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>

namespace fcitx {

class Addon {
    friend class AddonManagerPrivate;

public:
    Addon(const std::string &name, RawConfig &config)
        : info_(name), failed_(false) {
        info_.load(config);
    }

    const AddonInfo &info() const { return info_; }

    bool isValid() const { return info_.isValid() && !failed_; }

    bool loaded() const { return !!instance_; }

    AddonInstance *instance() { return instance_.get(); }

    void setFailed(bool failed = true) { failed_ = failed; }

private:
    AddonInfo info_;
    bool failed_;
    std::unique_ptr<AddonInstance> instance_;
};

enum class DependencyCheckStatus {
    Satisfied,
    Pending,
    PendingUpdateRequest,
    Failed
};

class AddonManagerPrivate {
public:
    AddonManagerPrivate() : instance_(nullptr) {}

    Addon *addon(const std::string &name) const {
        auto iter = addons_.find(name);
        if (iter != addons_.end()) {
            return iter->second.get();
        }
        return nullptr;
    }

    DependencyCheckStatus checkDependencies(const Addon &a) {
        auto &dependencies = a.info().dependencies();
        for (auto &dependency : dependencies) {
            Addon *dep = addon(dependency);
            if (!dep || !dep->isValid()) {
                return DependencyCheckStatus::Failed;
            }

            if (!dep->loaded()) {
                if (dep->info().onDemand() &&
                    requested_.insert(dep->info().uniqueName()).second) {
                    return DependencyCheckStatus::PendingUpdateRequest;
                }
                return DependencyCheckStatus::Pending;
            }
        }
        auto &optionalDependencies = a.info().optionalDependencies();
        for (auto &dependency : optionalDependencies) {
            Addon *dep = addon(dependency);
            // if not available, don't bother load it
            if (!dep || !dep->isValid()) {
                continue;
            }

            // otherwise wait for it
            if (!dep->loaded() && !dep->info().onDemand()) {
                return DependencyCheckStatus::Pending;
            }
        }

        return DependencyCheckStatus::Satisfied;
    }

    void loadAddons(AddonManager *q_ptr) {
        if (inLoadAddons_) {
            throw std::runtime_error("loadAddons is not reentrant, do not call "
                                     "addon(.., true) in constructor of addon");
        }
        inLoadAddons_ = true;
        bool changed = false;
        do {
            changed = false;

            for (auto &item : addons_) {
                changed |= loadAddon(q_ptr, *item.second.get());
            }
        } while (changed);
        inLoadAddons_ = false;
    }

    bool loadAddon(AddonManager *q_ptr, Addon &addon) {
        if (unloading_) {
            return false;
        }

        if (addon.loaded() || !addon.isValid()) {
            return false;
        }
        if (addon.info().onDemand() &&
            requested_.count(addon.info().uniqueName()) == 0) {
            return false;
        }
        auto result = checkDependencies(addon);
        FCITX_LOG(Debug) << "Call loadAddon() with "
                         << addon.info().uniqueName()
                         << " checkDependencies() returns "
                         << static_cast<int>(result);
        if (result == DependencyCheckStatus::Failed) {
            addon.setFailed();
        } else if (result == DependencyCheckStatus::Satisfied) {
            realLoad(q_ptr, addon);
            if (addon.loaded()) {
                loadOrder_.push_back(addon.info().uniqueName());
                return true;
            }
        } else if (result == DependencyCheckStatus::PendingUpdateRequest) {
            return true;
        }
        // here we are "pending" on others.
        return false;
    }

    void realLoad(AddonManager *q_ptr, Addon &addon) {
        if (!addon.isValid()) {
            return;
        }

        if (auto loader = findValue(loaders_, addon.info().type())) {
            addon.instance_.reset((*loader)->load(addon.info(), q_ptr));
        }
        if (!addon.instance_) {
            addon.setFailed(true);
        } else {
            FCITX_LOG(Info) << "Loaded addon " << addon.info().uniqueName();
        }
    }

    std::string addonConfigDir_ = "addon";

    bool unloading_ = false;
    bool inLoadAddons_ = false;

    std::unordered_map<std::string, std::unique_ptr<Addon>> addons_;
    std::unordered_map<std::string, std::unique_ptr<AddonLoader>> loaders_;
    std::unordered_set<std::string> requested_;

    std::vector<std::string> loadOrder_;

    Instance *instance_ = nullptr;
    EventLoop *eventLoop_ = nullptr;
};

AddonManager::AddonManager() : d_ptr(std::make_unique<AddonManagerPrivate>()) {}

AddonManager::AddonManager(const std::string &addonConfigDir) : AddonManager() {
    FCITX_D();
    d->addonConfigDir_ = addonConfigDir;
}

AddonManager::~AddonManager() { unload(); }

void AddonManager::registerLoader(std::unique_ptr<AddonLoader> loader) {
    FCITX_D();
    // same loader shouldn't register twice
    if (d->loaders_.count(loader->type())) {
        return;
    }
    d->loaders_.emplace(loader->type(), std::move(loader));
}

void AddonManager::registerDefaultLoader(StaticAddonRegistry *registry) {
    registerLoader(std::make_unique<SharedLibraryLoader>());
    if (registry) {
        registerLoader(std::make_unique<StaticLibraryLoader>(registry));
    }
}

void AddonManager::load(const std::unordered_set<std::string> &enabled,
                        const std::unordered_set<std::string> &disabled) {
    FCITX_D();
    auto &path = StandardPath::global();
    auto fileMap =
        path.multiOpenAll(StandardPath::Type::PkgData, d->addonConfigDir_,
                          O_RDONLY, filter::Suffix(".conf"));
    for (const auto &file : fileMap) {
        auto &files = file.second;
        RawConfig config;
        // reverse the order, so we end up parse user file at last.
        for (auto iter = files.rbegin(), end = files.rend(); iter != end;
             iter++) {
            auto fd = iter->fd();
            readFromIni(config, fd);
        }

        // remove .conf
        auto name = file.first.substr(0, file.first.size() - 5);
        // override configuration
        if (disabled.count(name)) {
            config.setValueByPath("Addon/Enabled", "False");
        } else if (enabled.count(name)) {
            config.setValueByPath("Addon/Enabled", "True");
        }

        auto addon = std::make_unique<Addon>(name, config);
        if (addon->isValid()) {
            d->addons_[addon->info().uniqueName()] = std::move(addon);
        }
    }

    d->loadAddons(this);
}

void AddonManager::unload() {
    FCITX_D();
    if (d->unloading_) {
        return;
    }
    d->unloading_ = true;
    // reverse the unload order
    for (auto iter = d->loadOrder_.rbegin(), end = d->loadOrder_.rend();
         iter != end; iter++) {
        FCITX_LOG(Info) << "Unloading addon " << *iter;
        d->addons_.erase(*iter);
    }
    d->loadOrder_.clear();
    d->requested_.clear();
    d->unloading_ = false;
}

void AddonManager::saveAll() {
    FCITX_D();
    if (d->unloading_) {
        return;
    }
    // reverse the unload order
    for (auto iter = d->loadOrder_.rbegin(), end = d->loadOrder_.rend();
         iter != end; iter++) {
        if (auto addonInst = addon(*iter)) {
            addonInst->save();
        }
    }
}

AddonInstance *AddonManager::addon(const std::string &name, bool load) {
    FCITX_D();
    auto addon = d->addon(name);
    if (!addon) {
        return nullptr;
    }
    if (addon->isValid() && !addon->loaded() && addon->info().onDemand() &&
        load) {
        d->requested_.insert(name);
        d->loadAddons(this);
    }
    return addon->instance();
}

const AddonInfo *AddonManager::addonInfo(const std::string &name) const {
    FCITX_D();
    auto addon = d->addon(name);
    if (addon && addon->isValid()) {
        return &addon->info();
    }
    return nullptr;
}

std::unordered_set<std::string>
AddonManager::addonNames(AddonCategory category) {
    FCITX_D();
    std::unordered_set<std::string> result;
    for (auto &item : d->addons_) {
        if (item.second->isValid() &&
            item.second->info().category() == category) {
            result.insert(item.first);
        }
    }
    return result;
}

Instance *AddonManager::instance() {
    FCITX_D();
    return d->instance_;
}

void AddonManager::setInstance(Instance *instance) {
    FCITX_D();
    d->instance_ = instance;
    d->eventLoop_ = &instance->eventLoop();
}

void AddonManager::setEventLoop(EventLoop *eventLoop) {
    FCITX_D();
    d->eventLoop_ = eventLoop;
}

EventLoop *AddonManager::eventLoop() {
    FCITX_D();
    return d->eventLoop_;
}
}
